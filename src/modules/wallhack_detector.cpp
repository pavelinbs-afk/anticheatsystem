#include "wallhack_detector.h"
#include "../plugin.h"

WallhackDetector::WallhackDetector()
	: m_maxPreAimMs(50.0f), m_wallBangSuspicionThreshold(15.0f)
{
}

void WallhackDetector::SetConfig(float maxPreAimMs, float wallBangSuspicionThreshold)
{
	m_maxPreAimMs = maxPreAimMs;
	m_wallBangSuspicionThreshold = wallBangSuspicionThreshold;
}

float WallhackDetector::Analyze(PlayerProfile& player, const std::vector<PlayerProfile*>& enemies)
{
	// Without engine visibility traces this detector is too noisy — return 0.
	// Kept for future EngineTrace integration.
	(void)player;
	(void)enemies;
	(void)m_maxPreAimMs;
	(void)m_wallBangSuspicionThreshold;
	return 0.0f;
}
