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

	// After the final admin warning at 50+, any further score gain arms the auto-ban.
	if (admin_warned_.count(steamId))
		continued_after_warn_.insert(steamId);

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
		if (!admin_warned_.count(player.steamId))
		{
			admin_warned_.insert(player.steamId);
			AC_Log("[ADMIN_WARN] steam=%llu name=%s score=%.1f (last HTML warning to admins)",
				(unsigned long long)player.steamId, player.name.c_str(), score);
			return ScorerAction::ADMIN_WARN;
		}

		if (continued_after_warn_.count(player.steamId))
		{
			if (!banned_.count(player.steamId))
			{
				banned_.insert(player.steamId);
				AC_Log("[BAN] steam=%llu name=%s score=%.1f (continued after admin warn)",
					(unsigned long long)player.steamId, player.name.c_str(), score);
			}
			return ScorerAction::BAN;
		}

		// Warned, waiting for further detections.
		return ScorerAction::NONE;
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
