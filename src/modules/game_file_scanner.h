#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>

enum class FileWatchSeverity : uint8_t
{
	Watch = 0,     // log only
	Critical = 1,  // log critical (never ban players)
};

struct GameFileSnapshot {
	std::string relativePath;
	bool exists = false;
	uint64_t size = 0;
	uint64_t mtime = 0;
	uint64_t hash = 0;
	FileWatchSeverity severity = FileWatchSeverity::Watch;
};

struct IntegrityScanResult {
	bool ok = true;
	bool anyChange = false;
	bool criticalChange = false;
	bool memoryBroken = false;
	std::string reason;
};

// Server gamedir + soft plugin memory check. Never bans players — client files are not readable.
class GameFileScanner {
public:
	GameFileScanner() = default;

	void SetEnabled(bool enabled) { m_enabled = enabled; }
	bool IsEnabled() const { return m_enabled; }

	void ScanAtStartup();
	IntegrityScanResult ScanNow(const char* tag);
	IntegrityScanResult ScanOnPlayerJoin(uint64_t steamId, const char* playerName);
	void Tick(float curtime);

	const char* LastCriticalReason() const { return m_lastCriticalReason.c_str(); }

private:
	struct WatchSpec {
		const char* relativePath;
		FileWatchSeverity severity;
	};

	bool ResolveGameDir(char* out, size_t outSize);
	bool BuildAbsolutePath(const char* relative, char* out, size_t outSize) const;
	GameFileSnapshot ReadOne(const WatchSpec& spec) const;
	std::vector<GameFileSnapshot> ReadAll() const;
	void LogSnapshot(const char* tag, const std::vector<GameFileSnapshot>& snaps) const;
	void DiffAgainstBaseline(const std::vector<GameFileSnapshot>& now, IntegrityScanResult& out) const;
	bool CheckPluginMemoryIntegrity(std::string& reason) const;

	bool m_enabled = true;
	char m_gameDir[512]{};
	std::vector<GameFileSnapshot> m_baseline;
	std::unordered_map<std::string, GameFileSnapshot> m_baselineByPath;
	std::string m_lastCriticalReason;
	float m_lastPeriodicScan = -999.0f;
	static constexpr float kPeriodicIntervalSec = 20.0f;
};
