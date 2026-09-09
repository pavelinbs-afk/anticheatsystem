#include "movement_analyzer.h"
#include "../plugin.h"

namespace {
// CS2 can briefly exceed 250 via jumps/boosts; rage speedhacks are usually >> 500.
// Keep threshold high so knife run / scout / boosts don't trip it.
constexpr float kGraceAfterTeamOrSpawnSec = 3.0f;
constexpr float kMinValidCoord = 1.0f;
}

MovementAnalyzer::MovementAnalyzer()
	: m_maxVelocity(520.0f), m_maxTeleportDistance(800.0f)
{
}

void MovementAnalyzer::SetConfig(float maxVelocity, float maxTeleportDistance)
{
	m_maxVelocity = maxVelocity > 0.0f ? maxVelocity : 520.0f;
	m_maxTeleportDistance = maxTeleportDistance > 0.0f ? maxTeleportDistance : 800.0f;
}

float MovementAnalyzer::Analyze(PlayerProfile& player, float deltaTime, float curtime, int teamNum, bool alive)
{
	// Spectators / unassigned / dead: never score movement (team switch often goes T→spec→CT).
	if (!alive || teamNum < 2 || teamNum > 3)
	{
		player.samplesValid = false;
		player.lastTeamNum = teamNum;
		player.wallAimStreak = 0;
		return 0.0f;
	}

	// Team change / first valid spawn → reset baseline, grace window (SMAC-style).
	if (player.lastTeamNum != 0 && player.lastTeamNum != teamNum)
	{
		player.movementIgnoreUntil = curtime + kGraceAfterTeamOrSpawnSec;
		player.samplesValid = false;
		player.lastPosition = player.position;
		AC_Log("movement grace (team %d->%d) steam=%llu",
			player.lastTeamNum, teamNum, (unsigned long long)player.steamId);
	}
	player.lastTeamNum = teamNum;

	if (curtime < player.movementIgnoreUntil)
	{
		player.lastPosition = player.position;
		player.samplesValid = false;
		return 0.0f;
	}

	// Invalid / uninitialized origins (common right after spawn / team switch).
	const float posLen = player.position.Length();
	const float lastLen = player.lastPosition.Length();
	if (posLen < kMinValidCoord || lastLen < kMinValidCoord)
	{
		player.samplesValid = false;
		return 0.0f;
	}

	if (!player.samplesValid)
	{
		player.samplesValid = true;
		player.lastPosition = player.position;
		return 0.0f;
	}

	float suspicionDelta = 0.0f;
	float horiz = std::sqrt(player.velocity.x * player.velocity.x + player.velocity.y * player.velocity.y);

	// Require sustained absurd speed (rage), not a single boost frame.
	if (!player.inAir && horiz > m_maxVelocity)
	{
		suspicionDelta += 2.0f;
		AC_Log("speedhack? horiz=%.0f thresh=%.0f steam=%llu",
			horiz, m_maxVelocity, (unsigned long long)player.steamId);
	}

	float distanceMoved = VectorDistance(player.position, player.lastPosition);
	// Teleport: only when distance is extreme for one tick AND not during grace.
	// Team change / round spawn can move thousands of units — already covered by grace.
	if (deltaTime > 0.001f && distanceMoved > m_maxTeleportDistance)
	{
		const float speedEst = distanceMoved / deltaTime;
		if (speedEst > 4000.0f)
		{
			suspicionDelta += 4.0f;
			AC_Log("teleport? dist=%.0f est=%.0f steam=%llu",
				distanceMoved, speedEst, (unsigned long long)player.steamId);
		}
	}

	return suspicionDelta;
}
