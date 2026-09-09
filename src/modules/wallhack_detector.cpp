#include "wallhack_detector.h"
#include "../plugin.h"
#include "../players.h"

WallhackDetector::WallhackDetector() = default;

void WallhackDetector::SetConfig(float trackFovDeg, float minTrackDistance, int streakTicksForScore)
{
	m_trackFovDeg = trackFovDeg > 0.0f ? trackFovDeg : 4.0f;
	m_minTrackDistance = minTrackDistance > 0.0f ? minTrackDistance : 400.0f;
	m_streakTicksForScore = streakTicksForScore > 0 ? streakTicksForScore : 32;
}

float WallhackDetector::Analyze(PlayerProfile& player, const std::vector<PlayerProfile*>& enemies)
{
	float suspicionDelta = 0.0f;
	if (enemies.empty() || !IsPlayerAlive(player.slot))
	{
		player.wallAimStreak = 0;
		player.wallAimTargetSteam = 0;
		return 0.0f;
	}

	float bestFov = 999.0f;
	uint64_t bestTarget = 0;
	float bestDist = 0.0f;

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
			bestDist = dist;
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

		// Sustained lock on a distant enemy — classic WH/pre-aim signal without EngineTrace.
		if (player.wallAimStreak >= m_streakTicksForScore &&
			(player.wallAimStreak % m_streakTicksForScore) == 0)
		{
			suspicionDelta += 12.0f;
			AC_Log("WH/pre-aim lock fov=%.1f dist=%.0f streak=%d steam=%llu -> %llu",
				bestFov, bestDist, player.wallAimStreak,
				(unsigned long long)player.steamId, (unsigned long long)bestTarget);
		}
	}
	else
	{
		player.wallAimStreak = 0;
		player.wallAimTargetSteam = 0;
	}

	return suspicionDelta;
}

float WallhackDetector::OnCombatKill(PlayerProfile& attacker, PlayerProfile& victim, const CombatKillFlags& flags)
{
	float suspicionDelta = 0.0f;
	float dist = flags.distance;
	if (dist <= 0.0f)
		dist = VectorDistance(attacker.position, victim.position);

	// Thru-smoke kill.
	if (flags.thrusmoke)
	{
		suspicionDelta += 5.0f;
		AC_Log("SMOKE kill hs=%d dist=%.0f steam=%llu -> %llu +5",
			(int)flags.headshot, dist,
			(unsigned long long)attacker.steamId, (unsigned long long)victim.steamId);
	}

	// Kill while flashed — WH / sound ESP.
	if (flags.attackerblind)
	{
		suspicionDelta += 12.0f;
		AC_Log("BLIND kill hs=%d steam=%llu -> %llu +12",
			(int)flags.headshot,
			(unsigned long long)attacker.steamId, (unsigned long long)victim.steamId);
	}

	// Multi-surface wallbang headshot at range.
	if (flags.penetrated > 0 && flags.headshot && dist >= 500.0f)
	{
		suspicionDelta += 12.0f;
		AC_Log("WALLBANG hs penetrated=%d dist=%.0f steam=%llu +12",
			flags.penetrated, dist, (unsigned long long)attacker.steamId);
	}

	// Prefire: long aim streak on this victim then kill.
	if (attacker.wallAimTargetSteam == victim.steamId && attacker.wallAimStreak >= m_streakTicksForScore)
	{
		suspicionDelta += 12.0f;
		AC_Log("prefire-after-lock streak=%d steam=%llu -> %llu +12",
			attacker.wallAimStreak,
			(unsigned long long)attacker.steamId, (unsigned long long)victim.steamId);
	}

	return suspicionDelta;
}
