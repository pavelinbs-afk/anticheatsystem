#include "suspicion_scorer.h"
#include "../plugin.h"

#include <cstring>

static bool ReasonHas(const std::string& reason, const char* needle)
{
	if (!needle || !needle[0])
		return false;
	const size_t nlen = std::strlen(needle);
	if (reason.size() < nlen)
		return false;
	for (size_t i = 0; i + nlen <= reason.size(); ++i)
	{
		bool ok = true;
		for (size_t j = 0; j < nlen; ++j)
		{
			unsigned char a = static_cast<unsigned char>(reason[i + j]);
			unsigned char b = static_cast<unsigned char>(needle[j]);
			if (a >= 'A' && a <= 'Z') a = static_cast<unsigned char>(a - 'A' + 'a');
			if (b >= 'A' && b <= 'Z') b = static_cast<unsigned char>(b - 'A' + 'a');
			if (a != b) { ok = false; break; }
		}
		if (ok)
			return true;
	}
	return false;
}

// Category + detail, e.g. "rage (aimlock)", "fps (hitch)".
static std::string TagFromDetection(const std::string& module, const std::string& reason)
{
	if (module == "FpsDropDetector" || ReasonHas(reason, "fps"))
		return "fps (hitch)";

	if (ReasonHas(reason, "aimlock"))
		return "rage (aimlock)";
	if (ReasonHas(reason, "doubletap"))
		return "rage (doubletap)";
	if (ReasonHas(reason, "trigger"))
		return "rage (trigger)";
	if (ReasonHas(reason, "rage") && ReasonHas(reason, "snap"))
		return "rage (snap)";
	if (ReasonHas(reason, "rage"))
		return "rage";
	if (module == "CombatHeuristics")
		return "rage (combat)";

	if (ReasonHas(reason, "smoke"))
		return "wh (smoke)";
	if (ReasonHas(reason, "blind"))
		return "wh (blind)";
	if (ReasonHas(reason, "prefire"))
		return "wh (prefire)";
	if (ReasonHas(reason, "wall track") || ReasonHas(reason, "track"))
		return "wh (track)";
	if (ReasonHas(reason, "wallbang"))
		return "wh (wallbang)";
	if (module == "WallhackDetector" || ReasonHas(reason, "wallhack"))
		return "wh";

	if (module == "ShotTracker" || ReasonHas(reason, "shot"))
		return "aim (shot)";
	if (ReasonHas(reason, "combat") && ReasonHas(reason, "aim"))
		return "aim (combat)";
	if (module == "AimAnalyzer" || ReasonHas(reason, "aim"))
		return "aim";

	if (module == "MovementAnalyzer")
		return "move";
	if (ReasonHas(reason, "spike"))
		return "stats (spike)";
	if (module == "StatisticsTracker")
		return "stats";
	if (module == "IntegrityChecker")
		return "integrity";
	return "detect";
}

void SuspicionScorer::SetThresholds(float monitor, float warn, float report, float adminWarn, float ban, float decayPerSec,
	float fastBan, float fastBanWindowSec)
{
	monitor_threshold_ = monitor;
	warn_threshold_ = warn;
	report_threshold_ = report;
	admin_warn_threshold_ = adminWarn;
	ban_threshold_ = ban;
	decay_rate_per_second_ = decayPerSec;
	fast_ban_threshold_ = fastBan > 0.0f ? fastBan : 60.0f;
	fast_ban_window_sec_ = fastBanWindowSec > 0.0f ? fastBanWindowSec : 60.0f;
}

void SuspicionScorer::MarkAdminWarned(uint64_t steamId)
{
	admin_warned_.insert(steamId);
	admin_warn_at_[steamId] = std::chrono::steady_clock::now();
}

