#pragma once
#include "math_utils.h"

class MovementAnalyzer {
public:
    MovementAnalyzer();

    void SetConfig(float maxVelocity, float maxTeleportDistance);

    float Analyze(PlayerProfile& player, float deltaTime);

private:
    float m_maxVelocity;
    float m_maxTeleportDistance;
};
