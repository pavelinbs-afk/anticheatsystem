#include "events.h"

#include <igameevents.h>
#include <cstring>

#include "plugin.h"
#include "anticheat_core.h"

static bool s_bRegistered = false;
static int s_iRetryThrottle = 0;

static const char* s_EventNames[] = {
	"player_death",
	"player_hurt",
	"weapon_fire",
	"round_start",
	"round_end",
};

static int EventSlot(IGameEvent* event, const char* key)
{
	CPlayerSlot slot = event->GetPlayerSlot(key);
	int i = slot.Get();
	return (i >= 0 && i < AC_MAXPLAYERS) ? i : -1;
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
			core->OnPlayerHurt(attacker, victim, dmg);
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
}
