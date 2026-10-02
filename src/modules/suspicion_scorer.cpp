#include "suspicion_scorer.h"
#include "../plugin.h"

void SuspicionScorer::SetThresholds(float monitor, float warn, float report, float adminWarn, float ban, float decayPerSec)
{
	monitor_threshold_ = monitor;
	warn_threshold_ = warn;
	report_threshold_ = report;
	admin_warn_threshold_ = adminWarn;
	ban_threshold_ = ban;
	decay_rate_per_second_ = decayPerSec;
}

void SuspicionScorer::AddScore(uint64_t steamId, const std::string& module, float score, const std::string& reason)
{
	if (score <= 0.0f || steamId == 0)
		return;

	// Still accumulate/log even if already banned — detection visibility matters.
	// ApplyBan is gated separately in CheckAndApplyActions.
	auto now = std::chrono::steady_clock::now();
	player_scores_[steamId].score += score;
	player_scores_[steamId].last_update = now;

	if (admin_warned_.count(steamId) && !banned_.count(steamId))
		continued_after_warn_.insert(steamId);

	AC_LogDebug("score +%.1f [%s] steam=%llu reason=%s total=%.1f",
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
	// Already banned this steamid this process lifetime — never re-trigger ban actions.
	if (banned_.count(player.steamId))
		return ScorerAction::NONE;

	float score = GetScore(player.steamId);

	// Hard ban line: only at ban_threshold (default 55), after admin warn + more detections.
	if (score >= ban_threshold_)
	{
		if (!admin_warned_.count(player.steamId))
		{
			admin_warned_.insert(player.steamId);
			AC_Log("[ADMIN_WARN] steam=%llu name=%s score=%.1f (jumped to ban line; HTML warn first)",
				(unsigned long long)player.steamId, player.name.c_str(), score);
			return ScorerAction::ADMIN_WARN;
		}

		if (continued_after_warn_.count(player.steamId))
		{
			if (!banned_.count(player.steamId))
			{
				banned_.insert(player.steamId);
				AC_Log("[BAN] steam=%llu name=%s score=%.1f (reached ban threshold after admin warn)",
					(unsigned long long)player.steamId, player.name.c_str(), score);
			}
			return ScorerAction::BAN;
		}

		return ScorerAction::NONE;
	}

	// Soft admin warn (default 50): notify staff, but do NOT ban until ban_threshold.
	if (score >= admin_warn_threshold_)
	{
		if (!admin_warned_.count(player.steamId))
		{
			admin_warned_.insert(player.steamId);
			AC_Log("[ADMIN_WARN] steam=%llu name=%s score=%.1f (ban deferred until %.0f)",
				(unsigned long long)player.steamId, player.name.c_str(), score, ban_threshold_);
			return ScorerAction::ADMIN_WARN;
		}
		// Already warned, still below ban line — keep waiting (report path already done).
		if (!reported_.count(player.steamId))
		{
			reported_.insert(player.steamId);
			return ScorerAction::REPORT;
		}
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

bool SuspicionScorer::IsAlreadyBanned(uint64_t steamId) const
{
	return steamId != 0 && banned_.count(steamId) > 0;
}

void SuspicionScorer::MarkBanned(uint64_t steamId)
{
	if (steamId)
		banned_.insert(steamId);
}

void SuspicionScorer::ClearBanned(uint64_t steamId)
{
	if (!steamId)
		return;
	banned_.erase(steamId);
	// Allow a fresh detection cycle after an admin unban.
	admin_warned_.erase(steamId);
	continued_after_warn_.erase(steamId);
	reported_.erase(steamId);
	auto it = player_scores_.find(steamId);
	if (it != player_scores_.end())
		it->second.score = 0.0f;
}
