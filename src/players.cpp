#include "players.h"

#include "plugin.h"
#include "schema.h"

CEntityInstance* GetControllerBySlot(int iSlot)
{
	if (!g_pGameEntitySystem || iSlot < 0 || iSlot >= AC_MAXPLAYERS)
		return nullptr;
	return g_pGameEntitySystem->GetEntityInstance(CEntityIndex(iSlot + 1));
}

CEntityInstance* GetPawnBySlot(int iSlot)
{
	CEntityInstance* pController = GetControllerBySlot(iSlot);
	if (!pController || !g_pGameEntitySystem)
		return nullptr;

	CEntityHandle hPawn = Schema_Get<CEntityHandle>(pController, "CBasePlayerController", "m_hPawn");
	return g_pGameEntitySystem->GetEntityInstance(hPawn);
}

int GetPlayerTeamNum(int iSlot)
{
	CEntityInstance* pController = GetControllerBySlot(iSlot);
	if (!pController)
		return 0;
	return Schema_Get<uint8_t>(pController, "CBaseEntity", "m_iTeamNum");
}

bool IsPlayerAlive(int iSlot)
{
	CEntityInstance* pPawn = GetPawnBySlot(iSlot);
	if (!pPawn)
		return false;
	return Schema_Get<int32_t>(pPawn, "CBaseEntity", "m_iHealth") > 0;
}

const char* GetPlayerNameSafe(int iSlot)
{
	(void)iSlot;
	return "";
}

bool SamplePlayerState(int iSlot, float* outPos, float* outAngles, float* outVel)
{
	CEntityInstance* pPawn = GetPawnBySlot(iSlot);
	CEntityInstance* pController = GetControllerBySlot(iSlot);
	if (!pPawn || !pController)
		return false;

	if (outPos)
	{
		void* pNode = Schema_Get<void*>(pPawn, "CBaseEntity", "m_pGameSceneNode");
		if (pNode)
		{
			struct Vec3 { float x, y, z; };
			Vec3 abs = Schema_Get<Vec3>(pNode, "CGameSceneNode", "m_vecAbsOrigin");
			outPos[0] = abs.x;
			outPos[1] = abs.y;
			outPos[2] = abs.z;
		}
	}

	if (outAngles)
	{
		struct Ang { float pitch, yaw, roll; };
		Ang eye = Schema_Get<Ang>(pController, "CCSPlayerController", "m_angEyeAngles");
		outAngles[0] = eye.pitch;
		outAngles[1] = eye.yaw;
		outAngles[2] = eye.roll;
	}

	if (outVel)
	{
		struct Vec3 { float x, y, z; };
		Vec3 vel = Schema_Get<Vec3>(pPawn, "CBaseEntity", "m_vecAbsVelocity");
		outVel[0] = vel.x;
		outVel[1] = vel.y;
		outVel[2] = vel.z;
	}

	return true;
}
