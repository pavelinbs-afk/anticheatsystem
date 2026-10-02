#include "commands.h"

#include "plugin.h"
#include "anticheat_core.h"
#include "events.h"

#include <tier1/convar.h>
#include <icvar.h>
#include <ctime>
#include <cstdio>

#ifndef AC_GIT_COMMIT
#define AC_GIT_COMMIT "unknown"
#endif
#ifndef AC_GIT_BRANCH
#define AC_GIT_BRANCH "unknown"
#endif
#ifndef AC_GIT_COMMIT_DATE
#define AC_GIT_COMMIT_DATE "unknown"
#endif
#ifndef AC_GIT_COMMIT_SUBJECT
#define AC_GIT_COMMIT_SUBJECT "unknown"
#endif
#ifndef AC_BUILD_TIMESTAMP
#define AC_BUILD_TIMESTAMP __DATE__ " " __TIME__
#endif

void Commands_Register()
{
	// Static CON_COMMAND_F objects are queued until ConVar_Register runs.
	ConVar_Register(FCVAR_RELEASE | FCVAR_GAMEDLL);
}

static void FormatLocalNow(char* out, size_t outSize)
{
	if (!out || outSize == 0)
		return;
	std::time_t now = std::time(nullptr);
	std::tm tmLocal{};
#if defined(_WIN32)
	localtime_s(&tmLocal, &now);
#else
	localtime_r(&now, &tmLocal);
#endif
	std::strftime(out, outSize, "%Y-%m-%d %H:%M:%S %z", &tmLocal);
}

CON_COMMAND_F(ac_status, "AntiCheat status: version, webhook, backend, modules, git build info", FCVAR_RELEASE | FCVAR_GAMEDLL)
{
	(void)context;
	(void)args;

	char nowBuf[64];
	FormatLocalNow(nowBuf, sizeof(nowBuf));

	const char* map = "?";
	if (CGlobalVars* gv = GetGlobals())
	{
		const char* m = STRING(gv->mapname);
		if (m && m[0])
			map = m;
	}

	Msg("========== [anticheat] ac_status ==========\n");
	Msg("  plugin      : %s\n", g_AntiCheatPlugin.GetName());
	Msg("  version     : %s\n", g_AntiCheatPlugin.GetVersion());
	Msg("  author      : %s\n", g_AntiCheatPlugin.GetAuthor());
	Msg("  compile     : %s (plugin GetDate)\n", g_AntiCheatPlugin.GetDate());
	Msg("  build_ts    : %s\n", AC_BUILD_TIMESTAMP);
	Msg("  today       : %s\n", nowBuf);
	Msg("  git_branch  : %s\n", AC_GIT_BRANCH);
	Msg("  git_commit  : %s\n", AC_GIT_COMMIT);
	Msg("  git_date    : %s\n", AC_GIT_COMMIT_DATE);
	Msg("  git_subject : %s\n", AC_GIT_COMMIT_SUBJECT);
	Msg("  map         : %s\n", map);
	Msg("  interfaces  : engine=%d server=%d events_mgr=%d entsys=%d cvar=%d\n",
		g_pEngine ? 1 : 0,
		g_pServer ? 1 : 0,
		g_pGameEventManager ? 1 : 0,
		g_pGameEntitySystem ? 1 : 0,
		g_pCVar ? 1 : 0);
	Msg("  events_hook : %s\n", Events_AreRegistered() ? "OK (listeners attached)" : "PENDING / not hooked yet");
	Msg("  debug_log   : %s (ac_debug 0|1)\n", AC_IsDebugLogEnabled() ? "ON" : "OFF");

	if (AntiCheatCore* core = AntiCheatCore::GetInstance())
		core->PrintStatus();
	else
		Msg("  core        : NOT INITIALIZED\n");

	Msg("===========================================\n");
}

CON_COMMAND_F(ac_debug, "ac_debug <0|1> — toggle verbose anticheat debug log", FCVAR_RELEASE | FCVAR_GAMEDLL)
{
	(void)context;
	if (args.ArgC() < 2)
	{
		Msg("[anticheat] debug_log=%s (usage: ac_debug 0|1)\n",
			AC_IsDebugLogEnabled() ? "ON" : "OFF");
		return;
	}

	const char* v = args.Arg(1);
	const bool on = (v && (v[0] == '1' || v[0] == 't' || v[0] == 'T' || v[0] == 'y' || v[0] == 'Y'));
	AC_SetDebugLog(on);
	Msg("[anticheat] debug_log=%s\n", on ? "ON" : "OFF");
}
