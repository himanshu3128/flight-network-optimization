#include "test_framework.hpp"

#include "flightnet/generator.hpp"
#include "flightnet/graph.hpp"
#include "flightnet/routing.hpp"

#include <set>
#include <stdexcept>

using namespace flightnet;

namespace {

// A network where the fewest-stop route and the cheapest route are deliberately
// different, so a test can tell the two objectives apart:
//
//   A --------- 1000, 600min --------- D      one leg, expensive, slow
//   A -- 100 -- B -- 100 -- C -- 100 -- D     three legs, cheap, fast
//
FlightNetwork divergentNetwork() {
    FlightNetwork g;
    g.addFlight("A", "D", 1000.0, 600, 100, "NS");
    g.addFlight("A", "B", 100.0, 60, 100, "L1");
    g.addFlight("B", "C", 100.0, 60, 100, "L2");
    g.addFlight("C", "D", 100.0, 60, 100, "L3");
    return g;
}

} // namespace

TEST(routing_bfs_finds_fewest_legs) {
    const FlightNetwork g = divergentNetwork();
    const Router r(g);
    const Route route = r.minStops(g.idOf("A"), g.idOf("D"));
    REQUIRE(route.found);
    CHECK_EQ(route.numLegs(), 1);
    CHECK_EQ(route.stops(), 0);
    CHECK_NEAR(route.totalCost, 1000.0, 1e-9);
}

TEST(routing_dijkstra_finds_cheapest_fare) {
    const FlightNetwork g = divergentNetwork();
    const Router r(g);
    const Route route = r.cheapest(g.idOf("A"), g.idOf("D"));
    REQUIRE(route.found);
    CHECK_EQ(route.numLegs(), 3);
    CHECK_NEAR(route.totalCost, 300.0, 1e-9);
    CHECK_EQ(route.totalDuration, 180);
}

TEST(routing_fastest_uses_block_time) {
    const FlightNetwork g = divergentNetwork();
    const Router r(g);
    const Route route = r.fastest(g.idOf("A"), g.idOf("D"));
    REQUIRE(route.found);
    // 180 minutes over three legs beats 600 minutes nonstop, even after the
    // per-leg connection penalty.
    CHECK_EQ(route.totalDuration, 180);
}

TEST(routing_per_leg_penalty_shifts_choice_to_nonstop) {
    const FlightNetwork g = divergentNetwork();
    const Router r(g);
    RouteWeights w;
    w.cost = 1.0; w.time = 0.0;
    w.perLeg = 400.0;    // three legs now cost 1200 in penalties alone
    const Route route = r.best(g.idOf("A"), g.idOf("D"), w);
    REQUIRE(route.found);
    CHECK_EQ(route.numLegs(), 1);
}

TEST(routing_reports_unreachable_destination) {
    FlightNetwork g;
    g.addFlight("A", "B", 10.0, 10, 10);
    g.addAirport("Z");                       // present but with no inbound leg
    const Router r(g);
    CHECK(!r.minStops(g.idOf("A"), g.idOf("Z")).found);
    CHECK(!r.cheapest(g.idOf("A"), g.idOf("Z")).found);
}

TEST(routing_respects_one_way_legs) {
    FlightNetwork g;
    g.addFlight("A", "B", 10.0, 10, 10);
    const Router r(g);
    CHECK(r.cheapest(g.idOf("A"), g.idOf("B")).found);
    CHECK(!r.cheapest(g.idOf("B"), g.idOf("A")).found);   // no return leg exists
}

TEST(routing_source_equals_destination_is_a_trivial_route) {
    const FlightNetwork g = divergentNetwork();
    const Router r(g);
    const Route route = r.cheapest(g.idOf("A"), g.idOf("A"));
    REQUIRE(route.found);
    CHECK_EQ(route.numLegs(), 0);
    CHECK_EQ(route.stops(), 0);
    CHECK_NEAR(route.totalCost, 0.0, 1e-12);
}

TEST(routing_rejects_negative_weights) {
    const FlightNetwork g = divergentNetwork();
    const Router r(g);
    RouteWeights w;
    w.cost = -1.0;                            // Dijkstra has no answer for this
    CHECK_THROWS(r.best(g.idOf("A"), g.idOf("D"), w), std::invalid_argument);
}

