#include "shot_tracker.h"
#include "../plugin.h"

void ShotTracker::SetConfig(float hitWindowSec, float aimFovDeg, float minHitDistance, int minShotsBeforeScore)
{
	m_hitWindowSec = hitWindowSec > 0.05f ? hitWindowSec : 0.35f;
	m_aimFovDeg = aimFovDeg > 0.1f ? aimFovDeg : 1.25f;
	m_minHitDistance = minHitDistance > 0.0f ? minHitDistance : 350.0f;
	m_minShotsBeforeScore = minShotsBeforeScore > 0 ? minShotsBeforeScore : 12;
}

void ShotTracker::OnWeaponFire(PlayerProfile& shooter, float curtime, const std::vector<PlayerProfile*>& enemies)
{
	shooter.shotsFired++;

	ShotRecord rec;
	rec.time = curtime;
	rec.angles = shooter.viewAngles;

	float dPitch = AngleDifference(shooter.viewAngles.pitch, shooter.lastViewAngles.pitch);
	float dYaw = AngleDifference(shooter.viewAngles.yaw, shooter.lastViewAngles.yaw);
	rec.snapDeg = std::sqrt(dPitch * dPitch + dYaw * dYaw);

	float bestFov = 999.0f;
	uint64_t bestSteam = 0;
	float bestDist = 0.0f;
	for (const PlayerProfile* e : enemies)
	{
		if (!e)
			continue;
		float dist = VectorDistance(shooter.position, e->position);
		float fov = CalculateFOV(shooter.viewAngles, shooter.position, e->position);
		if (fov < bestFov)
		{
			bestFov = fov;
			bestSteam = e->steamId;
			bestDist = dist;
		}
	}
	rec.bestEnemyFov = bestFov;
	rec.bestEnemySteam = bestSteam;
	rec.bestEnemyDist = bestDist;

	shooter.recentShots.push_back(rec);
	while (shooter.recentShots.size() > 64)
		shooter.recentShots.pop_front();

	shooter.timeCrosshairOnEnemy = (bestFov < 5.0f) ? 0.001f : 0.0f;
}

float ShotTracker::OnPlayerHurt(PlayerProfile& attacker, PlayerProfile& victim, float damage, int hitgroup, float curtime)
{
	(void)damage;
	float suspicion = 0.0f;
	const bool head = (hitgroup == 1); // CS2 HITGROUP_HEAD

	// Find newest unmatched shot aimed near this victim.
	ShotRecord* matched = nullptr;
	for (auto it = attacker.recentShots.rbegin(); it != attacker.recentShots.rend(); ++it)
	{
		if (it->consumedHit)
			continue;
		if (curtime - it->time > m_hitWindowSec)
			break;
		if (it->bestEnemySteam != 0 && it->bestEnemySteam != victim.steamId)
			continue;
		matched = &(*it);
		break;
	}

	if (!matched)
	{
		float fov = CalculateFOV(attacker.viewAngles, attacker.position, victim.position);
		float dist = VectorDistance(attacker.position, victim.position);
		if (attacker.shotsFired >= m_minShotsBeforeScore && head && fov < m_aimFovDeg && dist >= m_minHitDistance)
		{
			suspicion += 6.0f;
			AC_Log("shot-hit aim hs fov=%.2f dist=%.0f steam=%llu",
				fov, dist, (unsigned long long)attacker.steamId);
		}
		return suspicion;
	}

	matched->consumedHit = true;
	const float fov = matched->bestEnemyFov;
	const float dist = matched->bestEnemyDist > 0.0f
		? matched->bestEnemyDist
		: VectorDistance(attacker.position, victim.position);
	const float snap = matched->snapDeg;

	if (attacker.shotsFired < m_minShotsBeforeScore)
		return 0.0f;

	if (head && fov <= m_aimFovDeg && dist >= m_minHitDistance)
	{
		suspicion += 8.0f;
		if (snap >= 55.0f)
			suspicion += 3.0f;
		AC_Log("shot-track hs fov=%.2f snap=%.1f dist=%.0f steam=%llu -> %llu +%.0f",
			fov, snap, dist,
			(unsigned long long)attacker.steamId, (unsigned long long)victim.steamId, suspicion);
	}
	else if (fov <= m_aimFovDeg && dist >= m_minHitDistance && snap >= 70.0f)
	{
		suspicion += 4.0f;
		AC_Log("shot-track body fov=%.2f snap=%.1f dist=%.0f steam=%llu +4",
			fov, snap, dist, (unsigned long long)attacker.steamId);
	}

	return suspicion;
}

float ShotTracker::FlushStale(PlayerProfile& shooter, float curtime)
{
	while (!shooter.recentShots.empty() &&
		(curtime - shooter.recentShots.front().time) > (m_hitWindowSec * 4.0f))
	{
		shooter.recentShots.pop_front();
	}
	return 0.0f;
}
