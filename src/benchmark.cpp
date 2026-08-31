#include "flightnet/benchmark.hpp"

#include "flightnet/capacity.hpp"
#include "flightnet/generator.hpp"
#include "flightnet/graph.hpp"
#include "flightnet/maxflow.hpp"
#include "flightnet/routing.hpp"
#include "flightnet/timer.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <ostream>
#include <random>

namespace flightnet {

namespace {

double percentile(const std::vector<double>& sorted, double q) {
    if (sorted.empty()) return 0.0;
    if (sorted.size() == 1) return sorted[0];
    const double pos = q * static_cast<double>(sorted.size() - 1);
    const std::size_t lo = static_cast<std::size_t>(pos);
    const std::size_t hi = std::min(lo + 1, sorted.size() - 1);
    const double frac = pos - static_cast<double>(lo);
    return sorted[lo] + frac * (sorted[hi] - sorted[lo]);
}

} // namespace

Stats Stats::from(std::vector<double>& samples) {
    Stats s;
    if (samples.empty()) return s;
    std::sort(samples.begin(), samples.end());
    s.count = static_cast<long>(samples.size());
    for (std::size_t i = 0; i < samples.size(); ++i) s.total += samples[i];
    s.mean   = s.total / static_cast<double>(samples.size());
    s.median = percentile(samples, 0.50);
    s.p90    = percentile(samples, 0.90);
    s.p99    = percentile(samples, 0.99);
    s.min    = samples.front();
    s.max    = samples.back();
    return s;
}

BenchResult runBenchmark(const BenchConfig& cfg, std::ostream& log) {
    BenchResult r;
    r.ekEnabled = cfg.runEdmondsKarp;

    const int total = cfg.networks > 0 ? cfg.networks : 0;
    std::vector<double> ffMs, ekMs, dinicMs, bfsMs, dijkstraMs, ratios;
    ffMs.reserve(static_cast<std::size_t>(total));
    dinicMs.reserve(static_cast<std::size_t>(total));
    ratios.reserve(static_cast<std::size_t>(total));
    if (cfg.runEdmondsKarp) ekMs.reserve(static_cast<std::size_t>(total));
    if (cfg.runRouting) {
        bfsMs.reserve(static_cast<std::size_t>(total));
        dijkstraMs.reserve(static_cast<std::size_t>(total));
    }

    std::ofstream csv;
    if (!cfg.csvPath.empty()) {
        csv.open(cfg.csvPath.c_str());
        if (csv) csv << "network,airports,flights,max_flow,ff_ms,ek_ms,dinic_ms,ff_augmentations,dinic_phases\n";
    }

    // One master RNG drives the per-network seeds, so the whole batch is
    // reproducible from cfg.seed alone.
    std::mt19937 master(cfg.seed);
    double ffRunning = 0.0, dinicRunning = 0.0;   // running totals for progress output
    const int lo = std::max(2, cfg.minAirports);
    const int hi = std::max(lo, cfg.maxAirports);
    std::uniform_int_distribution<int> sizePick(lo, hi);

    for (int i = 0; i < total; ++i) {
        GeneratorConfig gc;
        gc.airports  = sizePick(master);
        gc.hubs      = std::min(cfg.hubs, std::max(1, gc.airports / 4));
        gc.meshProb  = cfg.meshProb;
        gc.spokeProb = cfg.spokeProb;
        gc.seed      = master();

        const FlightNetwork g = generate(gc);
        if (g.numAirports() < 2 || g.numFlights() == 0) continue;

        std::uniform_int_distribution<int> nodePick(0, g.numAirports() - 1);
        const int src = nodePick(master);
        int dst = nodePick(master);
        if (dst == src) dst = (src + 1) % g.numAirports();

        // --- max flow: all solvers share one network instance ---------------
        CapacityNetwork cap = CapacityNetwork::build(g, true);
        const int s = cap.sourceNode(src);
        const int t = cap.sinkNode(dst);

        cap.net().resetFlow();
        Timer timer;
        const FlowResult ff = FordFulkerson::run(cap.net(), s, t);
        const double ffTime = timer.elapsedMs();

        double ekTime = 0.0;
        FlowResult ek;
        if (cfg.runEdmondsKarp) {
            cap.net().resetFlow();
            timer.reset();
            ek = EdmondsKarp::run(cap.net(), s, t);
            ekTime = timer.elapsedMs();
        }

        cap.net().resetFlow();
        timer.reset();
        const FlowResult dn = Dinic::run(cap.net(), s, t);
        const double dnTime = timer.elapsedMs();

        if (cfg.verify) {
            if (ff.value != dn.value) ++r.flowMismatches;
            if (cfg.runEdmondsKarp && ek.value != dn.value) ++r.flowMismatches;

            // Max-flow min-cut: the residual graph left by Dinic must expose a
            // cut whose capacity is exactly the flow value.
            const std::vector<int> cutArcs = cap.net().minCutEdges(s);
            Cap cutCap = 0;
            for (std::size_t c = 0; c < cutArcs.size(); ++c)
                cutCap += cap.net().edge(cutArcs[c]).cap;
            if (cutCap != dn.value) ++r.cutMismatches;
        }

        ffMs.push_back(ffTime);
        dinicMs.push_back(dnTime);
        ffRunning    += ffTime;
        dinicRunning += dnTime;
        if (cfg.runEdmondsKarp) ekMs.push_back(ekTime);
        if (dnTime > 0.0) ratios.push_back(ffTime / dnTime);

        r.ffAugmentations += ff.augmentations;
        r.dinicPhases     += dn.augmentations;
        r.totalFlow       += dn.value;

        // --- routing --------------------------------------------------------
        if (cfg.runRouting) {
            const Router router(g);

            timer.reset();
            const Route byStops = router.minStops(src, dst);
            bfsMs.push_back(timer.elapsedMs());

            timer.reset();
            const Route byCost = router.cheapest(src, dst);
            dijkstraMs.push_back(timer.elapsedMs());

            if (cfg.verify) {
                // The two searches must agree on whether a route exists at all.
                if (byStops.found != byCost.found) {
                    ++r.routeMismatches;
                } else if (byStops.found) {
                    // BFS must be hop-optimal: unit-weight Dijkstra is an
                    // independent implementation of the same objective.
                    RouteWeights unit;
                    unit.cost = 0.0; unit.time = 0.0; unit.perLeg = 1.0;
                    const Route byHops = router.best(src, dst, unit);
                    if (byHops.numLegs() != byStops.numLegs()) ++r.routeMismatches;
                    // Dijkstra must not be beaten on fare by the BFS itinerary.
                    if (byCost.totalCost > byStops.totalCost + 1e-9) ++r.routeMismatches;
                }
            }
        }

        ++r.networks;
        r.totalAirports += g.numAirports();
        r.totalFlights  += g.numFlights();

        if (csv) {
            csv << i << ',' << g.numAirports() << ',' << g.numFlights() << ','
                << dn.value << ',' << ffTime << ',' << ekTime << ',' << dnTime << ','
                << ff.augmentations << ',' << dn.augmentations << '\n';
        }

        if (cfg.progressEvery > 0 && (i + 1) % cfg.progressEvery == 0) {
            log << "  " << (i + 1) << " / " << total << " networks"
                << "   ff=" << std::fixed << std::setprecision(1) << ffRunning << "ms"
                << "  dinic=" << dinicRunning << "ms"
                << std::endl;
        }
    }

    r.ff       = Stats::from(ffMs);
    r.dinic    = Stats::from(dinicMs);
    r.ek       = Stats::from(ekMs);
    r.bfs      = Stats::from(bfsMs);
    r.dijkstra = Stats::from(dijkstraMs);

    r.speedup = r.dinic.total > 0.0 ? r.ff.total / r.dinic.total : 0.0;
    if (!ratios.empty()) {
        std::sort(ratios.begin(), ratios.end());
        r.medianSpeedup = percentile(ratios, 0.50);
    }
    return r;
}

namespace {

void printStats(std::ostream& os, const char* name, const Stats& s) {
    if (s.count == 0) return;
    os << "  " << std::left << std::setw(16) << name << std::right
       << " total " << std::fixed << std::setprecision(1) << std::setw(9) << s.total << " ms"
       << "   mean " << std::setprecision(4) << std::setw(8) << s.mean
       << "   p50 "  << std::setw(8) << s.median
       << "   p90 "  << std::setw(8) << s.p90
       << "   p99 "  << std::setw(8) << s.p99
       << "   max "  << std::setw(8) << s.max
       << "\n";
}

} // namespace

void printBenchmark(const BenchResult& r, const BenchConfig& cfg, std::ostream& os) {
    os << "\n=== Benchmark ===\n";
    os << "networks          : " << r.networks << "\n";
    if (r.networks > 0) {
        os << "airports/network  : " << std::fixed << std::setprecision(1)
           << (static_cast<double>(r.totalAirports) / r.networks)
           << "   (range " << cfg.minAirports << ".." << cfg.maxAirports << ")\n";
        os << "flights/network   : "
           << (static_cast<double>(r.totalFlights) / r.networks) << "\n";
    }
    os << "seed              : " << cfg.seed << "\n";
    os << "timer resolution  : " << std::setprecision(6) << Timer::resolutionMs() << " ms\n";

    os << "\n-- timings (ms) --\n";
    printStats(os, "Ford-Fulkerson", r.ff);
    if (r.ekEnabled) printStats(os, "Edmonds-Karp", r.ek);
    printStats(os, "Dinic", r.dinic);
    if (r.bfs.count)      printStats(os, "BFS (min stops)", r.bfs);
    if (r.dijkstra.count) printStats(os, "Dijkstra (cost)", r.dijkstra);

    os << "\n-- max-flow work --\n";
    os << "  Ford-Fulkerson augmenting paths : " << r.ffAugmentations << "\n";
    os << "  Dinic phases                    : " << r.dinicPhases << "\n";
    os << "  total flow routed               : " << r.totalFlow << "\n";

    os << "\n-- speedup (Ford-Fulkerson -> Dinic) --\n";
    os << "  aggregate : " << std::setprecision(2) << r.speedup << "x\n";
    os << "  median    : " << std::setprecision(2) << r.medianSpeedup << "x per network\n";

    os << "\n-- verification --\n";
    if (!cfg.verify) {
        os << "  skipped (--no-verify)\n";
    } else {
        os << "  solver flow mismatches : " << r.flowMismatches << "\n";
        os << "  max-flow != min-cut    : " << r.cutMismatches << "\n";
        os << "  routing violations     : " << r.routeMismatches << "\n";
        os << "  result                 : " << (r.ok() ? "PASS" : "FAIL") << "\n";
    }
    if (!cfg.csvPath.empty()) os << "\nper-network samples written to " << cfg.csvPath << "\n";
    os << std::endl;
}

} // namespace flightnet

