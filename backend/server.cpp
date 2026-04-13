// server.cpp
#include <iostream>
#include <vector>
#include <string>
#include <tuple>
#include <algorithm>
#include <numeric>      // for accumulate
#include <random>       // for std::shuffle and random_device
#include <cstdlib>      // for rand
#include <ctime>        // for time()
#include <map>
#include <set>
#include <cmath>
#include <thread>
#include <chrono>
#include <mutex>
#include <atomic>
#include <functional>
#include "httplib.h"
#include "json.hpp"
#include "graph.h"
#include "algorithms.h"
#include "traffic.h"

using namespace std;
using json = nlohmann::json;

// Safe nodes (evacuation targets) — all within valid range 0–19
const vector<int> SAFE_NODES = {8, 9, 14, 15};  // Bandstand, Mount Mary, Bandra Fort, Pali Hill

// Disaster severity multipliers
const double FLOOD_SPEED_REDUCTION = 8.0;      // 8x slower travel
const double SURGE_SPEED_REDUCTION = 12.0;      // 12x slower (more dangerous)
const double CYCLONE_SPEED_REDUCTION = 5.0;     // 5x slower + some blocked
const double EARTHQUAKE_SPEED_REDUCTION = 3.0;  // 3x slower (road damage)
const double HEATWAVE_SPEED_REDUCTION = 1.5;    // Slight slowdown
const double DISEASE_SPEED_REDUCTION = 1.2;     // Minimal slowdown

// Add node elevations (meters above sea level) — critical for flood/surge logic
const map<int, double> NODE_ELEVATION = {
    {0, 13.0},   // Bandra Station
    {1, 11.0},   // Khar Station
    {2, 8.0},    // Linking Road
    {3, 9.0},    // S.V. Road
    {4, 5.0},    // Turner Road (low-lying)
    {5, 6.0},    // Waterfield Road
    {6, 4.0},    // 14th Road (coastal)
    {7, 4.0},    // Hill Road (coastal)
    {8, 2.0},    // Bandstand (sea level - NOT safe for surge!)
    {9, 24.0},   // Mount Mary (high ground - very safe)
    {10, 10.0},  // Bandra Talao
    {11, 7.0},   // Carter Road Promenade
    {12, 9.0},   // St. Andrew's Church
    {13, 5.0},   // Bandra Reclamation
    {14, 24.0},  // Bandra Fort (high ground - safe)
    {15, 30.0},  // Pali Hill (highest ground - very safe)
    {16, 8.0},   // Khar Gymkhana
    // NEW corridor nodes
    {17, 9.0},   // Khara Road
    {18, 8.0},   // 16th Road–Linking Jn.
    {19, 10.0},  // Perry Cross Road
};

// Smart evacuation: choose safe node based on elevation AND distance
pair<int, double> getSmartEvacuationNode(const CityGraph& graph, int source, 
                                          const vector<int>& safeNodes) {
    double bestScore = -1e9; // Higher is better
    int bestNode = -1;
    
    for (int safe : safeNodes) {
        auto [path, cost, time] = runDijkstra(graph, source, safe);
        if (cost < 0) continue; // unreachable
        
        // Elevation bonus (higher = safer)
        double elevBonus = NODE_ELEVATION.count(safe) ? NODE_ELEVATION.at(safe) : 10.0;
        
        // Combined score: prioritize high ground + short distance
        // Lower cost = better (shorter distance)
        // Higher elevation = better
        double score = (1000.0 / (cost + 100.0)) + (elevBonus / 5.0);
        
        if (score > bestScore) {
            bestScore = score;
            bestNode = safe;
        }
    }
    
    if (bestNode == -1) return {-1, -1};
    return {bestNode, std::get<1>(runDijkstra(graph, source, bestNode))};
}

// Flood: affects low-lying areas (elevation < 7m)
CityGraph applyUrbanFlood(const CityGraph& original) {
    CityGraph modified = original;
    auto& adj = modified.getMutableAdj();
    for (int u = 0; u < modified.nodeCount(); ++u) {
        for (auto& e : adj[u]) {
            double elevU = NODE_ELEVATION.count(u) ? NODE_ELEVATION.at(u) : 10.0;
            double elevV = NODE_ELEVATION.count(e.to) ? NODE_ELEVATION.at(e.to) : 10.0;
            if (elevU < 7.0 || elevV < 7.0) {
                e.weight *= FLOOD_SPEED_REDUCTION;
            }
        }
    }
    return modified;
}