TEST(routing_rejects_out_of_range_airports) {
    const FlightNetwork g = divergentNetwork();
    const Router r(g);
    CHECK_THROWS(r.minStops(0, 999), std::out_of_range);
    CHECK_THROWS(r.cheapest(-1, 0), std::out_of_range);
}

TEST(routing_prefers_cheaper_of_parallel_legs) {
    FlightNetwork g;
    g.addFlight("A", "B", 500.0, 60, 100, "EXPENSIVE");
    g.addFlight("A", "B", 120.0, 90, 100, "CHEAP");
    const Router r(g);
    const Route route = r.cheapest(g.idOf("A"), g.idOf("B"));
    REQUIRE(route.found);
    CHECK_EQ(g.flight(route.legs[0]).airline, std::string("CHEAP"));
}

TEST(routing_ban_forces_a_detour) {
    const FlightNetwork g = divergentNetwork();
    const Router r(g);
    RouteBan ban(g.numAirports(), g.numFlights());
    ban.airport[g.idOf("B")] = true;          // close the cheap corridor

    const Route route = r.best(g.idOf("A"), g.idOf("D"), RouteWeights::minCost(), ban);
    REQUIRE(route.found);
    CHECK_EQ(route.numLegs(), 1);             // only the nonstop survives
    CHECK_NEAR(route.totalCost, 1000.0, 1e-9);
}

TEST(routing_ban_on_endpoint_yields_no_route) {
    const FlightNetwork g = divergentNetwork();
    const Router r(g);
    RouteBan ban(g.numAirports(), g.numFlights());
    ban.airport[g.idOf("D")] = true;
    CHECK(!r.best(g.idOf("A"), g.idOf("D"), RouteWeights::minCost(), ban).found);
}

TEST(routing_distancesFrom_matches_pointwise_queries) {
    const FlightNetwork g = divergentNetwork();
    const Router r(g);
    const RouteWeights w = RouteWeights::minCost();
    const std::vector<double> dist = r.distancesFrom(g.idOf("A"), w);

    for (int v = 0; v < g.numAirports(); ++v) {
        const Route route = r.best(g.idOf("A"), v, w);
        if (route.found) CHECK_NEAR(dist[v], route.objective, 1e-9);
        else             CHECK(dist[v] == Router::infinity());
    }
}

TEST(routing_reachability_agrees_with_search) {
    const FlightNetwork g = divergentNetwork();
    const Router r(g);
    const std::vector<bool> seen = r.reachableFrom(g.idOf("A"));
    CHECK_EQ(r.reachableCount(g.idOf("A")), 4);
    for (int v = 0; v < g.numAirports(); ++v)
        CHECK_EQ(seen[v], r.minStops(g.idOf("A"), v).found);
}

// --- Yen's k shortest paths ------------------------------------------------

TEST(yen_returns_routes_in_nondecreasing_order) {
    const FlightNetwork g = divergentNetwork();
    const Router r(g);
    const std::vector<Route> ks = r.kBest(g.idOf("A"), g.idOf("D"), 5, RouteWeights::minCost());
    REQUIRE(ks.size() >= 2);
    for (std::size_t i = 1; i < ks.size(); ++i)
        CHECK(ks[i - 1].objective <= ks[i].objective + 1e-9);
}

TEST(yen_first_result_matches_the_optimum) {
    const FlightNetwork g = divergentNetwork();
    const Router r(g);
    const RouteWeights w = RouteWeights::minCost();
    const std::vector<Route> ks = r.kBest(g.idOf("A"), g.idOf("D"), 3, w);
    REQUIRE(!ks.empty());
    CHECK_NEAR(ks[0].objective, r.best(g.idOf("A"), g.idOf("D"), w).objective, 1e-9);
}

