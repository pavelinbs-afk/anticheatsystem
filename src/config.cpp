#include "config.h"

#include <fstream>
#include <sstream>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "plugin.h"

#include <tier1/utlstring.h>

static bool ReadFileText(const std::string& path, std::string& out)
{
	std::ifstream f(path);
	if (!f)
		return false;
	std::ostringstream ss;
	ss << f.rdbuf();
	out = ss.str();
	return true;
}

static void NormalizeSlashes(std::string& path)
{
	for (char& c : path)
	{
		if (c == '\\')
			c = '/';
	}
}

static bool ResolveGameDir(std::string& out)
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

static bool JsonFindNumber(const std::string& json, const char* key, double& out)
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
	char* end = nullptr;
	double v = std::strtod(json.c_str() + p, &end);
	if (end == json.c_str() + p)
		return false;
	out = v;
	return true;
}

static bool JsonFindString(const std::string& json, const char* key, std::string& out)
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
	if (p >= json.size() || json[p] != '"')
		return false;
	++p;
	out.clear();
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
	return true;
}

static bool JsonFindBool(const std::string& json, const char* key, bool& out)
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

AntiCheatConfig AntiCheatConfig::LoadFromFile(const std::string& filepath)
{
	(void)filepath;

	AntiCheatConfig cfg;
	std::string json;
	std::string usedPath;

	std::vector<std::string> candidates;
	std::string gameDir;
	if (ResolveGameDir(gameDir))
	{
		candidates.push_back(gameDir + "/addons/anticheat/configs/anticheat_config.json");
		candidates.push_back(gameDir + "/csgo/addons/anticheat/configs/anticheat_config.json");
	}
	candidates.push_back("addons/anticheat/configs/anticheat_config.json");
	candidates.push_back("csgo/addons/anticheat/configs/anticheat_config.json");
	if (!filepath.empty())
		candidates.insert(candidates.begin(), filepath);

	for (const std::string& path : candidates)
	{
		if (ReadFileText(path, json))
		{
			usedPath = path;
			break;
		}
	}

	if (usedPath.empty())
	{
		AC_Log("config not found (tried gameDir=%s), using defaults",
			gameDir.empty() ? "?" : gameDir.c_str());
		return cfg;
	}

	double d = 0;
	bool b = false;
	if (JsonFindNumber(json, "snap_angle_threshold", d)) cfg.snap_angle_threshold = (float)d;
	if (JsonFindNumber(json, "snap_time_ms", d)) cfg.snap_time_threshold_ms = (int)d;
	if (JsonFindNumber(json, "kd_threshold", d)) { /* informational */ }
	if (JsonFindNumber(json, "headshot_pct_threshold", d)) cfg.headshot_ratio_threshold = (float)(d / 100.0);
	if (JsonFindNumber(json, "min_kills", d)) cfg.min_kills_for_stats = (int)d;
	if (JsonFindNumber(json, "speed_threshold", d)) cfg.speed_threshold = (float)d;
	if (JsonFindNumber(json, "monitor_threshold", d)) cfg.monitor_threshold = (float)d;
	if (JsonFindNumber(json, "warn_threshold", d)) cfg.warn_threshold = (float)d;
	if (JsonFindNumber(json, "report_threshold", d)) cfg.report_threshold = (float)d;
	if (JsonFindNumber(json, "kick_threshold", d)) cfg.report_threshold = (float)d; // legacy name
	if (JsonFindNumber(json, "ban_threshold", d)) cfg.ban_threshold = (float)d;
	if (JsonFindNumber(json, "duration_days", d)) cfg.ban_duration_days = (int)d;
	if (JsonFindNumber(json, "score_decay_per_minute", d)) cfg.score_decay_per_second = (float)(d / 60.0);
	if (JsonFindBool(json, "bhop_detection", b)) cfg.enable_bhop_detection = b;
	if (JsonFindBool(json, "enable_wallhack_detection", b)) cfg.enable_wallhack_detection = b;
	if (JsonFindBool(json, "enable_smoke_detection", b)) cfg.enable_smoke_detection = b;
	if (JsonFindBool(json, "enable_aim_detection", b)) cfg.enable_aim_detection = b;
	if (JsonFindNumber(json, "wh_track_fov_deg", d)) cfg.wh_track_fov_deg = (float)d;
	if (JsonFindNumber(json, "wh_min_track_distance", d)) cfg.wh_min_track_distance = (float)d;
	if (JsonFindNumber(json, "wh_streak_ticks", d)) cfg.wh_streak_ticks = (int)d;
	if (JsonFindBool(json, "enable_fps_drop_detection", b)) cfg.enable_fps_drop_detection = b;
	if (JsonFindBool(json, "enable_game_file_scan", b)) cfg.enable_game_file_scan = b;
	if (JsonFindNumber(json, "fps_max_frame_ms", d)) cfg.fps_max_frame_ms = (float)d;
	if (JsonFindNumber(json, "fps_spike_stddev_ms", d)) cfg.fps_spike_stddev_ms = (float)d;
	if (JsonFindNumber(json, "fps_min_spikes", d)) cfg.fps_min_spikes = (int)d;
	if (JsonFindBool(json, "enable_shot_tracking", b)) cfg.enable_shot_tracking = b;
	if (JsonFindNumber(json, "shot_hit_window_sec", d)) cfg.shot_hit_window_sec = (float)d;
	if (JsonFindNumber(json, "shot_aim_fov_deg", d)) cfg.shot_aim_fov_deg = (float)d;
	if (JsonFindNumber(json, "shot_min_hit_distance", d)) cfg.shot_min_hit_distance = (float)d;
	if (JsonFindNumber(json, "shot_min_shots_before_score", d)) cfg.shot_min_shots_before_score = (int)d;
	if (JsonFindBool(json, "enable_backend_check", b)) cfg.enable_backend_check = b;
	{
		std::string s;
		if (JsonFindString(json, "backend_api_url", s)) cfg.backend_api_url = s;
		if (JsonFindString(json, "backend_api_token", s)) cfg.backend_api_token = s;
		if (JsonFindString(json, "reason", s)) cfg.ban_reason = s;
	}

	AC_Log("config loaded from %s (ban>=%.0f report>=%.0f snap>=%.0f backend=%d)",
		usedPath.c_str(), cfg.ban_threshold, cfg.report_threshold,
		cfg.snap_angle_threshold, (int)cfg.enable_backend_check);
	return cfg;
}
