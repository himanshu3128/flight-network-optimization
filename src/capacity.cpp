#include "flightnet/capacity.hpp"
#include "flightnet/timer.hpp"

#include <algorithm>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace flightnet {

namespace {

const Cap UNBOUNDED = std::numeric_limits<Cap>::max() / 8;

} // namespace

const char* algorithmName(FlowAlgorithm a) {
    switch (a) {
        case ALGO_FORD_FULKERSON: return "Ford-Fulkerson";
        case ALGO_EDMONDS_KARP:   return "Edmonds-Karp";
        case ALGO_DINIC:          return "Dinic";
    }
    return "unknown";
}

// ---------------------------------------------------------------------------
// CapacityNetwork
// ---------------------------------------------------------------------------

CapacityNetwork CapacityNetwork::build(const FlightNetwork& g, bool enforceAirportCapacity) {
    CapacityNetwork c;
    const int nA = g.numAirports();
    const int nF = g.numFlights();

    c.net_ = FlowNetwork(2 * nA);
    c.airportArc_.assign(static_cast<std::size_t>(nA), -1);
    c.flightArc_.assign(static_cast<std::size_t>(nF), -1);

    // Throughput arcs. An airport with capacity <= 0 means "not modelled", which
    // we read as unlimited rather than as closed.
    for (int v = 0; v < nA; ++v) {
        const int cap = g.airport(v).capacity;
        const Cap arcCap = (!enforceAirportCapacity || cap <= 0)
                               ? UNBOUNDED
                               : static_cast<Cap>(cap);
        const int arc = c.net_.addEdge(c.inNode(v), c.outNode(v), arcCap);
        c.airportArc_[static_cast<std::size_t>(v)] = arc;
    }

    // Leg arcs, out(from) -> in(to).
    for (int e = 0; e < nF; ++e) {
        const Flight& f = g.flight(e);
        const Cap seats = f.seats > 0 ? static_cast<Cap>(f.seats) : 0;
        const int arc = c.net_.addEdge(c.outNode(f.from), c.inNode(f.to), seats);
        c.flightArc_[static_cast<std::size_t>(e)] = arc;
    }

    // Reverse maps, sized to cover forward arcs (reverse arcs are never queried).
    const std::size_t arcCount = static_cast<std::size_t>(2 * (nA + nF));
    c.arcKind_.assign(arcCount, -1);
    c.arcOwner_.assign(arcCount, -1);
    for (int v = 0; v < nA; ++v) {
        const std::size_t a = static_cast<std::size_t>(c.airportArc_[static_cast<std::size_t>(v)]);
        c.arcKind_[a]  = 0;
        c.arcOwner_[a] = v;
    }
    for (int e = 0; e < nF; ++e) {
        const std::size_t a = static_cast<std::size_t>(c.flightArc_[static_cast<std::size_t>(e)]);
        c.arcKind_[a]  = 1;
        c.arcOwner_[a] = e;
    }
    return c;
}

int CapacityNetwork::airportOfArc(int arc) const {
    if (arc < 0 || arc >= static_cast<int>(arcKind_.size())) return -1;
    return arcKind_[static_cast<std::size_t>(arc)] == 0 ? arcOwner_[static_cast<std::size_t>(arc)] : -1;
}

int CapacityNetwork::flightOfArc(int arc) const {
    if (arc < 0 || arc >= static_cast<int>(arcKind_.size())) return -1;
    return arcKind_[static_cast<std::size_t>(arc)] == 1 ? arcOwner_[static_cast<std::size_t>(arc)] : -1;
}

Cap CapacityNetwork::setArcCapacity(int arc, Cap cap) {
    FlowNetwork::Edge& e = net_.edge(arc);
    const Cap old = e.cap;
    e.cap = cap;
    return old;
}

// ---------------------------------------------------------------------------
// FlowReport
// ---------------------------------------------------------------------------

Cap FlowReport::cutCapacity() const {
    Cap total = 0;
    for (std::size_t i = 0; i < minCut.size(); ++i) total += minCut[i].capacity;
    return total;
}

// ---------------------------------------------------------------------------
// CapacityAnalyzer
// ---------------------------------------------------------------------------

CapacityAnalyzer::CapacityAnalyzer(const FlightNetwork& g, bool enforceAirportCapacity)
    : g_(g), cap_(CapacityNetwork::build(g, enforceAirportCapacity)) {}

namespace {

FlowResult dispatch(FlowAlgorithm algo, FlowNetwork& net, int s, int t) {
    switch (algo) {
        case ALGO_FORD_FULKERSON: return FordFulkerson::run(net, s, t);
        case ALGO_EDMONDS_KARP:   return EdmondsKarp::run(net, s, t);
        case ALGO_DINIC:          return Dinic::run(net, s, t);
    }
    throw std::invalid_argument("dispatch: unknown flow algorithm");
}

} // namespace

