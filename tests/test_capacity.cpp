#include "test_framework.hpp"

#include "flightnet/capacity.hpp"
#include "flightnet/generator.hpp"
#include "flightnet/graph.hpp"

#include <stdexcept>

using namespace flightnet;

namespace {

// Everything from SRC to DST must pass through HUB. The legs are wide (500
// seats) but the hub itself only handles 120, so the airport ceiling -- not any
// leg -- is what limits throughput. Node splitting is the only reason the model
// can express that.
FlightNetwork hubLimitedNetwork(int hubCapacity) {
    FlightNetwork g;
    g.addAirport("SRC", "", "", 0, 0, 100000);
    g.addAirport("HUB", "", "", 0, 0, hubCapacity);
    g.addAirport("DST", "", "", 0, 0, 100000);
    g.addFlight("SRC", "HUB", 100.0, 60, 500, "X");
    g.addFlight("HUB", "DST", 100.0, 60, 500, "Y");
    return g;
}

} // namespace

TEST(capacity_airport_ceiling_limits_throughput) {
    const FlightNetwork g = hubLimitedNetwork(120);
    CapacityAnalyzer an(g, true);
    const FlowReport rep = an.maxFlow(g.idOf("SRC"), g.idOf("DST"));
    CHECK_EQ(rep.maxFlow, Cap(120));
}

TEST(capacity_ceiling_can_be_switched_off) {
    const FlightNetwork g = hubLimitedNetwork(120);
    CapacityAnalyzer an(g, false);            // legs only
    const FlowReport rep = an.maxFlow(g.idOf("SRC"), g.idOf("DST"));
    CHECK_EQ(rep.maxFlow, Cap(500));          // now the 500-seat legs bind
}

TEST(capacity_min_cut_names_the_limiting_airport) {
    const FlightNetwork g = hubLimitedNetwork(120);
    CapacityAnalyzer an(g, true);
    const FlowReport rep = an.maxFlow(g.idOf("SRC"), g.idOf("DST"));

    REQUIRE(rep.minCut.size() == 1);
    CHECK(rep.minCut[0].isAirport);
    CHECK_EQ(rep.minCut[0].id, g.idOf("HUB"));
    CHECK_EQ(rep.minCut[0].capacity, Cap(120));
    CHECK_EQ(rep.cutCapacity(), rep.maxFlow);
}

TEST(capacity_min_cut_names_the_limiting_leg) {
    FlightNetwork g;
    g.addAirport("SRC", "", "", 0, 0, 100000);
    g.addAirport("MID", "", "", 0, 0, 100000);
    g.addAirport("DST", "", "", 0, 0, 100000);
    g.addFlight("SRC", "MID", 100.0, 60, 90, "NARROW");
    g.addFlight("MID", "DST", 100.0, 60, 400, "WIDE");

    CapacityAnalyzer an(g, true);
    const FlowReport rep = an.maxFlow(g.idOf("SRC"), g.idOf("DST"));
    CHECK_EQ(rep.maxFlow, Cap(90));
    REQUIRE(rep.minCut.size() == 1);
    CHECK(!rep.minCut[0].isAirport);
    CHECK_EQ(g.flight(rep.minCut[0].id).airline, std::string("NARROW"));
}

TEST(capacity_unset_airport_capacity_means_unlimited) {
    FlightNetwork g;
    g.addAirport("SRC");                       // capacity 0 == not modelled
    g.addAirport("MID");
    g.addAirport("DST");
    g.addFlight("SRC", "MID", 1.0, 1, 250);
    g.addFlight("MID", "DST", 1.0, 1, 250);

    CapacityAnalyzer an(g, true);
    const FlowReport rep = an.maxFlow(g.idOf("SRC"), g.idOf("DST"));
    CHECK_EQ(rep.maxFlow, Cap(250));           // not zero, and not clamped
    // An unbounded throughput arc must never be reported as a bottleneck.
    for (std::size_t i = 0; i < rep.minCut.size(); ++i)
        CHECK(!rep.minCut[i].isAirport);
}

TEST(capacity_parallel_paths_add_up) {
    FlightNetwork g;
    g.addAirport("SRC", "", "", 0, 0, 100000);
    g.addAirport("N1",  "", "", 0, 0, 100000);
    g.addAirport("N2",  "", "", 0, 0, 100000);
    g.addAirport("DST", "", "", 0, 0, 100000);
    g.addFlight("SRC", "N1", 1.0, 1, 100);
    g.addFlight("N1", "DST", 1.0, 1, 100);
    g.addFlight("SRC", "N2", 1.0, 1, 150);
    g.addFlight("N2", "DST", 1.0, 1, 150);

    CapacityAnalyzer an(g, true);
    CHECK_EQ(an.maxFlow(g.idOf("SRC"), g.idOf("DST")).maxFlow, Cap(250));
}

TEST(capacity_all_algorithms_agree) {
    const FlightNetwork g = generate(GeneratorConfig());
    CapacityAnalyzer an(g, true);
    const std::vector<FlowReport> reps = an.compareAlgorithms(0, g.numAirports() - 1);
    REQUIRE(reps.size() == 3);
    CHECK_EQ(reps[0].maxFlow, reps[2].maxFlow);
    CHECK_EQ(reps[1].maxFlow, reps[2].maxFlow);
    for (std::size_t i = 0; i < reps.size(); ++i)
        CHECK_EQ(reps[i].cutCapacity(), reps[i].maxFlow);
}

