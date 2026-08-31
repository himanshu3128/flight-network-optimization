// graph.hpp -- Core flight network model: airports (vertices) and flights (directed edges).
//
// Storage is a compressed adjacency list of edge indices, which keeps the BFS and
// Dijkstra traversals cache-friendly and gives the O(V+E) / O(E log V) bounds the
// routing layer advertises.
#ifndef FLIGHTNET_GRAPH_HPP
#define FLIGHTNET_GRAPH_HPP

#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>

namespace flightnet {

// A vertex in the flight network.
struct Airport {
    int         id       = -1;   // dense index into FlightNetwork::airports
    std::string code;            // IATA-style code, e.g. "JFK"
    std::string city;
    std::string country;
    double      lat      = 0.0;
    double      lon      = 0.0;
    // Throughput ceiling: how many aircraft movements the airport can handle per
    // planning window. Used as the node capacity when we build the flow network.
    int         capacity = 0;
};

// A directed edge: one scheduled flight leg.
struct Flight {
    int         id       = -1;
    int         from     = -1;
    int         to       = -1;
    double      cost     = 0.0;  // fare in currency units
    int         duration = 0;    // block time in minutes
    int         seats    = 0;    // per-leg capacity, the edge capacity for max-flow
    std::string airline;
};

// Directed multigraph of airports and flight legs.
class FlightNetwork {
public:
    // --- construction -----------------------------------------------------
    // Adds an airport, or returns the existing id if the code is already known.
    int addAirport(const std::string& code,
                   const std::string& city    = "",
                   const std::string& country = "",
                   double lat = 0.0, double lon = 0.0,
                   int capacity = 0);

    // Adds a directed leg. Both endpoints must already exist.
    int addFlight(int from, int to, double cost, int duration,
                  int seats, const std::string& airline = "");

    // Convenience overload that resolves (and creates) airports by code.
    int addFlight(const std::string& fromCode, const std::string& toCode,
                  double cost, int duration, int seats,
                  const std::string& airline = "");

    // Reserves storage; worthwhile when loading large synthetic networks.
    void reserve(std::size_t airports, std::size_t flights);

    // --- lookup -----------------------------------------------------------
    int  idOf(const std::string& code) const;      // -1 when unknown
    bool hasAirport(const std::string& code) const { return idOf(code) >= 0; }

    const Airport& airport(int id) const { return airports_[id]; }
    const Flight&  flight(int id)  const { return flights_[id]; }

    // Edge indices leaving `v`. Indexes into flights().
    const std::vector<int>& outgoing(int v) const { return adj_[v]; }
    // Edge indices entering `v`.
    const std::vector<int>& incoming(int v) const { return radj_[v]; }

    int numAirports() const { return static_cast<int>(airports_.size()); }
    int numFlights()  const { return static_cast<int>(flights_.size()); }

    const std::vector<Airport>& airports() const { return airports_; }
    const std::vector<Flight>&  flights()  const { return flights_; }

    // Airport code, or "#<id>" when the network was generated without codes.
    std::string codeOf(int id) const;

    // Out-degree + in-degree; a cheap proxy for how hub-like an airport is.
    int degree(int v) const;

private:
    std::vector<Airport>                   airports_;
    std::vector<Flight>                    flights_;
    std::vector<std::vector<int> >         adj_;
    std::vector<std::vector<int> >         radj_;
    std::unordered_map<std::string, int>   index_;
};

} // namespace flightnet

#endif // FLIGHTNET_GRAPH_HPP
