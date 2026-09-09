#include "config.h"

#include <fstream>
#include <sstream>
#include <cstdlib>
#include <cstring>

#include "plugin.h"

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
	AntiCheatConfig cfg;
	std::string json;
	if (!ReadFileText(filepath, json))
	{
		AC_Log("config not found (%s), using defaults", filepath.c_str());
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
	if (JsonFindBool(json, "enable_aim_detection", b)) cfg.enable_aim_detection = b;

	AC_Log("config loaded from %s (ban>=%.0f report>=%.0f)", filepath.c_str(), cfg.ban_threshold, cfg.report_threshold);
	return cfg;
}
