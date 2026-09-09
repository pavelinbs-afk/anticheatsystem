#pragma once

#include <string>
#include <cstdint>

struct AntiCheatConfig {
	float snap_angle_threshold = 55.0f;
	int snap_time_threshold_ms = 50;
	float headshot_ratio_threshold = 0.85f;
	int min_kills_for_stats = 12;

	float speed_threshold = 380.0f;
	bool enable_bhop_detection = false;

	float monitor_threshold = 15.0f;
	float warn_threshold = 25.0f;
	float report_threshold = 35.0f;
	float ban_threshold = 50.0f;

	int ban_duration_days = 45;
	std::string ban_reason = "Использование читов (Античит система)";
	int ban_countdown_seconds = 10;

	bool enable_aim_detection = true;
	bool enable_wallhack_detection = false; // needs reliable visibility traces
	bool enable_skill_spike_detection = true;
	bool enable_stats_tracking = true;
	bool enable_movement_detection = true;

	float score_decay_per_second = 0.02f;

	static AntiCheatConfig LoadFromFile(const std::string& filepath);
};
