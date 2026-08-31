#include "flightnet/loader.hpp"

#include <cstdlib>
#include <fstream>
#include <sstream>

namespace flightnet {

namespace {

std::string trim(const std::string& s) {
    std::size_t b = 0, e = s.size();
    while (b < e && (s[b] == ' ' || s[b] == '\t' || s[b] == '\r' || s[b] == '\n')) ++b;
    while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r' || s[e - 1] == '\n')) --e;
    return s.substr(b, e - b);
}

std::vector<std::string> splitCsv(const std::string& line) {
    std::vector<std::string> out;
    std::string field;
    std::istringstream is(line);
    while (std::getline(is, field, ',')) out.push_back(trim(field));
    return out;
}

bool skippable(const std::string& line) {
    const std::string t = trim(line);
    return t.empty() || t[0] == '#';
}

// A header row is any first data line whose numeric columns do not parse.
bool looksLikeHeader(const std::vector<std::string>& cols, std::size_t numericCol) {
    if (cols.size() <= numericCol) return false;
    const std::string& s = cols[numericCol];
    if (s.empty()) return false;
    char* end = 0;
    std::strtod(s.c_str(), &end);
    return end == s.c_str();          // nothing consumed -> not a number -> header
}

double toDouble(const std::string& s, const std::string& file, int line) {
    char* end = 0;
    const double v = std::strtod(s.c_str(), &end);
    if (end == s.c_str())
        throw DataError(file + ":" + std::to_string(line) + ": expected a number, got '" + s + "'");
    return v;
}

int toInt(const std::string& s, const std::string& file, int line) {
    return static_cast<int>(toDouble(s, file, line));
}

} // namespace

FlightNetwork loadFromCsv(const std::string& airportsPath, const std::string& routesPath) {
    FlightNetwork g;

    // --- airports ----------------------------------------------------------
    {
        std::ifstream in(airportsPath.c_str());
        if (!in) throw DataError("cannot open airports file: " + airportsPath);

        std::string line;
        int lineNo = 0;
        bool firstData = true;
        while (std::getline(in, line)) {
            ++lineNo;
            if (skippable(line)) continue;
            const std::vector<std::string> c = splitCsv(line);
            if (firstData) {
                firstData = false;
                if (looksLikeHeader(c, 5)) continue;    // capacity column
            }
            if (c.size() < 6)
                throw DataError(airportsPath + ":" + std::to_string(lineNo) +
                                ": expected 6 columns (code,city,country,lat,lon,capacity), got " +
                                std::to_string(c.size()));
            g.addAirport(c[0], c[1], c[2],
                         toDouble(c[3], airportsPath, lineNo),
                         toDouble(c[4], airportsPath, lineNo),
                         toInt(c[5], airportsPath, lineNo));
        }
    }

    // --- routes ------------------------------------------------------------
    {
        std::ifstream in(routesPath.c_str());
        if (!in) throw DataError("cannot open routes file: " + routesPath);

        std::string line;
        int lineNo = 0;
        bool firstData = true;
        while (std::getline(in, line)) {
            ++lineNo;
            if (skippable(line)) continue;
            const std::vector<std::string> c = splitCsv(line);
            if (firstData) {
                firstData = false;
                if (looksLikeHeader(c, 2)) continue;    // cost column
            }
            if (c.size() < 5)
                throw DataError(routesPath + ":" + std::to_string(lineNo) +
                                ": expected at least 5 columns (from,to,cost,duration,seats), got " +
                                std::to_string(c.size()));
            const std::string airline = c.size() >= 6 ? c[5] : std::string();
            g.addFlight(c[0], c[1],
                        toDouble(c[2], routesPath, lineNo),
                        toInt(c[3], routesPath, lineNo),
                        toInt(c[4], routesPath, lineNo),
                        airline);
        }
    }

    return g;
}

void saveToCsv(const FlightNetwork& g,
               const std::string& airportsPath,
               const std::string& routesPath) {
    {
        std::ofstream out(airportsPath.c_str());
        if (!out) throw DataError("cannot write airports file: " + airportsPath);
        out << "code,city,country,lat,lon,capacity\n";
        for (int v = 0; v < g.numAirports(); ++v) {
            const Airport& a = g.airport(v);
            out << a.code << ',' << a.city << ',' << a.country << ','
                << a.lat << ',' << a.lon << ',' << a.capacity << '\n';
        }
    }
    {
        std::ofstream out(routesPath.c_str());
        if (!out) throw DataError("cannot write routes file: " + routesPath);
        out << "from,to,cost,duration_min,seats,airline\n";
        for (int e = 0; e < g.numFlights(); ++e) {
            const Flight& f = g.flight(e);
            out << g.codeOf(f.from) << ',' << g.codeOf(f.to) << ','
                << f.cost << ',' << f.duration << ',' << f.seats << ',' << f.airline << '\n';
        }
    }
}

FlightNetwork sampleNetwork() {
    FlightNetwork g;

    //          code   city            country  lat      lon       capacity
    g.addAirport("JFK", "New York",    "US",  40.6413,  -73.7781, 1400);
    g.addAirport("ORD", "Chicago",     "US",  41.9742,  -87.9073, 1200);
    g.addAirport("LAX", "Los Angeles", "US",  33.9416, -118.4085, 1100);
    g.addAirport("LHR", "London",      "GB",  51.4700,   -0.4543, 1500);
    g.addAirport("CDG", "Paris",       "FR",  49.0097,    2.5479, 1000);
    g.addAirport("FRA", "Frankfurt",   "DE",  50.0379,    8.5622, 1300);
    g.addAirport("DXB", "Dubai",       "AE",  25.2532,   55.3657, 1600);
    g.addAirport("DEL", "Delhi",       "IN",  28.5562,   77.1000,  900);
    g.addAirport("SIN", "Singapore",   "SG",   1.3644,  103.9915, 1250);
    g.addAirport("HND", "Tokyo",       "JP",  35.5494,  139.7798, 1050);
    g.addAirport("SYD", "Sydney",      "AU", -33.9399,  151.1753,  800);
    g.addAirport("GRU", "Sao Paulo",   "BR", -23.4356,  -46.4731,  700);

    // from   to     cost   min  seats airline
    g.addFlight("JFK", "LHR",  480.0, 420, 300, "BA");
    g.addFlight("LHR", "JFK",  460.0, 465, 300, "BA");
    g.addFlight("JFK", "CDG",  520.0, 435, 240, "AF");
    g.addFlight("JFK", "ORD",  145.0, 155, 180, "AA");
    g.addFlight("ORD", "JFK",  140.0, 145, 180, "AA");
    g.addFlight("ORD", "LAX",  190.0, 260, 200, "UA");
    g.addFlight("LAX", "HND",  790.0, 690, 260, "NH");
    g.addFlight("LAX", "SYD",  980.0, 900, 280, "QF");
    g.addFlight("LHR", "FRA",  120.0,  95, 150, "LH");
    g.addFlight("LHR", "DXB",  610.0, 420, 340, "EK");
    g.addFlight("CDG", "FRA",  110.0,  80, 140, "LH");
    g.addFlight("CDG", "DXB",  590.0, 400, 300, "EK");
    g.addFlight("FRA", "DXB",  540.0, 375, 320, "EK");
    g.addFlight("FRA", "DEL",  620.0, 450, 260, "LH");
    g.addFlight("DXB", "DEL",  230.0, 195, 280, "EK");
    g.addFlight("DXB", "SIN",  430.0, 445, 350, "EK");
    g.addFlight("DXB", "SYD",  890.0, 840, 300, "EK");
    g.addFlight("DEL", "SIN",  310.0, 335, 220, "SQ");
    g.addFlight("SIN", "HND",  420.0, 420, 250, "SQ");
    g.addFlight("SIN", "SYD",  510.0, 480, 270, "QF");
    g.addFlight("HND", "SYD",  700.0, 585, 240, "QF");
    g.addFlight("JFK", "GRU",  640.0, 590, 200, "LA");
    g.addFlight("GRU", "SYD", 1150.0, 900, 180, "QF");
    g.addFlight("LHR", "SIN",  760.0, 800, 290, "SQ");

    return g;
}

} // namespace flightnet
