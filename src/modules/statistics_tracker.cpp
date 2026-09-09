#include "statistics_tracker.h"
#include "../plugin.h"
#include <cmath>

StatisticsTracker::StatisticsTracker() = default;
StatisticsTracker::~StatisticsTracker() = default;

void StatisticsTracker::ComputeMetrics(PlayerProfile& player)
{
	player.kdRatio = player.GetKD();
	player.headshotPercentage = player.GetHSPercent();
	player.accuracy = player.GetAccuracy();
	player.adr = player.GetADR();
}

void StatisticsTracker::UpdateStats(PlayerProfile& player, const GameEvent& event)
{
	(void)event;
	ComputeMetrics(player);
}

float StatisticsTracker::AnalyzeSession(PlayerProfile& player)
{
	ComputeMetrics(player);
	float suspicionDelta = 0.0f;

	// Require enough sample size; both KD and HS absurdly high.
	const int minKills = 12;
	if (player.kills >= minKills &&
		player.kdRatio >= 7.0f &&
		player.headshotPercentage > 85.0f)
	{
		suspicionDelta += 12.0f;
		AC_Log("high stats KD=%.1f HS=%.0f%% kills=%d steam=%llu",
			player.kdRatio, player.headshotPercentage, player.kills,
			(unsigned long long)player.steamId);
	}

	return suspicionDelta;
}

HistoricalStats StatisticsTracker::GetHistoricalStats(uint64_t steamId)
{
	auto it = historical_stats_cache_.find(steamId);
	if (it != historical_stats_cache_.end())
		return it->second;
	return HistoricalStats();
}

float StatisticsTracker::CompareWithHistory(PlayerProfile& player)
{
	ComputeMetrics(player);
	if (player.kills < 10 || player.roundsPlayed < 5)
		return 0.0f;

	HistoricalStats history = GetHistoricalStats(player.steamId);
	float spikeSuspicion = 0.0f;

	// Only when we have real history (std_dev set from DB). Defaults are weak.
	if (history.std_dev_kd > 0.1f &&
		player.kdRatio > history.mean_kd + 3.5f * history.std_dev_kd)
	{
		spikeSuspicion += 8.0f;
	}

	if (history.std_dev_hs > 1.0f &&
		player.headshotPercentage > history.mean_hs + 3.5f * history.std_dev_hs)
	{
		spikeSuspicion += 5.0f;
	}

	return spikeSuspicion;
}
