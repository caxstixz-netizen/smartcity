// traffic.h
#pragma once

#include <string> // for edge IDs, severity labels, API key.
#include <vector> // for storing traffic updates.
#include <map> //  for storing previous multipliers and static edge list.
#include <mutex> // for thread safety in smoothing and graph updates.
#include <cstdint> // for int64_t timestamp.

//Represents a single traffic update for one road edge.
struct TrafficUpdate {
    std::string edgeId; //"0-2" (canonical form: smaller node ID first).
    double currentSpeed; // current speed on the edge (km/h).
    double freeFlowSpeed; // typical speed on the edge without traffic (km/h).
    double multiplier; // freeFlowSpeed / currentSpeed (e.g., 1.5 means 50% slower than free flow).
    std::string severity; // e.g., "green", "yellow", "red".
};

//A complete snapshot of traffic conditions at a given moment.
struct TrafficSnapshot {
    std::vector<TrafficUpdate> updates; // list of per‑edge traffic data.
    int64_t timestampMs; // Unix timestamp in milliseconds when the snapshot was taken.
    bool isLive; // true if fetched from live API, false if simulated.

    //true if any smoothed multiplier changed by more than 0.15 compared to the raw value 
    //(used to decide whether to force a UI refresh).
    bool significantChange;
};

class TrafficFetcher {
public:
    explicit TrafficFetcher(const std::string& apiKey = "");

    //takes an optional TomTom API key. 
     //If the key is empty, the fetcher runs in simulation mode.
    TrafficSnapshot fetch();
    //Fetches the latest traffic data. 
    //If apiKey was provided, tries to fetch live data from TomTom.
     

private:
    std::vector<TrafficUpdate> fetchTomTom(); //for api not used
    
    std::vector<TrafficUpdate> simulateTraffic(); 
     /*generates pseudo‑random traffic data using a random walk. 
     The simulation maintains a multiplier for each edge that randomly 
     fluctuates over time, but stays within reasonable bounds (e.g., 0.8 to 2.5). 
     Each call to simulateTraffic nudges the multipliers slightly, 
     creating a dynamic traffic pattern that changes smoothly over time.*/

     
     double smoothMultiplier(const std::string& edgeId, double raw);
     /*The EMA smoothing function takes the raw multiplier for an edge and combines 
     it with the previous smoothed value using a weighted average. 
     The alpha parameter (0.3) controls how much weight is given to the 
     new raw value versus the historical smoothed value. 
     This prevents sudden spikes or drops in traffic from causing abrupt 
     changes in the graph weights, making the user experience smoother.*/
    
    static std::string severityLabel(double multiplier);//static helper to map a multiplier to a colour string.

    std::string apiKey_;//stored API key (or empty).
    bool simulationMode_;//truse if api

    static const std::vector<std::string> MUMBAI_EDGE_IDS;
    //static list of all edges in canonical "from-to" form, used for iteration.

    std::mutex smoothMutex_;
    /*because fetch() can be called from multiple threads (the background traffic thread, 
    plus the manual refresh endpoint). Without this mutex, concurrent access could corrupt the map.*/
    
    std::map<std::string, double> prevMultipliers_; //stores the last smoothed multiplier for each edge.
    double alpha_ = 0.3;
    static constexpr double CHANGE_THRESHOLD = 0.15;
    // if |smoothed - raw| > 0.15, the snapshot will mark significantChange = true.
};