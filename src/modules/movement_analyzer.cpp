#include "movement_analyzer.h"
#include "../plugin.h"

MovementAnalyzer::MovementAnalyzer()
	: m_maxVelocity(380.0f), m_maxTeleportDistance(250.0f)
{
}

void MovementAnalyzer::SetConfig(float maxVelocity, float maxTeleportDistance)
{
	m_maxVelocity = maxVelocity;
	m_maxTeleportDistance = maxTeleportDistance;
}

float MovementAnalyzer::Analyze(PlayerProfile& player, float deltaTime)
{
	float suspicionDelta = 0.0f;

	float speed = player.velocity.Length();
	float horiz = std::sqrt(player.velocity.x * player.velocity.x + player.velocity.y * player.velocity.y);

	if (!player.inAir && horiz > m_maxVelocity)
	{
		suspicionDelta += 3.0f;
		AC_Log("speedhack? horiz=%.0f steam=%llu", horiz, (unsigned long long)player.steamId);
	}

	float distanceMoved = VectorDistance(player.position, player.lastPosition);
	if (deltaTime > 0.0f && distanceMoved > m_maxTeleportDistance && (distanceMoved / deltaTime) > 2500.0f)
	{
		suspicionDelta += 6.0f;
		AC_Log("teleport? dist=%.0f steam=%llu", distanceMoved, (unsigned long long)player.steamId);
	}

	(void)speed;
	return suspicionDelta;
}
