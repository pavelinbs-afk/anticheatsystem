#pragma once

#include "../player_profile.h"
#include <unordered_map>
#include <string>
#include <chrono>
#include <unordered_set>
#include <vector>
#include <algorithm>

enum class ScorerAction {
	NONE,
	MONITOR,
	WARN,
	REPORT,
	ADMIN_WARN, // last HTML warning to online admins (before ban threshold)
	BAN         // ban_threshold after continued detections, or fast 50→60 in <1 min
};

struct PlayerSuspicion {
	float score = 0.0f;
	std::chrono::steady_clock::time_point last_update;
};

class SuspicionScorer {
public:
	SuspicionScorer() = default;
	~SuspicionScorer() = default;

	void SetThresholds(float monitor, float warn, float report, float adminWarn, float ban, float decayPerSec,
		float fastBan = 60.0f, float fastBanWindowSec = 60.0f);

	void AddScore(uint64_t steamId, const std::string& module, float score, const std::string& reason);
	float GetScore(uint64_t steamId);
	float GetDecayPerSecond() const { return decay_rate_per_second_; }
	/// Short cheat-type labels for Discord/ban, e.g. "rage (aimlock) / aim (shot) / fps (hitch)".
	std::string GetBanReasonTags(uint64_t steamId) const;
	ScorerAction EvaluatePlayer(PlayerProfile& player);
	void DecayScores(float deltaTime);

	bool IsAlreadyBanned(uint64_t steamId) const;
	void MarkBanned(uint64_t steamId);
	void ClearBanned(uint64_t steamId);

private:
	std::unordered_map<uint64_t, PlayerSuspicion> player_scores_;
	std::unordered_map<uint64_t, std::unordered_map<std::string, float>> module_tags_;
	std::unordered_set<uint64_t> reported_;
	std::unordered_set<uint64_t> admin_warned_;
	std::unordered_map<uint64_t, std::chrono::steady_clock::time_point> admin_warn_at_;
	std::unordered_set<uint64_t> continued_after_warn_;
	std::unordered_set<uint64_t> banned_;

	float monitor_threshold_ = 15.0f;
	float warn_threshold_ = 25.0f;
	float report_threshold_ = 35.0f;
	float admin_warn_threshold_ = 50.0f;
	float ban_threshold_ = 55.0f;
	float fast_ban_threshold_ = 60.0f;
	float fast_ban_window_sec_ = 60.0f;
	float decay_rate_per_second_ = 0.02f;

	void MarkAdminWarned(uint64_t steamId);
	bool ShouldFastBan(uint64_t steamId, float score) const;
};
