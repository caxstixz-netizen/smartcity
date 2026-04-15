// algorithms.h
/*– A preprocessor directive that ensures this header 
file is included only once per compilation unit. 
It prevents duplicate declarations and compilation 
errors caused by multiple #includes*/
#pragma once

/*header, which defines the CityGraph class 
(your graph representation with nodes, 
edges, weights, dynamic traffic, etc.). 
All three algorithms need to read the graph structure.*/
#include "graph.h"
#include <tuple> //(path, cost, execution time).
#include <vector> //used to store the sequence of node IDs in the found path.


/*Parameters:
const CityGraph& graph – Read‑only reference to the graph.
int src – Source node index.
int dst – Destination node index.*/
std::tuple<std::vector<int>, double, long long> runDijkstra(const CityGraph& graph, int src, int dst, double raiot = -1.0);
std::tuple<std::vector<int>, double, long long> runBellmanFord(const CityGraph& graph, int src, int dst, double raiot = -1.0);
std::tuple<std::vector<int>, double, long long> runBacktracking(const CityGraph& graph, int src, int dst, double raiot = -1.0);

/*Return type: std::tuple<std::vector<int>, double, long long>
First element: std::vector<int> – The shortest path as a sequence of node IDs (including source and destination). Empty if no path exists.
Second element: double – Total cost (sum of edge weights) of the found path, or -1.0 if no path exists.
Third element: long long – Execution time in microseconds (µs), measured internally.*/