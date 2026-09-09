#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>

enum class FileWatchSeverity : uint8_t
{
	Watch = 0,     // log only
	Critical = 1,  // ban + reject join
};

struct GameFileSnapshot {
	std::string relativePath;
	bool exists = false;
	uint64_t size = 0;
	uint64_t mtime = 0;
	uint64_t hash = 0; // FNV-1a 64 (+ size/mtime mix)
	FileWatchSeverity severity = FileWatchSeverity::Watch;
};

struct IntegrityScanResult {
	bool ok = true;
	bool anyChange = false;
	bool criticalChange = false;
	bool memoryBroken = false;
	std::string reason;
};

// Server gamedir + plugin self-integrity. Client disk/memory is not readable from MetaMod.
class GameFileScanner {
public:
	GameFileScanner() = default;

	void SetEnabled(bool enabled) { m_enabled = enabled; }
	bool IsEnabled() const { return m_enabled; }

	void ScanAtStartup();

	// Full rescan vs baseline + memory check.
	IntegrityScanResult ScanNow(const char* tag);

	// On join: rescan; if critical → caller should ban before player plays.
	IntegrityScanResult ScanOnPlayerJoin(uint64_t steamId, const char* playerName);

	// Cheap tick: periodically re-scan; sets lockdown on critical tamper.
	void Tick(float curtime);

	bool IsLockdownActive() const { return m_lockdown; }
	bool IsSteamIntegrityBanned(uint64_t steamId) const;
	void MarkIntegrityBan(uint64_t steamId);
	const char* LockdownReason() const { return m_lockdownReason.c_str(); }

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
	void ApplyCriticalLockdown(const char* reason);

	bool m_enabled = true;
	char m_gameDir[512]{};
	std::vector<GameFileSnapshot> m_baseline;
	std::unordered_map<std::string, GameFileSnapshot> m_baselineByPath;
	std::unordered_set<uint64_t> m_integrityBanned;

	bool m_lockdown = false;
	std::string m_lockdownReason;
	float m_lastPeriodicScan = -999.0f;
	static constexpr float kPeriodicIntervalSec = 20.0f;
};
