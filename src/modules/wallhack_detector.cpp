#include "wallhack_detector.h"
#include "../plugin.h"
#include "../players.h"

WallhackDetector::WallhackDetector() = default;

void WallhackDetector::SetConfig(float trackFovDeg, float minTrackDistance, int streakTicksForScore)
{
	m_trackFovDeg = trackFovDeg > 0.0f ? trackFovDeg : 3.0f;
	m_minTrackDistance = minTrackDistance > 0.0f ? minTrackDistance : 800.0f;
	m_streakTicksForScore = streakTicksForScore > 0 ? streakTicksForScore : 96; // ~1.5s @64
}

float WallhackDetector::Analyze(PlayerProfile& player, const std::vector<PlayerProfile*>& enemies)
{
	// Per-tick FOV lock without EngineTrace false-positives hard (holding an angle is normal).
	// Only track streak for prefire-on-kill bonus; do NOT add score here.
	if (enemies.empty() || !IsPlayerAlive(player.slot))
	{
		player.wallAimStreak = 0;
		player.wallAimTargetSteam = 0;
		return 0.0f;
	}

	float bestFov = 999.0f;
	uint64_t bestTarget = 0;

	for (const PlayerProfile* enemy : enemies)
	{
		if (!enemy || !IsPlayerAlive(enemy->slot))
			continue;

		float dist = VectorDistance(player.position, enemy->position);
		if (dist < m_minTrackDistance)
			continue;

		float fov = CalculateFOV(player.viewAngles, player.position, enemy->position);
		if (fov < bestFov)
		{
			bestFov = fov;
			bestTarget = enemy->steamId;
		}
	}

	if (bestTarget != 0 && bestFov <= m_trackFovDeg)
	{
		if (player.wallAimTargetSteam == bestTarget)
			player.wallAimStreak++;
		else
		{
			player.wallAimTargetSteam = bestTarget;
			player.wallAimStreak = 1;
		}
	}
	else
	{
		player.wallAimStreak = 0;
		player.wallAimTargetSteam = 0;
	}

	return 0.0f;
}

float WallhackDetector::OnCombatKill(PlayerProfile& attacker, PlayerProfile& victim, const CombatKillFlags& flags)
{
	float suspicionDelta = 0.0f;
	float dist = flags.distance;
	if (dist <= 0.0f)
		dist = VectorDistance(attacker.position, victim.position);

	// Thru-smoke: only HS at range (smoke spray kills are common).
	if (flags.thrusmoke && flags.headshot && dist >= 500.0f)
	{
		suspicionDelta += 5.0f;
		AC_Log("SMOKE HS dist=%.0f steam=%llu -> %llu +5",
			dist, (unsigned long long)attacker.steamId, (unsigned long long)victim.steamId);
	}

	// Blind kill: only HS (spray while flashed happens).
	if (flags.attackerblind && flags.headshot && dist >= 300.0f)
	{
		suspicionDelta += 12.0f;
		AC_Log("BLIND HS dist=%.0f steam=%llu -> %llu +12",
			dist, (unsigned long long)attacker.steamId, (unsigned long long)victim.steamId);
	}

	// Wallbang: multi-pen + HS + long range only.
	if (flags.penetrated >= 2 && flags.headshot && dist >= 700.0f)
	{
		suspicionDelta += 12.0f;
		AC_Log("WALLBANG hs pen=%d dist=%.0f steam=%llu +12",
			flags.penetrated, dist, (unsigned long long)attacker.steamId);
	}

	// Prefire after long distant lock (~1.5s).
	if (attacker.wallAimTargetSteam == victim.steamId &&
		attacker.wallAimStreak >= m_streakTicksForScore &&
		dist >= m_minTrackDistance)
	{
		suspicionDelta += 12.0f;
		AC_Log("prefire-after-lock streak=%d steam=%llu -> %llu +12",
			attacker.wallAimStreak,
			(unsigned long long)attacker.steamId, (unsigned long long)victim.steamId);
	}

	return suspicionDelta;
}
