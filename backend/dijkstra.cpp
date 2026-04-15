// dijkstra.cpp
#ifndef DIJKSTRA_CPP //prevents multiple inclusions
#define DIJKSTRA_CPP //prevents multiple inclusions
#include "graph.h" //Provides CityGraph and Edge.
#include <vector> //For distance and predecessor arrays.
#include <queue> // For std::priority_queue (min‑heap).
#include <chrono> // For high‑resolution timing.
#include <climits> //Provides INT_MAX as initial distance.
#include <algorithm> //For std::reverse.
using namespace std; 

//Start timer to measure execution time in microseconds.
tuple<vector<int>, double, long long> runDijkstra(const CityGraph& graph, int src, int dst, double raiot) {
    auto start = chrono::high_resolution_clock::now();

    int n = graph.getAdj().size();
    vector<double> dist(n, INT_MAX);
    vector<int> prev(n, -1);
    vector<bool> visited(n, false);

    //Min‑heap priority queue – stores (distance, node) pairs. greater<pair<double,int>> makes it a min‑heap (smallest distance on top).
    priority_queue<pair<double,int>, vector<pair<double,int>>, greater<pair<double,int>>> pq;
    dist[src] = 0;
    pq.push({0, src});

    while (!pq.empty()) {
        //Mark node u as visited (its final distance is now known).
        //Early exit If u is the destination, we can stop because Dijkstra
        auto [d, u] = pq.top(); pq.pop();
        if (visited[u]) continue;
        visited[u] = true;
        if (u == dst) break;

        //Iterate over all outgoing edges from u
        for (const Edge& e : graph.getAdj()[u]) {
            if (graph.isBlocked(u, e.to)) continue;
            double nd = d + e.weight;
            //If nd is better than the current known distance to e.to, 
            //update and push the new pair into the priority queue 
            //(old entries will be ignored later when visited).
            if (nd < dist[e.to]) {
                dist[e.to] = nd;
                prev[e.to] = u;
                pq.push({nd, e.to});
            }
        }
    }

    //Stop timer, compute microseconds.
    auto end = chrono::high_resolution_clock::now();
    long long elapsed = chrono::duration_cast<chrono::microseconds>(end - start).count();

    if (dist[dst] == INT_MAX) return {{}, -1, elapsed};

    // raiot check
    if (raiot >= 0 && dist[dst] > raiot) return {{}, -1, elapsed};

    //Reconstruct the path from destination back to source using prev array, 
    //then reverse to get source→destination order.
    vector<int> path;
    for (int at = dst; at != -1; at = prev[at]) path.push_back(at);
    reverse(path.begin(), path.end());

    return {path, dist[dst], elapsed};
}
#endif