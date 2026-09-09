#pragma once
#include "math_utils.h"

class AimAnalyzer {
public:
    AimAnalyzer();
    
    void SetConfig(float snapThreshold, float minReactionTimeMs, float maxFov, float minSmoothness);
    
    float Analyze(PlayerProfile& player, float deltaTime);
    float OnPlayerShoot(PlayerProfile& shooter, PlayerProfile& victim, bool headshot, float distance);

private:
    float m_snapThreshold;     // degrees per tick
    float m_minReactionTimeMs; // inhuman reaction < 50ms
    float m_maxFov;            // fov threshold for aimbot
    float m_minSmoothness;     // for jitter/curve detection
};
