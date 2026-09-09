#include "game_file_scanner.h"

#include "../plugin.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <vector>
#include <sys/stat.h>

#ifdef __linux__
#include <dlfcn.h>
#endif

namespace {

constexpr size_t kMaxHashBytes = 4 * 1024 * 1024; // 4 MiB content hash cap

enum class Sev : uint8_t { Watch = 0, Critical = 1 };

struct Spec {
	const char* path;
	Sev sev;
};

// Critical = plugin/loader/game identity (runtime tamper → lockdown + ban on join).
// Watch = configs that admins may edit (log only).
// NOTE: never mark banned_steamids.json as Critical — AdminPlugin rewrites it on every ban.
static const Spec kWatchSpecs[] = {
	{ "addons/anticheat/bin/linuxsteamrt64/anticheat.so", Sev::Critical },
	{ "addons/anticheat/bin/win64/anticheat.dll", Sev::Critical },
	{ "addons/metamod/anticheat.vdf", Sev::Critical },
	{ "addons/metamod/metaplugins.ini", Sev::Critical },
	{ "gameinfo.gi", Sev::Critical },
	{ "steam.inf", Sev::Critical },
	{ "addons/anticheat/configs/anticheat_config.json", Sev::Watch },
	{ "addons/counterstrikesharp/configs/plugins/AdminPlugin/banned_steamids.json", Sev::Watch },
	{ "cfg/server.cfg", Sev::Watch },
	{ "cfg/gamemode_competitive.cfg", Sev::Watch },
	{ "cfg/gamemode_casual.cfg", Sev::Watch },
	{ "cfg/gamemode_deathmatch.cfg", Sev::Watch },
};

static void NormalizeSlashes(char* path)
{
	if (!path)
		return;
	for (char* p = path; *p; ++p)
	{
		if (*p == '\\')
			*p = '/';
	}
}

static uint64_t Fnv1a64(const uint8_t* data, size_t len)
{
	uint64_t h = 14695981039346656037ULL;
	for (size_t i = 0; i < len; ++i)
	{
		h ^= data[i];
		h *= 1099511628211ULL;
	}
	return h;
}

static uint64_t MixHash(uint64_t h, uint64_t v)
{
	h ^= v + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
	return h;
}

} // namespace

bool GameFileScanner::ResolveGameDir(char* out, size_t outSize)
{
	if (!out || outSize == 0)
		return false;
	out[0] = '\0';

	if (g_pEngine)
	{
		CBufferStringN<512> gameDir;
		g_pEngine->GetGameDir(gameDir);
		const char* gd = gameDir.Get();
		if (gd && *gd)
		{
			V_strncpy(out, gd, (int)outSize);
			NormalizeSlashes(out);
			return true;
		}
	}

	const char* plat = Plat_GetGameDirectory();
	if (plat && *plat)
	{
		V_strncpy(out, plat, (int)outSize);
		NormalizeSlashes(out);
		return true;
	}

	return false;
}

bool GameFileScanner::BuildAbsolutePath(const char* relative, char* out, size_t outSize) const
{
	if (!relative || !*relative || !out || outSize == 0 || !m_gameDir[0])
		return false;

	V_snprintf(out, (int)outSize, "%s/%s", m_gameDir, relative);
	NormalizeSlashes(out);
	return true;
}

GameFileSnapshot GameFileScanner::ReadOne(const WatchSpec& spec) const
{
	GameFileSnapshot snap;
	snap.relativePath = spec.relativePath ? spec.relativePath : "";
	snap.severity = spec.severity;

	char absPath[768];
	if (!BuildAbsolutePath(spec.relativePath, absPath, sizeof(absPath)))
		return snap;

	struct stat st{};
	if (stat(absPath, &st) != 0)
		return snap;

	snap.exists = true;
	snap.size = static_cast<uint64_t>(st.st_size);
	snap.mtime = static_cast<uint64_t>(st.st_mtime);

	std::ifstream f(absPath, std::ios::binary);
	if (!f)
		return snap;

	const size_t toRead = static_cast<size_t>(snap.size > kMaxHashBytes ? kMaxHashBytes : snap.size);
	std::vector<uint8_t> buf(toRead);
	uint64_t h = 14695981039346656037ULL;
	if (toRead > 0)
	{
		f.read(reinterpret_cast<char*>(buf.data()), static_cast<std::streamsize>(toRead));
		const auto got = static_cast<size_t>(f.gcount());
		h = Fnv1a64(buf.data(), got);
	}

	// For large binaries: also sample the last up-to-64KiB so trailing patches are visible.
	if (snap.size > kMaxHashBytes)
	{
		const size_t tail = 64 * 1024;
		const uint64_t offset = snap.size - tail;
		f.clear();
		f.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
		std::vector<uint8_t> tailBuf(tail);
		f.read(reinterpret_cast<char*>(tailBuf.data()), static_cast<std::streamsize>(tail));
		const auto got = static_cast<size_t>(f.gcount());
		h = MixHash(h, Fnv1a64(tailBuf.data(), got));
	}

	h = MixHash(h, snap.size);
	h = MixHash(h, snap.mtime);
	snap.hash = h;
	return snap;
}

