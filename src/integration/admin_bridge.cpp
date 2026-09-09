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

void AdminBridge_ApplyBan(uint64_t steamId, const char* playerName)
{
	if (!g_pEngine || steamId == 0)
		return;

	char safeName[96];
	EscapeForCommand(playerName, safeName, sizeof(safeName));
	char cmd[256];
	if (safeName[0])
		std::snprintf(cmd, sizeof(cmd), "css_anticheat_apply_ban %llu \"%s\"\n",
			(unsigned long long)steamId, safeName);
	else
		std::snprintf(cmd, sizeof(cmd), "css_anticheat_apply_ban %llu\n",
			(unsigned long long)steamId);

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
	char cmd[320];
	if (safeName[0])
		std::snprintf(cmd, sizeof(cmd), "css_anticheat_admin_warn %llu %.1f \"%s\"\n",
			(unsigned long long)steamId, score, safeName);
	else
		std::snprintf(cmd, sizeof(cmd), "css_anticheat_admin_warn %llu %.1f\n",
			(unsigned long long)steamId, score);

	AC_Log("bridge admin-warn -> %s", cmd);
	g_pEngine->ServerCommand(cmd);
}
