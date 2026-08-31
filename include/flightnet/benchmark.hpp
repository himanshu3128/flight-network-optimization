// benchmark.hpp -- Stress harness over large batches of synthetic networks.
//
// The harness has two jobs at once:
//   1. correctness -- every solver must return the same maximum flow on every
//      network, and that value must equal the capacity of the minimum cut;
//   2. performance -- compare wall-clock time of Ford-Fulkerson against Dinic
//      over thousands of independent networks, reported as a distribution
//      rather than a single lucky run.
#ifndef FLIGHTNET_BENCHMARK_HPP
#define FLIGHTNET_BENCHMARK_HPP

#include <cstdint>
#include <iosfwd>
#include <string>
#include <vector>

namespace flightnet {

struct BenchConfig {
    int      networks     = 10000;
    int      minAirports  = 20;
    int      maxAirports  = 60;
    int      hubs         = 3;
    double   meshProb     = 0.05;
    double   spokeProb    = 0.55;
    uint32_t seed         = 20240501u;

    bool     runEdmondsKarp = false;  // the O(V E^2) baseline; slow, off by default
    bool     runRouting     = true;   // also time BFS and Dijkstra
    bool     verify         = true;   // cross-check flow values and the min cut
    int      progressEvery  = 1000;   // 0 silences progress output

    std::string csvPath;              // optional per-network sample dump
};

// Summary of one timing series, in milliseconds.
struct Stats {
    long   count  = 0;
    double total  = 0.0;
    double mean   = 0.0;
    double median = 0.0;
    double p90    = 0.0;
    double p99    = 0.0;
    double min    = 0.0;
    double max    = 0.0;

    // Consumes (sorts) `samples`.
    static Stats from(std::vector<double>& samples);
};

struct BenchResult {
    long        networks       = 0;
    long long   totalAirports  = 0;
    long long   totalFlights   = 0;
    long long   totalFlow      = 0;

    Stats ff;          // Ford-Fulkerson
    Stats ek;          // Edmonds-Karp (only when enabled)
    Stats dinic;
    Stats bfs;         // minimum-stop routing
    Stats dijkstra;    // cost-optimal routing

    long long   ffAugmentations   = 0;
    long long   dinicPhases       = 0;

    // Total FF time / total Dinic time. Aggregate rather than per-network so a
    // few trivial networks cannot skew it.
    double      speedup           = 0.0;
    // Median of the per-network ratio: the speedup a typical network sees.
    double      medianSpeedup     = 0.0;

    long        flowMismatches    = 0;   // solvers disagreed
    long        cutMismatches     = 0;   // max-flow != min-cut capacity
    long        routeMismatches   = 0;   // BFS/Dijkstra optimality violations
    bool        ekEnabled         = false;

    bool ok() const {
        return flowMismatches == 0 && cutMismatches == 0 && routeMismatches == 0;
    }
};

// Runs the batch. Progress is written to `log`.
BenchResult runBenchmark(const BenchConfig& cfg, std::ostream& log);

// Formats a completed run as a report.
void printBenchmark(const BenchResult& r, const BenchConfig& cfg, std::ostream& os);

// One row of the scaling study.
struct ScalingPoint {
    int         minAirports = 0;
    int         maxAirports = 0;
    double      avgAirports = 0.0;
    double      avgFlights  = 0.0;
    BenchResult result;
};

// Repeats the batch across a ladder of network sizes. The Ford-Fulkerson to
// Dinic speedup is not a single number -- it grows with the network, because
// Ford-Fulkerson pays per unit of flow while Dinic pays per phase. Reporting the
// ladder is the honest way to state it.
std::vector<ScalingPoint> runScalingStudy(const BenchConfig& base,
                                          const std::vector<std::pair<int, int> >& sizes,
                                          std::ostream& log);

void printScaling(const std::vector<ScalingPoint>& rows, std::ostream& os);

} // namespace flightnet

#endif // FLIGHTNET_BENCHMARK_HPP
