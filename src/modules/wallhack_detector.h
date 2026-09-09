#pragma once
#include "math_utils.h"
#include <vector>

class WallhackDetector {
public:
    WallhackDetector();
    
    void SetConfig(float maxPreAimMs, float wallBangSuspicionThreshold);
    
    float Analyze(PlayerProfile& player, const std::vector<PlayerProfile*>& enemies);

private:
    float m_maxPreAimMs;
    float m_wallBangSuspicionThreshold;
};
