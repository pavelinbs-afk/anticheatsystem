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

	// CRITICAL: must only flag once per session — previously added score every GameFrame.
	if (player.statsFlaggedThisSession)
		return 0.0f;

	const int minKills = 18;
	if (player.kills >= minKills &&
		player.kdRatio >= 8.0f &&
		player.headshotPercentage > 90.0f &&
		player.roundsPlayed >= 8)
	{
		player.statsFlaggedThisSession = true;
		AC_Log("high stats KD=%.1f HS=%.0f%% kills=%d rounds=%d steam=%llu",
			player.kdRatio, player.headshotPercentage, player.kills, player.roundsPlayed,
			(unsigned long long)player.steamId);
		return 12.0f;
	}

	return 0.0f;
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
	if (player.kills < 15 || player.roundsPlayed < 8)
		return 0.0f;

	HistoricalStats history = GetHistoricalStats(player.steamId);
	float spikeSuspicion = 0.0f;

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
