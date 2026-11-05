#include "combat_heuristics.h"
#include "../plugin.h"
#include <cmath>
#include <algorithm>

void CombatHeuristics::OnTick(PlayerProfile& player, const std::vector<PlayerProfile*>& enemies, float curtime)
{
	float bestFov = 999.0f;
	uint64_t bestSteam = 0;
	float bestDist = 0.0f;
	AcVec3 bestPos{};

	for (const PlayerProfile* e : enemies)
	{
		if (!e)
			continue;
		const float dist = VectorDistance(player.position, e->position);
		const float fov = CalculateFOVToBody(player.viewAngles, player.position, e->position);
		if (fov < bestFov)
		{
			bestFov = fov;
			bestSteam = e->steamId;
			bestDist = dist;
			bestPos = e->position;
		}
	}

	player.isLookingAtEnemy = (bestSteam != 0 && bestFov <= kTriggerFovDeg);
	if (player.isLookingAtEnemy)
	{
		if (player.crosshairEnemySteam != bestSteam || player.crosshairOnEnemySince <= 0.0f)
		{
			player.crosshairEnemySteam = bestSteam;
			player.crosshairOnEnemySince = curtime;
			player.crosshairFreshContact = true;
		}
		player.timeCrosshairOnEnemy = curtime - player.crosshairOnEnemySince;
	}
	else
	{
		player.crosshairEnemySteam = 0;
		player.crosshairOnEnemySince = 0.0f;
		player.crosshairFreshContact = false;
		player.timeCrosshairOnEnemy = 0.0f;
	}

	// Aimlock: sustained FOV lock — soft decay on brief breaks (peek jitter), hard reset only after ~0.2s off.
	if (bestSteam != 0 && bestFov <= kAimlockFovDeg && bestDist >= kAimlockMinDist)
	{
		if (player.aimlockTargetSteam == bestSteam)
		{
			player.aimlockSamples++;
			player.aimlockOnTicks++;
			player.aimlockTravelAccum += VectorDistance(player.aimlockLastTargetPos, bestPos);
			player.aimlockLastTargetPos = bestPos;
			player.aimlockOffTicks = 0;
		}
		else
		{
			player.aimlockTargetSteam = bestSteam;
			player.aimlockSamples = 1;
			player.aimlockOnTicks = 1;
			player.aimlockTravelAccum = 0.0f;
			player.aimlockLastTargetPos = bestPos;
			player.aimlockStartDist = bestDist;
			player.aimlockOffTicks = 0;
		}
	}
	else if (player.aimlockTargetSteam != 0)
	{
		player.aimlockOffTicks++;
		player.aimlockSamples++; // keep coverage denominator honest
		if (player.aimlockOffTicks > 12) // ~0.2s @64
		{
			player.aimlockTargetSteam = 0;
			player.aimlockSamples = 0;
			player.aimlockOnTicks = 0;
			player.aimlockTravelAccum = 0.0f;
			player.aimlockOffTicks = 0;
		}
	}
}

