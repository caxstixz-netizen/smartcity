// graph.h — CityGraph with dynamic node/edge support + thread-safe traffic weights
#pragma once //Header guard, ensures the file is included only once.

//Includes necessary standard libraries:
#include <vector>
#include <string>
#include <utility>
#include <unordered_set>
#include <tuple>
#include <iostream>
#include <algorithm>
#include <map>
#include <mutex>

/*Represents a directed edge from a node to 
"to" with a given weight (distance in meters, 
or time/cost after traffic multipliers).*/
struct Edge {
    int to;
    double weight;
};
/*for each physical road, added two Edge objects (both directions). 
The to field indicates the neighbour node.*/

struct PairHash {
    size_t operator()(const std::pair<int,int>& p) const {
        return std::hash<int>()(p.first) ^ (std::hash<int>()(p.second) << 1);
    }
};

class CityGraph {
public:
//Default constructor, delegates to the parameterised constructor with nodes = 0
    CityGraph() : CityGraph(0) {}
    CityGraph(int nodes) : adj(nodes), nodeNames(), nodeActive(nodes, true) {}
    /*adj – adjacency list, resized to nodes.
    nodeNames – empty map (filled later).
    nodeActive – vector of bool initialised to true 
    for all nodes (all nodes active by default)*/

    // std::mutex is not copyable/movable, so we must define these manually.
    // The copy creates a fresh mutex — the copied graph gets its own independent lock.
    CityGraph(const CityGraph& other) {
        std::lock_guard<std::mutex> lock(other.weightMutex);
        adj                = other.adj;
        nodeNames          = other.nodeNames;
        nodeActive         = other.nodeActive;
        nodeCoords         = other.nodeCoords;
        blockedEdges       = other.blockedEdges;
        riotNodes          = other.riotNodes;
        originalEdges      = other.originalEdges;
        rawEdges           = other.rawEdges;
        baseWeights        = other.baseWeights;
        trafficMultipliers = other.trafficMultipliers;
        // weightMutex is default-constructed (fresh, unlocked) — intentional
    }

    /*Copy assignment: Locks both other and this mutexes 
    (in a fixed order to avoid deadlock). Copies all members.*/
    CityGraph& operator=(const CityGraph& other) {
        if (this == &other) return *this;
        // Lock both objects to be safe (disaster functions assign into an
        // already-constructed local, so deadlock is not possible here)
        std::lock_guard<std::mutex> lockOther(other.weightMutex);
        std::lock_guard<std::mutex> lockSelf(weightMutex);
        adj                = other.adj;
        nodeNames          = other.nodeNames;
        nodeActive         = other.nodeActive;
        nodeCoords         = other.nodeCoords;
        blockedEdges       = other.blockedEdges;
        riotNodes          = other.riotNodes;
        originalEdges      = other.originalEdges;
        rawEdges           = other.rawEdges;
        baseWeights        = other.baseWeights;
        trafficMultipliers = other.trafficMultipliers;
        return *this;
    }

    // Move constructor Locks the source's mutex, then moves all members. 
    //The source is left in a valid but unspecified state.
    CityGraph(CityGraph&& other) noexcept {
        std::lock_guard<std::mutex> lock(other.weightMutex);
        adj                = std::move(other.adj);
        nodeNames          = std::move(other.nodeNames);
        nodeActive         = std::move(other.nodeActive);
        nodeCoords         = std::move(other.nodeCoords);
        blockedEdges       = std::move(other.blockedEdges);
        riotNodes          = std::move(other.riotNodes);
        originalEdges      = std::move(other.originalEdges);
        rawEdges           = std::move(other.rawEdges);
        baseWeights        = std::move(other.baseWeights);
        trafficMultipliers = std::move(other.trafficMultipliers);
    }

    //Move assignment: Similar locking and moving.
    CityGraph& operator=(CityGraph&& other) noexcept {
        if (this == &other) return *this;
        std::lock_guard<std::mutex> lockOther(other.weightMutex);
        std::lock_guard<std::mutex> lockSelf(weightMutex);
        adj                = std::move(other.adj);
        nodeNames          = std::move(other.nodeNames);
        nodeActive         = std::move(other.nodeActive);
        nodeCoords         = std::move(other.nodeCoords);
        blockedEdges       = std::move(other.blockedEdges);
        riotNodes          = std::move(other.riotNodes);
        originalEdges      = std::move(other.originalEdges);
        rawEdges           = std::move(other.rawEdges);
        baseWeights        = std::move(other.baseWeights);
        trafficMultipliers = std::move(other.trafficMultipliers);
        return *this;
    }

