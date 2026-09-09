#pragma once

#include <cstdint>
#include <string>
#include <mutex>
#include <deque>
#include <vector>

struct BackendCheckRequest {
	uint64_t steamId = 0;
	std::string clientIp;
	std::string playerName;
	int slot = -1;
};

struct BackendCheckResult {
	uint64_t steamId = 0;
	int slot = -1;
	std::string playerName;
	std::string clientIp;
	bool ok = false;
	bool steamBanned = false;
	bool shouldEnforce = false;
	std::string enforceReason;
	int exactIpLinked = 0;
	int prefixIpLinked = 0;
	std::string rawError;
};

// Async HTTPS client for GET /api/cs2/anticheat/check (same data as site /bypass).
class BackendClient {
public:
	void SetConfig(const std::string& baseUrl, const std::string& bearerToken, bool enabled);
	bool IsEnabled() const { return m_enabled && !m_baseUrl.empty() && !m_token.empty(); }

	void RequestCheck(const BackendCheckRequest& req);
	void PollResults(std::vector<BackendCheckResult>& out);

private:
	static void WorkerMain(BackendClient* self);
	BackendCheckResult PerformHttp(const BackendCheckRequest& req) const;

	bool m_enabled = false;
	std::string m_baseUrl;
	std::string m_token;

	std::mutex m_mu;
	std::deque<BackendCheckRequest> m_queue;
	std::deque<BackendCheckResult> m_results;
	bool m_workerStarted = false;
};