float CombatHeuristics::OnWeaponFire(PlayerProfile& shooter, float curtime, const std::vector<PlayerProfile*>& enemies)
{
	(void)enemies;
	float suspicion = 0.0f;

	// Doubletap: two fires within ~2 ticks — require repeat pairs (already ≥2).
	if (shooter.lastFireTime > 0.0f && (curtime - shooter.lastFireTime) <= kDoubleTapWindow)
	{
		shooter.doubleTapPairs++;
		if (shooter.doubleTapPairs >= 2)
		{
			suspicion += 10.0f;
			AC_Log("DOUBLETAP pairs=%d dt=%.3f steam=%llu +10",
				shooter.doubleTapPairs, curtime - shooter.lastFireTime,
				(unsigned long long)shooter.steamId);
			shooter.doubleTapPairs = 0;
		}
	}
	else if (shooter.lastFireTime > 0.0f && (curtime - shooter.lastFireTime) > 0.25f)
	{
		shooter.doubleTapPairs = 0;
	}
	shooter.prevFireTime = shooter.lastFireTime;
	shooter.lastFireTime = curtime;

	// Triggerbot: accumulate on fire, award score only after hit confirm (OnPlayerHurt).
	if (shooter.crosshairFreshContact && shooter.crosshairOnEnemySince > 0.0f)
	{
		const float reaction = curtime - shooter.crosshairOnEnemySince;
		if (reaction >= 0.0f && reaction <= 0.05f)
		{
			shooter.triggerScore += 2;
			shooter.crosshairFreshContact = false;
			AC_Log("TRIGGER? reaction=%.3fs score=%d steam=%llu",
				reaction, shooter.triggerScore, (unsigned long long)shooter.steamId);
		}
		else if (reaction > 0.05f && reaction < 0.2f)
		{
			shooter.triggerScore = std::max(0, shooter.triggerScore - 3);
			shooter.crosshairFreshContact = false;
		}
	}

	if (shooter.isLookingAtEnemy && shooter.timeCrosshairOnEnemy >= 0.05f)
		shooter.aimedShots++;

	if (shooter.aimlockSamples >= kAimlockMinSamples)
	{
		const float coverage = shooter.aimlockSamples > 0
			? (float)shooter.aimlockOnTicks / (float)shooter.aimlockSamples
			: 0.0f;
		const float needTravel = shooter.aimlockStartDist > 1.0f
			? RadiansToDegrees(std::atan2(128.0f, shooter.aimlockStartDist))
			: 5.0f;
		if (coverage >= kAimlockCoverage && shooter.aimlockTravelAccum >= needTravel)
		{
			shooter.aimlockEpisodes++;
			AC_Log("AIMLOCK? cov=%.2f travel=%.1f need=%.1f ep=%d steam=%llu",
				coverage, shooter.aimlockTravelAccum, needTravel,
				shooter.aimlockEpisodes, (unsigned long long)shooter.steamId);
			if (shooter.aimlockEpisodes >= 3)
			{
				suspicion += 14.0f;
				shooter.aimlockEpisodes = 0;
				AC_Log("AIMLOCK detect steam=%llu +14", (unsigned long long)shooter.steamId);
			}
			shooter.aimlockSamples = 0;
			shooter.aimlockOnTicks = 0;
			shooter.aimlockTravelAccum = 0.0f;
			shooter.aimlockOffTicks = 0;
		}
	}

	return suspicion;
}

float CombatHeuristics::OnPlayerHurt(PlayerProfile& attacker, PlayerProfile& victim, float curtime)
{
	(void)victim;
	float suspicion = 0.0f;

	if (attacker.aimedShots > 0 && attacker.isLookingAtEnemy)
		attacker.aimedHits++;

	if (attacker.aimedShots >= kAimedShotsMin)
	{
		const float pct = (float)attacker.aimedHits / (float)attacker.aimedShots;
		if (pct >= kAimedHitPct && !attacker.inhumanAccuracyFlagged)
		{
			attacker.inhumanAccuracyFlagged = true;
			suspicion += 12.0f;
			AC_Log("INHUMAN ACC aimed=%d hits=%d pct=%.0f%% steam=%llu +12",
				attacker.aimedShots, attacker.aimedHits, pct * 100.0f,
				(unsigned long long)attacker.steamId);
		}
	}

	// Trigger confirm: hit after fresh acquire (fire only accumulates score).
	if (attacker.crosshairOnEnemySince > 0.0f)
	{
		const float reaction = curtime - attacker.crosshairOnEnemySince;
		if (reaction >= 0.0f && reaction <= 0.05f)
		{
			attacker.triggerScore += 2;
			if (attacker.triggerScore >= kTriggerScoreDetect)
			{
				suspicion += 12.0f;
				attacker.triggerScore = 0;
				AC_Log("TRIGGERBOT (hit confirm) steam=%llu +12", (unsigned long long)attacker.steamId);
			}
		}
	}

	return suspicion;
}
