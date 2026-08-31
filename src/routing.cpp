#include "flightnet/routing.hpp"

#include <algorithm>
#include <deque>
#include <queue>
#include <sstream>
#include <stdexcept>

namespace flightnet {

// ---------------------------------------------------------------------------
// Route
// ---------------------------------------------------------------------------

std::string Route::describe(const FlightNetwork& g) const {
    if (!found) return "(no route)";
    std::ostringstream os;
    for (std::size_t i = 0; i < airports.size(); ++i) {
        if (i) os << " -> ";
        os << g.codeOf(airports[i]);
    }
    os << "  | legs=" << numLegs()
       << " stops=" << stops()
       << " cost=" << totalCost
       << " time=" << (totalDuration / 60) << "h" << (totalDuration % 60) << "m";
    return os.str();
}

// ---------------------------------------------------------------------------
// Path reconstruction
// ---------------------------------------------------------------------------

Route Router::buildRoute(int src, int dst, const std::vector<int>& parentEdge,
                         const RouteWeights& w) const {
    Route r;
    if (src == dst) {
        r.found = true;
        r.airports.push_back(src);
        return r;
    }
    if (parentEdge[dst] < 0) return r;   // unreachable; found stays false

    // Walk backwards along parent edges, then reverse.
    std::vector<int> legsRev;
    int v = dst;
    while (v != src) {
        const int e = parentEdge[v];
        if (e < 0) return Route();       // broken chain -> report unreachable
        legsRev.push_back(e);
        v = g_.flight(e).from;
    }
    std::reverse(legsRev.begin(), legsRev.end());

    r.found = true;
    r.legs  = legsRev;
    r.airports.reserve(legsRev.size() + 1);
    r.airports.push_back(src);
    for (std::size_t i = 0; i < legsRev.size(); ++i) {
        const Flight& f = g_.flight(legsRev[i]);
        r.airports.push_back(f.to);
        r.totalCost     += f.cost;
        r.totalDuration += f.duration;
        r.objective     += edgeWeight(f, w);
    }
    return r;
}

// ---------------------------------------------------------------------------
// BFS -- fewest legs, O(V + E)
// ---------------------------------------------------------------------------

Route Router::minStops(int src, int dst) const {
    const int n = g_.numAirports();
    if (src < 0 || src >= n || dst < 0 || dst >= n)
        throw std::out_of_range("minStops: airport id out of range");

    std::vector<int>  parentEdge(static_cast<std::size_t>(n), -1);
    std::vector<char> seen(static_cast<std::size_t>(n), 0);
    std::deque<int>   q;

    seen[src] = 1;
    q.push_back(src);
    while (!q.empty()) {
        const int u = q.front();
        q.pop_front();
        if (u == dst) break;               // layer order: the first hit is optimal
        const std::vector<int>& out = g_.outgoing(u);
        for (std::size_t i = 0; i < out.size(); ++i) {
            const int e = out[i];
            const int v = g_.flight(e).to;
            if (seen[v]) continue;
            seen[v]       = 1;
            parentEdge[v] = e;
            q.push_back(v);
        }
    }

    // Report the trip with its real fare and duration; the hop count was only
    // the thing we minimized.
    RouteWeights unit;
    unit.cost = 0.0; unit.time = 0.0; unit.perLeg = 1.0;
    return buildRoute(src, dst, parentEdge, unit);
}

// ---------------------------------------------------------------------------
// Dijkstra -- O(E log V) with a binary heap and lazy deletion
// ---------------------------------------------------------------------------

namespace {

// Heap entry. Ties break on leg count then airport id so runs are reproducible
// regardless of how the heap happens to order equal keys.
struct HeapNode {
    double dist;
    int    hops;
    int    node;
};

struct HeapGreater {
    bool operator()(const HeapNode& a, const HeapNode& b) const {
        if (a.dist != b.dist) return a.dist > b.dist;
        if (a.hops != b.hops) return a.hops > b.hops;
        return a.node > b.node;
    }
};

} // namespace

Route Router::best(int src, int dst, const RouteWeights& w) const {
    return best(src, dst, w, RouteBan());
}

Route Router::best(int src, int dst, const RouteWeights& w, const RouteBan& ban) const {
    const int n = g_.numAirports();
    if (src < 0 || src >= n || dst < 0 || dst >= n)
        throw std::out_of_range("best: airport id out of range");
    if (!w.valid())
        throw std::invalid_argument("best: RouteWeights must be non-negative for Dijkstra");

    const bool hasAirportBan = !ban.airport.empty();
    const bool hasFlightBan  = !ban.flight.empty();

    if (hasAirportBan && (ban.airport[src] || ban.airport[dst])) return Route();

    const double INF = infinity();
    std::vector<double> dist(static_cast<std::size_t>(n), INF);
    std::vector<int>    hops(static_cast<std::size_t>(n), 0);
    std::vector<int>    parentEdge(static_cast<std::size_t>(n), -1);
    std::vector<char>   done(static_cast<std::size_t>(n), 0);

    std::priority_queue<HeapNode, std::vector<HeapNode>, HeapGreater> pq;
    dist[src] = 0.0;
    HeapNode start; start.dist = 0.0; start.hops = 0; start.node = src;
    pq.push(start);

    while (!pq.empty()) {
        const HeapNode cur = pq.top();
        pq.pop();
        const int u = cur.node;
        if (done[u]) continue;             // stale entry left behind by lazy deletion
        done[u] = 1;
        if (u == dst) break;

        const std::vector<int>& out = g_.outgoing(u);
        for (std::size_t i = 0; i < out.size(); ++i) {
            const int e = out[i];
            if (hasFlightBan && ban.flight[e]) continue;
            const Flight& f = g_.flight(e);
            const int v = f.to;
            if (done[v]) continue;
            if (hasAirportBan && ban.airport[v]) continue;

            const double nd = dist[u] + edgeWeight(f, w);
            const int    nh = cur.hops + 1;
            if (nd < dist[v] || (nd == dist[v] && nh < hops[v])) {
                dist[v]       = nd;
                hops[v]       = nh;
                parentEdge[v] = e;
                HeapNode nxt; nxt.dist = nd; nxt.hops = nh; nxt.node = v;
                pq.push(nxt);
            }
        }
    }

    if (dist[dst] == INF) return Route();
    return buildRoute(src, dst, parentEdge, w);
}

Route Router::cheapest(int src, int dst) const {
    return best(src, dst, RouteWeights::minCost());
}

Route Router::fastest(int src, int dst) const {
    return best(src, dst, RouteWeights::minTime());
}

std::vector<double> Router::distancesFrom(int src, const RouteWeights& w) const {
    const int n = g_.numAirports();
    if (src < 0 || src >= n) throw std::out_of_range("distancesFrom: bad source");
    if (!w.valid()) throw std::invalid_argument("distancesFrom: negative weights");

    const double INF = infinity();
    std::vector<double> dist(static_cast<std::size_t>(n), INF);
    std::vector<char>   done(static_cast<std::size_t>(n), 0);
    std::priority_queue<HeapNode, std::vector<HeapNode>, HeapGreater> pq;

    dist[src] = 0.0;
    HeapNode start; start.dist = 0.0; start.hops = 0; start.node = src;
    pq.push(start);

    while (!pq.empty()) {
        const HeapNode cur = pq.top();
        pq.pop();
        const int u = cur.node;
        if (done[u]) continue;
        done[u] = 1;
        const std::vector<int>& out = g_.outgoing(u);
        for (std::size_t i = 0; i < out.size(); ++i) {
            const Flight& f = g_.flight(out[i]);
            const double nd = dist[u] + edgeWeight(f, w);
            if (nd < dist[f.to]) {
                dist[f.to] = nd;
                HeapNode nxt; nxt.dist = nd; nxt.hops = cur.hops + 1; nxt.node = f.to;
                pq.push(nxt);
            }
        }
    }
    return dist;
}

// ---------------------------------------------------------------------------
// Reachability
// ---------------------------------------------------------------------------

std::vector<bool> Router::reachableFrom(int src) const {
    const int n = g_.numAirports();
    std::vector<bool> seen(static_cast<std::size_t>(n), false);
    if (src < 0 || src >= n) return seen;

    std::deque<int> q;
    seen[src] = true;
    q.push_back(src);
    while (!q.empty()) {
        const int u = q.front();
        q.pop_front();
        const std::vector<int>& out = g_.outgoing(u);
        for (std::size_t i = 0; i < out.size(); ++i) {
            const int v = g_.flight(out[i]).to;
            if (!seen[v]) { seen[v] = true; q.push_back(v); }
        }
    }
    return seen;
}

int Router::reachableCount(int src) const {
    const std::vector<bool> seen = reachableFrom(src);
    int c = 0;
    for (std::size_t i = 0; i < seen.size(); ++i) if (seen[i]) ++c;
    return c;
}

// ---------------------------------------------------------------------------
// Yen's K shortest loopless paths
// ---------------------------------------------------------------------------

namespace {

bool sameLegs(const std::vector<int>& a, const std::vector<int>& b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin());
}

struct RouteCheaper {
    bool operator()(const Route& a, const Route& b) const {
        if (a.objective != b.objective) return a.objective < b.objective;
        return a.legs.size() < b.legs.size();
    }
};

} // namespace

