// traffic.cpp
#include "traffic.h"
#include "httplib.h"
#include "json.hpp"

#include <vector>
#include <chrono>
#include <cmath>
#include <iostream>
#include <random>
#include <sstream>
#include <stdexcept>
#include <thread>

using json = nlohmann::json;

TrafficFetcher::TrafficFetcher(const std::string& apiKey)
    : apiKey_(apiKey), simulationMode_(apiKey.empty()) {}

// ─── Public fetch ──────────────────────────────────────────────────────────
TrafficSnapshot TrafficFetcher::fetch() {
    TrafficSnapshot snap;
    snap.timestampMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    snap.significantChange = false;

    std::vector<TrafficUpdate> updates;
    bool usedLive = false;

    if (!simulationMode_ && !apiKey_.empty()) {
        try {
            updates = fetchTomTom();
            usedLive = true;
        } catch (const std::exception& ex) {
            std::cerr << "[TrafficFetcher] Live fetch failed: " << ex.what()
                      << " — falling back to simulation\n";
        }
    }

    if (!usedLive) {
        updates = simulateTraffic();
    }

    // Apply EMA smoothing and detect significant changes
    {
        std::lock_guard<std::mutex> lock(smoothMutex_);
        for (auto& u : updates) {
            double smoothed = smoothMultiplier(u.edgeId, u.multiplier);
            if (std::abs(smoothed - u.multiplier) > CHANGE_THRESHOLD) {
                snap.significantChange = true;
            }
            u.multiplier = smoothed;
            u.severity   = severityLabel(smoothed);
        }
    }

    snap.updates = std::move(updates);
    snap.isLive  = usedLive;
    return snap;
}

// ─── TomTom API fetch ──────────────────────────────────────────────────────
std::vector<TrafficUpdate> TrafficFetcher::fetchTomTom() {
    httplib::Client cli("https://api.tomtom.com");
    cli.set_connection_timeout(5, 0);
    cli.set_read_timeout(8, 0);

    // Precomputed midpoints for each edge
    static const std::map<std::string, std::pair<double,double>> EDGE_MIDPOINTS = {
        {"0-2",  {19.058, 72.838}}, {"0-3",  {19.058, 72.840}}, {"0-10", {19.055, 72.839}},
        {"1-2",  {19.067, 72.835}}, {"1-3",  {19.067, 72.838}}, {"1-15", {19.069, 72.831}},
        {"1-16", {19.070, 72.836}}, {"2-4",  {19.059, 72.831}}, {"2-5",  {19.059, 72.833}},
        {"2-16", {19.067, 72.836}}, {"2-18", {19.063, 72.837}},
        {"3-4",  {19.058, 72.834}}, {"3-5",  {19.058, 72.836}}, {"3-10", {19.059, 72.839}},
        {"3-17", {19.063, 72.838}},
        {"4-6",  {19.053, 72.826}}, {"4-7",  {19.053, 72.829}}, {"4-12", {19.055, 72.825}},
        {"5-6",  {19.053, 72.828}}, {"5-7",  {19.053, 72.830}}, {"5-13", {19.053, 72.832}},
        {"6-8",  {19.048, 72.824}},
        {"7-9",  {19.049, 72.828}}, {"7-11", {19.058, 72.826}},
        {"8-9",  {19.046, 72.825}}, {"8-14", {19.043, 72.820}},
        {"9-14", {19.045, 72.823}},
        {"10-15",{19.062, 72.832}},
        {"11-12",{19.060, 72.824}}, {"11-19",{19.064, 72.828}},
        {"12-15",{19.062, 72.825}},
        {"14-15",{19.055, 72.822}},
        {"15-19",{19.066, 72.830}},
        // NEW corridor edges 16–19
        {"16-17",{19.067, 72.837}},
        {"17-18",{19.065, 72.836}},
        {"18-19",{19.064, 72.834}},
    };

    std::vector<TrafficUpdate> updates;
    updates.reserve(MUMBAI_EDGE_IDS.size());

    for (const auto& edgeId : MUMBAI_EDGE_IDS) {
        auto it = EDGE_MIDPOINTS.find(edgeId);
        if (it == EDGE_MIDPOINTS.end()) continue;

        auto [lat, lng] = it->second;
        std::ostringstream path;
        path << "/traffic/services/4/flowSegmentData/absolute/10/json"
             << "?key=" << apiKey_
             << "&point=" << lat << "," << lng;

        auto res = cli.Get(path.str());
        if (!res || res->status != 200) {
            updates.push_back({edgeId, 40.0, 40.0, 1.0, "green"});
            continue;
        }

        try {
            auto body = json::parse(res->body);
            const auto& fsd = body.at("flowSegmentData");
            double currentSpeed  = fsd.value("currentSpeed",  40.0);
            double freeFlowSpeed = fsd.value("freeFlowSpeed", 50.0);
            if (currentSpeed <= 0) currentSpeed = 1.0;
            double multiplier = freeFlowSpeed / currentSpeed;

            updates.push_back({
                edgeId,
                currentSpeed,
                freeFlowSpeed,
                multiplier,
                severityLabel(multiplier)
            });
        } catch (...) {
            updates.push_back({edgeId, 40.0, 50.0, 1.25, "yellow"});
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }

    return updates;
}

// ─── Simulation (random walk) ──────────────────────────────────────────────
std::vector<TrafficUpdate> TrafficFetcher::simulateTraffic() {
    static std::mt19937 rng(std::random_device{}());
    static std::uniform_real_distribution<double> nudge(-0.15, 0.15);
    static std::map<std::string, double> simState;

    std::vector<TrafficUpdate> updates;
    updates.reserve(MUMBAI_EDGE_IDS.size());

    for (const auto& edgeId : MUMBAI_EDGE_IDS) {
        if (simState.find(edgeId) == simState.end()) {
            std::uniform_real_distribution<double> init(1.0, 1.5);
            simState[edgeId] = init(rng);
        }

        double& m = simState[edgeId];
        m += nudge(rng);
        m = std::max(0.8, std::min(2.5, m));

        double freeFlow = 50.0;
        double current  = freeFlow / m;

        updates.push_back({edgeId, current, freeFlow, m, severityLabel(m)});
    }
    return updates;
}

// ─── EMA smoothing ─────────────────────────────────────────────────────────
double TrafficFetcher::smoothMultiplier(const std::string& edgeId, double raw) {
    auto it = prevMultipliers_.find(edgeId);
    if (it == prevMultipliers_.end()) {
        prevMultipliers_[edgeId] = raw;
        return raw;
    }
    double smoothed = alpha_ * raw + (1.0 - alpha_) * it->second;
    it->second = smoothed;
    return smoothed;
}

// ─── Severity label ────────────────────────────────────────────────────────
std::string TrafficFetcher::severityLabel(double multiplier) {
    if (multiplier < 1.2) return "green";
    if (multiplier < 2.0) return "yellow";
    return "red";
}

// ─── Static edge list (canonical "min-max" ids) ─────────────────────────────
const std::vector<std::string> TrafficFetcher::MUMBAI_EDGE_IDS = {
    "0-2",  "0-3",  "0-10",
    "1-2",  "1-3",  "1-15", "1-16",
    "2-4",  "2-5",  "2-16", "2-18",
    "3-4",  "3-5",  "3-10", "3-17",
    "4-6",  "4-7",  "4-12",
    "5-6",  "5-7",  "5-13",
    "6-8",
    "7-9",  "7-11",
    "8-9",  "8-14",
    "9-14",
    "10-15",
    "11-12","11-19",
    "12-15",
    "14-15",
    "15-19",
    "16-17",
    "17-18",
    "18-19"
};