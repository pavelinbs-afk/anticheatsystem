#include "wallhack_detector.h"
#include "../plugin.h"
#include "../players.h"

WallhackDetector::WallhackDetector() = default;

void WallhackDetector::SetConfig(float trackFovDeg, float minTrackDistance, int streakTicksForScore)
{
	m_trackFovDeg = trackFovDeg > 0.0f ? trackFovDeg : 3.5f;
	m_minTrackDistance = minTrackDistance > 0.0f ? minTrackDistance : 500.0f;
	m_streakTicksForScore = streakTicksForScore > 0 ? streakTicksForScore : 240;
	m_trackScoreEveryTicks = 128; // ~2s between hold scores @64 tick
}

float WallhackDetector::Analyze(PlayerProfile& player, const std::vector<PlayerProfile*>& enemies)
{
	if (enemies.empty() || !IsPlayerAlive(player.slot))
	{
		player.wallAimStreak = 0;
		player.wallAimTargetSteam = 0;
		player.wallTrackScoreTicks = 0;
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
			player.wallTrackScoreTicks = 0;
		}
	}
	else
	{
		player.wallAimStreak = 0;
		player.wallAimTargetSteam = 0;
		player.wallTrackScoreTicks = 0;
		return 0.0f;
	}

	// Soft score while holding a tight distant lock (WH pre-aim). Rate-limited.
	if (player.wallAimStreak < m_streakTicksForScore)
		return 0.0f;

	player.wallTrackScoreTicks++;
	if (player.wallTrackScoreTicks == 1)
	{
		AC_Log("WH track lock streak=%d fov=%.2f dist=%.0f steam=%llu target=%llu +3",
			player.wallAimStreak, bestFov, bestDist,
			(unsigned long long)player.steamId, (unsigned long long)bestTarget);
		return 3.0f;
	}
	if (player.wallTrackScoreTicks > 1 &&
		(player.wallTrackScoreTicks % m_trackScoreEveryTicks) == 0)
	{
		AC_Log("WH track hold streak=%d dist=%.0f steam=%llu +2",
			player.wallAimStreak, bestDist, (unsigned long long)player.steamId);
		return 2.0f;
	}
	return 0.0f;
}

float WallhackDetector::OnCombatKill(PlayerProfile& attacker, PlayerProfile& victim, const CombatKillFlags& flags)
{
	float suspicionDelta = 0.0f;
	float dist = flags.distance;
	if (dist <= 0.0f)
		dist = VectorDistance(attacker.position, victim.position);

	if (flags.thrusmoke)
	{
		if (flags.headshot && dist >= 400.0f)
		{
			suspicionDelta += 10.0f;
			AC_Log("SMOKE HS dist=%.0f steam=%llu -> %llu +10",
				dist, (unsigned long long)attacker.steamId, (unsigned long long)victim.steamId);
		}
		else if (dist >= 650.0f)
		{
			suspicionDelta += 5.0f;
			AC_Log("SMOKE kill dist=%.0f steam=%llu -> %llu +5",
				dist, (unsigned long long)attacker.steamId, (unsigned long long)victim.steamId);
		}
	}

	if (flags.attackerblind && flags.headshot && dist >= 250.0f)
	{
		suspicionDelta += 16.0f;
		AC_Log("BLIND HS dist=%.0f steam=%llu -> %llu +16",
			dist, (unsigned long long)attacker.steamId, (unsigned long long)victim.steamId);
	}
	else if (flags.attackerblind && dist >= 400.0f)
	{
		suspicionDelta += 8.0f;
		AC_Log("BLIND kill dist=%.0f steam=%llu -> %llu +8",
			dist, (unsigned long long)attacker.steamId, (unsigned long long)victim.steamId);
	}

	if (flags.penetrated >= 2 && flags.headshot && dist >= 550.0f)
	{
		suspicionDelta += 16.0f;
		AC_Log("WALLBANG hs pen=%d dist=%.0f steam=%llu +16",
			flags.penetrated, dist, (unsigned long long)attacker.steamId);
	}
	else if (flags.penetrated >= 1 && flags.headshot && dist >= 700.0f)
	{
		suspicionDelta += 10.0f;
		AC_Log("WALLBANG hs pen=%d dist=%.0f steam=%llu +10",
			flags.penetrated, dist, (unsigned long long)attacker.steamId);
	}
	else if (flags.penetrated >= 2 && dist >= 800.0f)
	{
		suspicionDelta += 7.0f;
		AC_Log("WALLBANG body pen=%d dist=%.0f steam=%llu +7",
			flags.penetrated, dist, (unsigned long long)attacker.steamId);
	}

	if (attacker.wallAimTargetSteam == victim.steamId &&
		attacker.wallAimStreak >= m_streakTicksForScore &&
		dist >= m_minTrackDistance)
	{
		const float prefire = attacker.wallAimStreak >= (m_streakTicksForScore * 2) ? 20.0f : 14.0f;
		suspicionDelta += prefire;
		AC_Log("prefire-after-lock streak=%d steam=%llu -> %llu +%.0f",
			attacker.wallAimStreak,
			(unsigned long long)attacker.steamId, (unsigned long long)victim.steamId, prefire);
	}

	return suspicionDelta;
}

float WallhackDetector::OnCombatHurt(PlayerProfile& attacker, PlayerProfile& victim, const CombatKillFlags& flags, bool headshot)
{
	float suspicionDelta = 0.0f;
	float dist = flags.distance;
	if (dist <= 0.0f)
		dist = VectorDistance(attacker.position, victim.position);

	// Prefire hit after distant FOV lock (strong WH signal even without kill).
	if (attacker.wallAimTargetSteam == victim.steamId &&
		attacker.wallAimStreak >= m_streakTicksForScore &&
		dist >= m_minTrackDistance)
	{
		const float hitScore = headshot ? 10.0f : 6.0f;
		suspicionDelta += hitScore;
		AC_Log("WH prefire-hit streak=%d hs=%d dist=%.0f steam=%llu -> %llu +%.0f",
			attacker.wallAimStreak, (int)headshot, dist,
			(unsigned long long)attacker.steamId, (unsigned long long)victim.steamId, hitScore);
		// Consume part of streak so one spray doesn't stack infinitely.
		attacker.wallAimStreak = m_streakTicksForScore / 2;
		attacker.wallTrackScoreTicks = 0;
	}

	if (flags.thrusmoke && headshot && dist >= 450.0f)
	{
		suspicionDelta += 6.0f;
		AC_Log("SMOKE HS hit dist=%.0f steam=%llu +6",
			dist, (unsigned long long)attacker.steamId);
	}

	if (flags.penetrated >= 2 && headshot && dist >= 600.0f)
	{
		suspicionDelta += 8.0f;
		AC_Log("WALLBANG HS hit pen=%d dist=%.0f steam=%llu +8",
			flags.penetrated, dist, (unsigned long long)attacker.steamId);
	}

	return suspicionDelta;
}
