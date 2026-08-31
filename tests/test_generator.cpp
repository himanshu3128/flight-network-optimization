#include "test_framework.hpp"

#include "flightnet/generator.hpp"
#include "flightnet/routing.hpp"

#include <set>
#include <string>

using namespace flightnet;

TEST(generator_is_deterministic_for_a_seed) {
    // Reproducibility is the whole point: a benchmark failure has to be
    // re-creatable from its seed alone.
    GeneratorConfig cfg;
    cfg.airports = 30;
    cfg.seed     = 777;

    const FlightNetwork a = generate(cfg);
    const FlightNetwork b = generate(cfg);

    REQUIRE(a.numAirports() == b.numAirports());
    REQUIRE(a.numFlights() == b.numFlights());
    for (int e = 0; e < a.numFlights(); ++e) {
        CHECK_EQ(a.flight(e).from, b.flight(e).from);
        CHECK_EQ(a.flight(e).to, b.flight(e).to);
        CHECK_EQ(a.flight(e).seats, b.flight(e).seats);
        CHECK_EQ(a.flight(e).duration, b.flight(e).duration);
        CHECK_NEAR(a.flight(e).cost, b.flight(e).cost, 1e-12);
    }
    for (int v = 0; v < a.numAirports(); ++v)
        CHECK_EQ(a.airport(v).capacity, b.airport(v).capacity);
}

TEST(generator_different_seeds_give_different_networks) {
    GeneratorConfig cfg;
    cfg.airports = 30;
    cfg.seed     = 1;
    const FlightNetwork a = generate(cfg);
    cfg.seed = 2;
    const FlightNetwork b = generate(cfg);
    CHECK(a.numFlights() != b.numFlights() ||
          a.flight(0).seats != b.flight(0).seats);
}

TEST(generator_honours_the_requested_size) {
    for (int n = 2; n <= 60; n += 13) {
        GeneratorConfig cfg;
        cfg.airports = n;
        cfg.seed     = static_cast<unsigned>(n);
        const FlightNetwork g = generate(cfg);
        CHECK_EQ(g.numAirports(), n);
    }
}

TEST(generator_clamps_degenerate_sizes) {
    GeneratorConfig cfg;
    cfg.airports = 0;                  // nonsense input must not crash
    const FlightNetwork g = generate(cfg);
    CHECK(g.numAirports() >= 2);
}

TEST(generator_produces_strongly_connected_networks) {
    // forceConnected lays down a directed ring, so every airport must reach
    // every other one. Routing benchmarks depend on this.
    for (unsigned seed = 1; seed <= 20; ++seed) {
        GeneratorConfig cfg;
        cfg.airports       = 25;
        cfg.seed           = seed;
        cfg.forceConnected = true;
        const FlightNetwork g = generate(cfg);
        const Router r(g);
        for (int v = 0; v < g.numAirports(); v += 6)
            CHECK_EQ(r.reachableCount(v), g.numAirports());
    }
}

TEST(generator_emits_no_duplicate_directed_legs) {
    GeneratorConfig cfg;
    cfg.airports = 40;
    cfg.seed     = 31337;
    const FlightNetwork g = generate(cfg);

    std::set<std::pair<int, int> > seen;
    for (int e = 0; e < g.numFlights(); ++e) {
        const Flight& f = g.flight(e);
        CHECK(seen.insert(std::make_pair(f.from, f.to)).second);
        CHECK(f.from != f.to);            // no self-loops
    }
}

TEST(generator_attributes_stay_inside_the_configured_ranges) {
    GeneratorConfig cfg;
    cfg.airports = 40;
    cfg.seed     = 8;
    const FlightNetwork g = generate(cfg);

    for (int e = 0; e < g.numFlights(); ++e) {
        const Flight& f = g.flight(e);
        CHECK(f.seats >= cfg.minSeats);
        CHECK(f.seats <= cfg.maxSeats);
        CHECK(f.duration >= cfg.minDuration);
        CHECK(f.duration <= cfg.maxDuration);
        CHECK(f.cost >= cfg.minCost);
        CHECK(!f.airline.empty());
    }
    for (int v = 0; v < g.numAirports(); ++v)
        CHECK(g.airport(v).capacity >= cfg.minCapacity);
}

TEST(generator_gives_hubs_the_highest_degree) {
    GeneratorConfig cfg;
    cfg.airports  = 60;
    cfg.hubs      = 3;
    cfg.meshProb  = 0.02;      // keep the mesh thin so the hub structure shows
    cfg.spokeProb = 0.7;
    cfg.seed      = 5150;
    const FlightNetwork g = generate(cfg);

    int hubDegreeMin = 1 << 30;
    for (int v = 0; v < cfg.hubs; ++v) hubDegreeMin = std::min(hubDegreeMin, g.degree(v));

    int spokeDegreeMax = 0;
    for (int v = cfg.hubs; v < g.numAirports(); ++v)
        spokeDegreeMax = std::max(spokeDegreeMax, g.degree(v));

    CHECK(hubDegreeMin > spokeDegreeMax);
}

TEST(generator_unidirectional_mode_produces_one_way_legs) {
    GeneratorConfig cfg;
    cfg.airports      = 20;
    cfg.bidirectional = false;
    cfg.meshProb      = 0.0;
    cfg.seed          = 12;
    const FlightNetwork g = generate(cfg);

    int mutual = 0;
    std::set<std::pair<int, int> > legs;
    for (int e = 0; e < g.numFlights(); ++e)
        legs.insert(std::make_pair(g.flight(e).from, g.flight(e).to));
    for (std::set<std::pair<int, int> >::const_iterator it = legs.begin(); it != legs.end(); ++it)
        if (legs.count(std::make_pair(it->second, it->first))) ++mutual;

    // The connectivity ring can still create a few mutual pairs, but far from all.
    CHECK(mutual < static_cast<int>(legs.size()));
}

TEST(generator_synthetic_codes_are_unique_and_short) {
    std::set<std::string> codes;
    for (int i = 0; i < 3000; ++i) {
        const std::string c = syntheticCode(i);
        CHECK(c.size() >= 4 && c.size() <= 5);
        CHECK(codes.insert(c).second);
    }
    CHECK_EQ(syntheticCode(0), std::string("A000"));
    CHECK_EQ(syntheticCode(1), std::string("A001"));
    CHECK_EQ(syntheticCode(1000), std::string("B000"));
}
