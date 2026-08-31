// capacity.hpp -- Airport capacity as a flow network, and resilience analysis.
//
// A flight network has capacity on its *airports* (gates, runway slots, ground
// handling) as well as on its flight legs, but max-flow only understands edge
// capacity. The standard fix is node splitting: every airport v becomes
//
//        in(v)  --[ airport throughput ]-->  out(v)
//
// with every arriving leg landing on in(v) and every departing leg leaving from
// out(v). Any flow through the airport is then forced across the throughput arc,
// so an airport ceiling becomes an ordinary edge ceiling.
//
// Splitting also makes failure simulation cheap: closing an airport is just
// setting its throughput arc to zero capacity, with no rebuild.
#ifndef FLIGHTNET_CAPACITY_HPP
#define FLIGHTNET_CAPACITY_HPP

#include "flightnet/graph.hpp"
#include "flightnet/maxflow.hpp"

#include <string>
#include <vector>

namespace flightnet {

// Which max-flow solver to use.
enum FlowAlgorithm {
    ALGO_FORD_FULKERSON,
    ALGO_EDMONDS_KARP,
    ALGO_DINIC
};

const char* algorithmName(FlowAlgorithm a);

// The split flow network, plus the maps back to airports and flights.
class CapacityNetwork {
public:
    // `enforceAirportCapacity == false` leaves every throughput arc unbounded,
    // which isolates the effect of leg capacity alone.
    static CapacityNetwork build(const FlightNetwork& g, bool enforceAirportCapacity = true);

    int inNode(int airportId)  const { return 2 * airportId; }
    int outNode(int airportId) const { return 2 * airportId + 1; }

    // Arc carrying airport v's throughput (the in->out arc).
    int airportArc(int airportId) const { return airportArc_[static_cast<std::size_t>(airportId)]; }
    // Arc carrying flight e.
    int flightArc(int flightId)   const { return flightArc_[static_cast<std::size_t>(flightId)]; }

    // Inverse maps for reading a min-cut back out. Return -1 when the arc is not
    // of that kind.
    int airportOfArc(int arc) const;
    int flightOfArc(int arc)  const;

    // Traffic enters at the origin's in-node and leaves at the destination's
    // out-node, so both endpoints spend their own throughput on the trip.
    int sourceNode(int origin)      const { return inNode(origin); }
    int sinkNode(int destination)   const { return outNode(destination); }

    FlowNetwork&       net()       { return net_; }
    const FlowNetwork& net() const { return net_; }

    // Temporarily overrides an arc's capacity; returns the previous value so the
    // caller can restore it. Used to simulate closures without rebuilding.
    Cap setArcCapacity(int arc, Cap cap);

private:
    FlowNetwork      net_;
    std::vector<int> airportArc_;   // airport id -> arc id
    std::vector<int> flightArc_;    // flight id  -> arc id
    std::vector<int> arcKind_;      // arc id -> 0 airport, 1 flight, -1 other
    std::vector<int> arcOwner_;     // arc id -> airport id or flight id
};

// One bottleneck found in the minimum cut.
struct CutItem {
    bool        isAirport = false;   // false -> a flight leg
    int         id        = -1;      // airport id or flight id
    Cap         capacity  = 0;
    std::string label;               // "LHR (airport)" or "JFK->LHR (BA117)"
};

// Result of one maximum-flow computation.
struct FlowReport {
    Cap                  maxFlow       = 0;
    long                 augmentations = 0;
    long                 visits        = 0;
    double               elapsedMs     = 0.0;
    FlowAlgorithm        algorithm     = ALGO_DINIC;
    std::vector<CutItem> minCut;       // saturated arcs that separate origin from destination

    Cap cutCapacity() const;           // must equal maxFlow, by max-flow min-cut
};

// How much throughput is lost when a single airport or leg goes away.
struct Impact {
    bool        isAirport   = false;
    int         id          = -1;
    std::string label;
    Cap         flowAfter   = 0;
    Cap         flowLost    = 0;
    double      lostPercent = 0.0;
    bool        disconnects = false;   // no route survives at all
};

class CapacityAnalyzer {
public:
    // Keeps a reference to `g`; the graph must outlive the analyzer.
    CapacityAnalyzer(const FlightNetwork& g, bool enforceAirportCapacity = true);

    // Maximum passengers/movements routable from origin to destination.
    FlowReport maxFlow(int origin, int destination, FlowAlgorithm algo = ALGO_DINIC);

    // Runs all three solvers on the same instance and returns their reports in
    // the order Ford-Fulkerson, Edmonds-Karp, Dinic. Used by the CLI and tests
    // to confirm the three agree.
    std::vector<FlowReport> compareAlgorithms(int origin, int destination);

    // Airports ranked by the throughput lost when each one closes. The origin
    // and destination themselves are skipped. `topN <= 0` returns every airport.
    std::vector<Impact> rankAirports(int origin, int destination, int topN = 10);

    // Same, for individual flight legs.
    std::vector<Impact> rankFlights(int origin, int destination, int topN = 10);

private:
    Cap flowWithArcClosed(int origin, int destination, int arc);

    const FlightNetwork& g_;
    CapacityNetwork      cap_;
};

} // namespace flightnet

#endif // FLIGHTNET_CAPACITY_HPP
