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
	if (!s_loggedSchema)
	{
		s_loggedSchema = true;
		AC_Log("schema offs: v_angle=%d eyePawn=%d eyeCtrl=%d absOrigin=%d playerPawn=%d",
			Schema_GetOffset("CBasePlayerPawn", "v_angle"),
			Schema_GetOffset("CCSPlayerPawn", "m_angEyeAngles"),
			Schema_GetOffset("CCSPlayerController", "m_angEyeAngles"),
			Schema_GetOffset("CGameSceneNode", "m_vecAbsOrigin"),
			Schema_GetOffset("CCSPlayerController", "m_hPlayerPawn"));
	}

	Vec3 origin{};
	void* pNode = Schema_Get<void*>(pPawn, "CBaseEntity", "m_pGameSceneNode");
	if (pNode)
		origin = Schema_Get<Vec3>(pNode, "CGameSceneNode", "m_vecAbsOrigin");

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
		// v_angle = server view/cmd angles (best for aimbot snap detection).
		Ang eye = Schema_Get<Ang>(pPawn, "CBasePlayerPawn", "v_angle");
		const bool vAngleZero = (eye.pitch == 0.0f && eye.yaw == 0.0f);

		if (vAngleZero)
		{
			Ang pawnEye = Schema_Get<Ang>(pPawn, "CCSPlayerPawn", "m_angEyeAngles");
			if (pawnEye.pitch != 0.0f || pawnEye.yaw != 0.0f)
				eye = pawnEye;
			else
			{
				Ang ctrl = Schema_Get<Ang>(pController, "CCSPlayerController", "m_angEyeAngles");
				if (ctrl.pitch != 0.0f || ctrl.yaw != 0.0f)
					eye = ctrl;
			}
		}

		outAngles[0] = eye.pitch;
		outAngles[1] = eye.yaw;
		outAngles[2] = eye.roll;
	}

	if (outVel)
	{
		Vec3 vel = Schema_Get<Vec3>(pPawn, "CBaseEntity", "m_vecAbsVelocity");
		outVel[0] = vel.x;
		outVel[1] = vel.y;
		outVel[2] = vel.z;
	}

	return true;
}
