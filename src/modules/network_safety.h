#pragma once

#include "../player_profile.h"

// Network quality gate (from CS2AC NetworkSafety): soft detections should not punish laggy clients.
class NetworkSafety {
public:
	void Sample(PlayerProfile& player, class INetChannelInfo* net);
	bool ShouldVetoSoftDetections(const PlayerProfile& player) const;

private:
	static constexpr float kMaxPingMs = 150.0f;
	static constexpr float kMaxJitterMs = 25.0f;
	static constexpr float kMaxLoss = 0.02f;
	static constexpr float kMaxChoke = 0.02f;
};
