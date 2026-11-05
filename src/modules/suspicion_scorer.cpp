#include "suspicion_scorer.h"
#include "../plugin.h"

#include <cstring>
#include <cstdio>
#include <cmath>
#include <vector>
#include <algorithm>
#include <unordered_map>

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
	if (ReasonHas(reason, "untrusted") || ReasonHas(reason, "spin"))
		return "rage (spin)";
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

static float HalfLifeSec(AcChannel ch)
{
	switch (ch)
	{
	case AcChannel::Rage: return 48.0f;
	case AcChannel::Aim:  return 90.0f;
	case AcChannel::Vis:  return 100.0f;
	default:              return 70.0f;
	}
}

float SuspicionScorer::NowSec()
{
	static const auto t0 = std::chrono::steady_clock::now();
	return std::chrono::duration<float>(std::chrono::steady_clock::now() - t0).count();
}

AcChannel SuspicionScorer::ChannelFromTag(const std::string& tag)
{
	if (tag.size() >= 4 && tag.compare(0, 4, "rage") == 0)
		return AcChannel::Rage;
	if (tag.size() >= 2 && tag.compare(0, 2, "wh") == 0)
		return AcChannel::Vis;
	if (tag.size() >= 3 && tag.compare(0, 3, "aim") == 0)
		return AcChannel::Aim;
	return AcChannel::Other;
}

void SuspicionScorer::SetThresholds(float monitor, float warn, float report, float adminWarn, float ban, float decayPerSec,
	float fastBan, float fastBanWindowSec)
{
	// Old linear scale (ban 55 / monitor 15) would auto-ban on the 0–100 confidence meter.
	if (ban <= 56.0f && monitor <= 16.0f && adminWarn <= 51.0f)
	{
		AC_Log("scorer: migrating old linear thresholds to confidence watch (was ban=%.0f)", ban);
		monitor = 22.0f;
		warn = 34.0f;
		report = 45.0f;
		adminWarn = 56.0f;
		ban = 70.0f;
		if (fastBan <= 61.0f)
			fastBan = 82.0f;
	}

	monitor_threshold_ = monitor;
	warn_threshold_ = warn;
	report_threshold_ = report;
	admin_warn_threshold_ = adminWarn;
	ban_threshold_ = ban;
	decay_rate_per_second_ = decayPerSec;
	fast_ban_threshold_ = fastBan > 0.0f ? fastBan : 82.0f;
	fast_ban_window_sec_ = fastBanWindowSec > 0.0f ? fastBanWindowSec : 60.0f;
}

void SuspicionScorer::PushEvidence(PlayerWatch& w, AcChannel ch, const char* tag, float weight, float now)
{
	EvidenceEvent& e = w.ring[w.head];
	e.t = now;
	e.weight = weight;
	e.channel = ch;
	e.tag[0] = '\0';
	if (tag)
	{
		std::snprintf(e.tag, sizeof(e.tag), "%s", tag);
	}
	w.head = static_cast<uint8_t>((w.head + 1) % kEvidenceRing);
	if (w.count < kEvidenceRing)
		w.count++;
}

void SuspicionScorer::Recompute(PlayerWatch& w, float now) const
{
	float sum[(int)AcChannel::Count]{};

	for (int n = 0; n < w.count; ++n)
	{
		const int idx = (w.head - 1 - n + kEvidenceRing * 4) % kEvidenceRing;
		const EvidenceEvent& e = w.ring[idx];
		const float age = now - e.t;
		if (age < 0.0f || age > 240.0f)
			continue;

		const int ci = (int)e.channel;
		int dimN = 0;
		for (int m = 0; m < n; ++m)
		{
			const int j = (w.head - 1 - m + kEvidenceRing * 4) % kEvidenceRing;
			const EvidenceEvent& prev = w.ring[j];
			if (prev.channel != e.channel)
				continue;
			if ((now - prev.t) > 6.0f)
				continue;
			if (std::strcmp(prev.tag, e.tag) == 0)
				++dimN;
		}

		float dim = 1.0f;
		if (dimN == 1) dim = 0.40f;
		else if (dimN >= 2) dim = 0.15f;

		const float decay = std::pow(0.5f, age / HalfLifeSec(e.channel));
		sum[ci] += e.weight * decay * dim;
	}

	float prodStayInnocent = 1.0f;
	uint8_t hot = 0;
	for (int i = 0; i < (int)AcChannel::Count; ++i)
	{
		const float c = 1.0f - std::exp(-sum[i] / 26.0f);
		w.channelC[i] = c;
		prodStayInnocent *= (1.0f - c);
		if (c >= 0.18f)
			++hot;
	}

	w.channelsHot = hot;
	w.confidence = (1.0f - prodStayInnocent) * 100.0f;
	if (w.confidence < 0.0f)
		w.confidence = 0.0f;
	if (w.confidence > 100.0f)
		w.confidence = 100.0f;
}

