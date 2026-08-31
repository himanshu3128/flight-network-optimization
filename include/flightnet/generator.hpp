// generator.hpp -- Reproducible synthetic flight networks for stress testing.
//
// Real route maps are not Erdos-Renyi random graphs: they are hub-and-spoke with
// a thin mesh of point-to-point legs on top. The generator reproduces that shape
// so the flow benchmarks exercise realistic bottlenecks (a few high-degree hubs)
// rather than uniform noise.
#ifndef FLIGHTNET_GENERATOR_HPP
#define FLIGHTNET_GENERATOR_HPP

#include "flightnet/graph.hpp"

#include <cstdint>
#include <string>

namespace flightnet {

struct GeneratorConfig {
    int      airports        = 40;
    int      hubs            = 3;      // hub-and-spoke backbone size
    double   spokeProb       = 0.55;   // chance a given non-hub attaches to a given hub
    double   meshProb        = 0.05;   // chance of an extra point-to-point leg
    bool     bidirectional   = true;   // add the return leg for each route
    bool     forceConnected  = true;   // lay down a ring so every airport is reachable

    int      minSeats        = 60;
    int      maxSeats        = 380;
    int      minCapacity     = 300;    // airport throughput, 0 disables the ceiling
    int      maxCapacity     = 2500;
    double   minCost         = 45.0;
    double   maxCost         = 1400.0;
    int      minDuration     = 40;     // minutes
    int      maxDuration     = 760;

    uint32_t seed            = 1u;

    // Hubs get a throughput multiplier; a hub really is bigger than a spoke.
    double   hubCapacityBoost = 3.0;
};

// Builds a network from `cfg`. The same seed always yields the same network,
// which is what makes benchmark runs and failures reproducible.
FlightNetwork generate(const GeneratorConfig& cfg);

// Short deterministic airport code for index i: A000, A001, ..., B000, ...
std::string syntheticCode(int i);

} // namespace flightnet

#endif // FLIGHTNET_GENERATOR_HPP
