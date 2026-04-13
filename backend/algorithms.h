// algorithms.h
#pragma once

#include "graph.h"
#include <tuple>
#include <vector>

// raiot = maximum allowed total path cost (-1.0 means no limit)
std::tuple<std::vector<int>, double, long long> runDijkstra(const CityGraph& graph, int src, int dst, double raiot = -1.0);
std::tuple<std::vector<int>, double, long long> runBellmanFord(const CityGraph& graph, int src, int dst, double raiot = -1.0);
std::tuple<std::vector<int>, double, long long> runBacktracking(const CityGraph& graph, int src, int dst, double raiot = -1.0);