void SuspicionScorer::UpdateWatchLevel(uint64_t steamId, PlayerWatch& w, float now)
{
	(void)now;
	const uint8_t prev = w.watchLevel;
	uint8_t next = 0;
	if (w.confidence >= report_threshold_)
		next = 2;
	else if (w.confidence >= 20.0f)
		next = 1;

	// Hysteresis: don't drop watch on a tiny dip.
	if (prev == 2 && w.confidence >= (report_threshold_ - 8.0f))
		next = 2;
	else if (prev >= 1 && w.confidence >= 12.0f)
		next = next > 1 ? next : 1;

	if (next != prev)
	{
		w.watchLevel = next;
		if (next == 1 && prev == 0)
		{
			AC_Log("[WATCH] steam=%llu start conf=%.1f channels=%u (observing)",
				(unsigned long long)steamId, w.confidence, (unsigned)w.channelsHot);
		}
		else if (next == 2 && prev < 2)
		{
			AC_Log("[WATCH] steam=%llu focus conf=%.1f channels=%u (stacking independent evidence)",
				(unsigned long long)steamId, w.confidence, (unsigned)w.channelsHot);
		}
		else if (next == 0)
		{
			AC_Log("[WATCH] steam=%llu clear conf=%.1f",
				(unsigned long long)steamId, w.confidence);
		}
	}
}

bool SuspicionScorer::BanAllowed(const PlayerWatch& w) const
{
	// Need corroboration (2 channels) or a heavy rage channel — not a single aim spray.
	if (w.channelsHot >= 2)
		return true;
	if (w.channelC[(int)AcChannel::Rage] >= 0.55f)
		return true;
	return false;
}

void SuspicionScorer::MarkAdminWarned(uint64_t steamId)
{
	admin_warned_.insert(steamId);
	admin_warn_at_[steamId] = std::chrono::steady_clock::now();
}

bool SuspicionScorer::ShouldFastBan(uint64_t steamId, const PlayerWatch& w) const
{
	if (steamId == 0 || !admin_warned_.count(steamId))
		return false;
	if (admin_check_hold_.count(steamId))
		return false;
	const float elapsed = SecondsSinceAdminWarn(steamId);
	if (elapsed < kAdminReactSec)
		return false;
	if (w.confidence + 0.0001f < fast_ban_threshold_)
		return false;
	if (!BanAllowed(w))
		return false;
	auto it = admin_warn_at_.find(steamId);
	if (it == admin_warn_at_.end())
		return true;
	return elapsed <= fast_ban_window_sec_;
}

float SuspicionScorer::SecondsSinceAdminWarn(uint64_t steamId) const
{
	auto it = admin_warn_at_.find(steamId);
	if (it == admin_warn_at_.end())
		return 9999.0f;
	return std::chrono::duration<float>(std::chrono::steady_clock::now() - it->second).count();
}

void SuspicionScorer::SetAdminCheckHold(uint64_t steamId, bool hold)
{
	if (!steamId)
		return;
	if (hold)
		admin_check_hold_.insert(steamId);
	else
		admin_check_hold_.erase(steamId);
}

void SuspicionScorer::RetractIssuedBan(uint64_t steamId)
{
	if (!steamId)
		return;
	banned_.erase(steamId);
}

void SuspicionScorer::AddScore(uint64_t steamId, const std::string& module, float score, const std::string& reason)
{
	if (score <= 0.0f || steamId == 0)
		return;

	const std::string tag = TagFromDetection(module, reason);
	const AcChannel ch = ChannelFromTag(tag);
	const float now = NowSec();
	PlayerWatch& w = watchers_[steamId];

	float weight = score;
	if (w.lastTag[0] && tag == w.lastTag && (now - w.lastTagTime) < 3.5f)
		weight *= 0.25f; // same detector spraying

	// Watcher: already observing this player — new independent channel stacks harder.
	const int ci = (int)ch;
	const bool newChannel = w.channelC[ci] < 0.12f;
	if (w.watchLevel >= 1 && newChannel)
		weight *= (w.watchLevel >= 2) ? 1.32f : 1.18f;

	PushEvidence(w, ch, tag.c_str(), weight, now);
	std::snprintf(w.lastTag, sizeof(w.lastTag), "%s", tag.c_str());
	w.lastTagTime = now;

	Recompute(w, now);
	UpdateWatchLevel(steamId, w, now);

	if (admin_warned_.count(steamId) && !banned_.count(steamId))
		continued_after_warn_.insert(steamId);

	AC_LogDebug("watch +%.1f→w%.1f [%s] steam=%llu tag=%s ch=%d conf=%.1f hot=%u lvl=%u",
		score, weight, module.c_str(), (unsigned long long)steamId, tag.c_str(),
		(int)ch, w.confidence, (unsigned)w.channelsHot, (unsigned)w.watchLevel);
}

