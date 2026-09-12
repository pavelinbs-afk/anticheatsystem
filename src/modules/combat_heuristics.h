#pragma once

#include "math_utils.h"
#include <vector>
#include <cstdint>

// CS2AC-inspired combat heuristics that work without ProcessUsercmds:
// triggerbot (FOV acquire → fire), aimlock (sustained track), doubletap, inhuman accuracy.
class CombatHeuristics {
public:
	// Called every GameFrame while alive — updates crosshair / aimlock state.
	void OnTick(PlayerProfile& player, const std::vector<PlayerProfile*>& enemies, float curtime);

	// Called on weapon_fire — returns soft suspicion (may be vetoed by network safety).
	float OnWeaponFire(PlayerProfile& shooter, float curtime, const std::vector<PlayerProfile*>& enemies);

	// Called on player_hurt — accuracy / trigger confirm.
	float OnPlayerHurt(PlayerProfile& attacker, PlayerProfile& victim, float curtime);

private:
	static constexpr float kTriggerFovDeg = 3.0f;
	static constexpr float kAimlockFovDeg = 2.5f;
	static constexpr float kAimlockMinDist = 200.0f;
	static constexpr int kAimlockMinSamples = 64;   // ~1s @64
	static constexpr float kAimlockCoverage = 0.95f;
	static constexpr float kDoubleTapWindow = 0.04f; // ~2 ticks
	static constexpr int kTriggerScoreDetect = 10;
	static constexpr int kAimedShotsMin = 40;
	static constexpr float kAimedHitPct = 0.90f;
};
