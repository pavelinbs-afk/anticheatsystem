#pragma once

#include "math_utils.h"
#include <vector>
#include <cstdint>

// Tracks every weapon_fire and scores aimbot-like patterns on player_hurt.
class ShotTracker {
public:
	void SetConfig(float hitWindowSec, float aimFovDeg, float minHitDistance, int minShotsBeforeScore);

	float OnWeaponFire(PlayerProfile& shooter, float curtime, const std::vector<PlayerProfile*>& enemies);
	float OnPlayerHurt(PlayerProfile& attacker, PlayerProfile& victim, float damage, int hitgroup, float curtime);
	float FlushStale(PlayerProfile& shooter, float curtime);

private:
	float m_hitWindowSec = 0.45f;
	float m_aimFovDeg = 3.5f;
	float m_minHitDistance = 200.0f;
	int m_minShotsBeforeScore = 3;

	// Snap-to-target then hit (rage / hard flick aimbot)
	float m_snapHitDeg = 28.0f;
	// Looking far from victim at fire but still hit (silent aim)
	float m_silentAimFovDeg = 12.0f;
};