// Storm Surge: affects coastal areas (elevation < 5m) — makes Bandstand unsafe!
CityGraph applyStormSurge(const CityGraph& original) {
    CityGraph modified = original;
    auto& adj = modified.getMutableAdj();
    for (int u = 0; u < modified.nodeCount(); ++u) {
        for (auto& e : adj[u]) {
            double elevU = NODE_ELEVATION.count(u) ? NODE_ELEVATION.at(u) : 10.0;
            double elevV = NODE_ELEVATION.count(e.to) ? NODE_ELEVATION.at(e.to) : 10.0;
            if (elevU < 5.0 || elevV < 5.0) {
                e.weight *= SURGE_SPEED_REDUCTION;
            }
        }
    }
    return modified;
}

// Cyclone: high winds — affects exposed areas, random damage
CityGraph applyCyclone(const CityGraph& original) {
    CityGraph modified = original;
    auto& adj = modified.getMutableAdj();
    std::mt19937 rng(std::random_device{}());
    std::uniform_real_distribution<double> dist(0.0, 1.0);
    
    for (int u = 0; u < modified.nodeCount(); ++u) {
        for (auto& e : adj[u]) {
            double elevU = NODE_ELEVATION.count(u) ? NODE_ELEVATION.at(u) : 10.0;
            double elevV = NODE_ELEVATION.count(e.to) ? NODE_ELEVATION.at(e.to) : 10.0;
            // Coastal/exposed areas more affected
            if (elevU < 8.0 || elevV < 8.0) {
                e.weight *= CYCLONE_SPEED_REDUCTION;
            } else {
                e.weight *= (1.0 + dist(rng) * 2.0); // random damage
            }
            // 15% chance of road blockage
            if (dist(rng) < 0.15) {
                e.weight = 1e9; // effectively blocked
            }
        }
    }
    return modified;
}

// Earthquake: all roads damaged (weight increase)
CityGraph applyEarthquake(const CityGraph& original) {
    CityGraph modified = original;
    auto& adj = modified.getMutableAdj();
    for (int u = 0; u < modified.nodeCount(); ++u) {
        for (auto& e : adj[u]) {
            e.weight *= EARTHQUAKE_SPEED_REDUCTION;
        }
    }
    return modified;
}

// Heatwave: slight slowdown, no road damage
CityGraph applyHeatwave(const CityGraph& original) {
    CityGraph modified = original;
    auto& adj = modified.getMutableAdj();
    for (int u = 0; u < modified.nodeCount(); ++u) {
        for (auto& e : adj[u]) {
            e.weight *= HEATWAVE_SPEED_REDUCTION;
        }
    }
    return modified;
}

// Post-Flood Disease: minimal slowdown, but some areas risky
CityGraph applyDiseaseOutbreak(const CityGraph& original) {
    CityGraph modified = original;
    auto& adj = modified.getMutableAdj();
    // Low-lying areas (where water stagnates) slightly affected
    for (int u = 0; u < modified.nodeCount(); ++u) {
        for (auto& e : adj[u]) {
            double elevU = NODE_ELEVATION.count(u) ? NODE_ELEVATION.at(u) : 10.0;
            double elevV = NODE_ELEVATION.count(e.to) ? NODE_ELEVATION.at(e.to) : 10.0;
            if (elevU < 6.0 || elevV < 6.0) {
                e.weight *= DISEASE_SPEED_REDUCTION;
            }
        }
    }
    return modified;
}

// Sea-Level Rise (long-term): gradually makes low areas impassable
CityGraph applySeaLevelRise(const CityGraph& original) {
    CityGraph modified = original;
    auto& adj = modified.getMutableAdj();
    for (int u = 0; u < modified.nodeCount(); ++u) {
        for (auto& e : adj[u]) {
            double elevU = NODE_ELEVATION.count(u) ? NODE_ELEVATION.at(u) : 10.0;
            double elevV = NODE_ELEVATION.count(e.to) ? NODE_ELEVATION.at(e.to) : 10.0;
            if (elevU < 3.0 || elevV < 3.0) {
                e.weight = 1e9; // permanently submerged
            } else if (elevU < 5.0 || elevV < 5.0) {
                e.weight *= 4.0; // frequent flooding
            }
        }
    }
    return modified;
}

