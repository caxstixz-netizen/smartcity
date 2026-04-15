// bellman.cpp
#ifndef BELLMAN_CPP
#define BELLMAN_CPP
#include "graph.h" //Provides CityGraph and Edge.
#include <vector> //For distance and predecessor arrays.
#include <chrono> // For high‑resolution timing.
#include <climits> //Provides INT_MAX as initial distance.
#include <algorithm> //For std::reverse.
using namespace std;

//Starts a timer to measure execution time in microseconds.
tuple<vector<int>, double, long long> runBellmanFord(const CityGraph& graph, int src, int dst) {
    auto start = chrono::high_resolution_clock::now();

    int n = graph.getAdj().size();
    vector<double> dist(n, INT_MAX);
    vector<int> prev(n, -1);
    dist[src] = 0;

    // Collect non-blocked edges once — avoids re-checking isBlocked every iteration
    vector<tuple<int,int,double>> edges;
    edges.reserve(n * 4);
    /*reserves space for a rough upper bound 
    (since each node can have multiple edges).*/
    for (int u = 0; u < n; ++u) {
        for (const Edge& e : graph.getAdj()[u]) {
            if (!graph.isBlocked(u, e.to))
                edges.emplace_back(u, e.to, e.weight);
        }
    }

    // Relax |V|-1 times with early exit
    for (int i = 0; i < n - 1; ++i) {
        bool updated = false;
        for (auto& [u, v, w] : edges) {
            if (dist[u] != INT_MAX && dist[u] + w < dist[v]) {
                dist[v] = dist[u] + w;
                prev[v] = u;
                updated = true;
                /*f in some iteration no distance improves, 
                we can stop early because all shortest paths 
                have already been found.*/
            }
        }
        if (!updated) break;
    }

    //Stop timer and compute elapsed microseconds.
    auto end = chrono::high_resolution_clock::now();
    long long elapsed = chrono::duration_cast<chrono::microseconds>(end - start).count();

    if (dist[dst] == INT_MAX) return {{}, -1, elapsed};

    //The loop pushes nodes in reverse order (from destination to source), 
    //so we reverse the vector to get the correct order from source to destination.
    vector<int> path;
    for (int at = dst; at != -1; at = prev[at]) path.push_back(at);
    reverse(path.begin(), path.end());

    return {path, dist[dst], elapsed};
}
#endif