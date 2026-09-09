#include "aim_analyzer.h"
#include "../plugin.h"
#include <cmath>

AimAnalyzer::AimAnalyzer()
	: m_snapThreshold(75.0f), m_minReactionTimeMs(50.0f), m_maxFov(3.0f), m_minSmoothness(0.5f)
{
}

void AimAnalyzer::SetConfig(float snapThreshold, float minReactionTimeMs, float maxFov, float minSmoothness)
{
	m_snapThreshold = snapThreshold > 0.0f ? snapThreshold : 75.0f;
	m_minReactionTimeMs = minReactionTimeMs;
	m_maxFov = maxFov > 0.0f ? maxFov : 3.0f;
	m_minSmoothness = minSmoothness;
}

float AimAnalyzer::Analyze(PlayerProfile& player, float deltaTime)
{
	(void)deltaTime;
	float suspicionDelta = 0.0f;

	const float pitch = player.viewAngles.pitch;
	const float roll = player.viewAngles.roll;
	if (!std::isfinite(pitch) || !std::isfinite(player.viewAngles.yaw) || !std::isfinite(roll) ||
		pitch < -89.5f || pitch > 89.5f || std::fabs(roll) > 2.0f)
	{
		player.untrustedAngleHits++;
		if (player.untrustedAngleHits >= 2)
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

	// Keep history for pattern analysis
	player.angleDeltaHistory.push_back(snapAngle);
	while (player.angleDeltaHistory.size() > 64)
		player.angleDeltaHistory.pop_front();

	// Rage snap (blatant)
	if (snapAngle > m_snapThreshold)
	{
		suspicionDelta += (snapAngle >= 120.0f) ? 8.0f : 4.0f;
		AC_Log("aim snap %.1f° steam=%llu +%.0f",
			snapAngle, (unsigned long long)player.steamId, suspicionDelta);
	}

	return suspicionDelta;
}

float AimAnalyzer::OnPlayerShoot(PlayerProfile& shooter, PlayerProfile& victim, bool headshot, float distance)
{
	float suspicionDelta = 0.0f;
	float fov = CalculateFOV(shooter.viewAngles, shooter.position, victim.position);

	// Prefer FOV from last shot record if present (angles at fire time).
	if (!shooter.recentShots.empty())
	{
		const ShotRecord& last = shooter.recentShots.back();
		float fovFire = CalculateFOV(last.angles, last.eyePos, victim.position);
		if (fovFire < fov)
			fov = fovFire;
	}

	if (headshot && distance >= 200.0f)
	{
		if (fov <= m_maxFov)
		{
			suspicionDelta += 11.0f;
			AC_Log("aim-kill hs fov=%.2f dist=%.0f steam=%llu +11",
				fov, distance, (unsigned long long)shooter.steamId);
		}
		else if (fov >= 12.0f)
		{
			// Kill while not looking at victim — classic silent aim.
			suspicionDelta += 14.0f;
			AC_Log("aim-kill SILENT fov=%.1f dist=%.0f steam=%llu +14",
				fov, distance, (unsigned long long)shooter.steamId);
		}
	}
	else if (!headshot && distance >= 250.0f && fov <= m_maxFov && shooter.kills >= 2)
	{
		suspicionDelta += 6.0f;
		AC_Log("aim-kill lock fov=%.2f dist=%.0f steam=%llu +6",
			fov, distance, (unsigned long long)shooter.steamId);
	}

	return suspicionDelta;
}