std::string SuspicionScorer::GetBanReasonTags(uint64_t steamId) const
{
	auto it = watchers_.find(steamId);
	if (it == watchers_.end() || it->second.count == 0)
		return "detect";

	const PlayerWatch& w = it->second;
	const float now = NowSec();
	std::unordered_map<std::string, float> acc;
	for (int n = 0; n < w.count; ++n)
	{
		const int idx = (w.head - 1 - n + kEvidenceRing * 4) % kEvidenceRing;
		const EvidenceEvent& e = w.ring[idx];
		if (!e.tag[0])
			continue;
		const float age = now - e.t;
		if (age > 180.0f)
			continue;
		const float decay = std::pow(0.5f, age / HalfLifeSec(e.channel));
		acc[e.tag] += e.weight * decay;
	}

	std::vector<std::pair<float, std::string>> ranked;
	ranked.reserve(acc.size());
	for (const auto& kv : acc)
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
	auto it = watchers_.find(steamId);
	if (it == watchers_.end())
		return 0.0f;
	Recompute(it->second, NowSec());
	return it->second.confidence;
}

ScorerAction SuspicionScorer::EvaluatePlayer(PlayerProfile& player)
{
	if (banned_.count(player.steamId))
		return ScorerAction::NONE;

	auto it = watchers_.find(player.steamId);
	if (it == watchers_.end())
		return ScorerAction::NONE;

	PlayerWatch& w = it->second;
	Recompute(w, NowSec());
	const float score = w.confidence;

	if (admin_check_hold_.count(player.steamId))
	{
		if (score >= report_threshold_ && !reported_.count(player.steamId))
		{
			reported_.insert(player.steamId);
			return ScorerAction::REPORT;
		}
		return ScorerAction::NONE;
	}

	const float sinceWarn = admin_warned_.count(player.steamId) ? SecondsSinceAdminWarn(player.steamId) : 9999.0f;
	const bool inAdminReact = admin_warned_.count(player.steamId) && sinceWarn < kAdminReactSec;

	if (ShouldFastBan(player.steamId, w))
	{
		if (!banned_.count(player.steamId))
		{
			banned_.insert(player.steamId);
			float elapsed = 0.0f;
			auto wit = admin_warn_at_.find(player.steamId);
			if (wit != admin_warn_at_.end())
				elapsed = std::chrono::duration<float>(std::chrono::steady_clock::now() - wit->second).count();
			AC_Log("[BAN] steam=%llu name=%s conf=%.1f hot=%u (fast watch: >=%.0f within %.0fs, elapsed=%.1fs)",
				(unsigned long long)player.steamId, player.name.c_str(), score,
				(unsigned)w.channelsHot, fast_ban_threshold_, fast_ban_window_sec_, elapsed);
		}
		return ScorerAction::BAN;
	}

	if (score >= ban_threshold_)
	{
		if (!admin_warned_.count(player.steamId))
		{
			MarkAdminWarned(player.steamId);
			AC_Log("[ADMIN_WARN] steam=%llu name=%s conf=%.1f hot=%u (watch jumped to ban line; %.0fs for cheat-check)",
				(unsigned long long)player.steamId, player.name.c_str(), score, (unsigned)w.channelsHot,
				kAdminReactSec);
			return ScorerAction::ADMIN_WARN;
		}

		if (inAdminReact)
			return ScorerAction::NONE;

		if (continued_after_warn_.count(player.steamId) && BanAllowed(w))
		{
			if (!banned_.count(player.steamId))
			{
				banned_.insert(player.steamId);
				AC_Log("[BAN] steam=%llu name=%s conf=%.1f hot=%u (watch confirmed after admin warn)",
					(unsigned long long)player.steamId, player.name.c_str(), score, (unsigned)w.channelsHot);
			}
			return ScorerAction::BAN;
		}

		return ScorerAction::NONE;
	}

	if (score >= admin_warn_threshold_)
	{
		if (!admin_warned_.count(player.steamId))
		{
			MarkAdminWarned(player.steamId);
			AC_Log("[ADMIN_WARN] steam=%llu name=%s conf=%.1f hot=%u (%.0fs to call cheat-check; then ban if conf>=%.0f)",
				(unsigned long long)player.steamId, player.name.c_str(), score, (unsigned)w.channelsHot,
				kAdminReactSec, fast_ban_threshold_);
			return ScorerAction::ADMIN_WARN;
		}
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
			AC_Log("[REPORT] steam=%llu name=%s conf=%.1f hot=%u",
				(unsigned long long)player.steamId, player.name.c_str(), score, (unsigned)w.channelsHot);
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
	(void)deltaTime;
	const float now = NowSec();
	for (auto& pair : watchers_)
	{
		if (banned_.count(pair.first))
			continue;
		Recompute(pair.second, now);
		UpdateWatchLevel(pair.first, pair.second, now);
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
	admin_check_hold_.erase(steamId);
	admin_warned_.erase(steamId);
	admin_warn_at_.erase(steamId);
	continued_after_warn_.erase(steamId);
	reported_.erase(steamId);
	watchers_.erase(steamId);
}