// ---------------------------------------------------------------------------
// Scaling study
// ---------------------------------------------------------------------------

namespace flightnet {

std::vector<ScalingPoint> runScalingStudy(const BenchConfig& base,
                                          const std::vector<std::pair<int, int> >& sizes,
                                          std::ostream& log) {
    std::vector<ScalingPoint> rows;
    for (std::size_t i = 0; i < sizes.size(); ++i) {
        BenchConfig cfg   = base;
        cfg.minAirports   = sizes[i].first;
        cfg.maxAirports   = sizes[i].second;
        cfg.progressEvery = 0;
        cfg.csvPath.clear();
        // Same seed at every rung, so differences come from size alone.
        log << "  sizing " << cfg.minAirports << ".." << cfg.maxAirports
            << " airports over " << cfg.networks << " networks ..." << std::endl;

        ScalingPoint row;
        row.minAirports = cfg.minAirports;
        row.maxAirports = cfg.maxAirports;
        row.result      = runBenchmark(cfg, log);
        if (row.result.networks > 0) {
            row.avgAirports = static_cast<double>(row.result.totalAirports) / row.result.networks;
            row.avgFlights  = static_cast<double>(row.result.totalFlights) / row.result.networks;
        }
        rows.push_back(row);
    }
    return rows;
}

void printScaling(const std::vector<ScalingPoint>& rows, std::ostream& os) {
    os << "\n=== Scaling study: Ford-Fulkerson vs Dinic ===\n\n";
    os << "  " << std::left
       << std::setw(12) << "airports"
       << std::right
       << std::setw(9)  << "avg V"
       << std::setw(10) << "avg E"
       << std::setw(12) << "FF total"
       << std::setw(12) << "Dinic total"
       << std::setw(11) << "aggregate"
       << std::setw(10) << "median"
       << "  verify\n";
    os << "  " << std::string(78, '-') << "\n";

    for (std::size_t i = 0; i < rows.size(); ++i) {
        const ScalingPoint& r = rows[i];
        std::ostringstream range;
        range << r.minAirports << ".." << r.maxAirports;
        os << "  " << std::left << std::setw(12) << range.str() << std::right
           << std::fixed << std::setprecision(1)
           << std::setw(9)  << r.avgAirports
           << std::setw(10) << r.avgFlights
           << std::setw(10) << r.result.ff.total    << "ms"
           << std::setw(10) << r.result.dinic.total << "ms"
           << std::setprecision(2)
           << std::setw(10) << r.result.speedup       << "x"
           << std::setw(9)  << r.result.medianSpeedup << "x"
           << "  " << (r.result.ok() ? "PASS" : "FAIL")
           << "\n";
    }
    os << "\nFord-Fulkerson augments once per unit of bottleneck flow, so its cost grows\n"
          "with total capacity; Dinic augments a whole blocking flow per phase and needs\n"
          "O(V) phases. The gap therefore widens with network size rather than being a\n"
          "single constant factor.\n" << std::endl;
}

} // namespace flightnet
