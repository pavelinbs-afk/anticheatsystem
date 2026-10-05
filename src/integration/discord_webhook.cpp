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

	m_httpTool = ResolveHttpTool(m_httpBin);

	if (IsEnabled() && !m_workerStarted)
	{
		m_workerStarted = true;
		std::thread([this]() { WorkerMain(this); }).detach();
		AC_Log("discord webhook enabled (worker started, http_tool=%s)", GetHttpToolLabel());
		if (m_httpTool == DiscordHttpTool::None)
			AC_Log("discord webhook WARNING: no curl/wget/python3 found — install one of them");
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

const char* DiscordWebhook::GetHttpToolLabel() const
{
	switch (m_httpTool)
	{
	case DiscordHttpTool::Curl: return m_httpBin.empty() ? "curl" : m_httpBin.c_str();
	case DiscordHttpTool::Wget: return m_httpBin.empty() ? "wget" : m_httpBin.c_str();
	case DiscordHttpTool::Python3: return m_httpBin.empty() ? "python3" : m_httpBin.c_str();
	default: return "NONE (install curl or python3)";
	}
}

DiscordWebhookLastStatus DiscordWebhook::GetLastStatus() const
{
	std::lock_guard<std::mutex> lock(m_mu);
	return m_last;
}

void DiscordWebhook::SetLastStatus(bool ok, int httpCode, uint64_t steamId, const std::string& name, const std::string& detail)
{
	std::lock_guard<std::mutex> lock(m_mu);
	m_last.everAttempted = true;
	m_last.ok = ok;
	m_last.httpCode = httpCode;
	m_last.steamId = steamId;
	m_last.playerName = name;
	m_last.detail = detail;
	m_last.atUnix = std::time(nullptr);
}

void DiscordWebhook::NotifyBan(const DiscordBanNotify& ban)
{
	if (!IsEnabled() || ban.steamId == 0)
	{
		AC_Log("discord webhook NotifyBan skipped (enabled=%d steam=%llu)",
			(int)IsEnabled(), (unsigned long long)ban.steamId);
		return;
	}
	{
		std::lock_guard<std::mutex> lock(m_mu);
		m_queue.push_back(ban);
	}
	AC_Log("discord webhook queued ban steam=%llu name=%s (worker=%d)",
		(unsigned long long)ban.steamId, ban.playerName.c_str(), (int)m_workerStarted);
}

void DiscordWebhook::NotifyTest()
{
	if (!IsEnabled())
	{
		AC_Log("discord webhook test skipped — not enabled");
		return;
	}
	DiscordBanNotify n;
	n.steamId = 76561198000000000ULL;
	n.playerName = "ac_webhook_test";
	n.reason = "Тест вебхука (ac_webhook_test)";
	n.durationDays = 0;
	n.suspicionScore = 55.0f;
	n.decayPerSecond = 0.05f;
	n.mapName = "test";
	{
		std::lock_guard<std::mutex> lock(m_mu);
		m_queue.push_back(n);
	}
	AC_Log("discord webhook test queued");
}

void DiscordWebhook::NotifyResend(const DiscordBanNotify& ban)
{
	if (!IsEnabled() || ban.steamId == 0)
	{
		AC_Log("discord webhook resend skipped (enabled=%d steam=%llu)",
			(int)IsEnabled(), (unsigned long long)ban.steamId);
		return;
	}
	DiscordBanNotify copy = ban;
	copy.resend = true;
	{
		std::lock_guard<std::mutex> lock(m_mu);
		m_queue.push_back(copy);
	}
	AC_Log("discord webhook resend queued steam=%llu name=%s",
		(unsigned long long)ban.steamId, ban.playerName.c_str());
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
		// Discord webhook rate limit (~5/2s) — pause between jobs (esp. resend_all).
		std::this_thread::sleep_for(std::chrono::milliseconds(600));
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

bool DiscordWebhook::PathIsExecutable(const char* path)
{
	return path && path[0] && access(path, X_OK) == 0;
}

DiscordHttpTool DiscordWebhook::ResolveHttpTool(std::string& outBin)
{
	outBin.clear();

	static const char* curlPaths[] = {
		"/usr/bin/curl", "/bin/curl", "/usr/local/bin/curl",
	};
	for (const char* p : curlPaths)
	{
		if (PathIsExecutable(p))
		{
			outBin = p;
			return DiscordHttpTool::Curl;
		}
	}

	static const char* wgetPaths[] = {
		"/usr/bin/wget", "/bin/wget", "/usr/local/bin/wget",
	};
	for (const char* p : wgetPaths)
	{
		if (PathIsExecutable(p))
		{
			outBin = p;
			return DiscordHttpTool::Wget;
		}
	}

	static const char* pyPaths[] = {
		"/usr/bin/python3", "/bin/python3", "/usr/local/bin/python3",
		"/usr/bin/python",
	};
	for (const char* p : pyPaths)
	{
		if (PathIsExecutable(p))
		{
			outBin = p;
			return DiscordHttpTool::Python3;
		}
	}

	// Last resort: ask the shell (PATH may contain them even if absolute paths failed).
	{
		std::string out;
		if (RunPipedCommand("command -v curl 2>/dev/null", out) == 0)
		{
			while (!out.empty() && (out.back() == '\n' || out.back() == '\r'))
				out.pop_back();
			if (!out.empty())
			{
				outBin = out;
				return DiscordHttpTool::Curl;
			}
		}
		out.clear();
		if (RunPipedCommand("command -v wget 2>/dev/null", out) == 0)
		{
			while (!out.empty() && (out.back() == '\n' || out.back() == '\r'))
				out.pop_back();
			if (!out.empty())
			{
				outBin = out;
				return DiscordHttpTool::Wget;
			}
		}
		out.clear();
		if (RunPipedCommand("command -v python3 2>/dev/null", out) == 0)
		{
			while (!out.empty() && (out.back() == '\n' || out.back() == '\r'))
				out.pop_back();
			if (!out.empty())
			{
				outBin = out;
				return DiscordHttpTool::Python3;
			}
		}
	}

	return DiscordHttpTool::None;
}

int DiscordWebhook::RunPipedCommand(const std::string& cmd, std::string& out)
{
	out.clear();
	FILE* pipe = popen(cmd.c_str(), "r");
	if (!pipe)
		return -1;
	std::array<char, 256> buf{};
	while (fgets(buf.data(), (int)buf.size(), pipe))
		out += buf.data();
	return pclose(pipe);
}

int DiscordWebhook::ParseTrailingHttpCode(const std::string& out)
{
	size_t i = out.size();
	while (i > 0 && (out[i - 1] == '\n' || out[i - 1] == '\r' || out[i - 1] == ' '))
		--i;
	size_t end = i;
	while (i > 0 && out[i - 1] >= '0' && out[i - 1] <= '9')
		--i;
	if (i < end)
		return std::atoi(out.substr(i, end - i).c_str());
	return 0;
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
	json += "{\"username\":\"Anticheat\",\"embeds\":[{";
	if (ban.resend)
		json += "\"title\":\"Автобан античита (повторная отправка)\",";
	else
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
	json += "\"footer\":{\"text\":\"Anti-Cheat System\"},";
	json += "\"timestamp\":\"" + std::string(ts) + "\"";
	json += "}]}";
	return json;
}

void DiscordWebhook::PerformHttp(const DiscordBanNotify& ban)
{
	const std::string json = BuildBanEmbedJson(ban);

	// Re-resolve each send — host PATH / packages may change without plugin reload.
	std::string httpBin;
	DiscordHttpTool tool = ResolveHttpTool(httpBin);
	m_httpTool = tool;
	m_httpBin = httpBin;

	if (tool == DiscordHttpTool::None)
	{
		SetLastStatus(false, 0, ban.steamId, ban.playerName,
			"no HTTP tool: install curl OR python3 (apt install curl)");
		AC_Log("discord webhook FAILED steam=%llu — curl/wget/python3 not found on host",
			(unsigned long long)ban.steamId);
		return;
	}

	char path[256];
	std::snprintf(path, sizeof(path), "/tmp/ac_discord_%llu_%d.json",
		(unsigned long long)ban.steamId, (int)getpid());

	FILE* f = std::fopen(path, "wb");
	if (!f)
	{
		SetLastStatus(false, 0, ban.steamId, ban.playerName, std::string("fopen failed: ") + path);
		AC_Log("discord webhook: fopen failed for %s", path);
		return;
	}
	const size_t written = std::fwrite(json.data(), 1, json.size(), f);
	std::fclose(f);
	if (written != json.size())
	{
		std::remove(path);
		SetLastStatus(false, 0, ban.steamId, ban.playerName, "incomplete json write");
		AC_Log("discord webhook: incomplete write");
		return;
	}

	std::string cmd;
	if (tool == DiscordHttpTool::Curl)
	{
		cmd = ShellSingleQuote(httpBin) +
			" -sS --max-time 10"
			" -H " + ShellSingleQuote("Content-Type: application/json; charset=utf-8") +
			" --data-binary @" + ShellSingleQuote(path) +
			" -o /dev/null -w '%{http_code}' " +
			ShellSingleQuote(m_webhookUrl) +
			" 2>&1; rm -f " + ShellSingleQuote(path);
	}
	else if (tool == DiscordHttpTool::Wget)
	{
		// wget prints status on stderr with --server-response; we ask for quiet body and parse 2xx.
		cmd = ShellSingleQuote(httpBin) +
			" -q -O /dev/null --timeout=10 --server-response"
			" --header=" + ShellSingleQuote("Content-Type: application/json; charset=utf-8") +
			" --post-file=" + ShellSingleQuote(path) +
			" " + ShellSingleQuote(m_webhookUrl) +
			" 2>&1; echo; rm -f " + ShellSingleQuote(path);
	}
	else // Python3
	{
		auto PyQuote = [](const std::string& s) -> std::string
		{
			std::string o = "\"";
			for (unsigned char c : s)
			{
				if (c == '\\' || c == '"')
					o.push_back('\\');
				else if (c == '\n')
				{
					o += "\\n";
					continue;
				}
				o.push_back(static_cast<char>(c));
			}
			o.push_back('"');
			return o;
		};

		char pyPath[256];
		std::snprintf(pyPath, sizeof(pyPath), "/tmp/ac_discord_%llu_%d.py",
			(unsigned long long)ban.steamId, (int)getpid());
		FILE* pf = std::fopen(pyPath, "wb");
		if (!pf)
		{
			std::remove(path);
			SetLastStatus(false, 0, ban.steamId, ban.playerName, "fopen py helper failed");
			return;
		}
		std::string pyFile;
		pyFile += "import urllib.request, urllib.error\n";
		pyFile += "p = " + PyQuote(path) + "\n";
		pyFile += "u = " + PyQuote(m_webhookUrl) + "\n";
		pyFile += "d = open(p, 'rb').read()\n";
		pyFile += "req = urllib.request.Request(u, data=d, method='POST', headers={'Content-Type': 'application/json; charset=utf-8'})\n";
		pyFile += "try:\n";
		pyFile += "    print(urllib.request.urlopen(req, timeout=10).status)\n";
		pyFile += "except urllib.error.HTTPError as e:\n";
		pyFile += "    print(e.code)\n";
		pyFile += "except Exception:\n";
		pyFile += "    print(0)\n";
		std::fwrite(pyFile.data(), 1, pyFile.size(), pf);
		std::fclose(pf);

		cmd = ShellSingleQuote(httpBin) + " " + ShellSingleQuote(pyPath) +
			" 2>&1; rm -f " + ShellSingleQuote(path) + " " + ShellSingleQuote(pyPath);
	}

	std::string out;
	const int rc = RunPipedCommand(cmd, out);
	int httpCode = ParseTrailingHttpCode(out);

	// wget: look for "HTTP/1.1 204" style lines if trailing parse failed.
	if (httpCode == 0 && tool == DiscordHttpTool::Wget)
	{
		size_t p = out.rfind("HTTP/");
		if (p != std::string::npos)
		{
			size_t sp = out.find(' ', p);
			if (sp != std::string::npos)
				httpCode = std::atoi(out.c_str() + sp + 1);
		}
	}

	const bool ok = (httpCode == 200 || httpCode == 204);
	if (ok)
	{
		SetLastStatus(true, httpCode, ban.steamId, ban.playerName,
			std::string("sent OK via ") + GetHttpToolLabel());
		AC_Log("discord webhook sent steam=%llu name=%s http=%d via=%s",
			(unsigned long long)ban.steamId, ban.playerName.c_str(), httpCode, GetHttpToolLabel());
		return;
	}

	std::string detail = std::string("via=") + GetHttpToolLabel() +
		" http=" + std::to_string(httpCode) + " pclose_rc=" + std::to_string(rc);
	if (!out.empty())
	{
		std::string clip = out;
		if (clip.size() > 160)
			clip = clip.substr(0, 160) + "...";
		for (char& c : clip)
		{
			if (c == '\n' || c == '\r')
				c = ' ';
		}
		detail += " out=" + clip;
	}
	SetLastStatus(false, httpCode, ban.steamId, ban.playerName, detail);
	AC_Log("discord webhook FAILED steam=%llu http=%d rc=%d via=%s out=%s",
		(unsigned long long)ban.steamId, httpCode, rc, GetHttpToolLabel(), out.c_str());
}
