// routing.hpp -- Shortest-path routing over the flight network.
//
//   minStops()  breadth-first search,               O(V + E)
//   cheapest()  Dijkstra with a binary heap,        O(E log V)
//   best()      Dijkstra over a scalarized multi-objective edge weight
//   kBest()     Yen's algorithm over best(), for itinerary alternates
#ifndef FLIGHTNET_ROUTING_HPP
#define FLIGHTNET_ROUTING_HPP

#include "flightnet/graph.hpp"

#include <limits>
#include <string>
#include <vector>

namespace flightnet {

// Relative importance of each objective. The router minimizes
//     sum over legs of ( cost*fare + time*minutes + perLeg )
// so `perLeg` is what discourages extra connections: it is the fixed price the
// traveller puts on one more change of plane.
struct RouteWeights {
    double cost   = 1.0;   // per currency unit of fare
    double time   = 0.0;   // per minute of block time
    double perLeg = 0.0;   // flat penalty charged once per flight leg

    static RouteWeights minCost()  { RouteWeights w; w.cost = 1.0; w.time = 0.0;  w.perLeg = 0.0;   return w; }
    static RouteWeights minTime()  { RouteWeights w; w.cost = 0.0; w.time = 1.0;  w.perLeg = 60.0;  return w; }
    // A traveller who cares about money, values an hour at ~100 units, and would
    // pay ~75 units to avoid a connection.
    static RouteWeights balanced() { RouteWeights w; w.cost = 1.0; w.time = 100.0 / 60.0; w.perLeg = 75.0; return w; }

    bool valid() const { return cost >= 0.0 && time >= 0.0 && perLeg >= 0.0; }
};

// A concrete itinerary.
struct Route {
    bool             found    = false;
    std::vector<int> airports;        // vertex sequence, source .. destination
    std::vector<int> legs;            // flight ids, size() == airports.size() - 1
    double           totalCost     = 0.0;
    int              totalDuration = 0;   // minutes of block time
    double           objective     = 0.0; // scalarized value that was minimized

    int numLegs() const { return static_cast<int>(legs.size()); }
    // Intermediate stops, i.e. connections. A nonstop route has 0.
    int stops()   const { return legs.empty() ? 0 : static_cast<int>(legs.size()) - 1; }

    // Human-readable "JFK -> LHR -> DXB  | 2 legs, ...".
    std::string describe(const FlightNetwork& g) const;
};

// Constraints used by Yen's algorithm and by the resilience analysis to route
// around a failed airport or a cancelled leg.
struct RouteBan {
    std::vector<bool> airport;   // airport[v] == true  -> v may not be used
    std::vector<bool> flight;    // flight[e]  == true  -> leg e may not be used

    RouteBan() {}
    RouteBan(int nAirports, int nFlights)
        : airport(static_cast<std::size_t>(nAirports), false),
          flight(static_cast<std::size_t>(nFlights), false) {}

    bool empty() const { return airport.empty() && flight.empty(); }
};

class Router {
public:
    explicit Router(const FlightNetwork& g) : g_(g) {}

    // Fewest flight legs. Unweighted BFS, O(V + E).
    Route minStops(int src, int dst) const;

    // Cheapest fare. Dijkstra, O(E log V).
    Route cheapest(int src, int dst) const;

    // Shortest block time, with a connection penalty so it does not chase
    // absurd 12-leg itineraries that happen to save a minute.
    Route fastest(int src, int dst) const;

    // Multi-objective: minimizes the scalarized weight described on RouteWeights.
    Route best(int src, int dst, const RouteWeights& w) const;

    // Same, but forbidden to touch banned airports or legs.
    Route best(int src, int dst, const RouteWeights& w, const RouteBan& ban) const;

    // Yen's K shortest loopless routes, ordered by objective value ascending.
    // Returns fewer than K entries when the network has no more distinct routes.
    std::vector<Route> kBest(int src, int dst, int K, const RouteWeights& w) const;

    // Scalarized distance from `src` to every airport; infinity when unreachable.
    std::vector<double> distancesFrom(int src, const RouteWeights& w) const;

    // Airports reachable from `src` by any sequence of legs (BFS).
    std::vector<bool> reachableFrom(int src) const;

    // Number of airports reachable from `src`, including `src` itself.
    int reachableCount(int src) const;

    static double infinity() { return std::numeric_limits<double>::infinity(); }

private:
    // Scalarized weight of one leg. Always non-negative for a valid RouteWeights,
    // which is what Dijkstra requires.
    double edgeWeight(const Flight& f, const RouteWeights& w) const {
        return w.cost * f.cost + w.time * static_cast<double>(f.duration) + w.perLeg;
    }

    // Rebuilds a Route from a parent-edge array produced by BFS or Dijkstra.
    Route buildRoute(int src, int dst, const std::vector<int>& parentEdge,
                     const RouteWeights& w) const;

    const FlightNetwork& g_;
};

} // namespace flightnet

#endif // FLIGHTNET_ROUTING_HPP
