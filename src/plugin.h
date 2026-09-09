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
	bool Load(PluginId id, ISmmAPI* ismm, char* error, size_t maxlen, bool late) override;
	bool Unload(char* error, size_t maxlen) override;

	const char* GetAuthor() override		{ return "g3raakl3 & pRfect"; }
	const char* GetName() override			{ return "AntiCheat"; }
	const char* GetDescription() override	{ return "CS2 server-side anticheat (MetaMod)"; }
	const char* GetURL() override			{ return ""; }
	const char* GetLicense() override		{ return "Proprietary"; }
	const char* GetVersion() override		{ return "1.0.3"; }
	const char* GetDate() override			{ return __DATE__; }
	const char* GetLogTag() override		{ return "ANTICHEAT"; }

public:
	void Hook_GameFrame(bool simulating, bool bFirstTick, bool bLastTick);
	void Hook_StartupServer(const GameSessionConfiguration_t& config, ISource2WorldSession*, const char*);
	int  Hook_LoadEventsFromFile(const char* filename, bool bSearchAll);
	void Hook_EntitySystemSpawn(int nCount, const EntitySpawnInfo_t* pInfo);
	void Hook_ClientPutInServer(CPlayerSlot slot, char const* pszName, int type, uint64 xuid);
	bool Hook_ClientConnect(CPlayerSlot slot, const char* pszName, uint64 xuid, const char* pszNetworkID, bool unk1, CBufferString* pRejectReason);
	void Hook_ClientDisconnect(CPlayerSlot slot, ENetworkDisconnectionReason reason, const char* pszName, uint64 xuid, const char* pszNetworkID);
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
void AC_LogCritical(const char* fmt, ...);
