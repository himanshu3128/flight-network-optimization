#include "test_framework.hpp"

#include "flightnet/generator.hpp"
#include "flightnet/maxflow.hpp"

#include <stdexcept>

using namespace flightnet;

namespace {

// The worked example from CLRS chapter 26. Its maximum flow is 23, which makes
// it a useful fixed point: any solver that disagrees is wrong, full stop.
//   0 = s, 1 = v1, 2 = v2, 3 = v3, 4 = v4, 5 = t
FlowNetwork clrsNetwork() {
    FlowNetwork g(6);
    g.addEdge(0, 1, 16);
    g.addEdge(0, 2, 13);
    g.addEdge(1, 2, 10);
    g.addEdge(2, 1, 4);
    g.addEdge(1, 3, 12);
    g.addEdge(3, 2, 9);
    g.addEdge(2, 4, 14);
    g.addEdge(4, 3, 7);
    g.addEdge(3, 5, 20);
    g.addEdge(4, 5, 4);
    return g;
}

const Cap CLRS_MAXFLOW = 23;

// Confirms flow conservation at every node except the source and the sink, and
// that no arc exceeds its capacity. A solver can return the right total and
// still leave an inconsistent flow behind, so this is checked separately.
void checkFlowIsValid(const FlowNetwork& g, int s, int t) {
    for (int v = 0; v < g.numNodes(); ++v) {
        if (v == s || v == t) continue;
        Cap net = 0;
        const std::vector<int>& a = g.adj(v);
        for (std::size_t i = 0; i < a.size(); ++i) {
            const int e = a[i];
            if ((e & 1) == 0) net += g.edge(e).flow;      // leaving v
            else              net -= g.edge(e ^ 1).flow;  // entering v
        }
        CHECK_EQ(net, Cap(0));
    }
    for (int e = 0; e < 2 * g.numEdges(); e += 2) {
        CHECK(g.edge(e).flow <= g.edge(e).cap);
        CHECK(g.edge(e).flow >= 0);
    }
}

} // namespace

// --- the fixed reference instance ------------------------------------------

TEST(maxflow_ford_fulkerson_matches_clrs) {
    FlowNetwork g = clrsNetwork();
    const FlowResult r = FordFulkerson::run(g, 0, 5);
    CHECK_EQ(r.value, CLRS_MAXFLOW);
    CHECK_EQ(g.flowOutOf(0), CLRS_MAXFLOW);
    checkFlowIsValid(g, 0, 5);
}

TEST(maxflow_edmonds_karp_matches_clrs) {
    FlowNetwork g = clrsNetwork();
    const FlowResult r = EdmondsKarp::run(g, 0, 5);
    CHECK_EQ(r.value, CLRS_MAXFLOW);
    checkFlowIsValid(g, 0, 5);
}

TEST(maxflow_dinic_matches_clrs) {
    FlowNetwork g = clrsNetwork();
    const FlowResult r = Dinic::run(g, 0, 5);
    CHECK_EQ(r.value, CLRS_MAXFLOW);
    checkFlowIsValid(g, 0, 5);
}

TEST(maxflow_min_cut_capacity_equals_max_flow) {
    FlowNetwork g = clrsNetwork();
    const FlowResult r = Dinic::run(g, 0, 5);

    const std::vector<int> cut = g.minCutEdges(0);
    Cap cutCap = 0;
    for (std::size_t i = 0; i < cut.size(); ++i) cutCap += g.edge(cut[i]).cap;
    CHECK_EQ(cutCap, r.value);

    // Every cut arc must be saturated, and the sink must be on the far side.
    for (std::size_t i = 0; i < cut.size(); ++i)
        CHECK_EQ(g.edge(cut[i]).flow, g.edge(cut[i]).cap);
    const std::vector<bool> srcSide = g.residualReachable(0);
    CHECK(srcSide[0]);
    CHECK(!srcSide[5]);
}

// --- degenerate inputs ------------------------------------------------------

TEST(maxflow_source_equals_sink_is_zero) {
    FlowNetwork g = clrsNetwork();
    CHECK_EQ(FordFulkerson::run(g, 0, 0).value, Cap(0));
    CHECK_EQ(EdmondsKarp::run(g, 3, 3).value, Cap(0));
    CHECK_EQ(Dinic::run(g, 5, 5).value, Cap(0));
}

TEST(maxflow_disconnected_sink_is_zero) {
    FlowNetwork g(4);
    g.addEdge(0, 1, 10);
    g.addEdge(2, 3, 10);          // a separate component
    CHECK_EQ(Dinic::run(g, 0, 3).value, Cap(0));
    g.resetFlow();
    CHECK_EQ(FordFulkerson::run(g, 0, 3).value, Cap(0));
}

TEST(maxflow_zero_capacity_arcs_carry_nothing) {
    FlowNetwork g(3);
    g.addEdge(0, 1, 0);
    g.addEdge(1, 2, 5);
    CHECK_EQ(Dinic::run(g, 0, 2).value, Cap(0));
}

TEST(maxflow_single_arc_is_its_own_bottleneck) {
    FlowNetwork g(2);
    g.addEdge(0, 1, 7);
    CHECK_EQ(Dinic::run(g, 0, 1).value, Cap(7));
}

TEST(maxflow_parallel_arcs_capacities_add) {
    FlowNetwork g(2);
    g.addEdge(0, 1, 3);
    g.addEdge(0, 1, 4);
    CHECK_EQ(Dinic::run(g, 0, 1).value, Cap(7));
}

