#pragma once

#include "../player_profile.h"
#include <unordered_map>
#include <string>
#include <chrono>
#include <unordered_set>

enum class ScorerAction {
	NONE,
	MONITOR,
	WARN,
	REPORT,
	ADMIN_WARN, // 50+: last HTML warning to online admins
	BAN         // after ADMIN_WARN, if score keeps rising
};

struct PlayerSuspicion {
	float score = 0.0f;
	std::chrono::steady_clock::time_point last_update;
};

class SuspicionScorer {
public:
	SuspicionScorer() = default;
	~SuspicionScorer() = default;

	void SetThresholds(float monitor, float warn, float report, float ban, float decayPerSec);

	void AddScore(uint64_t steamId, const std::string& module, float score, const std::string& reason);
	float GetScore(uint64_t steamId);
	ScorerAction EvaluatePlayer(PlayerProfile& player);
	void DecayScores(float deltaTime);

	bool IsAlreadyBanned(uint64_t steamId) const;
	void MarkBanned(uint64_t steamId);

private:
	std::unordered_map<uint64_t, PlayerSuspicion> player_scores_;
	std::unordered_set<uint64_t> reported_;
	std::unordered_set<uint64_t> admin_warned_;
	std::unordered_set<uint64_t> continued_after_warn_;
	std::unordered_set<uint64_t> banned_;

	float monitor_threshold_ = 15.0f;
	float warn_threshold_ = 25.0f;
	float report_threshold_ = 35.0f;
	float ban_threshold_ = 50.0f;
	float decay_rate_per_second_ = 0.02f;
};
