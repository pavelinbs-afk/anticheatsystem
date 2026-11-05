#include "movement_analyzer.h"
#include "../plugin.h"

namespace {
constexpr float kGraceAfterTeamOrSpawnSec = 3.0f;
constexpr float kMinValidCoord = 1.0f;
constexpr int kSpeedMinStreak = 8;   // ~0.12s sustained before first score
constexpr int kSpeedScoreEvery = 16; // cadence while still overspeeding
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
	if (!alive || teamNum < 2 || teamNum > 3)
	{
		player.samplesValid = false;
		player.lastTeamNum = teamNum;
		player.wallAimStreak = 0;
		player.movementSpeedStreak = 0;
		return 0.0f;
	}

	if (player.lastTeamNum != 0 && player.lastTeamNum != teamNum)
	{
		player.movementIgnoreUntil = curtime + kGraceAfterTeamOrSpawnSec;
		player.samplesValid = false;
		player.lastPosition = player.position;
		player.movementSpeedStreak = 0;
		AC_Log("movement grace (team %d->%d) steam=%llu",
			player.lastTeamNum, teamNum, (unsigned long long)player.steamId);
	}
	player.lastTeamNum = teamNum;

	if (curtime < player.movementIgnoreUntil)
	{
		player.lastPosition = player.position;
		player.samplesValid = false;
		player.movementSpeedStreak = 0;
		return 0.0f;
	}

	const float posLen = player.position.Length();
	const float lastLen = player.lastPosition.Length();
	if (posLen < kMinValidCoord || lastLen < kMinValidCoord)
	{
		player.samplesValid = false;
		player.movementSpeedStreak = 0;
		return 0.0f;
	}

	if (!player.samplesValid)
	{
		player.samplesValid = true;
		player.lastPosition = player.position;
		player.movementSpeedStreak = 0;
		return 0.0f;
	}

	float suspicionDelta = 0.0f;
	float horiz = std::sqrt(player.velocity.x * player.velocity.x + player.velocity.y * player.velocity.y);

	if (!player.inAir && horiz > m_maxVelocity)
	{
		player.movementSpeedStreak++;
		if (player.movementSpeedStreak >= kSpeedMinStreak &&
			((player.movementSpeedStreak - kSpeedMinStreak) % kSpeedScoreEvery) == 0)
		{
			suspicionDelta += 2.0f;
			AC_Log("speedhack? horiz=%.0f streak=%d thresh=%.0f steam=%llu +2",
				horiz, player.movementSpeedStreak, m_maxVelocity,
				(unsigned long long)player.steamId);
		}
	}
	else
	{
		player.movementSpeedStreak = 0;
	}

	float distanceMoved = VectorDistance(player.position, player.lastPosition);
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
