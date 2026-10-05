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

	// Per-tick: FOV lock on distant enemy → soft score on long tracks (possible WH).
	float Analyze(PlayerProfile& player, const std::vector<PlayerProfile*>& enemies);

	// On kill: smoke / wallbang / blind / prefire.
	float OnCombatKill(PlayerProfile& attacker, PlayerProfile& victim, const CombatKillFlags& flags);

	// On hurt: prefire / smoke / wallbang hits (before kill).
	float OnCombatHurt(PlayerProfile& attacker, PlayerProfile& victim, const CombatKillFlags& flags, bool headshot);

private:
	float m_trackFovDeg = 3.5f;
	float m_minTrackDistance = 500.0f;
	int m_streakTicksForScore = 240; // ~3.75s @64 tick
	int m_trackScoreEveryTicks = 128; // soft score cadence while locked (~2s)
};
