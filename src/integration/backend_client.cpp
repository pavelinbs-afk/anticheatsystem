#include "backend_client.h"

#include "../plugin.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <chrono>
#include <array>

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
		AC_Log("backend client enabled url=%s", m_baseUrl.c_str());
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

	// curl is present on CS2 Linux hosts; avoids linking libcurl into the .so.
	char cmd[1400];
	std::snprintf(cmd, sizeof(cmd),
		"curl -sS --max-time 5 -H \"Authorization: Bearer %s\" \"%s\" 2>/dev/null",
		m_token.c_str(), url);

	FILE* pipe = popen(cmd, "r");
	if (!pipe)
	{
		r.rawError = "popen_failed";
		return r;
	}

	std::string body;
	std::array<char, 512> buf{};
	while (fgets(buf.data(), (int)buf.size(), pipe))
		body += buf.data();
	const int rc = pclose(pipe);
	if (rc != 0 && body.empty())
	{
		r.rawError = "curl_failed";
		return r;
	}

	r.ok = true;
	JsonExtractBool(body, "steam_banned", r.steamBanned);
	JsonExtractBool(body, "should_enforce", r.shouldEnforce);
	r.enforceReason = JsonExtractString(body, "enforce_reason");
	r.exactIpLinked = CountOccurrences(body, "\"match\":\"exact\"");
	r.prefixIpLinked = CountOccurrences(body, "\"match\":\"prefix24\"");

	AC_Log("backend check steam=%llu ip=%s banned=%d enforce=%d exactIp=%d /24=%d reason=%s",
		(unsigned long long)r.steamId, r.clientIp.c_str(),
		(int)r.steamBanned, (int)r.shouldEnforce, r.exactIpLinked, r.prefixIpLinked,
		r.enforceReason.c_str());

	return r;
}
