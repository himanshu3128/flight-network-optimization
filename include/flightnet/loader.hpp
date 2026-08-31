// loader.hpp -- CSV import/export and the bundled example network.
#ifndef FLIGHTNET_LOADER_HPP
#define FLIGHTNET_LOADER_HPP

#include "flightnet/graph.hpp"

#include <stdexcept>
#include <string>

namespace flightnet {

// Thrown for a missing file or a malformed row; the message names the file and
// line so a bad dataset is easy to fix.
class DataError : public std::runtime_error {
public:
    explicit DataError(const std::string& what) : std::runtime_error(what) {}
};

// airports.csv: code,city,country,lat,lon,capacity
// routes.csv:   from,to,cost,duration_min,seats,airline
//
// Blank lines and lines starting with '#' are ignored, and a header row is
// detected and skipped automatically. Routes referring to an unknown airport
// code create a stub airport rather than failing the load.
FlightNetwork loadFromCsv(const std::string& airportsPath, const std::string& routesPath);

// Writes the network back out in the same format.
void saveToCsv(const FlightNetwork& g,
               const std::string& airportsPath,
               const std::string& routesPath);

// A small hand-built intercontinental network, used when no data directory is
// present so the CLI always has something to demonstrate.
FlightNetwork sampleNetwork();

} // namespace flightnet

#endif // FLIGHTNET_LOADER_HPP
