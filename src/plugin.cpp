#include "plugin.h"

#include <schemasystem/schemasystem.h>
#include <interfaces/interfaces.h>
#include <cstdarg>

#include "events.h"
#include "anticheat_core.h"
#include "vtable_finder.h"
#include "players.h"

AntiCheatPlugin g_AntiCheatPlugin;
PLUGIN_EXPOSE(AntiCheatPlugin, g_AntiCheatPlugin);

IVEngineServer2* g_pEngine = nullptr;
ISource2Server* g_pServer = nullptr;
IGameEventManager2* g_pGameEventManager = nullptr;
INetworkServerService* g_pNetServerService = nullptr;
CGameEntitySystem* g_pGameEntitySystem = nullptr;

CGameEntitySystem* GameEntitySystem()
{
	return g_pGameEntitySystem;
}

static int g_iEventMgrHookId = 0;
static int g_iFireEventHookId = 0;
static int g_iEntSysHookId = 0;

#ifdef _WIN32
#define SERVER_LIB "server.dll"
#else
#define SERVER_LIB "/libserver.so"
#endif

class GameSessionConfiguration_t
{
};

SH_DECL_HOOK3_void(IServerGameDLL, GameFrame, SH_NOATTRIB, 0, bool, bool, bool);
SH_DECL_HOOK3_void(INetworkServerService, StartupServer, SH_NOATTRIB, 0, const GameSessionConfiguration_t&, ISource2WorldSession*, const char*);
SH_DECL_HOOK2(IGameEventManager2, LoadEventsFromFile, SH_NOATTRIB, 0, int, const char*, bool);
SH_DECL_HOOK2(IGameEventManager2, FireEvent, SH_NOATTRIB, 0, bool, IGameEvent*, bool);
SH_DECL_HOOK2_void(CEntitySystem, Spawn, SH_NOATTRIB, 0, int, const EntitySpawnInfo_t*);
SH_DECL_HOOK4_void(IServerGameClients, ClientPutInServer, SH_NOATTRIB, 0, CPlayerSlot, char const*, int, uint64);
SH_DECL_HOOK5_void(IServerGameClients, ClientDisconnect, SH_NOATTRIB, 0, CPlayerSlot, ENetworkDisconnectionReason, const char*, uint64, const char*);

static IServerGameClients* g_pGameClients = nullptr;

void AC_Log(const char* fmt, ...)
{
	char buf[512];
	va_list va;
	va_start(va, fmt);
	V_vsnprintf(buf, sizeof(buf), fmt, va);
	va_end(va);
	ConColorMsg(Color(120, 200, 120, 255), "[anticheat] %s\n", buf);
}

void AC_LogCritical(const char* fmt, ...)
{
	char buf[512];
	va_list va;
	va_start(va, fmt);
	V_vsnprintf(buf, sizeof(buf), fmt, va);
	va_end(va);
	ConColorMsg(Color(255, 80, 80, 255), "[anticheat][CRITICAL] %s\n", buf);
}

bool AntiCheatPlugin::Load(PluginId id, ISmmAPI* ismm, char* error, size_t maxlen, bool late)
{
	PLUGIN_SAVEVARS();

	GET_V_IFACE_CURRENT(GetEngineFactory, g_pCVar, ICvar, CVAR_INTERFACE_VERSION);
	GET_V_IFACE_ANY(GetEngineFactory, g_pSchemaSystem, ISchemaSystem, SCHEMASYSTEM_INTERFACE_VERSION);
	GET_V_IFACE_CURRENT(GetEngineFactory, g_pEngine, IVEngineServer2, SOURCE2ENGINETOSERVER_INTERFACE_VERSION);
	GET_V_IFACE_CURRENT(GetServerFactory, g_pServer, ISource2Server, SOURCE2SERVER_INTERFACE_VERSION);
	GET_V_IFACE_CURRENT(GetEngineFactory, g_pNetServerService, INetworkServerService, NETWORKSERVERSERVICE_INTERFACE_VERSION);
	GET_V_IFACE_ANY(GetServerFactory, g_pGameClients, IServerGameClients, SOURCE2GAMECLIENTS_INTERFACE_VERSION);

	g_SMAPI->AddListener(this, this);

	SH_ADD_HOOK(IServerGameDLL, GameFrame, g_pServer, SH_MEMBER(this, &AntiCheatPlugin::Hook_GameFrame), true);
	SH_ADD_HOOK(INetworkServerService, StartupServer, g_pNetServerService, SH_MEMBER(this, &AntiCheatPlugin::Hook_StartupServer), true);
	SH_ADD_HOOK(IServerGameClients, ClientPutInServer, g_pGameClients, SH_MEMBER(this, &AntiCheatPlugin::Hook_ClientPutInServer), true);
	SH_ADD_HOOK(IServerGameClients, ClientDisconnect, g_pGameClients, SH_MEMBER(this, &AntiCheatPlugin::Hook_ClientDisconnect), true);

	if (void* pEventMgrVtbl = FindVirtualTable(SERVER_LIB, "CGameEventManager"))
	{
		auto* pMgrAsIface = reinterpret_cast<IGameEventManager2*>(pEventMgrVtbl);
		g_iEventMgrHookId = SH_ADD_DVPHOOK(IGameEventManager2, LoadEventsFromFile,
			pMgrAsIface, SH_MEMBER(this, &AntiCheatPlugin::Hook_LoadEventsFromFile), false);
		// Mid-map / late load: LoadEventsFromFile may never run again — capture mgr on FireEvent.
		g_iFireEventHookId = SH_ADD_DVPHOOK(IGameEventManager2, FireEvent,
			pMgrAsIface, SH_MEMBER(this, &AntiCheatPlugin::Hook_FireEvent), false);
	}
	else
	{
		V_strncpy(error, "Failed to locate CGameEventManager vtable", maxlen);
		return false;
	}

	if (void* pEntSysVtbl = FindVirtualTable(SERVER_LIB, "CGameEntitySystem"))
	{
		g_iEntSysHookId = SH_ADD_DVPHOOK(CEntitySystem, Spawn,
			reinterpret_cast<CEntitySystem*>(pEntSysVtbl),
			SH_MEMBER(this, &AntiCheatPlugin::Hook_EntitySystemSpawn), true);
	}
	else
	{
		V_strncpy(error, "Failed to locate CGameEntitySystem vtable", maxlen);
		return false;
	}

	if (!AntiCheatCore::GetInstance()->Initialize())
	{
		V_strncpy(error, "AntiCheatCore init failed", maxlen);
		return false;
	}

	AC_Log("loaded %s v%s (%s)", GetName(), GetVersion(), GetDate());
	META_CONPRINTF("[%s] Loaded %s v%s\n", GetLogTag(), GetName(), GetVersion());
	return true;
}

