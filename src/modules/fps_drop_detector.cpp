#include "fps_drop_detector.h"
#include "../plugin.h"

void FpsDropDetector::SetConfig(float maxFrameTimeMs, float spikeStdDevMs, int minSpikes)
{
	m_maxFrameTimeMs = maxFrameTimeMs;
	m_spikeStdDevMs = spikeStdDevMs;
	m_minSpikes = minSpikes > 0 ? minSpikes : 1;
}

float FpsDropDetector::Analyze(PlayerProfile& player, float frameTimeSec, float frameTimeStdDevSec)
{
	if (frameTimeSec <= 0.0f)
		return 0.0f;

	const float frameMs = frameTimeSec * 1000.0f;
	const float stdMs = frameTimeStdDevSec * 1000.0f;
	const float approxFps = frameTimeSec > 0.0f ? (1.0f / frameTimeSec) : 0.0f;

	// Only severe hitches (~<15 FPS sustained). Weak PCs must not rack up ban score.
	const bool hitch = frameMs >= m_maxFrameTimeMs;
	const bool unstable = stdMs >= m_spikeStdDevMs && frameMs >= (m_maxFrameTimeMs * 0.75f);

	if (hitch || unstable)
	{
		player.fpsDropStreak++;
		player.lastClientFrameMs = frameMs;
	}
	else
	{
		if (player.fpsDropStreak > 0)
			player.fpsDropStreak--;
		player.lastClientFrameMs = frameMs;
		return 0.0f;
	}

	if (player.fpsDropStreak < m_minSpikes)
		return 0.0f;

	// Score rarely — once every (minSpikes * 8) bad samples.
	const int period = m_minSpikes * 8;
	if ((player.fpsDropStreak % period) != 0)
		return 0.0f;

	AC_Log("client FPS hitch ~%.0ffps (ft=%.1fms std=%.1fms) steam=%llu streak=%d",
		approxFps, frameMs, stdMs, (unsigned long long)player.steamId, player.fpsDropStreak);

	return 10.0f;
}