bool SuspicionScorer::ShouldFastBan(uint64_t steamId, float score) const
{
	if (steamId == 0 || !admin_warned_.count(steamId))
		return false;
	if (score + 0.0001f < fast_ban_threshold_)
		return false;
	auto it = admin_warn_at_.find(steamId);
	if (it == admin_warn_at_.end())
		return true;
	const float elapsed = std::chrono::duration<float>(std::chrono::steady_clock::now() - it->second).count();
	return elapsed <= fast_ban_window_sec_;
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

	const std::string tag = TagFromDetection(module, reason);
	if (!tag.empty())
		module_tags_[steamId][tag] += score;

	AC_LogDebug("score +%.1f [%s] steam=%llu reason=%s tag=%s total=%.1f",
		score, module.c_str(), (unsigned long long)steamId, reason.c_str(),
		tag.c_str(),
		player_scores_[steamId].score);
}

std::string SuspicionScorer::GetBanReasonTags(uint64_t steamId) const
{
	auto it = module_tags_.find(steamId);
	if (it == module_tags_.end() || it->second.empty())
		return "detect";

	std::vector<std::pair<float, std::string>> ranked;
	ranked.reserve(it->second.size());
	for (const auto& kv : it->second)
	{
		if (kv.second > 0.05f)
			ranked.push_back({ kv.second, kv.first });
	}
	if (ranked.empty())
		return "detect";

	std::sort(ranked.begin(), ranked.end(),
		[](const auto& a, const auto& b) { return a.first > b.first; });

	std::string out;
	const size_t n = ranked.size() < 3 ? ranked.size() : 3;
	for (size_t i = 0; i < n; ++i)
	{
		if (!out.empty())
			out += " / ";
		out += ranked[i].second;
	}
	return out.empty() ? "detect" : out;
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

	// After 50 (admin warn): if score hits 60 within 60s → ban (covers a single spike past 60).
	if (ShouldFastBan(player.steamId, score))
	{
		if (!banned_.count(player.steamId))
		{
			banned_.insert(player.steamId);
			float elapsed = 0.0f;
			auto it = admin_warn_at_.find(player.steamId);
			if (it != admin_warn_at_.end())
				elapsed = std::chrono::duration<float>(std::chrono::steady_clock::now() - it->second).count();
			AC_Log("[BAN] steam=%llu name=%s score=%.1f (fast: >=%.0f within %.0fs after admin warn, elapsed=%.1fs)",
				(unsigned long long)player.steamId, player.name.c_str(), score,
				fast_ban_threshold_, fast_ban_window_sec_, elapsed);
		}
		return ScorerAction::BAN;
	}

	// Hard ban line: ban_threshold (default 55), after admin warn + more detections.
	if (score >= ban_threshold_)
	{
		if (!admin_warned_.count(player.steamId))
		{
			MarkAdminWarned(player.steamId);
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

	// Soft admin warn (default 50): notify staff; fast ban if 60 within 1 min, else 55+ continued.
	if (score >= admin_warn_threshold_)
	{
		if (!admin_warned_.count(player.steamId))
		{
			MarkAdminWarned(player.steamId);
			AC_Log("[ADMIN_WARN] steam=%llu name=%s score=%.1f (ban if >=%.0f in %.0fs, else %.0f+ continued)",
				(unsigned long long)player.steamId, player.name.c_str(), score,
				fast_ban_threshold_, fast_ban_window_sec_, ban_threshold_);
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
			const float old = pair.second.score;
			pair.second.score -= deltaTime * decay_rate_per_second_;
			if (pair.second.score < 0.0f)
				pair.second.score = 0.0f;
			auto tags = module_tags_.find(pair.first);
			if (tags != module_tags_.end() && old > 0.0001f)
			{
				const float k = pair.second.score / old;
				for (auto& t : tags->second)
					t.second *= k;
			}
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
	admin_warn_at_.erase(steamId);
	continued_after_warn_.erase(steamId);
	reported_.erase(steamId);
	auto it = player_scores_.find(steamId);
	if (it != player_scores_.end())
		it->second.score = 0.0f;
	module_tags_.erase(steamId);
}
