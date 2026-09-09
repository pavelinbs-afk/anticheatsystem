#pragma once

#include "math_utils.h"
#include <vector>
#include <cstdint>

// Tracks every weapon_fire (not only kills) and matches player_hurt for aim analysis.
class ShotTracker {
public:
	void SetConfig(float hitWindowSec, float aimFovDeg, float minHitDistance, int minShotsBeforeScore);

	void OnWeaponFire(PlayerProfile& shooter, float curtime, const std::vector<PlayerProfile*>& enemies);
	float OnPlayerHurt(PlayerProfile& attacker, PlayerProfile& victim, float damage, int hitgroup, float curtime);
	float FlushStale(PlayerProfile& shooter, float curtime);

private:
	float m_hitWindowSec = 0.35f;
	float m_aimFovDeg = 1.25f;
	float m_minHitDistance = 350.0f;
	int m_minShotsBeforeScore = 12;
};
