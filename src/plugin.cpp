#include "plugin.h"

#include <schemasystem/schemasystem.h>
#include <interfaces/interfaces.h>
#include <cstdarg>

#include "events.h"
#include "anticheat_core.h"
#include "vtable_finder.h"
#include "players.h"
#include "commands.h"

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

static void* g_pEventMgrVtbl = nullptr;
static void* g_pEntSysVtbl = nullptr;

#ifdef _WIN32
#define SERVER_LIB "server.dll"
#else
#define SERVER_LIB "/libserver.so"
#endif

// Engine only passes this by reference; KHook needs a complete type for sizeof.
class GameSessionConfiguration_t
{
};

template <typename CLASS, typename RETURN, typename... ARGS>
static void AddGlobalByVtbl(KHook::Virtual<CLASS, RETURN, ARGS...>& hook, void* vtbl)
{
	struct { void* v; } fake{ vtbl };
	hook.AddGlobal(reinterpret_cast<CLASS*>(&fake));
}

template <typename CLASS, typename RETURN, typename... ARGS>
static void RemoveGlobalByVtbl(KHook::Virtual<CLASS, RETURN, ARGS...>& hook, void* vtbl)
{
	if (!vtbl)
		return;
	struct { void* v; } fake{ vtbl };
	hook.RemoveGlobal(reinterpret_cast<CLASS*>(&fake));
}

static IServerGameClients* g_pGameClients = nullptr;

AntiCheatPlugin::AntiCheatPlugin() :
	m_GameFrame(&ISource2Server::GameFrame, this, nullptr, &AntiCheatPlugin::Hook_GameFrame),
	m_StartupServer(&INetworkServerService::StartupServer, this, nullptr, &AntiCheatPlugin::Hook_StartupServer),
	m_LoadEventsFromFile(&IGameEventManager2::LoadEventsFromFile, this, &AntiCheatPlugin::Hook_LoadEventsFromFile, nullptr),
	m_FireEvent(&IGameEventManager2::FireEvent, this, &AntiCheatPlugin::Hook_FireEvent, nullptr),
	m_EntitySystemSpawn(&CEntitySystem::Spawn, this, nullptr, &AntiCheatPlugin::Hook_EntitySystemSpawn),
	m_ClientPutInServer(&IServerGameClients::ClientPutInServer, this, nullptr, &AntiCheatPlugin::Hook_ClientPutInServer),
	m_ClientDisconnect(&IServerGameClients::ClientDisconnect, this, nullptr, &AntiCheatPlugin::Hook_ClientDisconnect)
{
}

static bool g_acDebugLog = false;

void AC_SetDebugLog(bool enabled)
{
	g_acDebugLog = enabled;
}

bool AC_IsDebugLogEnabled()
{
	return g_acDebugLog;
}

void AC_Log(const char* fmt, ...)
{
	char buf[512];
	va_list va;
	va_start(va, fmt);
	V_vsnprintf(buf, sizeof(buf), fmt, va);
	va_end(va);
	ConColorMsg(Color(120, 200, 120, 255), "[anticheat] %s\n", buf);
}

void AC_LogDebug(const char* fmt, ...)
{
	if (!g_acDebugLog)
		return;
	char buf[512];
	va_list va;
	va_start(va, fmt);
	V_vsnprintf(buf, sizeof(buf), fmt, va);
	va_end(va);
	ConColorMsg(Color(140, 160, 140, 255), "[anticheat][debug] %s\n", buf);
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

	m_GameFrame.Add(g_pServer);
	m_StartupServer.Add(g_pNetServerService);
	m_ClientPutInServer.Add(g_pGameClients);
	m_ClientDisconnect.Add(g_pGameClients);

	g_pEventMgrVtbl = FindVirtualTable(SERVER_LIB, "CGameEventManager");
	if (!g_pEventMgrVtbl)
	{
		V_strncpy(error, "Failed to locate CGameEventManager vtable", maxlen);
		return false;
	}
	AddGlobalByVtbl(m_LoadEventsFromFile, g_pEventMgrVtbl);
	// Mid-map / late load: LoadEventsFromFile may never run again — capture mgr on FireEvent.
	AddGlobalByVtbl(m_FireEvent, g_pEventMgrVtbl);

	g_pEntSysVtbl = FindVirtualTable(SERVER_LIB, "CGameEntitySystem");
	if (!g_pEntSysVtbl)
	{
		V_strncpy(error, "Failed to locate CGameEntitySystem vtable", maxlen);
		return false;
	}
	AddGlobalByVtbl(m_EntitySystemSpawn, g_pEntSysVtbl);

	if (!AntiCheatCore::GetInstance()->Initialize())
	{
		V_strncpy(error, "AntiCheatCore init failed", maxlen);
		return false;
	}

	Commands_Register();

	AC_Log("loaded %s v%s (%s)", GetName(), GetVersion(), GetDate());
	META_CONPRINTF("[%s] Loaded %s v%s\n", GetLogTag(), GetName(), GetVersion());
	return true;
}

