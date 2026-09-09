#include "events.h"

#include <igameevents.h>
#include <cstring>

#include "plugin.h"
#include "anticheat_core.h"

static bool s_bRegistered = false;
static int s_iRetryThrottle = 0;
static int s_iBadSlotLogs = 0;

static const char* s_EventNames[] = {
	"player_death",
	"player_hurt",
	"weapon_fire",
	"round_start",
	"round_end",
};

static int SlotFromController(CEntityInstance* ctrl)
{
	if (!ctrl)
		return -1;
	const int idx = ctrl->GetEntityIndex().Get();
	// Player controllers live at entity indices 1..AC_MAXPLAYERS
	if (idx >= 1 && idx <= AC_MAXPLAYERS)
		return idx - 1;
	return -1;
}

static int EventSlot(IGameEvent* event, const char* key)
{
	if (!event || !key)
		return -1;

	// Most reliable in CS2: resolve controller entity → slot.
	int slot = SlotFromController(event->GetPlayerController(key));
	if (slot >= 0)
		return slot;

	CPlayerSlot ps = event->GetPlayerSlot(key);
	int i = ps.Get();
	if (i >= 0 && i < AC_MAXPLAYERS)
		return i;

	// Last resort: some builds store slot+1 in the int field (NOT growing userid).
	int maybe = event->GetInt(key);
	if (maybe >= 1 && maybe <= AC_MAXPLAYERS)
		return maybe - 1;

	if (s_iBadSlotLogs < 20)
	{
		++s_iBadSlotLogs;
		AC_Log("event slot fail key=%s GetInt=%d (events may not map players)", key, maybe);
	}
	return -1;
}

class ACEventListener final : public IGameEventListener2
{
public:
	void FireGameEvent(IGameEvent* event) override
	{
		if (!event)
			return;

		const char* name = event->GetName();
		if (!name)
			return;

		AntiCheatCore* core = AntiCheatCore::GetInstance();

		if (!strcmp(name, "player_death"))
		{
			int attacker = EventSlot(event, "attacker");
			int victim = EventSlot(event, "userid");
			bool hs = event->GetBool("headshot");
			bool thrusmoke = event->GetBool("thrusmoke");
			bool attackerblind = event->GetBool("attackerblind");
			bool noscope = event->GetBool("noscope");
			int penetrated = event->GetInt("penetrated");
			core->OnPlayerDeath(attacker, victim, hs, thrusmoke, attackerblind, noscope, penetrated);
			return;
		}

		if (!strcmp(name, "player_hurt"))
		{
			int attacker = EventSlot(event, "attacker");
			int victim = EventSlot(event, "userid");
			float dmg = event->GetFloat("dmg_health");
			int hitgroup = event->GetInt("hitgroup");
			core->OnPlayerHurt(attacker, victim, dmg, hitgroup);
			return;
		}

		if (!strcmp(name, "weapon_fire"))
		{
			int shooter = EventSlot(event, "userid");
			core->OnWeaponFire(shooter);
			return;
		}

		if (!strcmp(name, "round_start"))
		{
			core->OnRoundStart();
			return;
		}

		if (!strcmp(name, "round_end"))
		{
			core->OnRoundEnd();
			return;
		}
	}
};

static ACEventListener s_Listener;

void Events_TryRegister()
{
	if (s_bRegistered || !g_pGameEventManager)
		return;
	if (s_iRetryThrottle++ % 64 != 0)
		return;

	bool allOk = true;
	for (const char* name : s_EventNames)
	{
		if (g_pGameEventManager->FindListener(&s_Listener, name))
			continue;
		if (!g_pGameEventManager->AddListener(&s_Listener, name, true))
			allOk = false;
	}

	if (allOk)
	{
		s_bRegistered = true;
		AC_Log("game events hooked");
	}
}

void Events_Unregister()
{
	if (g_pGameEventManager)
		g_pGameEventManager->RemoveListener(&s_Listener);
	s_bRegistered = false;
}

void Events_OnStartupServer()
{
	s_bRegistered = false;
	s_iRetryThrottle = 0;
	s_iBadSlotLogs = 0;
}
