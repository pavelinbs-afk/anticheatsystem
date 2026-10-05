#pragma once

#include <ISmmPlugin.h>
#include <tier0/dbg.h>
#include <tier1/strtools.h>
#include <eiface.h>
#include <iserver.h>
#include <igameevents.h>
#include <icvar.h>
#include <entity2/entitysystem.h>

#define AC_MAXPLAYERS 64

class AntiCheatPlugin final : public ISmmPlugin, public IMetamodListener
{
public:
	AntiCheatPlugin();

	bool Load(PluginId id, ISmmAPI* ismm, char* error, size_t maxlen, bool late) override;
	bool Unload(char* error, size_t maxlen) override;

	const char* GetAuthor() override		{ return "g3raakl3 & pRfect"; }
	const char* GetName() override			{ return "AntiCheat"; }
	const char* GetDescription() override	{ return "CS2 server-side anticheat (MetaMod)"; }
	const char* GetURL() override			{ return ""; }
	const char* GetLicense() override		{ return "Proprietary"; }
	const char* GetVersion() override		{ return "1.2.3"; }
	const char* GetDate() override			{ return __DATE__; }
	const char* GetLogTag() override		{ return "ANTICHEAT"; }

public:
	KHook::Return<void> Hook_GameFrame(ISource2Server*, bool simulating, bool bFirstTick, bool bLastTick);
	KHook::Return<void> Hook_StartupServer(INetworkServerService*, const GameSessionConfiguration_t& config, ISource2WorldSession*, const char*);
	KHook::Return<int>  Hook_LoadEventsFromFile(IGameEventManager2* pThis, const char* filename, bool bSearchAll);
	KHook::Return<bool> Hook_FireEvent(IGameEventManager2* pThis, IGameEvent* event, bool bDontBroadcast);
	KHook::Return<void> Hook_EntitySystemSpawn(CEntitySystem* pThis, int nCount, const EntitySpawnInfo_t* pInfo);
	KHook::Return<void> Hook_ClientPutInServer(IServerGameClients*, CPlayerSlot slot, char const* pszName, int type, uint64 xuid);
	KHook::Return<void> Hook_ClientDisconnect(IServerGameClients*, CPlayerSlot slot, ENetworkDisconnectionReason reason, const char* pszName, uint64 xuid, const char* pszNetworkID);

protected:
	KHook::Virtual<ISource2Server, void, bool, bool, bool> m_GameFrame;
	KHook::Virtual<INetworkServerService, void, const GameSessionConfiguration_t&, ISource2WorldSession*, const char*> m_StartupServer;
	KHook::Virtual<IGameEventManager2, int, const char*, bool> m_LoadEventsFromFile;
	KHook::Virtual<IGameEventManager2, bool, IGameEvent*, bool> m_FireEvent;
	KHook::Virtual<CEntitySystem, void, int, const EntitySpawnInfo_t*> m_EntitySystemSpawn;
	KHook::Virtual<IServerGameClients, void, CPlayerSlot, char const*, int, uint64> m_ClientPutInServer;
	KHook::Virtual<IServerGameClients, void, CPlayerSlot, ENetworkDisconnectionReason, const char*, uint64, const char*> m_ClientDisconnect;
};

extern AntiCheatPlugin g_AntiCheatPlugin;

extern IVEngineServer2* g_pEngine;
extern ISource2Server* g_pServer;
extern IGameEventManager2* g_pGameEventManager;
extern INetworkServerService* g_pNetServerService;
extern CGameEntitySystem* g_pGameEntitySystem;

inline CGlobalVars* GetGlobals()
{
	return g_pEngine ? g_pEngine->GetServerGlobals() : nullptr;
}

void AC_Log(const char* fmt, ...);
void AC_LogDebug(const char* fmt, ...);
void AC_LogCritical(const char* fmt, ...);
void AC_SetDebugLog(bool enabled);
bool AC_IsDebugLogEnabled();