    // ── Core edge operations ──────────────────────────────────────────────
    void addEdge(int from, int to, double weight) {
        // Expand adjacency list if needed
        int maxIdx = std::max(from, to);
        if (maxIdx >= (int)adj.size()) {
            /*Adds a directed edge. If the node index is out of range, 
            resizes adjacency, nodeActive, and nodeCoords accordingly.*/
            adj.resize(maxIdx + 1);
            nodeActive.resize(maxIdx + 1, true);
            nodeCoords.resize(maxIdx + 1, {0.0, 0.0});
        }
        adj[from].push_back({to, weight});
    }

    /*Adds both directions and also stores the edge in rawEdges 
    (a list of all undirected edges, used for resetting).*/
    void addBidirectionalEdge(int from, int to, double weight) {
        addEdge(from, to, weight);
        addEdge(to, from, weight);
        rawEdges.emplace_back(from, to, weight);
    }

    /*Removes both directed edges and also removes the undirected 
    representation from rawEdges.*/
    void removeEdge(int from, int to) {
        auto& v = adj[from];
        v.erase(std::remove_if(v.begin(), v.end(),
            [to](const Edge& e){ return e.to == to; }), v.end());
        auto& u = adj[to];
        u.erase(std::remove_if(u.begin(), u.end(),
            [from](const Edge& e){ return e.to == from; }), u.end());
        rawEdges.erase(std::remove_if(rawEdges.begin(), rawEdges.end(),
            [from, to](const auto& t){
                return (std::get<0>(t)==from && std::get<1>(t)==to) ||
                       (std::get<0>(t)==to   && std::get<1>(t)==from);
            }), rawEdges.end());
    }

    // ── Node operations ───────────────────────────────────────────────────
    int addNode(const std::string& name, double lat, double lng) {
        int id = (int)adj.size();
        adj.emplace_back();
        nodeNames[id] = name;
        nodeActive.push_back(true);
        nodeCoords.push_back({lat, lng});
        return id;
    }

    void removeNode(int id) {
        if (id < 0 || id >= (int)adj.size()) return;
        // Remove all edges incident to this node
        for (int u = 0; u < (int)adj.size(); ++u) {
            auto& v = adj[u];
            v.erase(std::remove_if(v.begin(), v.end(),
                [id](const Edge& e){ return e.to == id; }), v.end());
        }
        adj[id].clear();
        nodeActive[id] = false;
        nodeNames.erase(id);
        rawEdges.erase(std::remove_if(rawEdges.begin(), rawEdges.end(),
            [id](const auto& t){
                return std::get<0>(t)==id || std::get<1>(t)==id;
            }), rawEdges.end());
    }

    void updateNode(int id, const std::string& name, double lat, double lng) {
        if (id < 0 || id >= (int)adj.size()) return;
        nodeNames[id] = name;
        if ((int)nodeCoords.size() > id) nodeCoords[id] = {lat, lng};
    }

    // ── Blocked edges ─────────────────────────────────────────────────────
    void setBlockedEdges(const std::vector<std::pair<int,int>>& blocked) {
        //stores both directions of each blocked edge in an unordered set.
        blockedEdges.clear();
        for (const auto& e : blocked) {
            blockedEdges.insert(e);
            blockedEdges.insert({e.second, e.first});
        }
    }

    bool isBlocked(int u, int v) const { //checks if a directed edge is blocked.
        return blockedEdges.find({u, v}) != blockedEdges.end();
    }

    // ── Riot zone ─────────────────────────────────────────────────────────
    void setRiotNodes(const std::vector<int>& nodes) {
        riotNodes.clear();
        for (int n : nodes) riotNodes.insert(n);
    }

    bool isRiotAffected(int u, int v) const {
        return riotNodes.count(u) || riotNodes.count(v);
    }

    // ── Road polyline (intermediate waypoints for an edge) ─────────────────
    // Returns a sequence of {lat,lng} points for drawing curved road paths.
    // Uses a simple arc: one midpoint offset perpendicular to the straight line.
    std::vector<std::pair<double,double>> getRoadPoints(int u, int v) const {
        if (u < 0 || u >= (int)nodeCoords.size() ||
            v < 0 || v >= (int)nodeCoords.size()) return {};
        auto [lat1, lng1] = nodeCoords[u];
        auto [lat2, lng2] = nodeCoords[v];
        // Perpendicular offset: 15% of segment length, alternating direction
        double dlat = lat2 - lat1, dlng = lng2 - lng1;
        double sign = ((u + v) % 2 == 0) ? 1.0 : -1.0;
        double midLat = (lat1 + lat2) / 2.0 + sign * dlng * 0.15;
        double midLng = (lng1 + lng2) / 2.0 - sign * dlat * 0.15;
        return { {lat1, lng1}, {midLat, midLng}, {lat2, lng2} };
    }