FlowReport CapacityAnalyzer::maxFlow(int origin, int destination, FlowAlgorithm algo) {
    if (origin < 0 || origin >= g_.numAirports() ||
        destination < 0 || destination >= g_.numAirports())
        throw std::out_of_range("maxFlow: airport id out of range");

    FlowReport rep;
    rep.algorithm = algo;
    if (origin == destination) return rep;

    const int s = cap_.sourceNode(origin);
    const int t = cap_.sinkNode(destination);

    cap_.net().resetFlow();
    Timer timer;
    const FlowResult fr = dispatch(algo, cap_.net(), s, t);
    rep.elapsedMs     = timer.elapsedMs();
    rep.maxFlow       = fr.value;
    rep.augmentations = fr.augmentations;
    rep.visits        = fr.visits;

    // Read the bottleneck set off the residual graph.
    const std::vector<int> cutArcs = cap_.net().minCutEdges(s);
    for (std::size_t i = 0; i < cutArcs.size(); ++i) {
        const int arc = cutArcs[i];
        CutItem item;
        item.capacity = cap_.net().edge(arc).cap;

        const int airportId = cap_.airportOfArc(arc);
        if (airportId >= 0) {
            // An unbounded throughput arc can never be a real bottleneck.
            if (item.capacity >= UNBOUNDED) continue;
            item.isAirport = true;
            item.id        = airportId;
            item.label     = g_.codeOf(airportId) + " (airport)";
            rep.minCut.push_back(item);
            continue;
        }
        const int flightId = cap_.flightOfArc(arc);
        if (flightId >= 0) {
            const Flight& f = g_.flight(flightId);
            std::ostringstream os;
            os << g_.codeOf(f.from) << "->" << g_.codeOf(f.to);
            if (!f.airline.empty()) os << " (" << f.airline << ")";
            item.isAirport = false;
            item.id        = flightId;
            item.label     = os.str();
            rep.minCut.push_back(item);
        }
    }
    return rep;
}

std::vector<FlowReport> CapacityAnalyzer::compareAlgorithms(int origin, int destination) {
    std::vector<FlowReport> out;
    out.push_back(maxFlow(origin, destination, ALGO_FORD_FULKERSON));
    out.push_back(maxFlow(origin, destination, ALGO_EDMONDS_KARP));
    out.push_back(maxFlow(origin, destination, ALGO_DINIC));
    return out;
}

Cap CapacityAnalyzer::flowWithArcClosed(int origin, int destination, int arc) {
    const Cap saved = cap_.setArcCapacity(arc, 0);
    cap_.net().resetFlow();
    const FlowResult fr = Dinic::run(cap_.net(),
                                     cap_.sourceNode(origin),
                                     cap_.sinkNode(destination));
    cap_.setArcCapacity(arc, saved);
    return fr.value;
}

namespace {

struct MoreImpact {
    bool operator()(const Impact& a, const Impact& b) const {
        if (a.flowLost != b.flowLost) return a.flowLost > b.flowLost;
        return a.id < b.id;                 // stable, reproducible ordering
    }
};

void finish(std::vector<Impact>& v, Cap baseline, int topN) {
    for (std::size_t i = 0; i < v.size(); ++i) {
        v[i].flowLost    = baseline - v[i].flowAfter;
        v[i].lostPercent = baseline > 0 ? (100.0 * static_cast<double>(v[i].flowLost) /
                                           static_cast<double>(baseline))
                                        : 0.0;
        v[i].disconnects = (v[i].flowAfter == 0);
    }
    std::sort(v.begin(), v.end(), MoreImpact());
    if (topN > 0 && static_cast<int>(v.size()) > topN)
        v.resize(static_cast<std::size_t>(topN));
}

} // namespace

std::vector<Impact> CapacityAnalyzer::rankAirports(int origin, int destination, int topN) {
    std::vector<Impact> out;
    const Cap baseline = maxFlow(origin, destination, ALGO_DINIC).maxFlow;

    for (int v = 0; v < g_.numAirports(); ++v) {
        if (v == origin || v == destination) continue;   // closing an endpoint is not interesting
        if (g_.degree(v) == 0) continue;                 // isolated airport carries nothing

        Impact im;
        im.isAirport = true;
        im.id        = v;
        im.label     = g_.codeOf(v);
        im.flowAfter = flowWithArcClosed(origin, destination, cap_.airportArc(v));
        out.push_back(im);
    }
    finish(out, baseline, topN);
    return out;
}

std::vector<Impact> CapacityAnalyzer::rankFlights(int origin, int destination, int topN) {
    std::vector<Impact> out;
    const Cap baseline = maxFlow(origin, destination, ALGO_DINIC).maxFlow;

    for (int e = 0; e < g_.numFlights(); ++e) {
        const Flight& f = g_.flight(e);
        if (f.seats <= 0) continue;

        std::ostringstream os;
        os << g_.codeOf(f.from) << "->" << g_.codeOf(f.to);
        if (!f.airline.empty()) os << " (" << f.airline << ")";

        Impact im;
        im.isAirport = false;
        im.id        = e;
        im.label     = os.str();
        im.flowAfter = flowWithArcClosed(origin, destination, cap_.flightArc(e));
        out.push_back(im);
    }
    finish(out, baseline, topN);
    return out;
}

} // namespace flightnet
