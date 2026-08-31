// maxflow.hpp -- Residual flow network and three maximum-flow solvers.
//
// All three solvers operate on the same FlowNetwork so their results can be
// cross-checked against each other and so the min-cut can be read off the
// residual graph afterwards.
//
//   FordFulkerson  DFS augmenting paths      O(E * maxflow)
//   EdmondsKarp    BFS augmenting paths      O(V * E^2)
//   Dinic          level graph + blocking    O(V^2 * E)
#ifndef FLIGHTNET_MAXFLOW_HPP
#define FLIGHTNET_MAXFLOW_HPP

#include <cstdint>
#include <vector>

namespace flightnet {

typedef long long Cap;

// Directed graph with paired forward/backward arcs. Edge `e` and edge `e ^ 1`
// are always each other's reverse, which is what makes residual updates a
// two-line operation.
class FlowNetwork {
public:
    struct Edge {
        int to;
        Cap cap;
        Cap flow;
    };

    FlowNetwork() : n_(0) {}
    explicit FlowNetwork(int n) : n_(n), adj_(static_cast<std::size_t>(n)) {}

    // Adds arc u->v with capacity `cap` plus its zero-capacity reverse.
    // Returns the index of the forward arc.
    int addEdge(int u, int v, Cap cap);

    int numNodes() const { return n_; }
    // Arc pairs, i.e. the number of user-visible edges.
    int numEdges() const { return static_cast<int>(edges_.size() / 2); }

    const std::vector<int>& adj(int u) const { return adj_[static_cast<std::size_t>(u)]; }

    Edge&       edge(int e)       { return edges_[static_cast<std::size_t>(e)]; }
    const Edge& edge(int e) const { return edges_[static_cast<std::size_t>(e)]; }

    Cap residual(int e) const {
        const Edge& x = edges_[static_cast<std::size_t>(e)];
        return x.cap - x.flow;
    }

    // Pushes `amount` along arc `e`, debiting its reverse.
    void push(int e, Cap amount) {
        edges_[static_cast<std::size_t>(e)].flow     += amount;
        edges_[static_cast<std::size_t>(e ^ 1)].flow -= amount;
    }

    // Clears all flow so another solver can run on the same instance.
    void resetFlow();

    // Total flow leaving `s`; valid once a solver has finished.
    Cap flowOutOf(int s) const;

    // Nodes reachable from `s` in the residual graph. After a maximum flow this
    // is exactly the source side of a minimum cut.
    std::vector<bool> residualReachable(int s) const;

    // Forward arcs crossing a minimum cut: saturated, source side -> sink side.
    // Call only after a solver has run to completion.
    std::vector<int> minCutEdges(int s) const;

private:
    int                            n_;
    std::vector<Edge>              edges_;
    std::vector<std::vector<int> > adj_;
};

// What a solver did, so benchmarks can report more than just the answer.
struct FlowResult {
    Cap  value         = 0;   // maximum flow from s to t
    long augmentations = 0;   // augmenting paths (FF/EK) or phases (Dinic)
    long visits        = 0;   // nodes touched; a machine-independent work proxy
};

// Ford-Fulkerson with depth-first augmenting paths, iterative so that deep
// residual graphs cannot overflow the call stack.
struct FordFulkerson {
    static FlowResult run(FlowNetwork& g, int s, int t);
};

// Ford-Fulkerson specialized to shortest (fewest-arc) augmenting paths.
struct EdmondsKarp {
    static FlowResult run(FlowNetwork& g, int s, int t);
};

// Dinic: repeatedly build a level graph by BFS, then saturate a blocking flow.
struct Dinic {
    static FlowResult run(FlowNetwork& g, int s, int t);
};

} // namespace flightnet

#endif // FLIGHTNET_MAXFLOW_HPP
