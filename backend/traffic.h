// traffic.h
#pragma once

#include <string>
#include <vector>
#include <map>
#include <mutex>
#include <cstdint>

struct TrafficUpdate {
    std::string edgeId;
    double currentSpeed;
    double freeFlowSpeed;
    double multiplier;
    std::string severity;
};

struct TrafficSnapshot {
    std::vector<TrafficUpdate> updates;
    int64_t timestampMs;
    bool isLive;
    bool significantChange;
};

class TrafficFetcher {
public:
    explicit TrafficFetcher(const std::string& apiKey = "");

    TrafficSnapshot fetch();

private:
    std::vector<TrafficUpdate> fetchTomTom();
    std::vector<TrafficUpdate> simulateTraffic();
    double smoothMultiplier(const std::string& edgeId, double raw);
    static std::string severityLabel(double multiplier);

    std::string apiKey_;
    bool simulationMode_;

    static const std::vector<std::string> MUMBAI_EDGE_IDS;

    std::mutex smoothMutex_;
    std::map<std::string, double> prevMultipliers_;
    double alpha_ = 0.3;
    static constexpr double CHANGE_THRESHOLD = 0.15;
};