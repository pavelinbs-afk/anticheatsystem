#include "players.h"

#include "plugin.h"
#include "schema.h"

#include <cmath>

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

	// Prefer dedicated player pawn — m_hPawn can be observer when dead/spec.
	CEntityHandle hPlayerPawn = Schema_Get<CEntityHandle>(pController, "CCSPlayerController", "m_hPlayerPawn");
	CEntityInstance* pPawn = g_pGameEntitySystem->GetEntityInstance(hPlayerPawn);
	if (pPawn)
		return pPawn;

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

	struct Vec3 { float x, y, z; };
	struct Ang { float pitch, yaw, roll; }; // QAngle: x=pitch, y=yaw, z=roll

	static bool s_loggedSchema = false;
	static int32_t s_offBody = -2;
	static int32_t s_offNode = -2;
	static int32_t s_offOrigin = -2;
	static int32_t s_offVAngle = -2;
	static int32_t s_offEye = -2;
	static int32_t s_offVel = -2;
	if (s_offBody == -2)
	{
		s_offBody = Schema_GetOffset("CBaseEntity", "m_CBodyComponent");
		s_offNode = Schema_GetOffset("CBodyComponent", "m_pSceneNode");
		s_offOrigin = Schema_GetOffset("CGameSceneNode", "m_vecAbsOrigin");
		s_offVAngle = Schema_GetOffset("CBasePlayerPawn", "v_angle");
		s_offEye = Schema_GetOffset("CCSPlayerPawn", "m_angEyeAngles");
		s_offVel = Schema_GetOffset("CBaseEntity", "m_vecAbsVelocity");
		if (!s_loggedSchema)
		{
			s_loggedSchema = true;
			AC_Log("schema offs: v_angle=%d eyePawn=%d body=%d sceneNode=%d absOrigin=%d playerPawn=%d",
				s_offVAngle, s_offEye, s_offBody, s_offNode, s_offOrigin,
				Schema_GetOffset("CCSPlayerController", "m_hPlayerPawn"));
		}
	}

	// Missing schema → refuse rather than dereference garbage.
	if (s_offBody < 0 || s_offNode < 0 || s_offOrigin < 0)
		return false;

	Vec3 origin{};
	void* pBody = Schema_Get<void*>(pPawn, "CBaseEntity", "m_CBodyComponent", nullptr);
	if (!pBody)
		return false;
	void* pNode = Schema_Get<void*>(pBody, "CBodyComponent", "m_pSceneNode", nullptr);
	if (!pNode)
		return false;
	origin = Schema_Get<Vec3>(pNode, "CGameSceneNode", "m_vecAbsOrigin");
	if (!std::isfinite(origin.x) || !std::isfinite(origin.y) || !std::isfinite(origin.z))
		return false;

	// Eye height — FOV from feet origin breaks pitch checks.
	// m_vecViewOffset is a networked quantized type; use standing fallback.
	const float eyeZ = 64.0f;

	if (outPos)
	{
		outPos[0] = origin.x;
		outPos[1] = origin.y;
		outPos[2] = origin.z + eyeZ;
	}

	if (outAngles)
	{
		Ang eye{};
		if (s_offVAngle >= 0)
			eye = Schema_Get<Ang>(pPawn, "CBasePlayerPawn", "v_angle");
		const bool vAngleZero = (eye.pitch == 0.0f && eye.yaw == 0.0f);

		if (vAngleZero && s_offEye >= 0)
		{
			Ang pawnEye = Schema_Get<Ang>(pPawn, "CCSPlayerPawn", "m_angEyeAngles");
			if (pawnEye.pitch != 0.0f || pawnEye.yaw != 0.0f)
				eye = pawnEye;
		}

		if (!std::isfinite(eye.pitch) || !std::isfinite(eye.yaw) || !std::isfinite(eye.roll))
			return false;

		outAngles[0] = eye.pitch;
		outAngles[1] = eye.yaw;
		outAngles[2] = eye.roll;
	}

	if (outVel)
	{
		Vec3 vel{};
		if (s_offVel >= 0)
			vel = Schema_Get<Vec3>(pPawn, "CBaseEntity", "m_vecAbsVelocity");
		if (!std::isfinite(vel.x) || !std::isfinite(vel.y) || !std::isfinite(vel.z))
			return false;
		outVel[0] = vel.x;
		outVel[1] = vel.y;
		outVel[2] = vel.z;
	}

	return true;
}
