#pragma once

#include "../player_profile.h"
#include <unordered_map>
#include <cstdint>

class StatisticsTracker {
public:
    StatisticsTracker();
    ~StatisticsTracker();

    void UpdateStats(PlayerProfile& player, const GameEvent& event);
    float AnalyzeSession(PlayerProfile& player);
    float CompareWithHistory(PlayerProfile& player);

private:
    void ComputeMetrics(PlayerProfile& player);
    HistoricalStats GetHistoricalStats(uint64_t steamId);
    
    std::unordered_map<uint64_t, HistoricalStats> historical_stats_cache_;
};
