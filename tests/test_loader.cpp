#include "test_framework.hpp"

#include "flightnet/generator.hpp"
#include "flightnet/graph.hpp"
#include "flightnet/loader.hpp"
#include "flightnet/routing.hpp"

#include <cstdio>
#include <fstream>
#include <string>

using namespace flightnet;

namespace {

const char* const AIRPORTS_TMP = "build/_test_airports.csv";
const char* const ROUTES_TMP   = "build/_test_routes.csv";

void writeFile(const char* path, const std::string& body) {
    std::ofstream out(path);
    out << body;
}

void removeFile(const char* path) { std::remove(path); }

} // namespace

TEST(loader_reads_a_well_formed_dataset) {
    writeFile(AIRPORTS_TMP,
              "code,city,country,lat,lon,capacity\n"
              "AAA,Alpha,XA,10.5,20.5,500\n"
              "BBB,Beta,XB,-5.25,30.0,750\n");
    writeFile(ROUTES_TMP,
              "from,to,cost,duration_min,seats,airline\n"
              "AAA,BBB,199.5,120,180,ZZ\n");

    const FlightNetwork g = loadFromCsv(AIRPORTS_TMP, ROUTES_TMP);
    CHECK_EQ(g.numAirports(), 2);
    CHECK_EQ(g.numFlights(), 1);
    CHECK_EQ(g.airport(g.idOf("AAA")).city, std::string("Alpha"));
    CHECK_EQ(g.airport(g.idOf("BBB")).capacity, 750);
    CHECK_NEAR(g.airport(g.idOf("BBB")).lat, -5.25, 1e-9);

    const Flight& f = g.flight(0);
    CHECK_NEAR(f.cost, 199.5, 1e-9);
    CHECK_EQ(f.duration, 120);
    CHECK_EQ(f.seats, 180);
    CHECK_EQ(f.airline, std::string("ZZ"));

    removeFile(AIRPORTS_TMP);
    removeFile(ROUTES_TMP);
}

TEST(loader_skips_comments_and_blank_lines) {
    writeFile(AIRPORTS_TMP,
              "# a comment\n"
              "\n"
              "code,city,country,lat,lon,capacity\n"
              "AAA,Alpha,XA,0,0,100\n"
              "\n"
              "# trailing note\n");
    writeFile(ROUTES_TMP,
              "from,to,cost,duration_min,seats,airline\n"
              "AAA,AAA,1,1,1,X\n");

    const FlightNetwork g = loadFromCsv(AIRPORTS_TMP, ROUTES_TMP);
    CHECK_EQ(g.numAirports(), 1);
    CHECK_EQ(g.numFlights(), 1);

    removeFile(AIRPORTS_TMP);
    removeFile(ROUTES_TMP);
}

TEST(loader_works_without_a_header_row) {
    writeFile(AIRPORTS_TMP, "AAA,Alpha,XA,0,0,100\nBBB,Beta,XB,0,0,200\n");
    writeFile(ROUTES_TMP,   "AAA,BBB,50,60,90,QQ\n");

    const FlightNetwork g = loadFromCsv(AIRPORTS_TMP, ROUTES_TMP);
    CHECK_EQ(g.numAirports(), 2);
    CHECK_EQ(g.numFlights(), 1);

    removeFile(AIRPORTS_TMP);
    removeFile(ROUTES_TMP);
}

TEST(loader_creates_stubs_for_airports_only_named_in_routes) {
    writeFile(AIRPORTS_TMP, "code,city,country,lat,lon,capacity\nAAA,Alpha,XA,0,0,100\n");
    writeFile(ROUTES_TMP,   "from,to,cost,duration_min,seats,airline\nAAA,GHOST,10,20,30,X\n");

    const FlightNetwork g = loadFromCsv(AIRPORTS_TMP, ROUTES_TMP);
    CHECK_EQ(g.numAirports(), 2);
    CHECK(g.hasAirport("GHOST"));
    CHECK_EQ(g.airport(g.idOf("GHOST")).capacity, 0);   // unknown, so unmodelled

    removeFile(AIRPORTS_TMP);
    removeFile(ROUTES_TMP);
}

TEST(loader_reports_missing_files) {
    CHECK_THROWS(loadFromCsv("build/_no_such_airports.csv", "build/_no_such_routes.csv"),
                 DataError);
}

TEST(loader_reports_short_rows) {
    writeFile(AIRPORTS_TMP, "code,city,country,lat,lon,capacity\nAAA,Alpha,XA,0,0\n");
    writeFile(ROUTES_TMP,   "from,to,cost,duration_min,seats,airline\n");
    CHECK_THROWS(loadFromCsv(AIRPORTS_TMP, ROUTES_TMP), DataError);
    removeFile(AIRPORTS_TMP);
    removeFile(ROUTES_TMP);
}

TEST(loader_reports_non_numeric_fields) {
    writeFile(AIRPORTS_TMP, "code,city,country,lat,lon,capacity\nAAA,Alpha,XA,0,0,lots\n");
    writeFile(ROUTES_TMP,   "from,to,cost,duration_min,seats,airline\n");
    CHECK_THROWS(loadFromCsv(AIRPORTS_TMP, ROUTES_TMP), DataError);
    removeFile(AIRPORTS_TMP);
    removeFile(ROUTES_TMP);
}

TEST(loader_csv_round_trip_preserves_the_network) {
    GeneratorConfig cfg;
    cfg.airports = 25;
    cfg.seed     = 4242;
    const FlightNetwork original = generate(cfg);

    saveToCsv(original, AIRPORTS_TMP, ROUTES_TMP);
    const FlightNetwork reloaded = loadFromCsv(AIRPORTS_TMP, ROUTES_TMP);

    CHECK_EQ(reloaded.numAirports(), original.numAirports());
    CHECK_EQ(reloaded.numFlights(), original.numFlights());

    for (int v = 0; v < original.numAirports(); ++v) {
        const int rv = reloaded.idOf(original.codeOf(v));
        REQUIRE(rv >= 0);
        CHECK_EQ(reloaded.airport(rv).capacity, original.airport(v).capacity);
        CHECK_EQ(reloaded.airport(rv).country, original.airport(v).country);
    }
    for (int e = 0; e < original.numFlights(); ++e) {
        const Flight& a = original.flight(e);
        const Flight& b = reloaded.flight(e);
        CHECK_EQ(reloaded.codeOf(b.from), original.codeOf(a.from));
        CHECK_EQ(reloaded.codeOf(b.to), original.codeOf(a.to));
        CHECK_EQ(b.duration, a.duration);
        CHECK_EQ(b.seats, a.seats);
        CHECK_EQ(b.airline, a.airline);
    }

    removeFile(AIRPORTS_TMP);
    removeFile(ROUTES_TMP);
}

TEST(loader_sample_network_is_usable) {
    const FlightNetwork g = sampleNetwork();
    CHECK(g.numAirports() >= 10);
    CHECK(g.numFlights() >= 20);
    CHECK(g.hasAirport("JFK"));
    CHECK(g.hasAirport("SYD"));

    // The demo advertises a JFK -> SYD itinerary, so it had better exist.
    const Router r(g);
    CHECK(r.cheapest(g.idOf("JFK"), g.idOf("SYD")).found);

    for (int v = 0; v < g.numAirports(); ++v)
        CHECK(g.airport(v).capacity > 0);
}
