#include "shot_tracker.h"
#include "../plugin.h"
#include <cmath>

void ShotTracker::SetConfig(float hitWindowSec, float aimFovDeg, float minHitDistance, int minShotsBeforeScore)
{
	m_hitWindowSec = hitWindowSec > 0.05f ? hitWindowSec : 0.45f;
	m_aimFovDeg = aimFovDeg > 0.1f ? aimFovDeg : 3.5f;
	m_minHitDistance = minHitDistance > 0.0f ? minHitDistance : 200.0f;
	m_minShotsBeforeScore = minShotsBeforeScore > 0 ? minShotsBeforeScore : 3;
}

float ShotTracker::OnWeaponFire(PlayerProfile& shooter, float curtime, const std::vector<PlayerProfile*>& enemies)
{
	shooter.shotsFired++;

	ShotRecord rec;
	rec.time = curtime;
	rec.angles = shooter.viewAngles;
	rec.eyePos = shooter.position;

	float dPitch = AngleDifference(shooter.viewAngles.pitch, shooter.lastViewAngles.pitch);
	float dYaw = AngleDifference(shooter.viewAngles.yaw, shooter.lastViewAngles.yaw);
	rec.snapDeg = std::sqrt(dPitch * dPitch + dYaw * dYaw);

	if (shooter.hasLastShotAngles)
	{
		float sp = AngleDifference(shooter.viewAngles.pitch, shooter.lastShotAngles.pitch);
		float sy = AngleDifference(shooter.viewAngles.yaw, shooter.lastShotAngles.yaw);
		rec.snapFromPrevShot = std::sqrt(sp * sp + sy * sy);
	}
	shooter.lastShotAngles = shooter.viewAngles;
	shooter.hasLastShotAngles = true;

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
	while (shooter.recentShots.size() > 96)
		shooter.recentShots.pop_front();

	if (shooter.shotsFired <= 5 || (shooter.shotsFired % 40) == 0)
	{
		AC_Log("shot#%d ang=%.1f/%.1f snap=%.1f bestFov=%.1f dist=%.0f enemies=%d steam=%llu",
			shooter.shotsFired, shooter.viewAngles.pitch, shooter.viewAngles.yaw,
			rec.snapDeg, bestFov, bestDist, (int)enemies.size(),
			(unsigned long long)shooter.steamId);
	}

	if (bestFov < 4.0f)
		shooter.timeCrosshairOnEnemy = std::min(shooter.timeCrosshairOnEnemy + 0.05f, 2.0f);
	else
		shooter.timeCrosshairOnEnemy = 0.0f;

	float suspicion = 0.0f;
	const float snap = std::max(rec.snapDeg, rec.snapFromPrevShot);

	// Only blatant fire-time rage (huge snap onto enemy). Medium snaps wait for a hit.
	if (snap >= 75.0f && bestFov <= 4.0f && bestDist >= 200.0f && !enemies.empty())
	{
		suspicion += 12.0f;
		AC_Log("RAGE snap-to-target snap=%.1f fov=%.1f dist=%.0f steam=%llu +12",
			snap, bestFov, bestDist, (unsigned long long)shooter.steamId);
	}
	else if (snap >= 150.0f)
	{
		suspicion += 5.0f;
		AC_Log("RAGE snap snap=%.1f steam=%llu +5", snap, (unsigned long long)shooter.steamId);
	}

	return suspicion;
}

