#include "flightnet/graph.hpp"

#include <stdexcept>

namespace flightnet {

int FlightNetwork::addAirport(const std::string& code, const std::string& city,
                              const std::string& country, double lat, double lon,
                              int capacity) {
    std::unordered_map<std::string, int>::const_iterator it = index_.find(code);
    if (it != index_.end()) {
        // Already present: fill in any details the earlier (stub) insert lacked.
        Airport& a = airports_[it->second];
        if (a.city.empty())    a.city    = city;
        if (a.country.empty()) a.country = country;
        if (a.capacity == 0)   a.capacity = capacity;
        if (a.lat == 0.0 && a.lon == 0.0) { a.lat = lat; a.lon = lon; }
        return it->second;
    }
    Airport a;
    a.id       = static_cast<int>(airports_.size());
    a.code     = code;
    a.city     = city;
    a.country  = country;
    a.lat      = lat;
    a.lon      = lon;
    a.capacity = capacity;
    airports_.push_back(a);
    adj_.push_back(std::vector<int>());
    radj_.push_back(std::vector<int>());
    index_[code] = a.id;
    return a.id;
}

int FlightNetwork::addFlight(int from, int to, double cost, int duration,
                             int seats, const std::string& airline) {
    if (from < 0 || from >= numAirports() || to < 0 || to >= numAirports())
        throw std::out_of_range("addFlight: airport id out of range");

    Flight f;
    f.id       = static_cast<int>(flights_.size());
    f.from     = from;
    f.to       = to;
    f.cost     = cost;
    f.duration = duration;
    f.seats    = seats;
    f.airline  = airline;
    flights_.push_back(f);
    adj_[from].push_back(f.id);
    radj_[to].push_back(f.id);
    return f.id;
}

int FlightNetwork::addFlight(const std::string& fromCode, const std::string& toCode,
                             double cost, int duration, int seats,
                             const std::string& airline) {
    const int u = addAirport(fromCode);
    const int v = addAirport(toCode);
    return addFlight(u, v, cost, duration, seats, airline);
}

void FlightNetwork::reserve(std::size_t airports, std::size_t flights) {
    airports_.reserve(airports);
    adj_.reserve(airports);
    radj_.reserve(airports);
    flights_.reserve(flights);
    index_.reserve(airports);
}

int FlightNetwork::idOf(const std::string& code) const {
    std::unordered_map<std::string, int>::const_iterator it = index_.find(code);
    return it == index_.end() ? -1 : it->second;
}

std::string FlightNetwork::codeOf(int id) const {
    if (id < 0 || id >= numAirports()) return "?";
    const std::string& c = airports_[id].code;
    if (!c.empty()) return c;
    return "#" + std::to_string(id);
}

int FlightNetwork::degree(int v) const {
    return static_cast<int>(adj_[v].size() + radj_[v].size());
}

} // namespace flightnet