// ─── SSE Client Registry ──────────────────────────────────────────────────────
// Connected browser clients that listen on /api/traffic-stream.
// Guarded by clientsMutex.
static vector<httplib::DataSink*> connectedClients;
static mutex clientsMutex;

// Broadcast a JSON payload to all connected SSE clients.
static void broadcastTrafficUpdate(const json& payload) {
    string data = "data: " + payload.dump() + "\n\n";
    lock_guard<mutex> lock(clientsMutex);
    connectedClients.erase(
        remove_if(connectedClients.begin(), connectedClients.end(),
            [&](httplib::DataSink* sink) {
                if (!sink->is_writable()) return true;
                sink->write(data.data(), data.size());
                return false;
            }),
        connectedClients.end());
}

// ─── Traffic state ────────────────────────────────────────────────────────────
// Shared between the background traffic thread and HTTP handlers.
static json  lastTrafficPayload;   // last broadcast payload (for latecomers)
static mutex trafficPayloadMutex;

int main() {
    httplib::Server svr;

    // Serve static files from the frontend folder (one level up)
    svr.set_mount_point("/", "../frontend");

    // Global CORS headers for all responses
    svr.set_pre_routing_handler([](const auto& req, auto& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Access-Control-Allow-Headers", "Content-Type");
        if (req.method == "OPTIONS") {
            res.status = 204;
            return httplib::Server::HandlerResponse::Handled;
        }
        return httplib::Server::HandlerResponse::Unhandled;
    });

    CityGraph graph(20);

    // Define edges: from, to, weight (meters)
    // 20 nodes (0–19): 17 original Bandra nodes + 3 NEW Bandra-Khar corridor nodes
    vector<tuple<int,int,double>> mumbaiEdges = {
        // ── Core Bandra grid (nodes 0–8) ──────────────────────────────────
        {0,2,1600}, // Bandra Stn → Linking Rd (~1.6 km)
        {0,3,900},  // Bandra Stn → S.V. Road (~900 m)
        {1,2,800},  // Khar Stn   → Linking Rd (~800 m)
        {1,3,500},  // Khar Stn   → S.V. Road (~500 m)
        {2,4,700},  // Linking Rd → Turner Rd (~700 m)
        {2,5,600},  // Linking Rd → Waterfield Rd (~600 m)
        {3,4,1100}, // S.V. Road  → Turner Rd (~1.1 km)
        {3,5,800},  // S.V. Road  → Waterfield Rd (~800 m)
        {4,6,550},  // Turner Rd  → 14th Road (~550 m)
        {4,7,450},  // Turner Rd  → Hill Road (~450 m)  [FIX: was 750, real ~430 m]
        {5,6,600},  // Waterfield → 14th Road (~600 m)
        {5,7,700},  // Waterfield → Hill Road (~700 m)
        {6,8,650},  // 14th Road  → Bandstand (~650 m)
        {7,9,500},  // Hill Road  → Mount Mary (~500 m)
        {8,9,500},  // Bandstand  → Mount Mary (~500 m)  [FIX: was 900, real ~520 m]
        // ── Nodes 10–15 ───────────────────────────────────────────────────
        {0,10,350},   // Bandra Stn  → Bandra Talao (~350 m)
        {3,10,650},   // S.V. Road   → Bandra Talao (~650 m)  [FIX: was 200, real ~658 m]
        {7,11,1700},  // Hill Road   → Carter Rd Promenade (~1700 m)  [FIX: was 400, real ~1695 m]
        {4,12,300},   // Turner Rd   → St. Andrew's Church (~300 m)
        {11,12,1150}, // Carter Rd   → St. Andrew's Church (~1150 m)  [FIX: was 250, real ~1140 m]
        {5,13,450},   // Waterfield  → Bandra Reclamation (~450 m)
        {8,14,300},   // Bandstand   → Bandra Fort (~300 m)
        {9,14,500},   // Mount Mary  → Bandra Fort (~500 m)
        {1,15,600},   // Khar Stn    → Pali Hill (~600 m)
        {10,15,1800}, // Bandra Talao → Pali Hill (~1800 m)  [FIX: was 700, real ~1820 m]
        {12,15,1450}, // St. Andrew's → Pali Hill (~1450 m)  [FIX: was 350, real ~1455 m]
        {14,15,3000}, // Bandra Fort  → Pali Hill (~3000 m)  [FIX: was 800, real ~3019 m]
        // ── Node 16 ───────────────────────────────────────────────────────
        {1,16,250},  // Khar Stn   → Khar Gymkhana (~250 m)
        {2,16,550},  // Linking Rd → Khar Gymkhana (~550 m)
        // ── NEW nodes 17–19: Bandra–Khar corridor (~3–4 km total) ────────
        {16,17,250}, // Khar Gymkhana   → Khara Road (~250 m)  [FIX: was 400, real ~234 m]
        {3,17,600},  // S.V. Road       → Khara Road (~600 m)
        {17,18,350}, // Khar Danda Rd   → 16th Rd–Linking Jn (~350 m)
        {2,18,300},  // Linking Road    → 16th Rd–Linking Jn (~300 m)
        {18,19,400}, // 16th Rd–Lnkg Jn→ Perry Cross Road (~400 m)
        {15,19,450}, // Pali Hill       → Perry Cross Road (~450 m)
        {11,19,500}, // Carter Rd Prom  → Perry Cross Road (~500 m)
    };

    for (auto [u, v, w] : mumbaiEdges) {
        graph.addEdge(u, v, w);
        graph.addEdge(v, u, w);   // undirected
    }

    // Store original edges for reset
    graph.setOriginalEdges(mumbaiEdges);

    // Node names (for display)
    graph.nodeNames[0] = "Bandra Station";
    graph.nodeNames[1] = "Khar Station";
    graph.nodeNames[2] = "Linking Road";
    graph.nodeNames[3] = "S.V. Road";
    graph.nodeNames[4] = "Turner Road";
    graph.nodeNames[5] = "Waterfield Road";
    graph.nodeNames[6] = "14th Road";
    graph.nodeNames[7] = "Hill Road";
    graph.nodeNames[8] = "Bandstand";
    graph.nodeNames[9] = "Mount Mary";
    graph.nodeNames[10] = "Bandra Talao";
    graph.nodeNames[11] = "Carter Road Promenade";
    graph.nodeNames[12] = "St. Andrew's Church";
    graph.nodeNames[13] = "Bandra Reclamation";
    graph.nodeNames[14] = "Bandra Fort";
    graph.nodeNames[15] = "Pali Hill";
    graph.nodeNames[16] = "Khar Gymkhana";
    // NEW: 3 replacement nodes between Bandra West and Khar
    graph.nodeNames[17] = "Khara Road";
    graph.nodeNames[18] = "16th Road-Linking Jn.";
    graph.nodeNames[19] = "Perry Cross Road";

    // Node coordinates (lat, lng) – Mumbai map
    graph.nodeCoords = {
        {19.0544, 72.8406},   // 0:  Bandra Station
        {19.0711, 72.8362},   // 1:  Khar Station
        {19.0621, 72.8347},   // 2:  Linking Road
        {19.0622, 72.8396},   // 3:  S.V. Road
        {19.0548, 72.8282},   // 4:  Turner Road
        {19.0549, 72.8316},   // 5:  Waterfield Road
        {19.0509, 72.8247},   // 6:  14th Road
        {19.0511, 72.8294},   // 7:  Hill Road
        {19.0447, 72.8230},   // 8:  Bandstand
        {19.0478, 72.8267},   // 9:  Mount Mary
        {19.056424, 72.838245}, // 10: Bandra Talao
        {19.065185, 72.823235}, // 11: Carter Road Promenade
        {19.05500, 72.82444},   // 12: St. Andrew's Church
        {19.05106, 72.83305},   // 13: Bandra Reclamation
        {19.041770, 72.818580}, // 14: Bandra Fort
        {19.068, 72.826},       // 15: Pali Hill
        {19.0680, 72.8365},     // 16: Khar Gymkhana
        // NEW: 3 replacement nodes (Bandra–Khar corridor, ~3–4 km)
        {19.0660, 72.8372},     // 17: Khara Road
        {19.0645, 72.8355},     // 18: 16th Road–Linking Jn.
        {19.0635, 72.8330},     // 19: Perry Cross Road
    };

    // Existing solve endpoint
    svr.Post("/api/solve", [&](const httplib::Request& req, httplib::Response& res) {
        json result;
        try {
            auto body = json::parse(req.body);

            if (!body.contains("source") || !body.contains("target") || !body.contains("algorithm")) {
                res.status = 400;
                result = {{"error", "Missing required fields: source, target, algorithm"}};
                res.set_content(result.dump(), "application/json");
                return;
            }

            int source = body["source"];
            int target = body["target"];
            string algo = body["algorithm"];

            // raiot: maximum allowed total cost; -1 means no limit
            double raiot = -1.0;
            if (body.contains("raiot") && !body["raiot"].is_null()) {
                raiot = body["raiot"].get<double>();
            }

            if (source < 0 || source >= graph.nodeCount() || target < 0 || target >= graph.nodeCount()) {
                res.status = 400;
                result["error"] = "Source or target out of range (0-" + to_string(graph.nodeCount() - 1) + ")";
                res.set_content(result.dump(), "application/json");
                return;
            }

            // ── CRITICAL FIX: work on a LOCAL copy so blocked edges are
            //    never written to the shared global graph object.
            //    This eliminates the race condition between concurrent requests
            //    and also prevents blocked state leaking between runs.
            CityGraph localGraph = graph;

            if (body.contains("blockedEdges") && body["blockedEdges"].is_array()) {
                vector<pair<int,int>> blocked;
                for (auto& edge : body["blockedEdges"]) {
                    int u = edge[0];
                    int v = edge[1];
                    blocked.push_back({u, v});
                }
                localGraph.setBlockedEdges(blocked);
            } else {
                localGraph.setBlockedEdges({});
            }

            if (algo == "dijkstra") {
                auto [path, cost, timeUs] = runDijkstra(localGraph, source, target, raiot);
                result = {{"path", path}, {"cost", cost}, {"timeUs", timeUs}};
            } else if (algo == "bellman") {
                auto [path, cost, timeUs] = runBellmanFord(localGraph, source, target, raiot);
                result = {{"path", path}, {"cost", cost}, {"timeUs", timeUs}};
            } else if (algo == "backtrack") {
                auto [path, cost, timeUs] = runBacktracking(localGraph, source, target, raiot);
                result = {{"path", path}, {"cost", cost}, {"timeUs", timeUs}};
            } else {
                res.status = 400;
                result = {{"error", "Unknown algorithm. Use: dijkstra, bellman, backtrack"}};
            }
        } catch (const json::parse_error& e) {
            res.status = 400;
            result = {{"error", "Invalid JSON: " + string(e.what())}};
        } catch (const exception& e) {
            res.status = 500;
            result = {{"error", "Internal server error: " + string(e.what())}};
        }
        res.set_content(result.dump(), "application/json");
    });

    // Update edge weight
    svr.Post("/api/update_weight", [&](const httplib::Request& req, httplib::Response& res) {
        json result;
        try {
            auto body = json::parse(req.body);
            int u = body["u"];
            int v = body["v"];
            double newWeight = body["weight"];

            if (u < 0 || u >= graph.nodeCount() || v < 0 || v >= graph.nodeCount()) {
                res.status = 400;
                result = { {"error", "Node index out of range (0-" + to_string(graph.nodeCount() - 1) + ")"} };
            } else if (newWeight <= 0) {
                res.status = 400;
                result = {{"error", "Weight must be positive"}};
            } else {
                graph.updateEdgeWeight(u, v, newWeight);
                result = {{"success", true}, {"message", "Edge weight updated"}};
            }
        } catch (const exception& e) {
            res.status = 500;
            result = {{"error", e.what()}};
        }
        res.set_content(result.dump(), "application/json");
    });

    // Reset graph to original weights
    svr.Post("/api/reset", [&](const httplib::Request& req, httplib::Response& res) {
        graph.resetToOriginal();
        graph.setBlockedEdges({});   // ← also clear any lingering blocked edges
        json result = {{"success", true}, {"message", "Graph reset to original weights"}};
        res.set_content(result.dump(), "application/json");
    });

    // Get all current edges (for frontend sync)
    svr.Get("/api/edges", [&](const httplib::Request& req, httplib::Response& res) {
        json edgesJson = json::array();
        for (int u = 0; u < graph.nodeCount(); ++u) {
            for (const Edge& e : graph.getAdj()[u]) {
                if (u < e.to) { // avoid duplicates
                    edgesJson.push_back({{"from", u}, {"to", e.to}, {"weight", e.weight}});
                }
            }
        }
        res.set_content(edgesJson.dump(), "application/json");
    });

    // NEW: Traffic statistics endpoint (variance and mean of edge weights)
    svr.Get("/api/traffic_stats", [&](const httplib::Request& req, httplib::Response& res) {
        vector<double> weights;
        for (int u = 0; u < graph.nodeCount(); ++u) {
            for (const Edge& e : graph.getAdj()[u]) {
                if (u < e.to) weights.push_back(e.weight);
            }
        }
        if (weights.empty()) {
            res.set_content(json{{"variance", 0}, {"mean", 0}}.dump(), "application/json");
            return;
        }
        double sum = accumulate(weights.begin(), weights.end(), 0.0);
        double mean = sum / weights.size();
        double variance = 0.0;
        for (double w : weights) variance += (w - mean) * (w - mean);
        variance /= weights.size();
        json result = {{"variance", variance}, {"mean", mean}};
        res.set_content(result.dump(), "application/json");
    });

    // Disaster endpoint: apply disaster, find evacuation route, run all algorithms
    svr.Post("/api/disaster", [&](const httplib::Request& req, httplib::Response& res) {
        json result;
        try {
            auto body = json::parse(req.body);
            string disaster = body["disaster"];
            int location = body["location"];
            
            // Normalize disaster string for matching
            string disasterKey = disaster;
            transform(disasterKey.begin(), disasterKey.end(), disasterKey.begin(), ::tolower);
            for (char& c : disasterKey) {
                if (c == ' ') c = '_';
            }
            
            if (location < 0 || location >= graph.nodeCount()) {
                res.status = 400;
                result = {{"error", "Invalid location node (0-" + to_string(graph.nodeCount() - 1) + ")"}};
                res.set_content(result.dump(), "application/json");
                return;
            }
            
            CityGraph disasterGraph = graph;
            
            // Apply appropriate disaster
            if (disasterKey == "urban_flood" || disasterKey == "flood") {
                disasterGraph = applyUrbanFlood(graph);
            } else if (disasterKey == "storm_surge" || disasterKey == "surge") {
                disasterGraph = applyStormSurge(graph);
            } else if (disasterKey == "cyclone") {
                disasterGraph = applyCyclone(graph);
            } else if (disasterKey == "earthquake") {
                disasterGraph = applyEarthquake(graph);
            } else if (disasterKey == "heatwave") {
                disasterGraph = applyHeatwave(graph);
            } else if (disasterKey == "disease" || disasterKey == "post-flood_disease" || disasterKey == "post_flood_disease") {
                disasterGraph = applyDiseaseOutbreak(graph);
            } else if (disasterKey == "sea_level_rise" || disasterKey == "slr") {
                disasterGraph = applySeaLevelRise(graph);
            } else {
                res.status = 400;
                result = {{"error", string("Unknown disaster type: ") + disaster}};
                res.set_content(result.dump(), "application/json");
                return;
            }
            
            // Get smart evacuation node
            auto [safeNode, distToSafe] = getSmartEvacuationNode(disasterGraph, location, SAFE_NODES);
            if (safeNode == -1) {
                result = {{"error", "No evacuation route found"}};
                res.set_content(result.dump(), "application/json");
                return;
            }
            
            // Run all algorithms
            auto [dijkPath, dijkCost, dijkTime] = runDijkstra(disasterGraph, location, safeNode);
            auto [bellPath, bellCost, bellTime] = runBellmanFord(disasterGraph, location, safeNode);
            auto [backPath, backCost, backTime] = runBacktracking(disasterGraph, location, safeNode, -1.0);
            
            // Choose fastest algorithm
            string chosenAlgo;
            long long bestTime = std::min(std::min(dijkTime, bellTime), backTime);
            if (bestTime == dijkTime) chosenAlgo = "dijkstra";
            else if (bestTime == bellTime) chosenAlgo = "bellman";
            else chosenAlgo = "backtrack";
            
            result = json{
                {"safeNode", safeNode},
                {"safeNodeName", graph.nodeNames[safeNode]},
                {"distance", distToSafe},
                {"chosenAlgorithm", chosenAlgo},
                {"results", {
                    {"dijkstra", {{"path", dijkPath}, {"cost", dijkCost}, {"timeUs", dijkTime}}},
                    {"bellman", {{"path", bellPath}, {"cost", bellCost}, {"timeUs", bellTime}}},
                    {"backtrack", {{"path", backPath}, {"cost", backCost}, {"timeUs", backTime}}}
                }}
            };
        } catch (const exception& e) {
            res.status = 500;
            result = {{"error", e.what()}};
        }
        res.set_content(result.dump(), "application/json");
    });

    // ── Traffic Fetcher Setup (Simulation Only) ──────────────────────────────
    // Always run in simulation mode – no API key required.
    std::string tomtomKey = "";               // Empty key → simulation mode
    TrafficFetcher trafficFetcher(tomtomKey);

    cout << "[Traffic] Running simulation\n";

    // Background thread: refresh traffic every 60 seconds.
    // Applies EMA-smoothed multipliers to graph edges, then broadcasts to SSE clients.
    std::thread trafficThread([&]() {
        while (true) {
            try {
                TrafficSnapshot snap = trafficFetcher.fetch();

                // Apply multipliers to graph (thread-safe via CityGraph::updateEdgeWeightDynamic)
                for (const auto& u : snap.updates) {
                    // Parse edge id "X-Y"
                    size_t dash = u.edgeId.find('-');
                    if (dash == std::string::npos) continue;
                    int from = std::stoi(u.edgeId.substr(0, dash));
                    int to   = std::stoi(u.edgeId.substr(dash + 1));
                    graph.updateEdgeWeightDynamic(from, to, u.multiplier);
                }

                // Build SSE payload
                json payload;
                payload["timestamp"]       = snap.timestampMs;
                payload["isLive"]          = snap.isLive;          // will be false
                payload["significantChange"] = snap.significantChange;
                payload["mode"]            = "simulation";

                json edgesArr = json::array();
                for (const auto& u : snap.updates) {
                    edgesArr.push_back({
                        {"edgeId",       u.edgeId},
                        {"currentSpeed", u.currentSpeed},
                        {"freeFlowSpeed",u.freeFlowSpeed},
                        {"multiplier",   u.multiplier},
                        {"severity",     u.severity}
                    });
                }
                payload["edges"] = edgesArr;

                // Cache for late-joining SSE clients
                {
                    lock_guard<mutex> lock(trafficPayloadMutex);
                    lastTrafficPayload = payload;
                }

                broadcastTrafficUpdate(payload);
                cout << "[Traffic] Updated " << snap.updates.size() << " edges (simulation)\n";
            } catch (const std::exception& ex) {
                cerr << "[Traffic] Background thread error: " << ex.what() << "\n";
            }

            // Wait 60 seconds before next refresh
            std::this_thread::sleep_for(std::chrono::seconds(60));
        }
    });
    trafficThread.detach();

    // ── SSE: /api/traffic-stream ───────────────────────────────────────────
    svr.Get("/api/traffic-stream", [&](const httplib::Request&, httplib::Response& res) {
        res.set_header("Cache-Control", "no-cache");
        res.set_header("X-Accel-Buffering", "no");

        res.set_chunked_content_provider("text/event-stream",
            [&](size_t /*offset*/, httplib::DataSink& sink) {
                // Register this client
                {
                    lock_guard<mutex> lock(clientsMutex);
                    connectedClients.push_back(&sink);
                }

                // Send last known state immediately so the client isn't blank
                {
                    lock_guard<mutex> lock(trafficPayloadMutex);
                    if (!lastTrafficPayload.empty()) {
                        string data = "data: " + lastTrafficPayload.dump() + "\n\n";
                        sink.write(data.data(), data.size());
                    }
                }

                // Keep connection alive — httplib will call this repeatedly.
                // Return false only when the client disconnects.
                while (sink.is_writable()) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(500));
                }

                // Deregister on disconnect
                {
                    lock_guard<mutex> lock(clientsMutex);
                    connectedClients.erase(
                        std::remove(connectedClients.begin(), connectedClients.end(), &sink),
                        connectedClients.end());
                }
                return false; // done
            });
    });

    // ── GET /api/traffic/status ────────────────────────────────────────────
    // Returns current traffic multiplier and severity for every edge.
    svr.Get("/api/traffic/status", [&](const httplib::Request&, httplib::Response& res) {
        json result = json::array();
        auto multipliers = graph.getAllTrafficMultipliers();
        for (const auto& [edge, mult] : multipliers) {
            std::string severity = "green";
            if (mult >= 2.0) severity = "red";
            else if (mult >= 1.2) severity = "yellow";

            result.push_back({
                {"from",       edge.first},
                {"to",         edge.second},
                {"multiplier", mult},
                {"severity",   severity},
                {"weight",     graph.getEdgeWeight(edge.first, edge.second)}
            });
        }
        res.set_content(result.dump(), "application/json");
    });

    // ── POST /api/traffic/refresh ──────────────────────────────────────────
    // Triggers an immediate out-of-cycle traffic refresh.
    svr.Post("/api/traffic/refresh", [&](const httplib::Request&, httplib::Response& res) {
        try {
            TrafficSnapshot snap = trafficFetcher.fetch();
            for (const auto& u : snap.updates) {
                size_t dash = u.edgeId.find('-');
                if (dash == std::string::npos) continue;
                int from = std::stoi(u.edgeId.substr(0, dash));
                int to   = std::stoi(u.edgeId.substr(dash + 1));
                graph.updateEdgeWeightDynamic(from, to, u.multiplier);
            }
            json payload;
            payload["timestamp"]        = snap.timestampMs;
            payload["isLive"]           = snap.isLive;
            payload["significantChange"]= snap.significantChange;
            payload["mode"]             = "simulation";
            json edgesArr = json::array();
            for (const auto& u : snap.updates) {
                edgesArr.push_back({
                    {"edgeId",       u.edgeId},
                    {"multiplier",   u.multiplier},
                    {"severity",     u.severity}
                });
            }
            payload["edges"] = edgesArr;
            {
                lock_guard<mutex> lock(trafficPayloadMutex);
                lastTrafficPayload = payload;
            }
            broadcastTrafficUpdate(payload);
            res.set_content(json{{"success", true}, {"edgesUpdated", (int)snap.updates.size()}}.dump(),
                            "application/json");
        } catch (const std::exception& ex) {
            res.status = 500;
            res.set_content(json{{"error", std::string(ex.what())}}.dump(), "application/json");
        }
    });

    svr.Get("/api/status", [](const httplib::Request&, httplib::Response& res) {
        res.set_content("{\"status\":\"ok\"}", "application/json");
    });
    svr.Get("/api/road_points", [&](const httplib::Request& req, httplib::Response& res) {
        json result;
        try {
            int u = std::stoi(req.get_param_value("u"));
            int v = std::stoi(req.get_param_value("v"));
            auto pts = graph.getRoadPoints(u, v);
            json arr = json::array();
            for (auto [lat, lng] : pts) arr.push_back({{"lat", lat}, {"lng", lng}});
            result = {{"points", arr}};
        } catch (...) {
            result = {{"error", "bad params"}};
        }
        res.set_content(result.dump(), "application/json");
    });

    const char* portEnv = std::getenv("PORT");
    int port = portEnv ? std::stoi(portEnv) : 8080;

    cout << "Server listening on port " << port << " (0.0.0.0)" << endl;

    // Auto-open browser – only if PORT is NOT set (i.e., local dev)
    if (!portEnv) {
        std::thread([](){
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            system("start http://localhost:8080");
        }).detach();
    }

    svr.listen("0.0.0.0", port);
    return 0;
}