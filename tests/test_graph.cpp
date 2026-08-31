#include "test_framework.hpp"

#include "flightnet/graph.hpp"

#include <stdexcept>

using namespace flightnet;

TEST(graph_addAirport_assigns_dense_ids) {
    FlightNetwork g;
    CHECK_EQ(g.addAirport("AAA"), 0);
    CHECK_EQ(g.addAirport("BBB"), 1);
    CHECK_EQ(g.addAirport("CCC"), 2);
    CHECK_EQ(g.numAirports(), 3);
}

TEST(graph_addAirport_is_idempotent_by_code) {
    FlightNetwork g;
    const int first = g.addAirport("LHR", "London", "GB", 51.47, -0.45, 1500);
    const int again = g.addAirport("LHR");
    CHECK_EQ(first, again);
    CHECK_EQ(g.numAirports(), 1);
    // The stub re-insert must not wipe details the first insert supplied.
    CHECK_EQ(g.airport(first).city, std::string("London"));
    CHECK_EQ(g.airport(first).capacity, 1500);
}

TEST(graph_addAirport_backfills_missing_details) {
    FlightNetwork g;
    const int id = g.addAirport("CDG");                       // stub, created by a route
    g.addAirport("CDG", "Paris", "FR", 49.0, 2.5, 1000);      // later, the real record
    CHECK_EQ(g.airport(id).city, std::string("Paris"));
    CHECK_EQ(g.airport(id).capacity, 1000);
}

TEST(graph_idOf_reports_unknown_codes) {
    FlightNetwork g;
    g.addAirport("JFK");
    CHECK_EQ(g.idOf("JFK"), 0);
    CHECK_EQ(g.idOf("ZZZ"), -1);
    CHECK(g.hasAirport("JFK"));
    CHECK(!g.hasAirport("ZZZ"));
}

TEST(graph_addFlight_by_code_creates_endpoints) {
    FlightNetwork g;
    const int e = g.addFlight("JFK", "LHR", 480.0, 420, 300, "BA");
    CHECK_EQ(g.numAirports(), 2);
    CHECK_EQ(g.numFlights(), 1);
    CHECK_EQ(g.flight(e).from, g.idOf("JFK"));
    CHECK_EQ(g.flight(e).to, g.idOf("LHR"));
    CHECK_EQ(g.flight(e).airline, std::string("BA"));
}

TEST(graph_adjacency_is_directed) {
    FlightNetwork g;
    g.addFlight("A", "B", 1.0, 10, 5);
    const int a = g.idOf("A"), b = g.idOf("B");
    CHECK_EQ(g.outgoing(a).size(), std::size_t(1));
    CHECK_EQ(g.incoming(a).size(), std::size_t(0));
    CHECK_EQ(g.outgoing(b).size(), std::size_t(0));
    CHECK_EQ(g.incoming(b).size(), std::size_t(1));
    CHECK_EQ(g.degree(a), 1);
    CHECK_EQ(g.degree(b), 1);
}

TEST(graph_supports_parallel_legs) {
    FlightNetwork g;
    g.addFlight("A", "B", 100.0, 60, 50, "X1");
    g.addFlight("A", "B", 90.0, 75, 40, "X2");   // same pair, different airline
    CHECK_EQ(g.numFlights(), 2);
    CHECK_EQ(g.outgoing(g.idOf("A")).size(), std::size_t(2));
}

TEST(graph_addFlight_rejects_bad_ids) {
    FlightNetwork g;
    g.addAirport("A");
    CHECK_THROWS(g.addFlight(0, 5, 1.0, 1, 1), std::out_of_range);
    CHECK_THROWS(g.addFlight(-1, 0, 1.0, 1, 1), std::out_of_range);
}

TEST(graph_codeOf_falls_back_for_unnamed_airports) {
    FlightNetwork g;
    g.addAirport("");
    CHECK_EQ(g.codeOf(0), std::string("#0"));
    CHECK_EQ(g.codeOf(99), std::string("?"));
}
