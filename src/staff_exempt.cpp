#include "staff_exempt.h"

#include "plugin.h"

#include <fstream>
#include <sstream>
#include <cstdlib>
#include <vector>
#include <cctype>

namespace {

void NormalizeSlashes(std::string& path)
{
	for (char& c : path)
	{
		if (c == '\\')
			c = '/';
	}
}

bool ResolveGameDir(std::string& out)
{
	out.clear();
	if (g_pEngine)
	{
		CBufferStringN<512> gameDir;
		g_pEngine->GetGameDir(gameDir);
		const char* gd = gameDir.Get();
		if (gd && *gd)
		{
			out = gd;
			NormalizeSlashes(out);
			return true;
		}
	}
	const char* plat = Plat_GetGameDirectory();
	if (plat && *plat)
	{
		out = plat;
		NormalizeSlashes(out);
		return true;
	}
	return false;
}

bool ReadFileText(const std::string& path, std::string& out)
{
	std::ifstream f(path);
	if (!f)
		return false;
	std::ostringstream ss;
	ss << f.rdbuf();
	out = ss.str();
	return true;
}

// Minimal parse of AdminPlugin admins.json:
// { "7656...": { "immunity": 95, ... }, ... }
void ParseExemptSteamIds(const std::string& json, std::unordered_set<uint64_t>& out, int minImmunity)
{
	out.clear();
	size_t i = 0;
	while (i < json.size())
	{
		// Find a quoted steam64 key (17 digits starting with 7656).
		if (json[i] != '"')
		{
			++i;
			continue;
		}
		size_t keyStart = i + 1;
		size_t keyEnd = json.find('"', keyStart);
		if (keyEnd == std::string::npos)
			break;

		std::string key = json.substr(keyStart, keyEnd - keyStart);
		i = keyEnd + 1;

		bool isSteam = key.size() == 17 && key.compare(0, 4, "7656") == 0;
		if (!isSteam)
			continue;
		for (char c : key)
		{
			if (!std::isdigit(static_cast<unsigned char>(c)))
			{
				isSteam = false;
				break;
			}
		}
		if (!isSteam)
			continue;

		// Look for "immunity" in the following object (until next top-level-ish steam key or brace depth).
		size_t objStart = json.find('{', i);
		if (objStart == std::string::npos || objStart > i + 64)
			continue;

		size_t immPos = json.find("\"immunity\"", objStart);
		size_t nextKey = json.find("\"7656", objStart + 1);
		if (immPos == std::string::npos)
			continue;
		if (nextKey != std::string::npos && immPos > nextKey)
			continue;

		size_t colon = json.find(':', immPos);
		if (colon == std::string::npos)
			continue;
		++colon;
		while (colon < json.size() && (json[colon] == ' ' || json[colon] == '\t'))
			++colon;

		char* end = nullptr;
		long imm = std::strtol(json.c_str() + colon, &end, 10);
		if (end == json.c_str() + colon)
			continue;

		if (imm >= minImmunity)
		{
			uint64_t sid = std::strtoull(key.c_str(), nullptr, 10);
			if (sid)
				out.insert(sid);
		}
	}
}

} // namespace

void StaffExemptList::Reload()
{
	std::string gameDir;
	std::vector<std::string> candidates;
	if (ResolveGameDir(gameDir))
	{
		candidates.push_back(gameDir + "/addons/counterstrikesharp/configs/plugins/AdminPlugin/admins.json");
		candidates.push_back(gameDir + "/csgo/addons/counterstrikesharp/configs/plugins/AdminPlugin/admins.json");
		candidates.push_back(gameDir + "/addons/counterstrikesharp/plugins/AdminPlugin/configs/plugins/admins.json");
	}
	candidates.push_back("addons/counterstrikesharp/configs/plugins/AdminPlugin/admins.json");

	std::string json;
	std::string used;
	for (const std::string& path : candidates)
	{
		if (ReadFileText(path, json))
		{
			used = path;
			break;
		}
	}

	std::unordered_set<uint64_t> next;
	if (!used.empty())
		ParseExemptSteamIds(json, next, 95); // Ст. Модератор+

	m_exempt.swap(next);
	AC_Log("staff exempt reload path=%s count=%d (imm>=95)",
		used.empty() ? "none" : used.c_str(), (int)m_exempt.size());
}

bool StaffExemptList::IsExempt(uint64_t steamId) const
{
	return steamId != 0 && m_exempt.count(steamId) > 0;
}

void StaffExemptList::Tick(float curtime)
{
	if (curtime < m_nextAllowedReload)
		return;
	m_nextAllowedReload = curtime + kReloadIntervalSec;
	Reload();
}
