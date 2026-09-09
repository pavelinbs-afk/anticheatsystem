#pragma once
#include "math_utils.h"
#include <vector>

struct CombatKillFlags {
	bool headshot = false;
	bool thrusmoke = false;
	bool attackerblind = false;
	bool noscope = false;
	int penetrated = 0;
	float distance = 0.0f;
};

class WallhackDetector {
public:
	WallhackDetector();

	void SetConfig(float trackFovDeg, float minTrackDistance, int streakTicksForScore);

	// Per-tick: tracking enemies with tight FOV (possible WH / pre-aim).
	float Analyze(PlayerProfile& player, const std::vector<PlayerProfile*>& enemies);

	// On kill: smoke / wallbang / blind kills.
	float OnCombatKill(PlayerProfile& attacker, PlayerProfile& victim, const CombatKillFlags& flags);

private:
	float m_trackFovDeg = 4.0f;
	float m_minTrackDistance = 400.0f;
	int m_streakTicksForScore = 32; // ~0.5s at 64 tick
};