std::vector<GameFileSnapshot> GameFileScanner::ReadAll() const
{
	std::vector<GameFileSnapshot> out;
	out.reserve(sizeof(kWatchSpecs) / sizeof(kWatchSpecs[0]));
	for (const Spec& s : kWatchSpecs)
	{
		WatchSpec ws;
		ws.relativePath = s.path;
		ws.severity = (s.sev == Sev::Critical) ? FileWatchSeverity::Critical : FileWatchSeverity::Watch;
		out.push_back(ReadOne(ws));
	}
	return out;
}

void GameFileScanner::LogSnapshot(const char* tag, const std::vector<GameFileSnapshot>& snaps) const
{
	int ok = 0;
	int missing = 0;
	for (const auto& s : snaps)
	{
		if (s.exists)
		{
			++ok;
			AC_Log("%s file ok %s sev=%d size=%llu mtime=%llu hash=%016llx",
				tag, s.relativePath.c_str(), (int)s.severity,
				(unsigned long long)s.size, (unsigned long long)s.mtime,
				(unsigned long long)s.hash);
		}
		else
		{
			++missing;
			AC_Log("%s file missing %s sev=%d", tag, s.relativePath.c_str(), (int)s.severity);
		}
	}
	AC_Log("%s summary: ok=%d missing=%d gamedir=%s", tag, ok, missing, m_gameDir);
}

void GameFileScanner::DiffAgainstBaseline(const std::vector<GameFileSnapshot>& now, IntegrityScanResult& out) const
{
	for (const auto& s : now)
	{
		auto it = m_baselineByPath.find(s.relativePath);
		if (it == m_baselineByPath.end())
		{
			AC_Log("[files] unexpected path %s", s.relativePath.c_str());
			out.anyChange = true;
			if (s.severity == FileWatchSeverity::Critical)
			{
				out.criticalChange = true;
				out.reason = std::string("unexpected critical path: ") + s.relativePath;
			}
			continue;
		}

		const GameFileSnapshot& base = it->second;

		// Platform-specific binaries: missing on wrong OS is fine if baseline also missing.
		if (!base.exists && !s.exists)
			continue;

		if (base.exists != s.exists || base.size != s.size || base.hash != s.hash || base.mtime != s.mtime)
		{
			AC_Log("[files] CHANGED %s sev=%d exists %d->%d size %llu->%llu mtime %llu->%llu hash %016llx->%016llx",
				s.relativePath.c_str(), (int)s.severity,
				(int)base.exists, (int)s.exists,
				(unsigned long long)base.size, (unsigned long long)s.size,
				(unsigned long long)base.mtime, (unsigned long long)s.mtime,
				(unsigned long long)base.hash, (unsigned long long)s.hash);
			out.anyChange = true;

			if (s.severity == FileWatchSeverity::Critical || base.severity == FileWatchSeverity::Critical)
			{
				out.criticalChange = true;
				out.reason = std::string("critical file changed: ") + s.relativePath;
			}
		}
	}
}