    // ── Weight operations ─────────────────────────────────────────────────
    void updateEdgeWeight(int u, int v, double newWeight) {
        for (auto& e : adj[u]) if (e.to == v) { e.weight = newWeight; break; }
        for (auto& e : adj[v]) if (e.to == u) { e.weight = newWeight; break; }
    }

    double getEdgeWeight(int u, int v) const {
        for (const auto& e : adj[u]) if (e.to == v) return e.weight;
        return -1.0;
    }

    // ── Dynamic (traffic-aware) weight updates — thread-safe ─────────────
    // multiplier is applied on top of the stored base weight.
    // Call from traffic background thread; pathfinding thread reads via getAdj().
    void updateEdgeWeightDynamic(int u, int v, double multiplier) {
        std::lock_guard<std::mutex> lock(weightMutex);
        // Retrieve base weight
        double base = baseWeight(u, v);
        if (base < 0) return; // edge doesn't exist
        double newW = base * multiplier;
        for (auto& e : adj[u]) if (e.to == v) { e.weight = newW; break; }
        for (auto& e : adj[v]) if (e.to == u) { e.weight = newW; break; }
        trafficMultipliers[{std::min(u,v), std::max(u,v)}] = multiplier;
    }

    // Reset all edges to base weights (clears traffic multipliers)
    void resetToBaseWeights() {
        std::lock_guard<std::mutex> lock(weightMutex);
        for (const auto& [u, v, w] : originalEdges) {
            for (auto& e : adj[u]) if (e.to == v) { e.weight = w; break; }
            for (auto& e : adj[v]) if (e.to == u) { e.weight = w; break; }
        }
        trafficMultipliers.clear();
    }

    double getTrafficMultiplier(int u, int v) const {
        std::lock_guard<std::mutex> lock(weightMutex);
        auto it = trafficMultipliers.find({std::min(u,v), std::max(u,v)});
        return (it != trafficMultipliers.end()) ? it->second : 1.0;
    }

    // Snapshot all current traffic multipliers (for /api/traffic/status)
    std::map<std::pair<int,int>, double> getAllTrafficMultipliers() const {
        std::lock_guard<std::mutex> lock(weightMutex);
        return trafficMultipliers;
    }

    // ── Original edge storage ─────────────────────────────────────────────
    void setOriginalEdges(const std::vector<std::tuple<int,int,double>>& edgeList) {
        originalEdges = edgeList;
        rawEdges = edgeList;
        // Pre-populate base weight map
        for (const auto& [u, v, w] : edgeList) {
            baseWeights[{std::min(u,v), std::max(u,v)}] = w;
        }
    }

    void resetToOriginal() {
        resetToBaseWeights();
    }

    // ── Accessors ─────────────────────────────────────────────────────────
    const std::vector<std::vector<Edge>>& getAdj() const { return adj; }
    std::vector<std::vector<Edge>>& getMutableAdj() { return adj; }
    int nodeCount() const { return (int)adj.size(); }
    bool isActive(int id) const {
        return id >= 0 && id < (int)nodeActive.size() && nodeActive[id];
    }
    const std::vector<std::tuple<int,int,double>>& getRawEdges() const { return rawEdges; }

    // ── Public data ───────────────────────────────────────────────────────
    std::map<int, std::string> nodeNames;
    std::vector<std::pair<double,double>> nodeCoords; // {lat, lng}
    std::vector<bool> nodeActive;

private:
    std::vector<std::vector<Edge>> adj;
    std::unordered_set<std::pair<int,int>, PairHash> blockedEdges;
    std::unordered_set<int> riotNodes;
    std::vector<std::tuple<int,int,double>> originalEdges;
    std::vector<std::tuple<int,int,double>> rawEdges;

    // Dynamic traffic support
    mutable std::mutex weightMutex;
    std::map<std::pair<int,int>, double> baseWeights;      // original distances
    std::map<std::pair<int,int>, double> trafficMultipliers;

    double baseWeight(int u, int v) const {
        auto it = baseWeights.find({std::min(u,v), std::max(u,v)});
        if (it != baseWeights.end()) return it->second;
        // Fallback: current adj weight divided by 1 (no multiplier stored)
        for (const auto& e : adj[u]) if (e.to == v) return e.weight;
        return -1.0;
    }
};