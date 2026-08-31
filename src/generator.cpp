#include "flightnet/generator.hpp"

#include <algorithm>
#include <random>
#include <set>
#include <utility>

namespace flightnet {

std::string syntheticCode(int i) {
    // A000..A999, B000..B999, ... then wraps into a two-letter prefix.
    const int block = i / 1000;
    const int rest  = i % 1000;
    std::string s;
    if (block < 26) {
        s += static_cast<char>('A' + block);
    } else {
        s += static_cast<char>('A' + (block / 26) % 26);
        s += static_cast<char>('A' + block % 26);
    }
    s += static_cast<char>('0' + (rest / 100) % 10);
    s += static_cast<char>('0' + (rest / 10) % 10);
    s += static_cast<char>('0' + rest % 10);
    return s;
}

namespace {

// Longer legs cost more and carry bigger aircraft, so attributes are correlated
// rather than drawn independently. Uniform noise on every field would make the
// cost-optimal and time-optimal routes nearly identical, which is not a useful
// test of multi-objective routing.
struct LegAttributes {
    double cost;
    int    duration;
    int    seats;
};

LegAttributes drawLeg(std::mt19937& rng, const GeneratorConfig& cfg, bool trunk) {
    std::uniform_real_distribution<double> unit(0.0, 1.0);
    const double len = unit(rng);                       // 0 = short hop, 1 = long haul

    LegAttributes a;
    a.duration = cfg.minDuration +
                 static_cast<int>(len * (cfg.maxDuration - cfg.minDuration));

    // Fare tracks distance but with real scatter, so cheapest != fastest.
    const double jitter = 0.65 + 0.7 * unit(rng);
    a.cost = (cfg.minCost + len * (cfg.maxCost - cfg.minCost)) * jitter;
    if (a.cost < cfg.minCost) a.cost = cfg.minCost;

    // Trunk routes between hubs get wide-body capacity.
    const double sizeMix = trunk ? (0.55 + 0.45 * unit(rng)) : unit(rng);
    a.seats = cfg.minSeats +
              static_cast<int>(sizeMix * (cfg.maxSeats - cfg.minSeats));
    if (a.seats < 1) a.seats = 1;
    return a;
}

const char* const AIRLINES[] = {"AA", "BA", "DL", "EK", "LH", "QF", "SQ", "UA"};
const int NUM_AIRLINES = 8;

} // namespace

FlightNetwork generate(const GeneratorConfig& cfg) {
    FlightNetwork g;
    const int n = cfg.airports < 2 ? 2 : cfg.airports;
    const int hubCount = std::max(0, std::min(cfg.hubs, n));

    std::mt19937 rng(cfg.seed);
    std::uniform_real_distribution<double> unit(0.0, 1.0);
    std::uniform_int_distribution<int>     airlinePick(0, NUM_AIRLINES - 1);

    g.reserve(static_cast<std::size_t>(n),
              static_cast<std::size_t>(n) * 8);

    // --- airports ----------------------------------------------------------
    // The first `hubCount` indices are the hubs; that keeps the layout stable
    // across seeds so benchmarks compare like with like.
    for (int i = 0; i < n; ++i) {
        const bool isHub = i < hubCount;
        int capacity = 0;
        if (cfg.maxCapacity > 0) {
            const int span = std::max(0, cfg.maxCapacity - cfg.minCapacity);
            capacity = cfg.minCapacity + static_cast<int>(unit(rng) * span);
            if (isHub) capacity = static_cast<int>(capacity * cfg.hubCapacityBoost);
        }
        // Coordinates are cosmetic here, but keep the CSV round-trip meaningful.
        const double lat = -60.0 + 120.0 * unit(rng);
        const double lon = -180.0 + 360.0 * unit(rng);
        g.addAirport(syntheticCode(i), "City" + std::to_string(i), "XX", lat, lon, capacity);
    }

    // Avoid emitting the same directed pair twice; parallel legs would inflate
    // capacity in a way the config does not describe.
    std::set<std::pair<int, int> > placed;

    const auto addLeg = [&](int u, int v, bool trunk) {
        if (u == v) return;
        if (!placed.insert(std::make_pair(u, v)).second) return;
        const LegAttributes a = drawLeg(rng, cfg, trunk);
        g.addFlight(u, v, a.cost, a.duration, a.seats, AIRLINES[airlinePick(rng)]);
    };

    const auto addRoute = [&](int u, int v, bool trunk) {
        addLeg(u, v, trunk);
        if (cfg.bidirectional) addLeg(v, u, trunk);
    };

    // --- trunk: hubs fully interconnected ----------------------------------
    for (int i = 0; i < hubCount; ++i)
        for (int j = i + 1; j < hubCount; ++j)
            addRoute(i, j, true);

    // --- spokes ------------------------------------------------------------
    for (int v = hubCount; v < n; ++v) {
        bool attached = false;
        for (int h = 0; h < hubCount; ++h) {
            if (unit(rng) < cfg.spokeProb) { addRoute(v, h, false); attached = true; }
        }
        // Every spoke needs at least one hub, or it is stranded by construction.
        if (!attached && hubCount > 0) {
            std::uniform_int_distribution<int> hubPick(0, hubCount - 1);
            addRoute(v, hubPick(rng), false);
        }
    }

    // --- point-to-point mesh ----------------------------------------------
    if (cfg.meshProb > 0.0) {
        for (int u = 0; u < n; ++u)
            for (int v = u + 1; v < n; ++v)
                if (unit(rng) < cfg.meshProb) addRoute(u, v, false);
    }

    // --- connectivity guarantee -------------------------------------------
    // A directed ring makes the whole network strongly connected, so a routing
    // query between any two airports is answerable and benchmarks never
    // degenerate into measuring "unreachable".
    if (cfg.forceConnected) {
        for (int i = 0; i < n; ++i) addLeg(i, (i + 1) % n, false);
    }

    return g;
}

} // namespace flightnet
