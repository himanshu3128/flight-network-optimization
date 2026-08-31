#include "flightnet/json_output.hpp"

#include "flightnet/routing.hpp"

#include <cmath>
#include <iomanip>
#include <sstream>

namespace flightnet {
namespace json {

std::string quote(const std::string& s) {
    std::ostringstream os;
    os << '"';
    for (std::size_t i = 0; i < s.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        switch (c) {
            case '"':  os << "\\\""; break;
            case '\\': os << "\\\\"; break;
            case '\b': os << "\\b";  break;
            case '\f': os << "\\f";  break;
            case '\n': os << "\\n";  break;
            case '\r': os << "\\r";  break;
            case '\t': os << "\\t";  break;
            default:
                // Control characters must be escaped; anything else (including
                // UTF-8 continuation bytes) passes through untouched.
                if (c < 0x20) {
                    os << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                       << static_cast<int>(c) << std::dec << std::setfill(' ');
                } else {
                    os << static_cast<char>(c);
                }
        }
    }
    os << '"';
    return os.str();
}

std::string num(double v) {
    // JSON has no way to spell NaN or Infinity, so emit null instead of
    // producing a document the browser cannot parse.
    if (!(v == v)) return "null";                       // NaN
    if (v > 1e300 || v < -1e300) return "null";         // infinite / unusable
    std::ostringstream os;
    os << std::setprecision(10) << v;
    return os.str();
}

std::string num(long long v) {
    std::ostringstream os;
    os << v;
    return os.str();
}

std::string route(const FlightNetwork& g, const Route& r,
                  const std::string& mode, const std::string& label) {
    std::ostringstream os;
    os << "{\"mode\":" << quote(mode)
       << ",\"label\":" << quote(label)
       << ",\"found\":" << (r.found ? "true" : "false");

    if (!r.found) {
        os << ",\"airports\":[],\"legs\":[]}";
        return os.str();
    }

    os << ",\"numLegs\":" << num(static_cast<long long>(r.numLegs()))
       << ",\"stops\":" << num(static_cast<long long>(r.stops()))
       << ",\"totalCost\":" << num(r.totalCost)
       << ",\"totalDuration\":" << num(static_cast<long long>(r.totalDuration))
       << ",\"objective\":" << num(r.objective);

    os << ",\"airports\":[";
    for (std::size_t i = 0; i < r.airports.size(); ++i) {
        if (i) os << ',';
        os << quote(g.codeOf(r.airports[i]));
    }
    os << "]";

    os << ",\"legs\":[";
    for (std::size_t i = 0; i < r.legs.size(); ++i) {
        const Flight& f = g.flight(r.legs[i]);
        if (i) os << ',';
        os << "{\"from\":" << quote(g.codeOf(f.from))
           << ",\"to\":" << quote(g.codeOf(f.to))
           << ",\"airline\":" << quote(f.airline)
           << ",\"cost\":" << num(f.cost)
           << ",\"duration\":" << num(static_cast<long long>(f.duration))
           << ",\"seats\":" << num(static_cast<long long>(f.seats))
           << "}";
    }
    os << "]}";
    return os.str();
}

std::string flowReport(const FlowReport& rep) {
    std::ostringstream os;
    os << "{\"algorithm\":" << quote(algorithmName(rep.algorithm))
       << ",\"maxFlow\":" << num(static_cast<long long>(rep.maxFlow))
       << ",\"augmentations\":" << num(static_cast<long long>(rep.augmentations))
       << ",\"visits\":" << num(static_cast<long long>(rep.visits))
       << ",\"elapsedMs\":" << num(rep.elapsedMs)
       << ",\"cutCapacity\":" << num(static_cast<long long>(rep.cutCapacity()))
       << ",\"minCut\":[";
    for (std::size_t i = 0; i < rep.minCut.size(); ++i) {
        const CutItem& c = rep.minCut[i];
        if (i) os << ',';
        os << "{\"label\":" << quote(c.label)
           << ",\"isAirport\":" << (c.isAirport ? "true" : "false")
           << ",\"capacity\":" << num(static_cast<long long>(c.capacity))
           << "}";
    }
    os << "]}";
    return os.str();
}

std::string impact(const Impact& im) {
    std::ostringstream os;
    os << "{\"label\":" << quote(im.label)
       << ",\"isAirport\":" << (im.isAirport ? "true" : "false")
       << ",\"flowAfter\":" << num(static_cast<long long>(im.flowAfter))
       << ",\"flowLost\":" << num(static_cast<long long>(im.flowLost))
       << ",\"lostPercent\":" << num(im.lostPercent)
       << ",\"disconnects\":" << (im.disconnects ? "true" : "false")
       << "}";
    return os.str();
}

std::string networkInfo(const FlightNetwork& g) {
    long long seats = 0;
    for (int e = 0; e < g.numFlights(); ++e) seats += g.flight(e).seats;

    int maxDeg = -1, busiest = -1;
    for (int v = 0; v < g.numAirports(); ++v)
        if (g.degree(v) > maxDeg) { maxDeg = g.degree(v); busiest = v; }

    // Strong connectivity tells the UI whether every origin/destination pair is
    // answerable at all.
    const Router router(g);
    int fullyConnected = 0;
    for (int v = 0; v < g.numAirports(); ++v)
        if (router.reachableCount(v) == g.numAirports()) ++fullyConnected;

    std::ostringstream os;
    os << "{\"numAirports\":" << num(static_cast<long long>(g.numAirports()))
       << ",\"numFlights\":" << num(static_cast<long long>(g.numFlights()))
       << ",\"totalSeats\":" << num(seats)
       << ",\"busiest\":" << quote(busiest >= 0 ? g.codeOf(busiest) : std::string())
       << ",\"stronglyConnected\":" << (fullyConnected == g.numAirports() ? "true" : "false")
       << ",\"airports\":[";
    for (int v = 0; v < g.numAirports(); ++v) {
        const Airport& a = g.airport(v);
        if (v) os << ',';
        os << "{\"code\":" << quote(g.codeOf(v))
           << ",\"city\":" << quote(a.city)
           << ",\"country\":" << quote(a.country)
           << ",\"capacity\":" << num(static_cast<long long>(a.capacity))
           << ",\"lat\":" << num(a.lat)
           << ",\"lon\":" << num(a.lon)
           << ",\"out\":" << num(static_cast<long long>(g.outgoing(v).size()))
           << ",\"in\":" << num(static_cast<long long>(g.incoming(v).size()))
           << "}";
    }
    os << "]}";
    return os.str();
}

std::string error(const std::string& message) {
    return "{\"ok\":false,\"error\":" + quote(message) + "}";
}

} // namespace json
} // namespace flightnet
