// main.cpp -- Command line front end for the flight network toolkit.
//
//   flightnet route       cheapest / fastest / fewest-stop itineraries
//   flightnet flow        maximum routable throughput and its bottleneck
//   flightnet resilience  what breaks when an airport or a leg is lost
//   flightnet bench       stress harness over many synthetic networks
//   flightnet generate    write a synthetic network to CSV
//   flightnet info        summary of the loaded network
//   flightnet demo        one guided pass over every feature

#include "flightnet/benchmark.hpp"
#include "flightnet/capacity.hpp"
#include "flightnet/generator.hpp"
#include "flightnet/graph.hpp"
#include "flightnet/json_output.hpp"
#include "flightnet/loader.hpp"
#include "flightnet/maxflow.hpp"
#include "flightnet/routing.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

using namespace flightnet;

namespace {

// ---------------------------------------------------------------------------
// Argument handling
// ---------------------------------------------------------------------------

class Args {
public:
    Args(int argc, char** argv) {
        for (int i = 2; i < argc; ++i) {          // argv[1] is the command
            std::string a = argv[i];
            if (a.size() > 2 && a[0] == '-' && a[1] == '-') {
                const std::string key = a.substr(2);
                const std::size_t eq = key.find('=');
                if (eq != std::string::npos) {
                    opts_[key.substr(0, eq)] = key.substr(eq + 1);
                } else if (i + 1 < argc && argv[i + 1][0] != '-') {
                    opts_[key] = argv[++i];
                } else {
                    opts_[key] = "1";             // bare flag
                }
            } else {
                positional_.push_back(a);
            }
        }
    }

    bool has(const std::string& k) const { return opts_.count(k) != 0; }

    std::string str(const std::string& k, const std::string& dflt = "") const {
        std::map<std::string, std::string>::const_iterator it = opts_.find(k);
        return it == opts_.end() ? dflt : it->second;
    }

    int num(const std::string& k, int dflt) const {
        std::map<std::string, std::string>::const_iterator it = opts_.find(k);
        return it == opts_.end() ? dflt : std::atoi(it->second.c_str());
    }

    double real(const std::string& k, double dflt) const {
        std::map<std::string, std::string>::const_iterator it = opts_.find(k);
        return it == opts_.end() ? dflt : std::atof(it->second.c_str());
    }

    bool flag(const std::string& k) const { return has(k) && str(k) != "0"; }

