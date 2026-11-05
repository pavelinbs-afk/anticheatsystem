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
	ADMIN_WARN,
	BAN
};

enum class AcChannel : uint8_t {
	Aim = 0,
	Rage = 1,
	Vis = 2,
	Other = 3,
	Count = 4
};

// Compact evidence sample — one detection, not a lifetime sum.
struct EvidenceEvent {
	float t = 0.0f;
	float weight = 0.0f;
	AcChannel channel = AcChannel::Other;
	char tag[20]{};
};

static constexpr int kEvidenceRing = 12;

struct PlayerWatch {
	EvidenceEvent ring[kEvidenceRing]{};
	uint8_t count = 0;
	uint8_t head = 0;
	uint8_t watchLevel = 0; // 0 idle, 1 watching, 2 focused
	uint8_t channelsHot = 0;
	float confidence = 0.0f;
	float channelC[(int)AcChannel::Count]{};
	float lastTagTime = -999.0f;
	char lastTag[20]{};
};

class SuspicionScorer {
public:
	SuspicionScorer() = default;

	void SetThresholds(float monitor, float warn, float report, float adminWarn, float ban, float decayPerSec,
		float fastBan = 82.0f, float fastBanWindowSec = 60.0f);

	void AddScore(uint64_t steamId, const std::string& module, float score, const std::string& reason);
	float GetScore(uint64_t steamId);
	float GetDecayPerSecond() const { return decay_rate_per_second_; }
	std::string GetBanReasonTags(uint64_t steamId) const;
	ScorerAction EvaluatePlayer(PlayerProfile& player);
	void DecayScores(float deltaTime);

	bool IsAlreadyBanned(uint64_t steamId) const;
	void MarkBanned(uint64_t steamId);
	void ClearBanned(uint64_t steamId);
	void SetAdminCheckHold(uint64_t steamId, bool hold);
	void RetractIssuedBan(uint64_t steamId);

private:
	std::unordered_map<uint64_t, PlayerWatch> watchers_;
	std::unordered_set<uint64_t> reported_;
	std::unordered_set<uint64_t> admin_warned_;
	std::unordered_map<uint64_t, std::chrono::steady_clock::time_point> admin_warn_at_;
	std::unordered_set<uint64_t> continued_after_warn_;
	std::unordered_set<uint64_t> banned_;
	std::unordered_set<uint64_t> admin_check_hold_;

	static constexpr float kAdminReactSec = 30.0f;

	float monitor_threshold_ = 22.0f;
	float warn_threshold_ = 34.0f;
	float report_threshold_ = 45.0f;
	float admin_warn_threshold_ = 56.0f;
	float ban_threshold_ = 70.0f;
	float fast_ban_threshold_ = 82.0f;
	float fast_ban_window_sec_ = 60.0f;
	float decay_rate_per_second_ = 0.05f;

	static float NowSec();
	static AcChannel ChannelFromTag(const std::string& tag);
	void PushEvidence(PlayerWatch& w, AcChannel ch, const char* tag, float weight, float now);
	void Recompute(PlayerWatch& w, float now) const;
	void UpdateWatchLevel(uint64_t steamId, PlayerWatch& w, float now);
	bool ShouldFastBan(uint64_t steamId, const PlayerWatch& w) const;
	void MarkAdminWarned(uint64_t steamId);
	bool BanAllowed(const PlayerWatch& w) const;
	float SecondsSinceAdminWarn(uint64_t steamId) const;
};
