#include "aim_analyzer.h"
#include "../plugin.h"
#include <cmath>

AimAnalyzer::AimAnalyzer()
	: m_snapThreshold(120.0f), m_minReactionTimeMs(50.0f), m_maxFov(1.0f), m_minSmoothness(0.5f)
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

	// SMAC / TB AntiCheat: untrusted angles (impossible eye angles).
	const float pitch = player.viewAngles.pitch;
	const float roll = player.viewAngles.roll;
	if (!std::isfinite(pitch) || !std::isfinite(player.viewAngles.yaw) || !std::isfinite(roll) ||
		pitch < -89.5f || pitch > 89.5f || std::fabs(roll) > 2.0f)
	{
		player.untrustedAngleHits++;
		if (player.untrustedAngleHits >= 3)
		{
			suspicionDelta += 11.0f;
			player.untrustedAngleHits = 0;
			AC_Log("untrusted angles pitch=%.1f roll=%.1f steam=%llu",
				pitch, roll, (unsigned long long)player.steamId);
		}
		return suspicionDelta;
	}
	player.untrustedAngleHits = 0;

	float dPitch = AngleDifference(player.viewAngles.pitch, player.lastViewAngles.pitch);
	float dYaw = AngleDifference(player.viewAngles.yaw, player.lastViewAngles.yaw);
	float snapAngle = std::sqrt(dPitch * dPitch + dYaw * dYaw);

	// Only extreme snaps (spinbot-like). Normal CS2 flicks are often 40–90°.
	if (snapAngle > m_snapThreshold)
	{
		suspicionDelta += 2.0f;
		AC_Log("aim snap %.1f° steam=%llu", snapAngle, (unsigned long long)player.steamId);
	}

	return suspicionDelta;
}

float AimAnalyzer::OnPlayerShoot(PlayerProfile& shooter, PlayerProfile& victim, bool headshot, float distance)
{
	float suspicionDelta = 0.0f;
	float fov = CalculateFOV(shooter.viewAngles, shooter.position, victim.position);

	// Aim kill: need sample size + perfect FOV + mid/long range (close HS is normal).
	if (headshot && fov < m_maxFov && shooter.kills >= 8 && distance >= 400.0f)
	{
		suspicionDelta += 11.0f;
		AC_Log("aim-kill hs fov=%.2f dist=%.0f steam=%llu",
			fov, distance, (unsigned long long)shooter.steamId);
	}

	// Trigger-like: only if we actually tracked crosshair time (currently rare).
	if (shooter.timeCrosshairOnEnemy > 0.0f &&
		shooter.timeCrosshairOnEnemy < (m_minReactionTimeMs / 1000.0f) &&
		shooter.kills >= 8 && headshot && distance >= 300.0f)
	{
		suspicionDelta += 12.0f;
		AC_Log("trigger-like reaction %.0fms steam=%llu",
			shooter.timeCrosshairOnEnemy * 1000.0f, (unsigned long long)shooter.steamId);
	}

	return suspicionDelta;
}
