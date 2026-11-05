#include "backend_client.h"

#include "../plugin.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <chrono>
#include <array>
#include <unistd.h>

static bool PathIsExecutable(const char* path)
{
	return path && path[0] && access(path, X_OK) == 0;
}

static int RunPipedCommand(const std::string& cmd, std::string& out)
{
	out.clear();
	FILE* pipe = popen(cmd.c_str(), "r");
	if (!pipe)
		return -1;
	std::array<char, 512> buf{};
	while (fgets(buf.data(), (int)buf.size(), pipe))
		out += buf.data();
	return pclose(pipe);
}

static std::string ShellSingleQuote(const std::string& s)
{
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

static std::string DiscoverBin(const char* name)
{
	static const char* prefixes[] = { "/usr/bin/", "/bin/", "/usr/local/bin/" };
	for (const char* pref : prefixes)
	{
		std::string p = std::string(pref) + name;
		if (PathIsExecutable(p.c_str()))
			return p;
	}
	std::string out;
	std::string cmd = std::string("command -v ") + name + " 2>/dev/null";
	if (RunPipedCommand(cmd, out) == 0)
	{
		while (!out.empty() && (out.back() == '\n' || out.back() == '\r'))
			out.pop_back();
		if (!out.empty())
			return out;
	}
	return {};
}

void BackendClient::SetConfig(const std::string& baseUrl, const std::string& bearerToken, bool enabled)
{
	m_baseUrl = baseUrl;
	while (!m_baseUrl.empty() && (m_baseUrl.back() == '/' || m_baseUrl.back() == ' '))
		m_baseUrl.pop_back();
	m_token = bearerToken;
	m_enabled = enabled;

	if (IsEnabled() && !m_workerStarted)
	{
		m_workerStarted = true;
		std::thread([this]() { WorkerMain(this); }).detach();

		const bool hasCurl = !DiscoverBin("curl").empty();
		const bool hasWget = !DiscoverBin("wget").empty();
		const bool hasPy = !DiscoverBin("python3").empty() || !DiscoverBin("python").empty();
		AC_Log("backend client enabled url=%s tools=curl:%d wget:%d python:%d",
			m_baseUrl.c_str(), (int)hasCurl, (int)hasWget, (int)hasPy);
		if (!hasCurl && !hasWget && !hasPy)
			AC_Log("backend check WARNING: no curl/wget/python3 — IP/bypass checks will fail");
	}
	else if (!IsEnabled())
	{
		AC_Log("backend client disabled (set backend_api_url + backend_api_token)");
	}
}

void BackendClient::RequestCheck(const BackendCheckRequest& req)
{
	if (!IsEnabled() || req.steamId == 0)
		return;
	std::lock_guard<std::mutex> lock(m_mu);
	m_queue.push_back(req);
}

void BackendClient::PollResults(std::vector<BackendCheckResult>& out)
{
	out.clear();
	std::lock_guard<std::mutex> lock(m_mu);
	while (!m_results.empty())
	{
		out.push_back(m_results.front());
		m_results.pop_front();
	}
}

void BackendClient::WorkerMain(BackendClient* self)
{
	while (self)
	{
		BackendCheckRequest req;
		bool have = false;
		{
			std::lock_guard<std::mutex> lock(self->m_mu);
			if (!self->m_queue.empty())
			{
				req = self->m_queue.front();
				self->m_queue.pop_front();
				have = true;
			}
		}

		if (!have)
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(200));
			continue;
		}

		BackendCheckResult result = self->PerformHttp(req);
		{
			std::lock_guard<std::mutex> lock(self->m_mu);
			self->m_results.push_back(result);
		}
	}
}

static std::string JsonExtractString(const std::string& json, const char* key)
{
	std::string needle = std::string("\"") + key + "\"";
	size_t p = json.find(needle);
	if (p == std::string::npos)
		return {};
	p = json.find(':', p);
	if (p == std::string::npos)
		return {};
	++p;
	while (p < json.size() && (json[p] == ' ' || json[p] == '\t'))
		++p;
	if (p < json.size() && json[p] == '"')
	{
		++p;
		std::string out;
		while (p < json.size() && json[p] != '"')
		{
			if (json[p] == '\\' && p + 1 < json.size())
			{
				out.push_back(json[p + 1]);
				p += 2;
				continue;
			}
			out.push_back(json[p++]);
		}
		return out;
	}
	return {};
}

static bool JsonExtractBool(const std::string& json, const char* key, bool& out)
{
	std::string needle = std::string("\"") + key + "\"";
	size_t p = json.find(needle);
	if (p == std::string::npos)
		return false;
	p = json.find(':', p);
	if (p == std::string::npos)
		return false;
	++p;
	while (p < json.size() && (json[p] == ' ' || json[p] == '\t'))
		++p;
	if (json.compare(p, 4, "true") == 0)
	{
		out = true;
		return true;
	}
	if (json.compare(p, 5, "false") == 0)
	{
		out = false;
		return true;
	}
	return false;
}

