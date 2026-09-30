#include "discord_webhook.h"

#include "../plugin.h"

#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <thread>
#include <unistd.h>

void DiscordWebhook::SetWebhookUrl(const std::string& url)
{
	std::string cleaned = url;
	while (!cleaned.empty() && (cleaned.back() == ' ' || cleaned.back() == '\t' || cleaned.back() == '\r' || cleaned.back() == '\n'))
		cleaned.pop_back();

	m_webhookUrl = cleaned;
	m_enabled = !m_webhookUrl.empty() &&
		(m_webhookUrl.find("https://discord.com/api/webhooks/") == 0 ||
		 m_webhookUrl.find("https://discordapp.com/api/webhooks/") == 0);

	if (m_webhookUrl.empty())
	{
		m_statusLabel = "off (empty url)";
	}
	else if (!m_enabled)
	{
		m_statusLabel = "INVALID (need https://discord.com/api/webhooks/...)";
	}
	else
	{
		// Mask token: show webhook id only.
		std::string id = "?";
		const char* prefixA = "https://discord.com/api/webhooks/";
		const char* prefixB = "https://discordapp.com/api/webhooks/";
		size_t start = 0;
		if (m_webhookUrl.find(prefixA) == 0)
			start = std::strlen(prefixA);
		else if (m_webhookUrl.find(prefixB) == 0)
			start = std::strlen(prefixB);
		if (start > 0)
		{
			size_t slash = m_webhookUrl.find('/', start);
			id = (slash == std::string::npos)
				? m_webhookUrl.substr(start)
				: m_webhookUrl.substr(start, slash - start);
		}
		m_statusLabel = std::string("ON (id=") + id + ", token=***)";
	}

	if (IsEnabled() && !m_workerStarted)
	{
		m_workerStarted = true;
		std::thread([this]() { WorkerMain(this); }).detach();
		AC_Log("discord webhook enabled");
	}
	else if (!m_enabled && !cleaned.empty())
	{
		AC_Log("discord webhook url rejected (need https://discord.com/api/webhooks/...)");
	}
	else if (!IsEnabled())
	{
		AC_Log("discord webhook disabled (ban.discord_webhook_url empty)");
	}
}

void DiscordWebhook::NotifyBan(const DiscordBanNotify& ban)
{
	if (!IsEnabled() || ban.steamId == 0)
		return;
	std::lock_guard<std::mutex> lock(m_mu);
	m_queue.push_back(ban);
}

void DiscordWebhook::WorkerMain(DiscordWebhook* self)
{
	while (self)
	{
		DiscordBanNotify ban;
		bool have = false;
		{
			std::lock_guard<std::mutex> lock(self->m_mu);
			if (!self->m_queue.empty())
			{
				ban = self->m_queue.front();
				self->m_queue.pop_front();
				have = true;
			}
		}

		if (!have)
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(200));
			continue;
		}

		self->PerformHttp(ban);
	}
}

std::string DiscordWebhook::JsonEscape(const std::string& s)
{
	std::string out;
	out.reserve(s.size() + 8);
	for (unsigned char c : s)
	{
		switch (c)
		{
		case '"':  out += "\\\""; break;
		case '\\': out += "\\\\"; break;
		case '\b': out += "\\b"; break;
		case '\f': out += "\\f"; break;
		case '\n': out += "\\n"; break;
		case '\r': out += "\\r"; break;
		case '\t': out += "\\t"; break;
		default:
			if (c < 0x20)
			{
				char buf[8];
				std::snprintf(buf, sizeof(buf), "\\u%04x", c);
				out += buf;
			}
			else
			{
				out.push_back(static_cast<char>(c));
			}
			break;
		}
	}
	return out;
}

std::string DiscordWebhook::ShellSingleQuote(const std::string& s)
{
	// Safe for POSIX: 'foo'\''bar'
	std::string out = "'";
	for (char c : s)
	{
		if (c == '\'')
			out += "'\\''";
		else
			out.push_back(c);
	}
	out.push_back('\'');
	return out;
}