TEST(maxflow_bottleneck_in_the_middle_caps_the_chain) {
    FlowNetwork g(4);
    g.addEdge(0, 1, 100);
    g.addEdge(1, 2, 5);           // the narrow link
    g.addEdge(2, 3, 100);
    CHECK_EQ(Dinic::run(g, 0, 3).value, Cap(5));
}

TEST(maxflow_rejects_invalid_edges) {
    FlowNetwork g(3);
    CHECK_THROWS(g.addEdge(0, 9, 1), std::out_of_range);
    CHECK_THROWS(g.addEdge(-1, 0, 1), std::out_of_range);
    CHECK_THROWS(g.addEdge(0, 1, -5), std::invalid_argument);
}

TEST(maxflow_resetFlow_allows_reuse) {
    FlowNetwork g = clrsNetwork();
    const Cap first = Dinic::run(g, 0, 5).value;
    g.resetFlow();
    const Cap second = FordFulkerson::run(g, 0, 5).value;
    CHECK_EQ(first, second);
    // A stale flow would have made the second run start from a saturated graph.
    CHECK_EQ(second, CLRS_MAXFLOW);
}

// --- randomized cross-validation -------------------------------------------

TEST(maxflow_all_solvers_agree_on_random_networks) {
    // Three independent algorithms agreeing on hundreds of instances is much
    // stronger evidence than any single hand-built case.
    for (unsigned seed = 1; seed <= 120; ++seed) {
        GeneratorConfig cfg;
        cfg.airports = 12 + static_cast<int>(seed % 20);
        cfg.seed     = seed;
        const FlightNetwork fg = generate(cfg);

        // Build a plain flow network straight from the legs.
        FlowNetwork g(fg.numAirports());
        for (int e = 0; e < fg.numFlights(); ++e) {
            const Flight& f = fg.flight(e);
            g.addEdge(f.from, f.to, f.seats);
        }
        const int s = 0;
        const int t = fg.numAirports() - 1;

        g.resetFlow();
        const Cap ff = FordFulkerson::run(g, s, t).value;
        g.resetFlow();
        const Cap ek = EdmondsKarp::run(g, s, t).value;
        g.resetFlow();
        const FlowResult dn = Dinic::run(g, s, t);

        CHECK_EQ(ff, dn.value);
        CHECK_EQ(ek, dn.value);
        checkFlowIsValid(g, s, t);

        const std::vector<int> cut = g.minCutEdges(s);
        Cap cutCap = 0;
        for (std::size_t i = 0; i < cut.size(); ++i) cutCap += g.edge(cut[i]).cap;
        CHECK_EQ(cutCap, dn.value);
    }
}

TEST(maxflow_is_bounded_by_the_source_and_sink_degrees) {
    for (unsigned seed = 1; seed <= 40; ++seed) {
        GeneratorConfig cfg;
        cfg.airports = 20;
        cfg.seed     = seed;
        const FlightNetwork fg = generate(cfg);

        FlowNetwork g(fg.numAirports());
        for (int e = 0; e < fg.numFlights(); ++e) {
            const Flight& f = fg.flight(e);
            g.addEdge(f.from, f.to, f.seats);
        }
        const int s = 0, t = fg.numAirports() - 1;

        Cap outOfSource = 0, intoSink = 0;
        for (int e = 0; e < fg.numFlights(); ++e) {
            const Flight& f = fg.flight(e);
            if (f.from == s) outOfSource += f.seats;
            if (f.to == t)   intoSink    += f.seats;
        }
        const Cap flow = Dinic::run(g, s, t).value;
        CHECK(flow <= outOfSource);
        CHECK(flow <= intoSink);
    }
}

// --- algorithmic guarantees, not just the answer ---------------------------

TEST(maxflow_dinic_respects_its_phase_bound) {
    // Dinic's O(V^2 E) bound rests on the shortest s-t distance strictly
    // increasing every phase, which caps the phase count at V. Checking only the
    // flow value would not notice a broken level graph, because a relaxed
    // admissibility rule still terminates with a correct (just slower) answer.
    for (unsigned seed = 1; seed <= 60; ++seed) {
        GeneratorConfig cfg;
        cfg.airports = 15 + static_cast<int>(seed % 25);
        cfg.seed     = seed;
        const FlightNetwork fg = generate(cfg);

        FlowNetwork g(fg.numAirports());
        for (int e = 0; e < fg.numFlights(); ++e) {
            const Flight& f = fg.flight(e);
            g.addEdge(f.from, f.to, f.seats);
        }
        const FlowResult r = Dinic::run(g, 0, fg.numAirports() - 1);
        CHECK(r.augmentations <= g.numNodes());
    }
}

TEST(maxflow_dinic_uses_far_fewer_phases_than_ford_fulkerson_paths) {
    // The performance claim in one assertion: on a network with wide arcs,
    // Ford-Fulkerson pays per unit of flow while Dinic pays per phase.
    GeneratorConfig cfg;
    cfg.airports = 60;
    cfg.seed     = 2024;
    const FlightNetwork fg = generate(cfg);

    FlowNetwork g(fg.numAirports());
    for (int e = 0; e < fg.numFlights(); ++e) {
        const Flight& f = fg.flight(e);
        g.addEdge(f.from, f.to, f.seats);
    }
    const int s = 0, t = fg.numAirports() - 1;

    g.resetFlow();
    const FlowResult ff = FordFulkerson::run(g, s, t);
    g.resetFlow();
    const FlowResult dn = Dinic::run(g, s, t);

    CHECK_EQ(ff.value, dn.value);
    CHECK(dn.augmentations < ff.augmentations);
}