bool AntiCheatPlugin::Unload(char* error, size_t maxlen)
{
	Events_Unregister();
	AntiCheatCore::GetInstance()->Shutdown();

	SH_REMOVE_HOOK(IServerGameDLL, GameFrame, g_pServer, SH_MEMBER(this, &AntiCheatPlugin::Hook_GameFrame), true);
	SH_REMOVE_HOOK(INetworkServerService, StartupServer, g_pNetServerService, SH_MEMBER(this, &AntiCheatPlugin::Hook_StartupServer), true);
	SH_REMOVE_HOOK(IServerGameClients, ClientPutInServer, g_pGameClients, SH_MEMBER(this, &AntiCheatPlugin::Hook_ClientPutInServer), true);
	SH_REMOVE_HOOK(IServerGameClients, ClientDisconnect, g_pGameClients, SH_MEMBER(this, &AntiCheatPlugin::Hook_ClientDisconnect), true);
	if (g_iEventMgrHookId)
		SH_REMOVE_HOOK_ID(g_iEventMgrHookId);
	if (g_iFireEventHookId)
		SH_REMOVE_HOOK_ID(g_iFireEventHookId);
	if (g_iEntSysHookId)
		SH_REMOVE_HOOK_ID(g_iEntSysHookId);

	AC_Log("unloaded");
	return true;
}

void AntiCheatPlugin::Hook_GameFrame(bool simulating, bool bFirstTick, bool bLastTick)
{
	Events_TryRegister();
	if (simulating)
		AntiCheatCore::GetInstance()->OnGameFrame();
}

void AntiCheatPlugin::Hook_StartupServer(const GameSessionConfiguration_t&, ISource2WorldSession*, const char*)
{
	Events_OnStartupServer();
}

int AntiCheatPlugin::Hook_LoadEventsFromFile(const char* filename, bool bSearchAll)
{
	(void)filename;
	(void)bSearchAll;
	if (!g_pGameEventManager)
		g_pGameEventManager = META_IFACEPTR(IGameEventManager2);
	RETURN_META_VALUE(MRES_IGNORED, 0);
}

bool AntiCheatPlugin::Hook_FireEvent(IGameEvent* event, bool bDontBroadcast)
{
	(void)event;
	(void)bDontBroadcast;
	if (!g_pGameEventManager)
	{
		g_pGameEventManager = META_IFACEPTR(IGameEventManager2);
		if (g_pGameEventManager)
			AC_Log("game event manager captured via FireEvent");
	}
	RETURN_META_VALUE(MRES_IGNORED, true);
}

void AntiCheatPlugin::Hook_EntitySystemSpawn(int nCount, const EntitySpawnInfo_t* pInfo)
{
	if (!g_pGameEntitySystem)
		g_pGameEntitySystem = reinterpret_cast<CGameEntitySystem*>(META_IFACEPTR(CEntitySystem));
}

void AntiCheatPlugin::Hook_ClientPutInServer(CPlayerSlot slot, char const* pszName, int type, uint64 xuid)
{
	(void)type;
	int iSlot = slot.Get();
	if (iSlot < 0 || iSlot >= AC_MAXPLAYERS)
		return;
	if (!xuid)
		return;
	AntiCheatCore::GetInstance()->OnPlayerConnect(iSlot, xuid, pszName ? pszName : "");
}

void AntiCheatPlugin::Hook_ClientDisconnect(CPlayerSlot slot, ENetworkDisconnectionReason, const char*, uint64 xuid, const char*)
{
	int iSlot = slot.Get();
	if (iSlot < 0 || iSlot >= AC_MAXPLAYERS)
		return;
	AntiCheatCore::GetInstance()->OnPlayerDisconnect(iSlot, xuid);
}