bool AntiCheatPlugin::Unload(char* error, size_t maxlen)
{
	Events_Unregister();
	AntiCheatCore::GetInstance()->Shutdown();

	m_GameFrame.Remove(g_pServer);
	m_StartupServer.Remove(g_pNetServerService);
	m_ClientPutInServer.Remove(g_pGameClients);
	m_ClientDisconnect.Remove(g_pGameClients);
	RemoveGlobalByVtbl(m_LoadEventsFromFile, g_pEventMgrVtbl);
	RemoveGlobalByVtbl(m_FireEvent, g_pEventMgrVtbl);
	RemoveGlobalByVtbl(m_EntitySystemSpawn, g_pEntSysVtbl);
	g_pEventMgrVtbl = nullptr;
	g_pEntSysVtbl = nullptr;

	AC_Log("unloaded");
	return true;
}

KHook::Return<void> AntiCheatPlugin::Hook_GameFrame(ISource2Server*, bool simulating, bool bFirstTick, bool bLastTick)
{
	Events_TryRegister();
	if (simulating)
		AntiCheatCore::GetInstance()->OnGameFrame();
	return { KHook::Action::Ignore };
}

KHook::Return<void> AntiCheatPlugin::Hook_StartupServer(INetworkServerService*, const GameSessionConfiguration_t&, ISource2WorldSession*, const char*)
{
	Events_OnStartupServer();
	return { KHook::Action::Ignore };
}

KHook::Return<int> AntiCheatPlugin::Hook_LoadEventsFromFile(IGameEventManager2* pThis, const char* filename, bool bSearchAll)
{
	(void)filename;
	(void)bSearchAll;
	if (!g_pGameEventManager && pThis)
		g_pGameEventManager = pThis;
	return { KHook::Action::Ignore };
}

KHook::Return<bool> AntiCheatPlugin::Hook_FireEvent(IGameEventManager2* pThis, IGameEvent* event, bool bDontBroadcast)
{
	(void)event;
	(void)bDontBroadcast;
	if (!g_pGameEventManager && pThis)
	{
		g_pGameEventManager = pThis;
		AC_Log("game event manager captured via FireEvent");
	}
	return { KHook::Action::Ignore };
}

KHook::Return<void> AntiCheatPlugin::Hook_EntitySystemSpawn(CEntitySystem* pThis, int nCount, const EntitySpawnInfo_t* pInfo)
{
	(void)nCount;
	(void)pInfo;
	if (!g_pGameEntitySystem && pThis)
		g_pGameEntitySystem = reinterpret_cast<CGameEntitySystem*>(pThis);
	return { KHook::Action::Ignore };
}

KHook::Return<void> AntiCheatPlugin::Hook_ClientPutInServer(IServerGameClients*, CPlayerSlot slot, char const* pszName, int type, uint64 xuid)
{
	(void)type;
	int iSlot = slot.Get();
	if (iSlot >= 0 && iSlot < AC_MAXPLAYERS && xuid)
		AntiCheatCore::GetInstance()->OnPlayerConnect(iSlot, xuid, pszName ? pszName : "");
	return { KHook::Action::Ignore };
}

KHook::Return<void> AntiCheatPlugin::Hook_ClientDisconnect(IServerGameClients*, CPlayerSlot slot, ENetworkDisconnectionReason, const char*, uint64 xuid, const char*)
{
	int iSlot = slot.Get();
	if (iSlot >= 0 && iSlot < AC_MAXPLAYERS)
		AntiCheatCore::GetInstance()->OnPlayerDisconnect(iSlot, xuid);
	return { KHook::Action::Ignore };
}

static void AC_CopyBareMapName(const char* raw, char* out, size_t outSize)
{
	if (!out || outSize == 0)
		return;
	out[0] = '\0';
	if (!raw || !raw[0] || (raw[0] == '?' && !raw[1]))
		return;

	const char* start = raw;
	for (const char* p = raw; *p; ++p)
	{
		if (*p == '/' || *p == '\\')
			start = p + 1;
	}
	if (!start[0] || (start[0] == '?' && !start[1]))
		return;

	V_strncpy(out, start, outSize);
	size_t len = V_strlen(out);
	auto strip_ext = [&](const char* ext)
	{
		const size_t el = V_strlen(ext);
		if (len < el)
			return;
		bool ok = true;
		for (size_t i = 0; i < el; ++i)
		{
			char a = out[len - el + i];
			char b = ext[i];
			if (a >= 'A' && a <= 'Z')
				a = char(a - 'A' + 'a');
			if (a != b)
			{
				ok = false;
				break;
			}
		}
		if (ok)
		{
			out[len - el] = '\0';
			len -= el;
		}
	};
	strip_ext(".vpk");
	strip_ext(".bsp");
	if (!out[0] || (out[0] == '?' && !out[1]))
		out[0] = '\0';
}

void AC_ResolveMapName(char* out, size_t outSize)
{
	if (!out || outSize == 0)
		return;
	out[0] = '\0';

	if (g_pNetServerService)
	{
		if (CNetworkGameServerBase* srv = g_pNetServerService->GetIGameServer())
		{
			AC_CopyBareMapName(srv->GetMapName(), out, outSize);
			if (out[0])
				return;
		}
	}

	if (CGlobalVars* gv = GetGlobals())
		AC_CopyBareMapName(STRING(gv->mapname), out, outSize);
}
