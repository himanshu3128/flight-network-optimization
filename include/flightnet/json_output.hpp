// json_output.hpp -- Machine-readable output for the CLI's --json mode.
//
// The web front end drives the same binary a human would, so it needs a stable
// output format. Emitting JSON from C++ is far more robust than having the
// server scrape the human-formatted tables, which would break the moment a
// column width changed.
#ifndef FLIGHTNET_JSON_OUTPUT_HPP
#define FLIGHTNET_JSON_OUTPUT_HPP

#include "flightnet/capacity.hpp"
#include "flightnet/graph.hpp"
#include "flightnet/routing.hpp"

#include <string>
#include <vector>

namespace flightnet {
namespace json {

// Escapes a string and wraps it in quotes, ready to drop into an object.
std::string quote(const std::string& s);

// Numbers, guarded against NaN and infinity (which are not legal JSON).
std::string num(double v);
std::string num(long long v);

// {"found":..,"airports":[..],"legs":[..],"totalCost":..,..}
// `mode` and `label` describe which objective produced the route.
std::string route(const FlightNetwork& g, const Route& r,
                  const std::string& mode, const std::string& label);

// {"algorithm":"Dinic","maxFlow":..,"minCut":[..],..}
std::string flowReport(const FlowReport& rep);

// {"label":"DXB","flowAfter":..,"flowLost":..,"lostPercent":..,..}
std::string impact(const Impact& im);

// Summary of the loaded network, including the airport list the UI needs to
// populate its dropdowns.
std::string networkInfo(const FlightNetwork& g);

// {"ok":false,"error":".."}
std::string error(const std::string& message);

} // namespace json
} // namespace flightnet

#endif // FLIGHTNET_JSON_OUTPUT_HPP
