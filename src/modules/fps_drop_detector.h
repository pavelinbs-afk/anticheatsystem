#pragma once

#include "../player_profile.h"

// Detects abnormal client FPS drops via INetChannelInfo::GetRemoteFramerate.
// Some overlays / cheats hitch the client (frameTime spikes) while still connected.
class FpsDropDetector {
public:
	FpsDropDetector() = default;

	void SetConfig(float maxFrameTimeMs, float spikeStdDevMs, int minSpikes);

	// Call once per game frame for an online player. Returns suspicion delta.
	float Analyze(PlayerProfile& player, float frameTimeSec, float frameTimeStdDevSec);

private:
	float m_maxFrameTimeMs = 45.0f;   // ~22 FPS floor — below this is a hitch
	float m_spikeStdDevMs = 20.0f;    // unstable frametime stddev
	int m_minSpikes = 4;              // consecutive bad samples before scoring
};
