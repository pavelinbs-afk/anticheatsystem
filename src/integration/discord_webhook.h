#pragma once

#include <cstdint>
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
};

// Fire-and-forget Discord webhook embeds (curl on worker thread, same pattern as BackendClient).
class DiscordWebhook {
public:
	void SetWebhookUrl(const std::string& url);
	bool IsEnabled() const { return m_enabled && !m_webhookUrl.empty(); }
	/// Human status for ac_status (no full token): off / invalid / on (masked id).
	const char* GetStatusLabel() const { return m_statusLabel.c_str(); }
	void NotifyBan(const DiscordBanNotify& ban);

private:
	static void WorkerMain(DiscordWebhook* self);
	void PerformHttp(const DiscordBanNotify& ban) const;
	static std::string BuildBanEmbedJson(const DiscordBanNotify& ban);
	static std::string JsonEscape(const std::string& s);
	static std::string ShellSingleQuote(const std::string& s);

	bool m_enabled = false;
	std::string m_webhookUrl;
	std::string m_statusLabel = "off (empty url)";

	std::mutex m_mu;
	std::deque<DiscordBanNotify> m_queue;
	bool m_workerStarted = false;
};
