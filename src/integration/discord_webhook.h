#pragma once

#include <cstdint>
#include <ctime>
#include <deque>
#include <mutex>
#include <string>

struct DiscordBanNotify {
	uint64_t steamId = 0;
	std::string playerName;
	std::string reason;
	int durationDays = 45;
	float suspicionScore = 0.0f;
	float decayPerSecond = 0.0f; // for ETA until score decays to 0
	std::string mapName;
	bool resend = false; // title note for catch-up sends
};

struct DiscordWebhookLastStatus {
	bool everAttempted = false;
	bool ok = false;
	int httpCode = 0;
	uint64_t steamId = 0;
	std::string playerName;
	std::string detail; // short human reason
	std::time_t atUnix = 0;
};

enum class DiscordHttpTool : uint8_t {
	None = 0,
	Curl,
	Wget,
	Python3,
};

// Fire-and-forget Discord webhook embeds (curl/wget/python3 via popen on worker thread).
class DiscordWebhook {
public:
	void SetWebhookUrl(const std::string& url);
	bool IsEnabled() const { return m_enabled && !m_webhookUrl.empty(); }
	/// Human status for ac_status (no full token): off / invalid / on (masked id).
	const char* GetStatusLabel() const { return m_statusLabel.c_str(); }
	/// Detected HTTP helper: curl / wget / python3 / none.
	const char* GetHttpToolLabel() const;
	DiscordWebhookLastStatus GetLastStatus() const;
	void NotifyBan(const DiscordBanNotify& ban);
	/// Queue a test embed (synthetic).
	void NotifyTest();
	/// Re-send embed for an already-banned player (manual / catch-up).
	void NotifyResend(const DiscordBanNotify& ban);

private:
	static void WorkerMain(DiscordWebhook* self);
	void PerformHttp(const DiscordBanNotify& ban);
	void SetLastStatus(bool ok, int httpCode, uint64_t steamId, const std::string& name, const std::string& detail);
	static std::string BuildBanEmbedJson(const DiscordBanNotify& ban);
	static std::string JsonEscape(const std::string& s);
	static std::string ShellSingleQuote(const std::string& s);
	static bool PathIsExecutable(const char* path);
	static DiscordHttpTool ResolveHttpTool(std::string& outBin);
	static int ParseTrailingHttpCode(const std::string& out);
	static int RunPipedCommand(const std::string& cmd, std::string& out);

	bool m_enabled = false;
	std::string m_webhookUrl;
	std::string m_statusLabel = "off (empty url)";
	DiscordHttpTool m_httpTool = DiscordHttpTool::None;
	std::string m_httpBin;

	mutable std::mutex m_mu;
	std::deque<DiscordBanNotify> m_queue;
	bool m_workerStarted = false;
	DiscordWebhookLastStatus m_last;
};