TEST(capacity_repeated_queries_do_not_leak_state) {
    // The analyzer reuses one flow network across calls, so a missed resetFlow
    // or an unrestored capacity would show up as a drifting answer here.
    const FlightNetwork g = generate(GeneratorConfig());
    CapacityAnalyzer an(g, true);
    const Cap first = an.maxFlow(0, g.numAirports() - 1).maxFlow;
    an.rankAirports(0, g.numAirports() - 1, 3);
    an.rankFlights(0, g.numAirports() - 1, 3);
    const Cap again = an.maxFlow(0, g.numAirports() - 1).maxFlow;
    CHECK_EQ(first, again);
}

TEST(capacity_rejects_out_of_range_airports) {
    const FlightNetwork g = hubLimitedNetwork(120);
    CapacityAnalyzer an(g, true);
    CHECK_THROWS(an.maxFlow(0, 99), std::out_of_range);
    CHECK_THROWS(an.maxFlow(-1, 0), std::out_of_range);
}

TEST(capacity_same_origin_and_destination_is_zero) {
    const FlightNetwork g = hubLimitedNetwork(120);
    CapacityAnalyzer an(g, true);
    CHECK_EQ(an.maxFlow(0, 0).maxFlow, Cap(0));
}

// --- resilience -------------------------------------------------------------

TEST(resilience_identifies_the_single_point_of_failure) {
    const FlightNetwork g = hubLimitedNetwork(120);
    CapacityAnalyzer an(g, true);
    const std::vector<Impact> ranked = an.rankAirports(g.idOf("SRC"), g.idOf("DST"), 5);

    REQUIRE(!ranked.empty());
    CHECK_EQ(ranked[0].id, g.idOf("HUB"));
    CHECK_EQ(ranked[0].flowAfter, Cap(0));
    CHECK_EQ(ranked[0].flowLost, Cap(120));
    CHECK(ranked[0].disconnects);
    CHECK_NEAR(ranked[0].lostPercent, 100.0, 1e-9);
}

TEST(resilience_skips_the_endpoints) {
    const FlightNetwork g = hubLimitedNetwork(120);
    CapacityAnalyzer an(g, true);
    const std::vector<Impact> ranked = an.rankAirports(g.idOf("SRC"), g.idOf("DST"), 0);
    for (std::size_t i = 0; i < ranked.size(); ++i) {
        CHECK(ranked[i].id != g.idOf("SRC"));
        CHECK(ranked[i].id != g.idOf("DST"));
    }
}

TEST(resilience_ranks_a_redundant_path_as_less_critical) {
    // Two disjoint corridors: losing either costs throughput but never all of it.
    FlightNetwork g;
    g.addAirport("SRC", "", "", 0, 0, 100000);
    g.addAirport("BIG", "", "", 0, 0, 100000);
    g.addAirport("SML", "", "", 0, 0, 100000);
    g.addAirport("DST", "", "", 0, 0, 100000);
    g.addFlight("SRC", "BIG", 1.0, 1, 300);
    g.addFlight("BIG", "DST", 1.0, 1, 300);
    g.addFlight("SRC", "SML", 1.0, 1, 50);
    g.addFlight("SML", "DST", 1.0, 1, 50);

    CapacityAnalyzer an(g, true);
    const std::vector<Impact> ranked = an.rankAirports(g.idOf("SRC"), g.idOf("DST"), 5);
    REQUIRE(ranked.size() >= 2);
    CHECK_EQ(ranked[0].id, g.idOf("BIG"));       // the wider corridor matters more
    CHECK_EQ(ranked[0].flowLost, Cap(300));
    CHECK(!ranked[0].disconnects);               // the small corridor still carries 50
    CHECK_EQ(ranked[1].id, g.idOf("SML"));
    CHECK_EQ(ranked[1].flowLost, Cap(50));
}

TEST(resilience_flight_ranking_matches_leg_capacity) {
    FlightNetwork g;
    g.addAirport("SRC", "", "", 0, 0, 100000);
    g.addAirport("BIG", "", "", 0, 0, 100000);
    g.addAirport("SML", "", "", 0, 0, 100000);
    g.addAirport("DST", "", "", 0, 0, 100000);
    g.addFlight("SRC", "BIG", 1.0, 1, 300, "B1");
    g.addFlight("BIG", "DST", 1.0, 1, 300, "B2");
    g.addFlight("SRC", "SML", 1.0, 1, 50, "S1");
    g.addFlight("SML", "DST", 1.0, 1, 50, "S2");

    CapacityAnalyzer an(g, true);
    const std::vector<Impact> ranked = an.rankFlights(g.idOf("SRC"), g.idOf("DST"), 4);
    REQUIRE(!ranked.empty());
    CHECK_EQ(ranked[0].flowLost, Cap(300));
    CHECK(!ranked[0].isAirport);
}

TEST(resilience_topN_limits_the_result) {
    const FlightNetwork g = generate(GeneratorConfig());
    CapacityAnalyzer an(g, true);
    CHECK(an.rankAirports(0, g.numAirports() - 1, 3).size() <= std::size_t(3));
    CHECK(an.rankFlights(0, g.numAirports() - 1, 2).size() <= std::size_t(2));
}

TEST(resilience_loss_never_exceeds_the_baseline) {
    for (unsigned seed = 1; seed <= 10; ++seed) {
        GeneratorConfig cfg;
        cfg.airports = 25;
        cfg.seed     = seed;
        const FlightNetwork g = generate(cfg);

        CapacityAnalyzer an(g, true);
        const Cap baseline = an.maxFlow(0, g.numAirports() - 1).maxFlow;
        const std::vector<Impact> ranked = an.rankAirports(0, g.numAirports() - 1, 0);

        for (std::size_t i = 0; i < ranked.size(); ++i) {
            // Removing capacity can never increase throughput.
            CHECK(ranked[i].flowAfter <= baseline);
            CHECK(ranked[i].flowLost >= 0);
            CHECK_EQ(ranked[i].flowAfter + ranked[i].flowLost, baseline);
        }
    }
}