    const std::vector<std::string>& positional() const { return positional_; }

private:
    std::map<std::string, std::string> opts_;
    std::vector<std::string>           positional_;
};

// ---------------------------------------------------------------------------
// Network loading
// ---------------------------------------------------------------------------

// --data <dir> reads <dir>/airports.csv and <dir>/routes.csv.
// --synthetic generates a network instead. Default is the bundled sample.
FlightNetwork loadNetwork(const Args& args, std::ostream& log) {
    if (args.has("synthetic")) {
        GeneratorConfig gc;
        gc.airports  = args.num("airports", 40);
        gc.hubs      = args.num("hubs", 3);
        gc.seed      = static_cast<unsigned>(args.num("seed", 1));
        gc.meshProb  = args.real("mesh", gc.meshProb);
        gc.spokeProb = args.real("spoke", gc.spokeProb);
        const FlightNetwork g = generate(gc);
        log << "loaded synthetic network: " << g.numAirports() << " airports, "
            << g.numFlights() << " flights (seed " << gc.seed << ")\n";
        return g;
    }

    if (args.has("data")) {
        const std::string dir = args.str("data");
        const FlightNetwork g = loadFromCsv(dir + "/airports.csv", dir + "/routes.csv");
        log << "loaded " << dir << ": " << g.numAirports() << " airports, "
            << g.numFlights() << " flights\n";
        return g;
    }

    const FlightNetwork g = sampleNetwork();
    log << "loaded built-in sample network: " << g.numAirports() << " airports, "
        << g.numFlights() << " flights\n";
    return g;
}

// Resolves an airport code, reporting clearly when it is not in the network.
int resolve(const FlightNetwork& g, const std::string& code, const char* role) {
    const int id = g.idOf(code);
    if (id < 0) {
        std::ostringstream os;
        os << "unknown " << role << " airport '" << code << "'";
        throw DataError(os.str());
    }
    return id;
}

// Reads --from / --to, defaulting to the first and last airport so the commands
// are runnable with no arguments at all.
void endpoints(const Args& args, const FlightNetwork& g, int& src, int& dst) {
    const std::string f = args.str("from");
    const std::string t = args.str("to");
    src = f.empty() ? 0 : resolve(g, f, "origin");
    dst = t.empty() ? g.numAirports() - 1 : resolve(g, t, "destination");
}

// ---------------------------------------------------------------------------
// Pretty printing
// ---------------------------------------------------------------------------

std::string hhmm(int minutes) {
    std::ostringstream os;
    os << (minutes / 60) << "h" << std::setw(2) << std::setfill('0') << (minutes % 60) << "m";
    return os.str();
}

void printRoute(const FlightNetwork& g, const Route& r, const std::string& title) {
    std::cout << "\n" << title << "\n";
    if (!r.found) {
        std::cout << "  no route exists\n";
        return;
    }
    std::cout << "  ";
    for (std::size_t i = 0; i < r.airports.size(); ++i) {
        if (i) std::cout << " -> ";
        std::cout << g.codeOf(r.airports[i]);
    }
    std::cout << "\n  " << r.numLegs() << " leg" << (r.numLegs() == 1 ? "" : "s")
              << ", " << r.stops() << " stop" << (r.stops() == 1 ? "" : "s")
              << " | fare " << std::fixed << std::setprecision(2) << r.totalCost
              << " | time " << hhmm(r.totalDuration) << "\n";

    for (std::size_t i = 0; i < r.legs.size(); ++i) {
        const Flight& f = g.flight(r.legs[i]);
        std::cout << "    " << std::setw(2) << (i + 1) << ". "
                  << std::left << std::setw(5) << g.codeOf(f.from) << " -> "
                  << std::setw(5) << g.codeOf(f.to) << std::right
                  << "  " << std::setw(4) << (f.airline.empty() ? "--" : f.airline)
                  << "  fare " << std::setw(8) << std::fixed << std::setprecision(2) << f.cost
                  << "  " << std::setw(6) << hhmm(f.duration)
                  << "  seats " << f.seats << "\n";
    }
}

void printFlowReport(const FlowReport& rep,
                     const std::string& from, const std::string& to) {
    std::cout << "\n" << algorithmName(rep.algorithm) << "\n";
    std::cout << "  max flow " << from << " -> " << to << " : " << rep.maxFlow << " seats\n";
    std::cout << "  augmenting " << (rep.algorithm == ALGO_DINIC ? "phases" : "paths")
              << " : " << rep.augmentations << "\n";
    std::cout << "  node visits            : " << rep.visits << "\n";
    std::cout << "  elapsed                : " << std::fixed << std::setprecision(4)
              << rep.elapsedMs << " ms\n";

    std::cout << "  minimum cut (" << rep.minCut.size() << " bottleneck"
              << (rep.minCut.size() == 1 ? "" : "s")
              << ", total capacity " << rep.cutCapacity() << "):\n";
    for (std::size_t i = 0; i < rep.minCut.size(); ++i) {
        const CutItem& c = rep.minCut[i];
        std::cout << "    " << std::left << std::setw(26) << c.label << std::right
                  << " capacity " << std::setw(6) << c.capacity
                  << (c.isAirport ? "   <- airport throughput" : "") << "\n";
    }
}

void printImpacts(const std::vector<Impact>& v, Cap baseline, const char* what) {
    std::cout << "\nmost critical " << what << " (baseline throughput " << baseline << " seats):\n";
    if (v.empty()) {
        std::cout << "  none to rank\n";
        return;
    }
    std::cout << "  " << std::left << std::setw(26) << "item" << std::right
              << std::setw(10) << "remains" << std::setw(10) << "lost"
              << std::setw(9) << "loss %" << "  note\n";
    for (std::size_t i = 0; i < v.size(); ++i) {
        const Impact& im = v[i];
        std::cout << "  " << std::left << std::setw(26) << im.label << std::right
                  << std::setw(10) << im.flowAfter
                  << std::setw(10) << im.flowLost
                  << std::setw(8) << std::fixed << std::setprecision(1) << im.lostPercent << "%"
                  << (im.disconnects ? "  SEVERED - no throughput survives" : "")
                  << "\n";
    }
}

// ---------------------------------------------------------------------------
// Commands
// ---------------------------------------------------------------------------

// In --json mode stdout must be one parseable document, so the human-readable
// "loaded ..." chatter is swallowed instead of printed.
std::ostream& logSink(const Args& args) {
    static std::ostringstream discard;
    if (!args.flag("json")) return std::cout;
    discard.str(std::string());
    return discard;
}

int cmdInfo(const Args& args) {
    const FlightNetwork g = loadNetwork(args, logSink(args));

    if (args.flag("json")) {
        std::cout << "{\"ok\":true,\"command\":\"info\",\"network\":"
                  << json::networkInfo(g) << "}\n";
        return 0;
    }

    long long seats = 0;
    int maxDeg = 0, hub = -1;
    for (int e = 0; e < g.numFlights(); ++e) seats += g.flight(e).seats;
    for (int v = 0; v < g.numAirports(); ++v) {
        if (g.degree(v) > maxDeg) { maxDeg = g.degree(v); hub = v; }
    }

    std::cout << "\n=== Network ===\n";
    std::cout << "airports        : " << g.numAirports() << "\n";
    std::cout << "flight legs     : " << g.numFlights() << "\n";
    std::cout << "total seats     : " << seats << "\n";
    if (g.numAirports() > 0)
        std::cout << "avg out-degree  : " << std::fixed << std::setprecision(2)
                  << (static_cast<double>(g.numFlights()) / g.numAirports()) << "\n";
    if (hub >= 0)
        std::cout << "busiest airport : " << g.codeOf(hub) << " (" << maxDeg << " legs in+out)\n";

    // Strong connectivity is what makes every routing query answerable.
    const Router router(g);
    int strandedFrom = 0;
    for (int v = 0; v < g.numAirports(); ++v)
        if (router.reachableCount(v) < g.numAirports()) ++strandedFrom;
    std::cout << "strongly conn.  : " << (strandedFrom == 0 ? "yes" : "no")
              << " (" << (g.numAirports() - strandedFrom) << "/" << g.numAirports()
              << " airports reach the whole network)\n";

    std::cout << "\n" << std::left << std::setw(6) << "code" << std::setw(16) << "city"
              << std::setw(4) << "cc" << std::right << std::setw(10) << "capacity"
              << std::setw(7) << "out" << std::setw(6) << "in" << "\n";
    for (int v = 0; v < g.numAirports() && v < 40; ++v) {
        const Airport& a = g.airport(v);
        std::cout << std::left << std::setw(6) << a.code
                  << std::setw(16) << (a.city.size() > 15 ? a.city.substr(0, 15) : a.city)
                  << std::setw(4) << a.country << std::right
                  << std::setw(10) << a.capacity
                  << std::setw(7) << g.outgoing(v).size()
                  << std::setw(6) << g.incoming(v).size() << "\n";
    }
    if (g.numAirports() > 40) std::cout << "  ... " << (g.numAirports() - 40) << " more\n";
    return 0;
}

int cmdRoute(const Args& args) {
    const FlightNetwork g = loadNetwork(args, logSink(args));
    int src = 0, dst = 0;
    endpoints(args, g, src, dst);

    const Router router(g);
    const std::string mode = args.str("mode", "all");
    const int alternates   = args.num("alternates", 0);

    // The balanced objective is tunable from the command line in both modes.
    RouteWeights bw = RouteWeights::balanced();
    bw.cost   = args.real("w-cost", bw.cost);
    bw.time   = args.real("w-time", bw.time);
    bw.perLeg = args.real("w-leg",  bw.perLeg);

    if (args.flag("json")) {
        std::cout << "{\"ok\":true,\"command\":\"route\""
                  << ",\"from\":" << json::quote(g.codeOf(src))
                  << ",\"to\":" << json::quote(g.codeOf(dst))
                  << ",\"results\":[";

        bool first = true;
        const auto emit = [&](const Route& r, const char* m, const char* label) {
            if (!first) std::cout << ',';
            first = false;
            std::cout << json::route(g, r, m, label);
        };
        if (mode == "stops" || mode == "all")
            emit(router.minStops(src, dst), "stops", "fewest stops - BFS, O(V+E)");
        if (mode == "cost" || mode == "all")
            emit(router.cheapest(src, dst), "cost", "cheapest fare - Dijkstra, O(E log V)");
        if (mode == "time" || mode == "all")
            emit(router.fastest(src, dst), "time", "shortest time - Dijkstra, O(E log V)");
        if (mode == "balanced" || mode == "all")
            emit(router.best(src, dst, bw), "balanced", "balanced trade-off");

        std::cout << "],\"alternates\":[";
        if (alternates > 0) {
            const std::vector<Route> ks = router.kBest(src, dst, alternates, bw);
            for (std::size_t i = 0; i < ks.size(); ++i) {
                if (i) std::cout << ',';
                std::cout << json::route(g, ks[i], "alternate", "Yen k-best");
            }
        }
        std::cout << "]}\n";
        return 0;
    }

    std::cout << "\nrouting " << g.codeOf(src) << " -> " << g.codeOf(dst) << "\n";

    if (mode == "stops" || mode == "all")
        printRoute(g, router.minStops(src, dst), "[fewest stops]  BFS, O(V+E)");
    if (mode == "cost" || mode == "all")
        printRoute(g, router.cheapest(src, dst), "[cheapest fare]  Dijkstra, O(E log V)");
    if (mode == "time" || mode == "all")
        printRoute(g, router.fastest(src, dst), "[shortest time]  Dijkstra, O(E log V)");
    if (mode == "balanced" || mode == "all") {
        std::ostringstream title;
        title << "[balanced]  weights cost=" << bw.cost
              << " time=" << std::setprecision(3) << bw.time
              << "/min leg=" << bw.perLeg;
        printRoute(g, router.best(src, dst, bw), title.str());
    }

    if (alternates > 0) {
        const std::vector<Route> ks = router.kBest(src, dst, alternates, bw);
        std::cout << "\n[" << ks.size() << " best alternates]  Yen's algorithm\n";
        for (std::size_t i = 0; i < ks.size(); ++i)
            std::cout << "  " << (i + 1) << ". " << ks[i].describe(g) << "\n";
    }
    return 0;
}

int cmdFlow(const Args& args) {
    const FlightNetwork g = loadNetwork(args, logSink(args));
    int src = 0, dst = 0;
    endpoints(args, g, src, dst);

    const bool enforce = !args.flag("no-airport-caps");
    CapacityAnalyzer an(g, enforce);

    if (args.flag("json")) {
        const std::vector<FlowReport> reps = an.compareAlgorithms(src, dst);
        bool agree = true;
        for (std::size_t i = 1; i < reps.size(); ++i)
            if (reps[i].maxFlow != reps[0].maxFlow) agree = false;

        std::cout << "{\"ok\":true,\"command\":\"flow\""
                  << ",\"from\":" << json::quote(g.codeOf(src))
                  << ",\"to\":" << json::quote(g.codeOf(dst))
                  << ",\"enforceAirportCaps\":" << (enforce ? "true" : "false")
                  << ",\"algorithms\":[";
        for (std::size_t i = 0; i < reps.size(); ++i) {
            if (i) std::cout << ',';
            std::cout << json::flowReport(reps[i]);
        }
        std::cout << "],\"agree\":" << (agree ? "true" : "false")
                  << ",\"minCutEqualsMaxFlow\":"
                  << ((reps[2].maxFlow == reps[2].cutCapacity()) ? "true" : "false")
                  << "}\n";
        return 0;
    }

    std::cout << "\nmax flow " << g.codeOf(src) << " -> " << g.codeOf(dst)
              << "   (airport capacity " << (enforce ? "enforced" : "ignored") << ")\n";

    const std::string algo = args.str("algo", "all");
    if (algo == "all") {
        const std::vector<FlowReport> reps = an.compareAlgorithms(src, dst);
        for (std::size_t i = 0; i < reps.size(); ++i)
            printFlowReport(reps[i], g.codeOf(src), g.codeOf(dst));

        bool agree = true;
        for (std::size_t i = 1; i < reps.size(); ++i)
            if (reps[i].maxFlow != reps[0].maxFlow) agree = false;
        std::cout << "\nall three solvers agree : " << (agree ? "yes" : "NO - BUG") << "\n";
        if (reps.size() == 3 && reps[2].elapsedMs > 0.0) {
            std::cout << "Dinic vs Ford-Fulkerson : " << std::fixed << std::setprecision(2)
                      << (reps[0].elapsedMs / reps[2].elapsedMs) << "x\n";
        }
        // Max-flow min-cut is the theorem this whole model rests on; show it holds.
        std::cout << "max-flow == min-cut     : "
                  << (reps[2].maxFlow == reps[2].cutCapacity() ? "yes" : "NO - BUG") << "\n";
    } else {
        FlowAlgorithm a = ALGO_DINIC;
        if      (algo == "ff" || algo == "ford-fulkerson") a = ALGO_FORD_FULKERSON;
        else if (algo == "ek" || algo == "edmonds-karp")   a = ALGO_EDMONDS_KARP;
        else if (algo == "dinic")                          a = ALGO_DINIC;
        else { std::cerr << "unknown --algo '" << algo << "' (ff|ek|dinic|all)\n"; return 2; }
        printFlowReport(an.maxFlow(src, dst, a), g.codeOf(src), g.codeOf(dst));
    }
    return 0;
}

int cmdResilience(const Args& args) {
    const FlightNetwork g = loadNetwork(args, logSink(args));
    int src = 0, dst = 0;
    endpoints(args, g, src, dst);

    const int topN = args.num("top", 8);
    CapacityAnalyzer an(g, !args.flag("no-airport-caps"));

    const FlowReport base = an.maxFlow(src, dst, ALGO_DINIC);

    if (args.flag("json")) {
        const std::vector<Impact> airports = an.rankAirports(src, dst, topN);
        const std::vector<Impact> flights  = an.rankFlights(src, dst, topN);

        std::cout << "{\"ok\":true,\"command\":\"resilience\""
                  << ",\"from\":" << json::quote(g.codeOf(src))
                  << ",\"to\":" << json::quote(g.codeOf(dst))
                  << ",\"baseline\":" << json::num(static_cast<long long>(base.maxFlow))
                  << ",\"base\":" << json::flowReport(base)
                  << ",\"airports\":[";
        for (std::size_t i = 0; i < airports.size(); ++i) {
            if (i) std::cout << ',';
            std::cout << json::impact(airports[i]);
        }
        std::cout << "],\"flights\":[";
        for (std::size_t i = 0; i < flights.size(); ++i) {
            if (i) std::cout << ',';
            std::cout << json::impact(flights[i]);
        }
        std::cout << "],\"contingency\":";

        // The practical answer to "what do we do when the worst hub closes".
        if (!airports.empty() && airports[0].flowLost > 0) {
            const Router router(g);
            RouteBan ban(g.numAirports(), g.numFlights());
            ban.airport[static_cast<std::size_t>(airports[0].id)] = true;
            const Route detour = router.best(src, dst, RouteWeights::balanced(), ban);
            std::cout << json::route(g, detour, "contingency",
                                     "best itinerary avoiding " + airports[0].label);
        } else {
            std::cout << "null";
        }
        std::cout << "}\n";
        return 0;
    }
    std::cout << "\nresilience of " << g.codeOf(src) << " -> " << g.codeOf(dst) << "\n";
    std::cout << "baseline throughput : " << base.maxFlow << " seats\n";

    std::cout << "\ncurrent bottleneck (minimum cut):\n";
    for (std::size_t i = 0; i < base.minCut.size(); ++i)
        std::cout << "  " << base.minCut[i].label
                  << "  capacity " << base.minCut[i].capacity << "\n";

    printImpacts(an.rankAirports(src, dst, topN), base.maxFlow, "airports");
    printImpacts(an.rankFlights(src, dst, topN), base.maxFlow, "flight legs");

    // A route that survives the single worst airport failure is the practical
    // answer to "what do we do when that hub closes".
    const std::vector<Impact> worst = an.rankAirports(src, dst, 1);
    if (!worst.empty() && worst[0].flowLost > 0) {
        const Router router(g);
        RouteBan ban(g.numAirports(), g.numFlights());
        ban.airport[static_cast<std::size_t>(worst[0].id)] = true;
        const Route detour = router.best(src, dst, RouteWeights::balanced(), ban);
        printRoute(g, detour, "[contingency]  best itinerary avoiding " + worst[0].label);
    }
    return 0;
}

int cmdBench(const Args& args) {
    BenchConfig cfg;
    cfg.networks       = args.num("networks", 10000);
    cfg.minAirports    = args.num("min", 20);
    cfg.maxAirports    = args.num("max", 60);
    cfg.hubs           = args.num("hubs", 3);
    cfg.seed           = static_cast<unsigned>(args.num("seed", 20240501));
    cfg.meshProb       = args.real("mesh", cfg.meshProb);
    cfg.spokeProb      = args.real("spoke", cfg.spokeProb);
    cfg.runEdmondsKarp = args.flag("ek");
    cfg.runRouting     = !args.flag("no-routing");
    cfg.verify         = !args.flag("no-verify");
    cfg.progressEvery  = args.num("progress", 1000);
    cfg.csvPath        = args.str("csv");

    if (args.flag("scaling")) {
        // A ladder of sizes, so the speedup is reported as a trend rather than
        // as one number that happens to depend on the default network size.
        std::vector<std::pair<int, int> > sizes;
        sizes.push_back(std::make_pair(10, 20));
        sizes.push_back(std::make_pair(20, 40));
        sizes.push_back(std::make_pair(40, 80));
        sizes.push_back(std::make_pair(80, 160));
        sizes.push_back(std::make_pair(150, 300));

        BenchConfig sc = cfg;
        sc.networks = args.num("networks", 200);   // smaller default; the big rungs are slow
        std::cout << "scaling study: " << sc.networks << " networks per size class\n";
        const std::vector<ScalingPoint> rows = runScalingStudy(sc, sizes, std::cout);
        printScaling(rows, std::cout);
        for (std::size_t i = 0; i < rows.size(); ++i)
            if (!rows[i].result.ok()) return 1;
        return 0;
    }

    std::cout << "stress testing " << cfg.networks << " synthetic networks ("
              << cfg.minAirports << ".." << cfg.maxAirports << " airports each)\n";
    const BenchResult r = runBenchmark(cfg, std::cout);
    printBenchmark(r, cfg, std::cout);
    return r.ok() ? 0 : 1;
}

int cmdGenerate(const Args& args) {
    GeneratorConfig gc;
    gc.airports  = args.num("airports", 60);
    gc.hubs      = args.num("hubs", 4);
    gc.seed      = static_cast<unsigned>(args.num("seed", 7));
    gc.meshProb  = args.real("mesh", gc.meshProb);
    gc.spokeProb = args.real("spoke", gc.spokeProb);

    const FlightNetwork g = generate(gc);
    const std::string dir = args.str("out", "data/synthetic");
    saveToCsv(g, dir + "/airports.csv", dir + "/routes.csv");

    std::cout << "generated " << g.numAirports() << " airports and " << g.numFlights()
              << " flights (seed " << gc.seed << ")\n"
              << "written to " << dir << "/airports.csv and " << dir << "/routes.csv\n"
              << "\nload it with:  flightnet info --data " << dir << "\n";
    return 0;
}

int cmdDemo(const Args& args) {
    const FlightNetwork g = args.has("data") || args.has("synthetic")
                                ? loadNetwork(args, std::cout)
                                : sampleNetwork();

    const int src = g.idOf("JFK") >= 0 ? g.idOf("JFK") : 0;
    const int dst = g.idOf("SYD") >= 0 ? g.idOf("SYD") : g.numAirports() - 1;

    std::cout << "\n########## 1. routing ##########\n";
    const Router router(g);
    printRoute(g, router.minStops(src, dst), "[fewest stops]  BFS, O(V+E)");
    printRoute(g, router.cheapest(src, dst), "[cheapest fare]  Dijkstra, O(E log V)");
    printRoute(g, router.fastest(src, dst), "[shortest time]  Dijkstra, O(E log V)");
    printRoute(g, router.best(src, dst, RouteWeights::balanced()), "[balanced]");

    const std::vector<Route> ks = router.kBest(src, dst, 3, RouteWeights::balanced());
    std::cout << "\n[alternates]  Yen's algorithm\n";
    for (std::size_t i = 0; i < ks.size(); ++i)
        std::cout << "  " << (i + 1) << ". " << ks[i].describe(g) << "\n";

    std::cout << "\n########## 2. capacity / max flow ##########\n";
    CapacityAnalyzer an(g, true);
    const std::vector<FlowReport> reps = an.compareAlgorithms(src, dst);
    for (std::size_t i = 0; i < reps.size(); ++i)
        printFlowReport(reps[i], g.codeOf(src), g.codeOf(dst));

    std::cout << "\n########## 3. resilience ##########\n";
    const FlowReport base = reps[2];
    printImpacts(an.rankAirports(src, dst, 5), base.maxFlow, "airports");
    printImpacts(an.rankFlights(src, dst, 5), base.maxFlow, "flight legs");

    std::cout << "\n########## 4. stress test ##########\n";
    BenchConfig bc;
    bc.networks      = args.num("networks", 500);
    bc.progressEvery = 0;
    const BenchResult r = runBenchmark(bc, std::cout);
    printBenchmark(r, bc, std::cout);

    std::cout << "for the full 10,000-network run:  flightnet bench --networks 10000\n";
    return r.ok() ? 0 : 1;
}

int usage(std::ostream& os) {
    os <<
        "flightnet -- flight network optimization and resilience\n"
        "\n"
        "usage: flightnet <command> [options]\n"
        "\n"
        "commands:\n"
        "  info          summarize the loaded network\n"
        "  route         minimum-stop, cheapest, fastest and balanced itineraries\n"
        "  flow          maximum throughput and the bottleneck that caps it\n"
        "  resilience    rank airports and legs by the throughput lost if they fail\n"
        "  bench         stress test many synthetic networks\n"
        "  generate      write a synthetic network to CSV\n"
        "  demo          run one guided pass over everything\n"
        "\n"
        "network selection (all commands):\n"
        "  --data <dir>        load <dir>/airports.csv and <dir>/routes.csv\n"
        "  --synthetic         generate one instead (--airports, --hubs, --seed)\n"
        "  --json              machine-readable output (info/route/flow/resilience)\n"
        "  (default: bundled 12-airport sample network)\n"
        "\n"
        "route:\n"
        "  --from CODE --to CODE\n"
        "  --mode stops|cost|time|balanced|all      (default all)\n"
        "  --alternates N                           N best distinct routes (Yen)\n"
        "  --w-cost F --w-time F --w-leg F          retune the balanced objective\n"
        "\n"
        "flow:\n"
        "  --from CODE --to CODE\n"
        "  --algo ff|ek|dinic|all                   (default all)\n"
        "  --no-airport-caps                        ignore airport throughput limits\n"
        "\n"
        "resilience:\n"
        "  --from CODE --to CODE --top N            (default 8)\n"
        "\n"
        "bench:\n"
        "  --networks N        number of networks    (default 10000)\n"
        "  --min N --max N     airports per network  (default 20..60)\n"
        "  --seed S            reproducibility       (default 20240501)\n"
        "  --ek                also run Edmonds-Karp (slow)\n"
        "  --no-verify         skip correctness cross-checks\n"
        "  --csv PATH          dump per-network samples\n"
        "\n"
        "generate:\n"
        "  --airports N --hubs N --seed S --out DIR\n"
        "\n"
        "examples:\n"
        "  flightnet demo\n"
        "  flightnet route --from JFK --to SYD --alternates 3\n"
        "  flightnet flow --from JFK --to SYD\n"
        "  flightnet resilience --from JFK --to SYD --top 5\n"
        "  flightnet bench --networks 10000 --csv build/bench.csv\n";
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) { usage(std::cout); return 0; }

    const std::string cmd = argv[1];
    if (cmd == "-h" || cmd == "--help" || cmd == "help") { usage(std::cout); return 0; }

    const Args args(argc, argv);
    try {
        if (cmd == "info")       return cmdInfo(args);
        if (cmd == "route")      return cmdRoute(args);
        if (cmd == "flow")       return cmdFlow(args);
        if (cmd == "resilience") return cmdResilience(args);
        if (cmd == "bench")      return cmdBench(args);
        if (cmd == "generate")   return cmdGenerate(args);
        if (cmd == "demo")       return cmdDemo(args);

        std::cerr << "unknown command '" << cmd << "'\n\n";
        usage(std::cerr);
        return 2;
    } catch (const std::exception& e) {
        // In --json mode the caller is a program, so a failure has to arrive as
        // a document it can parse rather than as bare text on stderr.
        if (args.flag("json")) std::cout << json::error(e.what()) << "\n";
        else                   std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
}
