// dijkstra.cpp
#ifndef DIJKSTRA_CPP
#define DIJKSTRA_CPP
#include "graph.h"
#include <vector>
#include <queue>
#include <chrono>
#include <climits>
#include <algorithm>
using namespace std;

tuple<vector<int>, double, long long> runDijkstra(const CityGraph& graph, int src, int dst, double raiot) {
    auto start = chrono::high_resolution_clock::now();

    int n = graph.getAdj().size();
    vector<double> dist(n, INT_MAX);
    vector<int> prev(n, -1);
    vector<bool> visited(n, false);

    priority_queue<pair<double,int>, vector<pair<double,int>>, greater<pair<double,int>>> pq;
    dist[src] = 0;
    pq.push({0, src});

    while (!pq.empty()) {
        auto [d, u] = pq.top(); pq.pop();
        if (visited[u]) continue;
        visited[u] = true;
        if (u == dst) break;

        for (const Edge& e : graph.getAdj()[u]) {
            if (graph.isBlocked(u, e.to)) continue;
            double nd = d + e.weight;
            if (nd < dist[e.to]) {
                dist[e.to] = nd;
                prev[e.to] = u;
                pq.push({nd, e.to});
            }
        }
    }

    auto end = chrono::high_resolution_clock::now();
    long long elapsed = chrono::duration_cast<chrono::microseconds>(end - start).count();

    if (dist[dst] == INT_MAX) return {{}, -1, elapsed};

    // raiot check
    if (raiot >= 0 && dist[dst] > raiot) return {{}, -1, elapsed};

    vector<int> path;
    for (int at = dst; at != -1; at = prev[at]) path.push_back(at);
    reverse(path.begin(), path.end());

    return {path, dist[dst], elapsed};
}
#endif