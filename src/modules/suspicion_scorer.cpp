#include "suspicion_scorer.h"
#include "../plugin.h"

void SuspicionScorer::SetThresholds(float monitor, float warn, float report, float ban, float decayPerSec)
{
	monitor_threshold_ = monitor;
	warn_threshold_ = warn;
	report_threshold_ = report;
	ban_threshold_ = ban;
	decay_rate_per_second_ = decayPerSec;
}

void SuspicionScorer::AddScore(uint64_t steamId, const std::string& module, float score, const std::string& reason)
{
	if (score <= 0.0f || banned_.count(steamId))
		return;

	auto now = std::chrono::steady_clock::now();
	player_scores_[steamId].score += score;
	player_scores_[steamId].last_update = now;

	// Server-only log — never shown to the suspicious player.
	AC_Log("score +%.1f [%s] steam=%llu reason=%s total=%.1f",
		score, module.c_str(), (unsigned long long)steamId, reason.c_str(),
		player_scores_[steamId].score);
}

float SuspicionScorer::GetScore(uint64_t steamId)
{
	auto it = player_scores_.find(steamId);
	return it != player_scores_.end() ? it->second.score : 0.0f;
}

ScorerAction SuspicionScorer::EvaluatePlayer(PlayerProfile& player)
{
	float score = GetScore(player.steamId);

	if (score >= ban_threshold_)
	{
		if (!banned_.count(player.steamId))
		{
			banned_.insert(player.steamId);
			AC_Log("[BAN] steam=%llu name=%s score=%.1f",
				(unsigned long long)player.steamId, player.name.c_str(), score);
		}
		return ScorerAction::DEFERRED_BAN;
	}

	if (score >= report_threshold_)
	{
		if (!reported_.count(player.steamId))
		{
			reported_.insert(player.steamId);
			AC_Log("[REPORT] steam=%llu name=%s score=%.1f",
				(unsigned long long)player.steamId, player.name.c_str(), score);
		}
		return ScorerAction::REPORT;
	}

	if (score >= warn_threshold_)
		return ScorerAction::WARN;
	if (score >= monitor_threshold_)
		return ScorerAction::MONITOR;
	return ScorerAction::NONE;
}

void SuspicionScorer::DecayScores(float deltaTime)
{
	for (auto& pair : player_scores_)
	{
		if (banned_.count(pair.first))
			continue;
		if (pair.second.score > 0.0f)
		{
			pair.second.score -= deltaTime * decay_rate_per_second_;
			if (pair.second.score < 0.0f)
				pair.second.score = 0.0f;
		}
	}
}
