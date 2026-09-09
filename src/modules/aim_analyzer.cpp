#include "aim_analyzer.h"
#include "../plugin.h"

AimAnalyzer::AimAnalyzer()
	: m_snapThreshold(55.0f), m_minReactionTimeMs(50.0f), m_maxFov(2.0f), m_minSmoothness(0.5f)
{
}

void AimAnalyzer::SetConfig(float snapThreshold, float minReactionTimeMs, float maxFov, float minSmoothness)
{
	m_snapThreshold = snapThreshold;
	m_minReactionTimeMs = minReactionTimeMs;
	m_maxFov = maxFov;
	m_minSmoothness = minSmoothness;
}

float AimAnalyzer::Analyze(PlayerProfile& player, float deltaTime)
{
	(void)deltaTime;
	float suspicionDelta = 0.0f;

	float dPitch = AngleDifference(player.viewAngles.pitch, player.lastViewAngles.pitch);
	float dYaw = AngleDifference(player.viewAngles.yaw, player.lastViewAngles.yaw);
	float snapAngle = std::sqrt(dPitch * dPitch + dYaw * dYaw);

	// Only flag extreme snaps; normal flicks in CS2 can be 30-50° in a tick.
	if (snapAngle > m_snapThreshold)
	{
		suspicionDelta += 2.0f;
		AC_Log("aim snap %.1f° steam=%llu", snapAngle, (unsigned long long)player.steamId);
	}

	return suspicionDelta;
}

float AimAnalyzer::OnPlayerShoot(PlayerProfile& shooter, PlayerProfile& victim, bool headshot, float distance)
{
	(void)distance;
	float suspicionDelta = 0.0f;

	float fov = CalculateFOV(shooter.viewAngles, shooter.position, victim.position);

	// Perfect headshot with near-zero FOV after a large recent snap is more suspicious than FOV alone.
	if (headshot && fov < m_maxFov && shooter.kills >= 5)
		suspicionDelta += 3.0f;

	if (shooter.timeCrosshairOnEnemy > 0.0f &&
		shooter.timeCrosshairOnEnemy < (m_minReactionTimeMs / 1000.0f) &&
		shooter.kills >= 3)
	{
		suspicionDelta += 8.0f;
		AC_Log("trigger-like reaction %.0fms steam=%llu",
			shooter.timeCrosshairOnEnemy * 1000.0f, (unsigned long long)shooter.steamId);
	}

	return suspicionDelta;
}
