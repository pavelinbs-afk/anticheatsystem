#include "admin_bridge.h"

#include "../plugin.h"

#include <cstdio>
#include <cstring>
#include <cstddef>
#include <deque>

namespace {

enum class BridgeKind : uint8_t
{
	Ban,
	Report,
	Warn,
	BackendCheck,
	DiscordNotify,
};

struct PendingBridge
{
	BridgeKind kind = BridgeKind::Report;
	uint64_t steamId = 0;
	char name[96]{};
	char ip[64]{};
	char map[96]{};
	char reason[96]{};
	float score = 0.0f;
	bool resend = false;
	int attemptsLeft = 0;
	float nextAt = 0.0f;
};

constexpr int kMaxPending = 48;
constexpr int kDefaultRetries = 2;      // initial send + 2 retries
constexpr float kRetryIntervalSec = 1.5f;

std::deque<PendingBridge> g_pending;

void EscapeForCommand(const char* in, char* out, size_t outSize)
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

const char* PluginVersion()
{
	const char* v = g_AntiCheatPlugin.GetVersion();
	return (v && v[0]) ? v : "0.0.0";
}

void FormatScoreInvariant(float score, char* out, size_t outSize)
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

void DispatchOne(const PendingBridge& p)
{
	if (!g_pEngine || p.steamId == 0)
		return;

	char scoreBuf[32];
	FormatScoreInvariant(p.score, scoreBuf, sizeof(scoreBuf));
	const char* ver = PluginVersion();
	char cmd[560];

	switch (p.kind)
	{
	case BridgeKind::Ban:
	{
		const char* mapArg = p.map[0] ? p.map : "?";
		const char* reason = p.reason[0] ? p.reason : "detect";
		if (p.name[0])
			std::snprintf(cmd, sizeof(cmd), "css_anticheat_apply_ban %llu \"%s\" %s \"%s\" \"%s\" \"%s\"\n",
				(unsigned long long)p.steamId, p.name, scoreBuf, mapArg, reason, ver);
		else
			std::snprintf(cmd, sizeof(cmd), "css_anticheat_apply_ban %llu %s \"%s\" \"%s\" \"%s\"\n",
				(unsigned long long)p.steamId, scoreBuf, mapArg, reason, ver);
		AC_Log("bridge ban -> %s", cmd);
		break;
	}
	case BridgeKind::Report:
		if (p.name[0])
			std::snprintf(cmd, sizeof(cmd), "css_anticheat_auto_report %llu \"%s\"\n",
				(unsigned long long)p.steamId, p.name);
		else
			std::snprintf(cmd, sizeof(cmd), "css_anticheat_auto_report %llu\n",
				(unsigned long long)p.steamId);
		AC_Log("bridge report -> %s", cmd);
		break;
	case BridgeKind::Warn:
		if (p.name[0])
			std::snprintf(cmd, sizeof(cmd), "css_anticheat_admin_warn %llu %s \"%s\"\n",
				(unsigned long long)p.steamId, scoreBuf, p.name);
		else
			std::snprintf(cmd, sizeof(cmd), "css_anticheat_admin_warn %llu %s\n",
				(unsigned long long)p.steamId, scoreBuf);
		AC_Log("bridge admin-warn -> %s", cmd);
		break;
	case BridgeKind::BackendCheck:
		if (p.name[0] && p.ip[0])
			std::snprintf(cmd, sizeof(cmd), "css_anticheat_backend_check %llu \"%s\" \"%s\"\n",
				(unsigned long long)p.steamId, p.ip, p.name);
		else if (p.ip[0])
			std::snprintf(cmd, sizeof(cmd), "css_anticheat_backend_check %llu \"%s\"\n",
				(unsigned long long)p.steamId, p.ip);
		else
			std::snprintf(cmd, sizeof(cmd), "css_anticheat_backend_check %llu\n",
				(unsigned long long)p.steamId);
		AC_Log("bridge backend-check -> %s", cmd);
		break;
	case BridgeKind::DiscordNotify:
	{
		const char* mapArg = p.map[0] ? p.map : "?";
		const char* reason = p.reason[0] ? p.reason : "detect";
		if (p.name[0])
			std::snprintf(cmd, sizeof(cmd), "css_anticheat_discord_notify %llu \"%s\" %s \"%s\" %d \"%s\" \"%s\"\n",
				(unsigned long long)p.steamId, p.name, scoreBuf, mapArg, p.resend ? 1 : 0, reason, ver);
		else
			std::snprintf(cmd, sizeof(cmd), "css_anticheat_discord_notify %llu %s \"%s\" %d \"%s\" \"%s\"\n",
				(unsigned long long)p.steamId, scoreBuf, mapArg, p.resend ? 1 : 0, reason, ver);
		AC_Log("bridge discord-notify -> %s", cmd);
		break;
	}
	default:
		return;
	}

	g_pEngine->ServerCommand(cmd);
}

void Enqueue(PendingBridge p, float curtime, int retries = kDefaultRetries)
{
	if (!g_pEngine || p.steamId == 0)
		return;

	// Immediate dispatch.
	DispatchOne(p);

	if (retries <= 0)
		return;

	p.attemptsLeft = retries;
	p.nextAt = curtime > 0.0f ? (curtime + kRetryIntervalSec) : kRetryIntervalSec;

	while ((int)g_pending.size() >= kMaxPending)
		g_pending.pop_front();
	g_pending.push_back(p);
}

} // namespace