float ShotTracker::OnPlayerHurt(PlayerProfile& attacker, PlayerProfile& victim, float damage, int hitgroup, float curtime)
{
	(void)damage;
	float suspicion = 0.0f;
	const bool head = (hitgroup == 1 || hitgroup == 8);

	ShotRecord* matched = nullptr;
	for (auto it = attacker.recentShots.rbegin(); it != attacker.recentShots.rend(); ++it)
	{
		if (it->consumedHit)
			continue;
		if (curtime - it->time > m_hitWindowSec)
			break;
		matched = &(*it);
		break;
	}

	const float distNow = VectorDistance(attacker.position, victim.position);

	if (!matched)
	{
		// Without a fire sample we only trust silent-style off-angle HS.
		float fov = CalculateFOV(attacker.viewAngles, attacker.position, victim.position);
		if (head && fov >= m_silentAimFovDeg && distNow >= m_minHitDistance)
		{
			suspicion += 8.0f;
			attacker.silentAimHits++;
			attacker.aimbotHitStreak++;
			AC_Log("SILENT-AIM? (no-shot-rec) fov=%.1f dist=%.0f steam=%llu",
				fov, distNow, (unsigned long long)attacker.steamId);
		}
		else
		{
			attacker.aimbotHitStreak = 0;
		}
		return suspicion;
	}

	matched->consumedHit = true;

	const float fovAtFire = CalculateFOV(matched->angles, matched->eyePos, victim.position);
	const float distAtFire = VectorDistance(matched->eyePos, victim.position);
	const float dist = distAtFire > 1.0f ? distAtFire : distNow;
	const float snap = std::max(matched->snapDeg, matched->snapFromPrevShot);

	const bool rageHit = (snap >= 45.0f && fovAtFire <= 5.0f);
	const bool silentHit = (fovAtFire >= m_silentAimFovDeg);
	if (!rageHit && !silentHit && attacker.shotsFired < m_minShotsBeforeScore)
		return 0.0f;
	if (dist < m_minHitDistance)
	{
		attacker.aimbotHitStreak = 0;
		return 0.0f;
	}

	// Silent aim: hit while view was clearly NOT on victim
	if (silentHit)
	{
		attacker.silentAimHits++;
		suspicion += head ? 14.0f : 9.0f;
		AC_Log("SILENT-AIM? fovAtFire=%.1f snap=%.1f hs=%d dist=%.0f hits=%d steam=%llu -> %llu",
			fovAtFire, snap, (int)head, dist, attacker.silentAimHits,
			(unsigned long long)attacker.steamId, (unsigned long long)victim.steamId);
		if (attacker.silentAimHits >= 2)
			suspicion += 6.0f;
		attacker.aimbotHitStreak++;
		return suspicion;
	}

	// Rage: snap onto victim then hit
	if (rageHit)
	{
		suspicion += head ? 15.0f : 10.0f;
		attacker.aimbotHitStreak++;
		AC_Log("RAGE hit snap=%.1f fov=%.2f hs=%d dist=%.0f steam=%llu -> %llu +%.0f",
			snap, fovAtFire, (int)head, dist,
			(unsigned long long)attacker.steamId, (unsigned long long)victim.steamId, suspicion);
		if (attacker.aimbotHitStreak >= 2)
			suspicion += 8.0f;
		return suspicion;
	}

	// Soft aimbot: on-target + meaningful snap (not perfect static aim).
	// Perfect FOV without snap is legit — do NOT score it.
	if (fovAtFire <= m_aimFovDeg && snap >= m_snapHitDeg)
	{
		suspicion += head ? 7.0f : 4.0f;
		attacker.aimbotHitStreak++;
		AC_Log("aimbot-like snap-hit fov=%.2f snap=%.1f hs=%d dist=%.0f streak=%d steam=%llu +%.0f",
			fovAtFire, snap, (int)head, dist, attacker.aimbotHitStreak,
			(unsigned long long)attacker.steamId, suspicion);
	}
	else if (snap >= (m_snapHitDeg + 20.0f) && fovAtFire <= (m_aimFovDeg + 3.0f) && head)
	{
		suspicion += 6.0f;
		attacker.aimbotHitStreak++;
		AC_Log("aimbot-like flick HS fov=%.2f snap=%.1f dist=%.0f steam=%llu +6",
			fovAtFire, snap, dist, (unsigned long long)attacker.steamId);
	}
	else
	{
		attacker.aimbotHitStreak = 0;
	}

	if (attacker.aimbotHitStreak >= 4 && (attacker.aimbotHitStreak % 4) == 0)
	{
		suspicion += 8.0f;
		AC_Log("aimbot streak=%d steam=%llu +8",
			attacker.aimbotHitStreak, (unsigned long long)attacker.steamId);
	}

	return suspicion;
}

float ShotTracker::FlushStale(PlayerProfile& shooter, float curtime)
{
	while (!shooter.recentShots.empty() &&
		(curtime - shooter.recentShots.front().time) > (m_hitWindowSec * 5.0f))
	{
		shooter.recentShots.pop_front();
	}
	return 0.0f;
}
