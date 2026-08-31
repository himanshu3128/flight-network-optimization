#include "flightnet/maxflow.hpp"

#include <algorithm>
#include <deque>
#include <limits>
#include <stdexcept>

namespace flightnet {

namespace {
const Cap CAP_INF = std::numeric_limits<Cap>::max() / 4;
}

// ---------------------------------------------------------------------------
// FlowNetwork
// ---------------------------------------------------------------------------

int FlowNetwork::addEdge(int u, int v, Cap cap) {
    if (u < 0 || u >= n_ || v < 0 || v >= n_)
        throw std::out_of_range("FlowNetwork::addEdge: node id out of range");
    if (cap < 0)
        throw std::invalid_argument("FlowNetwork::addEdge: negative capacity");

    const int e = static_cast<int>(edges_.size());
    Edge fwd; fwd.to = v; fwd.cap = cap; fwd.flow = 0;
    Edge rev; rev.to = u; rev.cap = 0;   rev.flow = 0;
    edges_.push_back(fwd);
    edges_.push_back(rev);
    adj_[static_cast<std::size_t>(u)].push_back(e);
    adj_[static_cast<std::size_t>(v)].push_back(e + 1);
    return e;
}

void FlowNetwork::resetFlow() {
    for (std::size_t i = 0; i < edges_.size(); ++i) edges_[i].flow = 0;
}

Cap FlowNetwork::flowOutOf(int s) const {
    Cap total = 0;
    const std::vector<int>& a = adj_[static_cast<std::size_t>(s)];
    for (std::size_t i = 0; i < a.size(); ++i) {
        const int e = a[i];
        if ((e & 1) == 0) total += edges_[static_cast<std::size_t>(e)].flow;  // forward arcs only
    }
    return total;
}

std::vector<bool> FlowNetwork::residualReachable(int s) const {
    std::vector<bool> seen(static_cast<std::size_t>(n_), false);
    if (s < 0 || s >= n_) return seen;

    std::deque<int> q;
    seen[static_cast<std::size_t>(s)] = true;
    q.push_back(s);
    while (!q.empty()) {
        const int u = q.front();
        q.pop_front();
        const std::vector<int>& a = adj_[static_cast<std::size_t>(u)];
        for (std::size_t i = 0; i < a.size(); ++i) {
            const int e = a[i];
            if (residual(e) <= 0) continue;
            const int v = edges_[static_cast<std::size_t>(e)].to;
            if (!seen[static_cast<std::size_t>(v)]) {
                seen[static_cast<std::size_t>(v)] = true;
                q.push_back(v);
            }
        }
    }
    return seen;
}

std::vector<int> FlowNetwork::minCutEdges(int s) const {
    const std::vector<bool> src = residualReachable(s);
    std::vector<int> cut;
    for (std::size_t e = 0; e < edges_.size(); e += 2) {   // forward arcs only
        const int from = edges_[e + 1].to;                 // reverse arc points back at the tail
        const int to   = edges_[e].to;
        if (src[static_cast<std::size_t>(from)] && !src[static_cast<std::size_t>(to)] && edges_[e].cap > 0)
            cut.push_back(static_cast<int>(e));
    }
    return cut;
}

// ---------------------------------------------------------------------------
// Ford-Fulkerson (DFS augmenting paths, iterative)
// ---------------------------------------------------------------------------

FlowResult FordFulkerson::run(FlowNetwork& g, int s, int t) {
    FlowResult res;
    if (s == t) return res;

    const int n = g.numNodes();
    std::vector<int>  iter(static_cast<std::size_t>(n), 0);   // next unexplored arc per node
    std::vector<char> onPath(static_cast<std::size_t>(n), 0);
    std::vector<int>  path;                                   // arc ids from s to the frontier
    path.reserve(static_cast<std::size_t>(n));

    for (;;) {
        std::fill(iter.begin(), iter.end(), 0);
        std::fill(onPath.begin(), onPath.end(), 0);
        path.clear();

        int  u     = s;
        bool found = false;
        onPath[static_cast<std::size_t>(s)] = 1;

        // One depth-first probe for an augmenting path.
        while (true) {
            if (u == t) { found = true; break; }

            bool advanced = false;
            while (iter[static_cast<std::size_t>(u)] < static_cast<int>(g.adj(u).size())) {
                const int e = g.adj(u)[static_cast<std::size_t>(iter[static_cast<std::size_t>(u)])];
                const int v = g.edge(e).to;
                if (g.residual(e) > 0 && !onPath[static_cast<std::size_t>(v)]) {
                    path.push_back(e);
                    onPath[static_cast<std::size_t>(v)] = 1;
                    u = v;
                    ++res.visits;
                    advanced = true;
                    break;
                }
                ++iter[static_cast<std::size_t>(u)];
            }
            if (advanced) continue;

            // Dead end: retreat one arc, or give up when we are back at the source.
            if (path.empty()) break;
            const int e = path.back();
            path.pop_back();
            onPath[static_cast<std::size_t>(u)] = 0;
            u = g.edge(e ^ 1).to;                 // tail of e
            ++iter[static_cast<std::size_t>(u)];  // that arc is exhausted
        }

        if (!found) break;

        // Bottleneck, then push it along every arc of the path.
        Cap bottleneck = CAP_INF;
        for (std::size_t i = 0; i < path.size(); ++i)
            bottleneck = std::min(bottleneck, g.residual(path[i]));
        for (std::size_t i = 0; i < path.size(); ++i)
            g.push(path[i], bottleneck);

        res.value += bottleneck;
        ++res.augmentations;
    }
    return res;
}

// ---------------------------------------------------------------------------
// Edmonds-Karp (BFS augmenting paths)
// ---------------------------------------------------------------------------

FlowResult EdmondsKarp::run(FlowNetwork& g, int s, int t) {
    FlowResult res;
    if (s == t) return res;

    const int n = g.numNodes();
    std::vector<int> parentArc(static_cast<std::size_t>(n));
    std::deque<int>  q;

    for (;;) {
        std::fill(parentArc.begin(), parentArc.end(), -1);
        parentArc[static_cast<std::size_t>(s)] = -2;   // sentinel: source has no parent
        q.clear();
        q.push_back(s);

        while (!q.empty() && parentArc[static_cast<std::size_t>(t)] == -1) {
            const int u = q.front();
            q.pop_front();
            ++res.visits;
            const std::vector<int>& a = g.adj(u);
            for (std::size_t i = 0; i < a.size(); ++i) {
                const int e = a[i];
                const int v = g.edge(e).to;
                if (g.residual(e) > 0 && parentArc[static_cast<std::size_t>(v)] == -1) {
                    parentArc[static_cast<std::size_t>(v)] = e;
                    q.push_back(v);
                }
            }
        }
        if (parentArc[static_cast<std::size_t>(t)] == -1) break;

        Cap bottleneck = CAP_INF;
        for (int v = t; v != s; ) {
            const int e = parentArc[static_cast<std::size_t>(v)];
            bottleneck = std::min(bottleneck, g.residual(e));
            v = g.edge(e ^ 1).to;
        }
        for (int v = t; v != s; ) {
            const int e = parentArc[static_cast<std::size_t>(v)];
            g.push(e, bottleneck);
            v = g.edge(e ^ 1).to;
        }
        res.value += bottleneck;
        ++res.augmentations;
    }
    return res;
}

// ---------------------------------------------------------------------------
// Dinic
// ---------------------------------------------------------------------------

namespace {

// Layers the residual graph by hop distance from s. False when t is unreachable,
// which is exactly the termination condition for Dinic.
bool dinicLevels(const FlowNetwork& g, int s, int t,
                 std::vector<int>& level, long& visits) {
    std::fill(level.begin(), level.end(), -1);
    std::deque<int> q;
    level[static_cast<std::size_t>(s)] = 0;
    q.push_back(s);
    while (!q.empty()) {
        const int u = q.front();
        q.pop_front();
        ++visits;
        const std::vector<int>& a = g.adj(u);
        for (std::size_t i = 0; i < a.size(); ++i) {
            const int e = a[i];
            const int v = g.edge(e).to;
            if (g.residual(e) > 0 && level[static_cast<std::size_t>(v)] < 0) {
                level[static_cast<std::size_t>(v)] = level[static_cast<std::size_t>(u)] + 1;
                q.push_back(v);
            }
        }
    }
    return level[static_cast<std::size_t>(t)] >= 0;
}

// Saturates a blocking flow in the current level graph. Iterative: `path` holds
// the arcs from s down to the current frontier, and `iter` remembers how far
// each node's arc list has been consumed, so no arc is examined twice per phase.
Cap dinicBlocking(FlowNetwork& g, int s, int t,
                  std::vector<int>& level, std::vector<int>& iter, long& visits) {
    Cap total = 0;
    std::vector<int> path;
    path.reserve(level.size());

    int u = s;
    while (true) {
        if (u == t) {
            // Found an s-t path in the level graph: push its bottleneck.
            Cap bottleneck = CAP_INF;
            for (std::size_t i = 0; i < path.size(); ++i)
                bottleneck = std::min(bottleneck, g.residual(path[i]));
            for (std::size_t i = 0; i < path.size(); ++i)
                g.push(path[i], bottleneck);
            total += bottleneck;

            // Retreat to the first arc the push saturated; everything before it
            // is still usable, so we keep that prefix.
            std::size_t cut = 0;
            while (cut < path.size() && g.residual(path[cut]) > 0) ++cut;
            path.resize(cut);
            u = (cut == 0) ? s : g.edge(path[cut - 1]).to;
            continue;
        }

        bool advanced = false;
        while (iter[static_cast<std::size_t>(u)] < static_cast<int>(g.adj(u).size())) {
            const int e = g.adj(u)[static_cast<std::size_t>(iter[static_cast<std::size_t>(u)])];
            const int v = g.edge(e).to;
            if (g.residual(e) > 0 &&
                level[static_cast<std::size_t>(v)] == level[static_cast<std::size_t>(u)] + 1) {
                path.push_back(e);
                u = v;
                ++visits;
                advanced = true;
                break;
            }
            ++iter[static_cast<std::size_t>(u)];
        }
        if (advanced) continue;

        // No admissible arc leaves u: drop it from this phase's level graph.
        if (u == s) break;
        level[static_cast<std::size_t>(u)] = -1;
        const int e = path.back();
        path.pop_back();
        u = g.edge(e ^ 1).to;
        ++iter[static_cast<std::size_t>(u)];
    }
    return total;
}

} // namespace

FlowResult Dinic::run(FlowNetwork& g, int s, int t) {
    FlowResult res;
    if (s == t) return res;

    const int n = g.numNodes();
    std::vector<int> level(static_cast<std::size_t>(n));
    std::vector<int> iter(static_cast<std::size_t>(n));

    while (dinicLevels(g, s, t, level, res.visits)) {
        std::fill(iter.begin(), iter.end(), 0);
        res.value += dinicBlocking(g, s, t, level, iter, res.visits);
        ++res.augmentations;   // one phase
    }
    return res;
}

} // namespace flightnet