void AdminBridge_Tick(float curtime)
{
	if (!g_pEngine || g_pending.empty())
		return;

	for (size_t i = 0; i < g_pending.size(); )
	{
		PendingBridge& p = g_pending[i];
		if (curtime + 0.001f < p.nextAt)
		{
			++i;
			continue;
		}

		DispatchOne(p);
		p.attemptsLeft--;
		if (p.attemptsLeft <= 0)
		{
			g_pending.erase(g_pending.begin() + (std::ptrdiff_t)i);
			continue;
		}
		p.nextAt = curtime + kRetryIntervalSec;
		++i;
	}
}

void AdminBridge_ApplyBan(uint64_t steamId, const char* playerName, float suspicionScore, const char* mapName, const char* detectReason)
{
	PendingBridge p;
	p.kind = BridgeKind::Ban;
	p.steamId = steamId;
	p.score = suspicionScore;
	EscapeForCommand(playerName, p.name, sizeof(p.name));
	EscapeForCommand(mapName, p.map, sizeof(p.map));
	EscapeForCommand(detectReason, p.reason, sizeof(p.reason));
	if (!p.reason[0])
		std::snprintf(p.reason, sizeof(p.reason), "detect");

	CGlobalVars* gv = GetGlobals();
	Enqueue(p, gv ? gv->curtime : 0.0f, kDefaultRetries);
}

void AdminBridge_SendReport(uint64_t steamId, const char* playerName)
{
	PendingBridge p;
	p.kind = BridgeKind::Report;
	p.steamId = steamId;
	EscapeForCommand(playerName, p.name, sizeof(p.name));
	CGlobalVars* gv = GetGlobals();
	Enqueue(p, gv ? gv->curtime : 0.0f, kDefaultRetries);
}

void AdminBridge_WarnAdmins(uint64_t steamId, const char* playerName, float score)
{
	PendingBridge p;
	p.kind = BridgeKind::Warn;
	p.steamId = steamId;
	p.score = score;
	EscapeForCommand(playerName, p.name, sizeof(p.name));
	CGlobalVars* gv = GetGlobals();
	Enqueue(p, gv ? gv->curtime : 0.0f, kDefaultRetries);
}

void AdminBridge_DiscordNotify(uint64_t steamId, const char* playerName, float suspicionScore, const char* mapName, bool resend, const char* detectReason)
{
	PendingBridge p;
	p.kind = BridgeKind::DiscordNotify;
	p.steamId = steamId;
	p.score = suspicionScore;
	p.resend = resend;
	EscapeForCommand(playerName, p.name, sizeof(p.name));
	EscapeForCommand(mapName, p.map, sizeof(p.map));
	EscapeForCommand(detectReason, p.reason, sizeof(p.reason));
	if (!p.reason[0])
		std::snprintf(p.reason, sizeof(p.reason), "detect");
	CGlobalVars* gv = GetGlobals();
	// Discord notify: one retry is enough (resend path is manual).
	Enqueue(p, gv ? gv->curtime : 0.0f, 1);
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

void AdminBridge_BackendCheck(uint64_t steamId, const char* clientIp, const char* playerName)
{
	PendingBridge p;
	p.kind = BridgeKind::BackendCheck;
	p.steamId = steamId;
	EscapeForCommand(clientIp, p.ip, sizeof(p.ip));
	EscapeForCommand(playerName, p.name, sizeof(p.name));
	CGlobalVars* gv = GetGlobals();
	Enqueue(p, gv ? gv->curtime : 0.0f, kDefaultRetries);
}