bool GameFileScanner::CheckPluginMemoryIntegrity(std::string& reason) const
{
#ifdef __linux__
	Dl_info info{};
	if (dladdr(reinterpret_cast<const void*>(&AC_Log), &info) == 0 || !info.dli_fname || !info.dli_fname[0])
	{
		reason = "dladdr failed for anticheat module";
		return false;
	}

	// Module must still be mapped from disk path containing anticheat.
	const char* fname = info.dli_fname;
	if (!strstr(fname, "anticheat"))
	{
		reason = std::string("unexpected module mapping: ") + fname;
		return false;
	}

	struct stat st{};
	if (stat(fname, &st) != 0)
	{
		reason = std::string("mapped module missing on disk: ") + fname;
		return false;
	}

	// Compare against baseline anticheat.so if we had one.
	const char* relLinux = "addons/anticheat/bin/linuxsteamrt64/anticheat.so";
	auto it = m_baselineByPath.find(relLinux);
	if (it != m_baselineByPath.end() && it->second.exists)
	{
		if (static_cast<uint64_t>(st.st_size) != it->second.size)
		{
			reason = "anticheat.so size mismatch (disk vs baseline while mapped)";
			return false;
		}
		if (static_cast<uint64_t>(st.st_mtime) != it->second.mtime)
		{
			reason = "anticheat.so replaced on disk while process running";
			return false;
		}
	}

	return true;
#else
	(void)reason;
	return true;
#endif
}

void GameFileScanner::ApplyCriticalLockdown(const char* reason)
{
	m_lockdown = true;
	m_lockdownReason = reason ? reason : "critical integrity failure";
	AC_LogCritical("[files] LOCKDOWN active: %s (reject joins + ban)", m_lockdownReason.c_str());
}

void GameFileScanner::ScanAtStartup()
{
	if (!m_enabled)
	{
		AC_Log("[files] scan disabled");
		return;
	}

	if (!ResolveGameDir(m_gameDir, sizeof(m_gameDir)))
	{
		AC_LogCritical("[files] cannot resolve game directory");
		return;
	}

	AC_Log("[files] startup scan gamedir=%s", m_gameDir);
	m_baseline = ReadAll();
	m_baselineByPath.clear();
	for (const auto& s : m_baseline)
		m_baselineByPath[s.relativePath] = s;

	LogSnapshot("[files][startup]", m_baseline);

	std::string memReason;
	if (!CheckPluginMemoryIntegrity(memReason))
	{
		AC_LogCritical("[files] memory integrity at startup: %s", memReason.c_str());
		ApplyCriticalLockdown(memReason.c_str());
	}
}

IntegrityScanResult GameFileScanner::ScanNow(const char* tag)
{
	IntegrityScanResult result;
	if (!m_enabled || !m_gameDir[0])
	{
		result.ok = !m_lockdown;
		return result;
	}

	AC_Log("[files] scan tag=%s", tag ? tag : "?");
	const auto now = ReadAll();
	DiffAgainstBaseline(now, result);

	std::string memReason;
	if (!CheckPluginMemoryIntegrity(memReason))
	{
		result.memoryBroken = true;
		result.criticalChange = true;
		result.anyChange = true;
		result.reason = memReason;
		AC_LogCritical("[files] memory integrity FAILED: %s", memReason.c_str());
	}

	if (result.criticalChange)
	{
		result.ok = false;
		ApplyCriticalLockdown(result.reason.empty() ? "critical file/memory change" : result.reason.c_str());
	}
	else if (!result.anyChange)
	{
		AC_Log("[files] scan ok (unchanged) tag=%s", tag ? tag : "?");
	}

	return result;
}

IntegrityScanResult GameFileScanner::ScanOnPlayerJoin(uint64_t steamId, const char* playerName)
{
	AC_Log("[files] join scan steam=%llu name=%s",
		(unsigned long long)steamId, playerName ? playerName : "");

	IntegrityScanResult result = ScanNow("join");
	if (result.criticalChange || m_lockdown)
	{
		result.ok = false;
		result.criticalChange = true;
		if (result.reason.empty())
			result.reason = m_lockdownReason.empty() ? "critical integrity failure" : m_lockdownReason;
		AC_LogCritical("[files] CRITICAL on join steam=%llu reason=%s",
			(unsigned long long)steamId, result.reason.c_str());
		MarkIntegrityBan(steamId);
	}
	return result;
}

void GameFileScanner::Tick(float curtime)
{
	if (!m_enabled || !m_gameDir[0])
		return;
	if (curtime - m_lastPeriodicScan < kPeriodicIntervalSec)
		return;
	m_lastPeriodicScan = curtime;
	ScanNow("periodic");
}

bool GameFileScanner::IsSteamIntegrityBanned(uint64_t steamId) const
{
	return steamId != 0 && m_integrityBanned.find(steamId) != m_integrityBanned.end();
}

void GameFileScanner::MarkIntegrityBan(uint64_t steamId)
{
	if (steamId)
		m_integrityBanned.insert(steamId);
}