TEST(yen_results_are_distinct_and_loopless) {
    const FlightNetwork g = divergentNetwork();
    const Router r(g);
    const std::vector<Route> ks = r.kBest(g.idOf("A"), g.idOf("D"), 5, RouteWeights::minCost());

    std::set<std::vector<int> > seenPaths;
    for (std::size_t i = 0; i < ks.size(); ++i) {
        CHECK(seenPaths.insert(ks[i].legs).second);          // no duplicates

        std::set<int> visited;
        for (std::size_t v = 0; v < ks[i].airports.size(); ++v)
            CHECK(visited.insert(ks[i].airports[v]).second); // no repeated airport
    }
}

TEST(yen_stops_when_the_network_runs_out_of_routes) {
    const FlightNetwork g = divergentNetwork();
    const Router r(g);
    // Only two distinct A->D routes exist; asking for 50 must not loop forever.
    const std::vector<Route> ks = r.kBest(g.idOf("A"), g.idOf("D"), 50, RouteWeights::minCost());
    CHECK_EQ(ks.size(), std::size_t(2));
}

TEST(yen_handles_zero_and_unreachable_requests) {
    const FlightNetwork g = divergentNetwork();
    const Router r(g);
    CHECK_EQ(r.kBest(g.idOf("A"), g.idOf("D"), 0, RouteWeights::minCost()).size(), std::size_t(0));
    CHECK_EQ(r.kBest(g.idOf("D"), g.idOf("A"), 3, RouteWeights::minCost()).size(), std::size_t(0));
}

// --- cross-checks on generated networks ------------------------------------

TEST(routing_bfs_hop_count_matches_unit_weight_dijkstra) {
    // Two independent implementations of the same objective must agree. This is
    // the property the large stress run checks; here it is pinned on fixed seeds.
    for (unsigned seed = 1; seed <= 25; ++seed) {
        GeneratorConfig cfg;
        cfg.airports = 30;
        cfg.seed     = seed;
        const FlightNetwork g = generate(cfg);
        const Router r(g);

        RouteWeights unit;
        unit.cost = 0.0; unit.time = 0.0; unit.perLeg = 1.0;

        for (int dst = 0; dst < g.numAirports(); dst += 7) {
            const Route bfs = r.minStops(0, dst);
            const Route dij = r.best(0, dst, unit);
            CHECK_EQ(bfs.found, dij.found);
            if (bfs.found) CHECK_EQ(bfs.numLegs(), dij.numLegs());
        }
    }
}

TEST(routing_cheapest_is_never_beaten_by_other_objectives) {
    for (unsigned seed = 1; seed <= 25; ++seed) {
        GeneratorConfig cfg;
        cfg.airports = 30;
        cfg.seed     = seed;
        const FlightNetwork g = generate(cfg);
        const Router r(g);

        for (int dst = 1; dst < g.numAirports(); dst += 5) {
            const Route cheap = r.cheapest(0, dst);
            if (!cheap.found) continue;
            const Route stops = r.minStops(0, dst);
            const Route fast  = r.fastest(0, dst);
            const Route bal   = r.best(0, dst, RouteWeights::balanced());
            CHECK(cheap.totalCost <= stops.totalCost + 1e-9);
            CHECK(cheap.totalCost <= fast.totalCost + 1e-9);
            CHECK(cheap.totalCost <= bal.totalCost + 1e-9);
        }
    }
}

TEST(routing_reported_totals_match_the_legs_taken) {
    // Guards against path reconstruction drifting out of sync with the totals.
    GeneratorConfig cfg;
    cfg.airports = 40;
    cfg.seed     = 99;
    const FlightNetwork g = generate(cfg);
    const Router r(g);

    for (int dst = 1; dst < g.numAirports(); dst += 3) {
        const Route route = r.best(0, dst, RouteWeights::balanced());
        if (!route.found) continue;

        double cost = 0.0;
        int    mins = 0;
        CHECK_EQ(route.airports.size(), route.legs.size() + 1);
        for (std::size_t i = 0; i < route.legs.size(); ++i) {
            const Flight& f = g.flight(route.legs[i]);
            CHECK_EQ(f.from, route.airports[i]);
            CHECK_EQ(f.to, route.airports[i + 1]);
            cost += f.cost;
            mins += f.duration;
        }
        CHECK_NEAR(route.totalCost, cost, 1e-9);
        CHECK_EQ(route.totalDuration, mins);
    }
}