std::vector<Route> Router::kBest(int src, int dst, int K, const RouteWeights& w) const {
    std::vector<Route> accepted;
    if (K <= 0) return accepted;

    const Route first = best(src, dst, w);
    if (!first.found) return accepted;
    accepted.push_back(first);

    std::vector<Route> candidates;
    const int nA = g_.numAirports();
    const int nF = g_.numFlights();

    while (static_cast<int>(accepted.size()) < K) {
        const Route prev = accepted.back();

        // Every prefix of the last accepted route is a possible deviation point.
        for (int i = 0; i < prev.numLegs(); ++i) {
            const int spurNode = prev.airports[static_cast<std::size_t>(i)];
            const std::vector<int> rootLegs(prev.legs.begin(), prev.legs.begin() + i);

            RouteBan ban(nA, nF);
            // Ban the leg every already-accepted route took out of this same root,
            // which forces the spur onto a genuinely different edge.
            for (std::size_t r = 0; r < accepted.size(); ++r) {
                const Route& a = accepted[r];
                if (a.numLegs() > i) {
                    const std::vector<int> aRoot(a.legs.begin(), a.legs.begin() + i);
                    if (sameLegs(aRoot, rootLegs))
                        ban.flight[static_cast<std::size_t>(a.legs[static_cast<std::size_t>(i)])] = true;
                }
            }
            // Ban the root's interior airports so the result stays loopless.
            for (int j = 0; j < i; ++j)
                ban.airport[static_cast<std::size_t>(prev.airports[static_cast<std::size_t>(j)])] = true;

            const Route spur = best(spurNode, dst, w, ban);
            if (!spur.found) continue;

            // Stitch root prefix + spur into a full candidate.
            Route cand;
            cand.found = true;
            cand.legs  = rootLegs;
            cand.legs.insert(cand.legs.end(), spur.legs.begin(), spur.legs.end());
            cand.airports.push_back(src);
            for (std::size_t e = 0; e < cand.legs.size(); ++e) {
                const Flight& f = g_.flight(cand.legs[e]);
                cand.airports.push_back(f.to);
                cand.totalCost     += f.cost;
                cand.totalDuration += f.duration;
                cand.objective     += edgeWeight(f, w);
            }

            bool dup = false;
            for (std::size_t c = 0; c < candidates.size() && !dup; ++c)
                dup = sameLegs(candidates[c].legs, cand.legs);
            for (std::size_t a = 0; a < accepted.size() && !dup; ++a)
                dup = sameLegs(accepted[a].legs, cand.legs);
            if (!dup) candidates.push_back(cand);
        }

        if (candidates.empty()) break;
        std::vector<Route>::iterator bestIt =
            std::min_element(candidates.begin(), candidates.end(), RouteCheaper());
        accepted.push_back(*bestIt);
        candidates.erase(bestIt);
    }
    return accepted;
}

} // namespace flightnet
