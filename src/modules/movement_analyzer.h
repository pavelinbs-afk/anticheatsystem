#pragma once
#include "math_utils.h"

class MovementAnalyzer {
public:
	MovementAnalyzer();

	void SetConfig(float maxVelocity, float maxTeleportDistance);

	// Returns suspicion delta. Caller must pass alive/team/grace context via profile fields.
	float Analyze(PlayerProfile& player, float deltaTime, float curtime, int teamNum, bool alive);

private:
	float m_maxVelocity;
	float m_maxTeleportDistance;
};