static int CountOccurrences(const std::string& hay, const char* needle)
{
	int n = 0;
	size_t pos = 0;
	const size_t len = std::strlen(needle);
	while ((pos = hay.find(needle, pos)) != std::string::npos)
	{
		++n;
		pos += len;
	}
	return n;
}

static bool TryHttpGet(const std::string& url, const std::string& token, std::string& body, std::string& via)
{
	body.clear();
	via.clear();
	const std::string auth = std::string("Authorization: Bearer ") + token;
	const std::string urlQ = ShellSingleQuote(url);
	const std::string authQ = ShellSingleQuote(auth);
	std::string lastErr = "no_http_tool";

	auto tryCmd = [&](const char* name, const std::string& cmd) -> bool
	{
		std::string out;
		const int rc = RunPipedCommand(cmd, out);
		if (rc == 0 && !out.empty())
		{
			body = std::move(out);
			via = name;
			return true;
		}
		lastErr = std::string(name) + "_failed";
		return false;
	};

	std::string curl = DiscoverBin("curl");
	if (!curl.empty())
	{
		std::string cmd = ShellSingleQuote(curl) + " -sS --max-time 5 -H " + authQ + " " + urlQ + " 2>/dev/null";
		if (tryCmd("curl", cmd))
			return true;
	}

	std::string wget = DiscoverBin("wget");
	if (!wget.empty())
	{
		std::string cmd = ShellSingleQuote(wget) + " -q -T 5 -O - --header=" + authQ + " " + urlQ + " 2>/dev/null";
		if (tryCmd("wget", cmd))
			return true;
	}

	std::string py = DiscoverBin("python3");
	if (py.empty())
		py = DiscoverBin("python");
	if (!py.empty())
	{
		char envTok[40];
		char envUrl[40];
		std::snprintf(envTok, sizeof(envTok), "AC_BEARER_%d", (int)getpid());
		std::snprintf(envUrl, sizeof(envUrl), "AC_URL_%d", (int)getpid());
		setenv(envTok, token.c_str(), 1);
		setenv(envUrl, url.c_str(), 1);

		char pyCmd[1024];
		std::snprintf(pyCmd, sizeof(pyCmd),
			"%s -c \"import os,urllib.request;"
			"tok=os.environ[%s];u=os.environ[%s];"
			"req=urllib.request.Request(u,headers={'Authorization':'Bearer '+tok});"
			"print(urllib.request.urlopen(req,timeout=5).read().decode())\" 2>/dev/null",
			ShellSingleQuote(py).c_str(),
			ShellSingleQuote(envTok).c_str(),
			ShellSingleQuote(envUrl).c_str());
		const bool ok = tryCmd("python3", pyCmd);
		unsetenv(envTok);
		unsetenv(envUrl);
		if (ok)
			return true;
	}

	via = lastErr;
	return false;
}

BackendCheckResult BackendClient::PerformHttp(const BackendCheckRequest& req) const
{
	BackendCheckResult r;
	r.steamId = req.steamId;
	r.slot = req.slot;
	r.playerName = req.playerName;
	r.clientIp = req.clientIp;

	char url[768];
	std::snprintf(url, sizeof(url),
		"%s/api/cs2/anticheat/check?steam_id_64=%llu&client_ip=%s",
		m_baseUrl.c_str(),
		(unsigned long long)req.steamId,
		req.clientIp.empty() ? "" : req.clientIp.c_str());

	std::string body;
	std::string via;
	if (!TryHttpGet(url, m_token, body, via))
	{
		r.rawError = via.empty() ? "http_failed" : via;
		return r;
	}

	r.ok = true;
	JsonExtractBool(body, "steam_banned", r.steamBanned);
	JsonExtractBool(body, "should_enforce", r.shouldEnforce);
	r.enforceReason = JsonExtractString(body, "enforce_reason");
	r.exactIpLinked = CountOccurrences(body, "\"match\":\"exact\"");
	r.prefixIpLinked = CountOccurrences(body, "\"match\":\"prefix24\"");

	AC_Log("backend check steam=%llu ip=%s banned=%d enforce=%d exactIp=%d /24=%d reason=%s via=%s",
		(unsigned long long)r.steamId, r.clientIp.c_str(),
		(int)r.steamBanned, (int)r.shouldEnforce, r.exactIpLinked, r.prefixIpLinked,
		r.enforceReason.c_str(), via.c_str());

	return r;
}
