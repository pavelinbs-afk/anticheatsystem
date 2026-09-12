#include "network_safety.h"
#include "../plugin.h"

#include <inetchannelinfo.h>
#include <cmath>

void NetworkSafety::Sample(PlayerProfile& player, INetChannelInfo* net)
{
	if (!net)
	{
		player.networkUnavailableStreak++;
		player.networkUnsafe = player.networkUnavailableStreak >= 3;
		return;
	}

	const float ping = net->GetAvgLatency() * 1000.0f;
	const float lossIn = net->GetAvgLoss(FLOW_INCOMING);
	const float lossOut = net->GetAvgLoss(FLOW_OUTGOING);
	const float chokeIn = net->GetAvgChoke(FLOW_INCOMING);
	const float chokeOut = net->GetAvgChoke(FLOW_OUTGOING);

	float jitter = 0.0f;
	if (player.lastPingMs > 0.0f)
		jitter = std::fabs(ping - player.lastPingMs);
	player.lastPingMs = ping;
	player.lastJitterMs = jitter;
	player.lastLoss = std::max(lossIn, lossOut);
	player.lastChoke = std::max(chokeIn, chokeOut);
	player.networkUnavailableStreak = 0;

	player.networkUnsafe =
		ping >= kMaxPingMs ||
		jitter >= kMaxJitterMs ||
		player.lastLoss >= kMaxLoss ||
		player.lastChoke >= kMaxChoke;
}

bool NetworkSafety::ShouldVetoSoftDetections(const PlayerProfile& player) const
{
	return player.networkUnsafe;
}
