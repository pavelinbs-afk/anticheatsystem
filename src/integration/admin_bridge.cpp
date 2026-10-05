#include "admin_bridge.h"

#include "../plugin.h"

#include <cstdio>

static void EscapeForCommand(const char* in, char* out, size_t outSize)
{
	if (!out || outSize == 0)
		return;
	size_t j = 0;
	if (!in)
	{
		out[0] = '\0';
		return;
	}
	for (size_t i = 0; in[i] && j + 2 < outSize; ++i)
	{
		char c = in[i];
		if (c == '"' || c == '\\' || c == ';' || c == '\n' || c == '\r')
			continue;
		out[j++] = c;
	}
	out[j] = '\0';
}

// AdminPlugin parses with CultureInfo.InvariantCulture — always emit '.' decimal.
static const char* PluginVersion()
{
	const char* v = g_AntiCheatPlugin.GetVersion();
	return (v && v[0]) ? v : "0.0.0";
}

static void FormatScoreInvariant(float score, char* out, size_t outSize)
{
	if (!out || outSize == 0)
		return;
	std::snprintf(out, outSize, "%.1f", score);
	for (char* p = out; *p; ++p)
	{
		if (*p == ',')
			*p = '.';
	}
}

void AdminBridge_ApplyBan(uint64_t steamId, const char* playerName, float suspicionScore, const char* mapName, const char* detectReason)
{
	if (!g_pEngine || steamId == 0)
		return;

	char safeName[96];
	EscapeForCommand(playerName, safeName, sizeof(safeName));
	char safeMap[96];
	EscapeForCommand(mapName, safeMap, sizeof(safeMap));
	char safeReason[96];
	EscapeForCommand(detectReason, safeReason, sizeof(safeReason));
	if (!safeReason[0])
		std::snprintf(safeReason, sizeof(safeReason), "detect");
	char scoreBuf[32];
	FormatScoreInvariant(suspicionScore, scoreBuf, sizeof(scoreBuf));
	const char* ver = PluginVersion();
	const char* mapArg = safeMap[0] ? safeMap : "?";

	char cmd[512];
	if (safeName[0])
		std::snprintf(cmd, sizeof(cmd), "css_anticheat_apply_ban %llu \"%s\" %s \"%s\" \"%s\" \"%s\"\n",
			(unsigned long long)steamId, safeName, scoreBuf, mapArg, safeReason, ver);
	else
		std::snprintf(cmd, sizeof(cmd), "css_anticheat_apply_ban %llu %s \"%s\" \"%s\" \"%s\"\n",
			(unsigned long long)steamId, scoreBuf, mapArg, safeReason, ver);

	AC_Log("bridge ban -> %s", cmd);
	g_pEngine->ServerCommand(cmd);
}

void AdminBridge_SendReport(uint64_t steamId, const char* playerName)
{
	if (!g_pEngine || steamId == 0)
		return;

	char safeName[96];
	EscapeForCommand(playerName, safeName, sizeof(safeName));
	char cmd[256];
	if (safeName[0])
		std::snprintf(cmd, sizeof(cmd), "css_anticheat_auto_report %llu \"%s\"\n",
			(unsigned long long)steamId, safeName);
	else
		std::snprintf(cmd, sizeof(cmd), "css_anticheat_auto_report %llu\n",
			(unsigned long long)steamId);

	AC_Log("bridge report -> %s", cmd);
	g_pEngine->ServerCommand(cmd);
}

void AdminBridge_WarnAdmins(uint64_t steamId, const char* playerName, float score)
{
	if (!g_pEngine || steamId == 0)
		return;

	char safeName[96];
	EscapeForCommand(playerName, safeName, sizeof(safeName));
	char scoreBuf[32];
	FormatScoreInvariant(score, scoreBuf, sizeof(scoreBuf));
	char cmd[320];
	if (safeName[0])
		std::snprintf(cmd, sizeof(cmd), "css_anticheat_admin_warn %llu %s \"%s\"\n",
			(unsigned long long)steamId, scoreBuf, safeName);
	else
		std::snprintf(cmd, sizeof(cmd), "css_anticheat_admin_warn %llu %s\n",
			(unsigned long long)steamId, scoreBuf);

	AC_Log("bridge admin-warn -> %s", cmd);
	g_pEngine->ServerCommand(cmd);
}

void AdminBridge_DiscordNotify(uint64_t steamId, const char* playerName, float suspicionScore, const char* mapName, bool resend, const char* detectReason)
{
	if (!g_pEngine || steamId == 0)
		return;

	char safeName[96];
	EscapeForCommand(playerName, safeName, sizeof(safeName));
	char safeMap[96];
	EscapeForCommand(mapName, safeMap, sizeof(safeMap));
	char safeReason[96];
	EscapeForCommand(detectReason, safeReason, sizeof(safeReason));
	if (!safeReason[0])
		std::snprintf(safeReason, sizeof(safeReason), "detect");
	char scoreBuf[32];
	FormatScoreInvariant(suspicionScore, scoreBuf, sizeof(scoreBuf));

	const char* mapArg = safeMap[0] ? safeMap : "?";
	const char* ver = PluginVersion();
	char cmd[540];
	if (safeName[0])
		std::snprintf(cmd, sizeof(cmd), "css_anticheat_discord_notify %llu \"%s\" %s \"%s\" %d \"%s\" \"%s\"\n",
			(unsigned long long)steamId, safeName, scoreBuf, mapArg, resend ? 1 : 0, safeReason, ver);
	else
		std::snprintf(cmd, sizeof(cmd), "css_anticheat_discord_notify %llu %s \"%s\" %d \"%s\" \"%s\"\n",
			(unsigned long long)steamId, scoreBuf, mapArg, resend ? 1 : 0, safeReason, ver);

	AC_Log("bridge discord-notify -> %s", cmd);
	g_pEngine->ServerCommand(cmd);
}

void AdminBridge_DiscordTest()
{
	if (!g_pEngine)
		return;
	char cmd[160];
	std::snprintf(cmd, sizeof(cmd), "css_anticheat_discord_test \"%s\"\n", PluginVersion());
	AC_Log("bridge discord-test -> %s", cmd);
	g_pEngine->ServerCommand(cmd);
}