std::string DiscordWebhook::BuildBanEmbedJson(const DiscordBanNotify& ban)
{
	char sid[32];
	std::snprintf(sid, sizeof(sid), "%llu", (unsigned long long)ban.steamId);

	const std::string nick = ban.playerName.empty() ? "?" : ban.playerName;
	const std::string reason = ban.reason.empty() ? "Использование читов (Античит система)" : ban.reason;
	const std::string map = ban.mapName.empty() ? "?" : ban.mapName;

	char duration[64];
	if (ban.durationDays <= 0)
		std::snprintf(duration, sizeof(duration), "навсегда");
	else
		std::snprintf(duration, sizeof(duration), "%d дн.", ban.durationDays);

	char scoreBuf[48];
	std::snprintf(scoreBuf, sizeof(scoreBuf), "%.1f", ban.suspicionScore);

	char decayBuf[160];
	if (ban.decayPerSecond > 0.0001f && ban.suspicionScore > 0.0f)
	{
		const float perMin = ban.decayPerSecond * 60.0f;
		const float secsToZero = ban.suspicionScore / ban.decayPerSecond;
		const int totalSec = (int)(secsToZero + 0.5f);
		const int h = totalSec / 3600;
		const int m = (totalSec % 3600) / 60;
		const int s = totalSec % 60;
		if (h > 0)
			std::snprintf(decayBuf, sizeof(decayBuf),
				"%.1f / мин · до 0 ≈ %dч %dм (без новых детектов)", perMin, h, m);
		else if (m > 0)
			std::snprintf(decayBuf, sizeof(decayBuf),
				"%.1f / мин · до 0 ≈ %dм %dс (без новых детектов)", perMin, m, s);
		else
			std::snprintf(decayBuf, sizeof(decayBuf),
				"%.1f / мин · до 0 ≈ %dс (без новых детектов)", perMin, s);
	}
	else
	{
		std::snprintf(decayBuf, sizeof(decayBuf), "decay выключен / неизвестен");
	}

	const std::string steamUrl = std::string("https://steamcommunity.com/profiles/") + sid;
	const std::string csTrackerUrl = std::string("https://cstracker.gg/players/") + sid;
	const std::string csTrackerNowUrl = std::string("https://steamcommunity.now/profiles/") + sid;
	const std::string cs2TrackerUrl = std::string("https://steamcommunity.ai/profiles/") + sid;
	const std::string csStatsUrl = std::string("https://csstats.gg/player/") + sid;
	const std::string leetifyUrl = std::string("https://leetify.com/app/profile/") + sid;

	const std::string links =
		"[Steam](" + steamUrl + ")\\n" +
		"[CSTracker](" + csTrackerUrl + ") · [CSTracker (.now)](" + csTrackerNowUrl + ")\\n" +
		"[CS2Tracker (.ai)](" + cs2TrackerUrl + ")\\n" +
		"[CSStats](" + csStatsUrl + ") · [Leetify](" + leetifyUrl + ")";

	// ISO-8601 UTC approximate (curl hosts usually have UTC clock).
	std::time_t now = std::time(nullptr);
	std::tm tmUtc{};
#if defined(_WIN32)
	gmtime_s(&tmUtc, &now);
#else
	gmtime_r(&now, &tmUtc);
#endif
	char ts[40];
	std::strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%SZ", &tmUtc);

	std::string json;
	json.reserve(1400);
	json += "{\"username\":\"Античит\",\"embeds\":[{";
	json += "\"title\":\"Автобан античита\",";
	json += "\"description\":\"Игрок **" + JsonEscape(nick) + "** заблокирован античитом.\\n"
		"Накопленный score на момент бана (с учётом decay).\",";
	json += "\"color\":15158332,";
	json += "\"fields\":[";
	json += "{\"name\":\"Ник\",\"value\":\"" + JsonEscape(nick) + "\",\"inline\":true},";
	json += "{\"name\":\"Срок бана\",\"value\":\"" + JsonEscape(duration) + "\",\"inline\":true},";
	json += "{\"name\":\"Карта\",\"value\":\"" + JsonEscape(map) + "\",\"inline\":true},";
	json += "{\"name\":\"Score\",\"value\":\"" + JsonEscape(scoreBuf) + "\",\"inline\":true},";
	json += "{\"name\":\"Истечение score\",\"value\":\"" + JsonEscape(decayBuf) + "\",\"inline\":false},";
	json += "{\"name\":\"SteamID64\",\"value\":\"`" + JsonEscape(sid) + "`\",\"inline\":false},";
	json += "{\"name\":\"Причина\",\"value\":\"" + JsonEscape(reason) + "\",\"inline\":false},";
	json += "{\"name\":\"Профиль и пробивы\",\"value\":\"" + links + "\",\"inline\":false}";
	json += "],";
	json += "\"footer\":{\"text\":\"cs2-anticheat\"},";
	json += "\"timestamp\":\"" + std::string(ts) + "\"";
	json += "}]}";
	return json;
}

void DiscordWebhook::PerformHttp(const DiscordBanNotify& ban) const
{
	const std::string json = BuildBanEmbedJson(ban);

	char path[256];
	std::snprintf(path, sizeof(path), "/tmp/ac_discord_%llu_%d.json",
		(unsigned long long)ban.steamId, (int)getpid());

	FILE* f = std::fopen(path, "wb");
	if (!f)
	{
		AC_Log("discord webhook: fopen failed for %s", path);
		return;
	}
	const size_t written = std::fwrite(json.data(), 1, json.size(), f);
	std::fclose(f);
	if (written != json.size())
	{
		std::remove(path);
		AC_Log("discord webhook: incomplete write");
		return;
	}

	// curl is present on CS2 Linux hosts; avoids linking libcurl into the .so.
	const std::string cmd =
		"curl -sS --max-time 8 -H " + ShellSingleQuote("Content-Type: application/json; charset=utf-8") +
		" --data-binary @" + ShellSingleQuote(path) +
		" " + ShellSingleQuote(m_webhookUrl) +
		" >/dev/null 2>&1; rm -f " + ShellSingleQuote(path);

	const int rc = std::system(cmd.c_str());
	if (rc != 0)
	{
		AC_Log("discord webhook failed steam=%llu rc=%d",
			(unsigned long long)ban.steamId, rc);
		return;
	}

	AC_Log("discord webhook sent steam=%llu name=%s",
		(unsigned long long)ban.steamId, ban.playerName.c_str());
}
