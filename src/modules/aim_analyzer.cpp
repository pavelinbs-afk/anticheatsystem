#include "aim_analyzer.h"
#include "../plugin.h"
#include <cmath>

AimAnalyzer::AimAnalyzer()
	: m_snapThreshold(55.0f), m_minReactionTimeMs(50.0f), m_maxFov(3.0f), m_minSmoothness(0.5f)
{
}

void AimAnalyzer::SetConfig(float snapThreshold, float minReactionTimeMs, float maxFov, float minSmoothness)
{
	m_snapThreshold = snapThreshold > 0.0f ? snapThreshold : 55.0f;
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

	player.angleDeltaHistory.push_back(snapAngle);
	while (player.angleDeltaHistory.size() > 64)
		player.angleDeltaHistory.pop_front();

	// Per-tick snaps without a shot are mostly look-around / legit flicks.
	// Only score blatant spinbot-class snaps here; combat snaps go through ShotTracker.
	if (snapAngle >= 120.0f)
	{
		suspicionDelta += 6.0f;
		AC_Log("aim snap %.1f° (spin) steam=%llu +6",
			snapAngle, (unsigned long long)player.steamId);
	}

	return suspicionDelta;
}

float AimAnalyzer::OnPlayerShoot(PlayerProfile& shooter, PlayerProfile& victim, bool headshot, float distance)
{
	float suspicionDelta = 0.0f;
	float fov = CalculateFOV(shooter.viewAngles, shooter.position, victim.position);
	float snap = 0.0f;

	if (!shooter.recentShots.empty())
	{
		const ShotRecord& last = shooter.recentShots.back();
		float fovFire = CalculateFOV(last.angles, last.eyePos, victim.position);
		fov = fovFire; // prefer fire-time FOV
		snap = std::max(last.snapDeg, last.snapFromPrevShot);
	}

	// Low FOV kill = normal aim. Only silent (off-angle) or snap+kill are suspicious.
	if (headshot && distance >= 200.0f && fov >= 14.0f)
	{
		suspicionDelta += 14.0f;
		AC_Log("aim-kill SILENT fov=%.1f dist=%.0f steam=%llu +14",
			fov, distance, (unsigned long long)shooter.steamId);
	}
	else if (distance >= 250.0f && snap >= 55.0f && fov <= 5.0f)
	{
		suspicionDelta += headshot ? 12.0f : 8.0f;
		AC_Log("aim-kill RAGE snap=%.1f fov=%.2f hs=%d dist=%.0f steam=%llu +%.0f",
			snap, fov, (int)headshot, distance, (unsigned long long)shooter.steamId, suspicionDelta);
	}

	return suspicionDelta;
}
