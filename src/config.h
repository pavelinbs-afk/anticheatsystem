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
	bool enable_wallhack_detection = true;
	bool enable_smoke_detection = true;
	bool enable_skill_spike_detection = true;
	bool enable_stats_tracking = true;
	bool enable_movement_detection = true;
	bool enable_fps_drop_detection = true;
	bool enable_game_file_scan = true;

	// WH / smoke
	float wh_track_fov_deg = 4.0f;
	float wh_min_track_distance = 400.0f;
	int wh_streak_ticks = 32;

	// Client FPS hitch thresholds (GetRemoteFramerate)
	float fps_max_frame_ms = 45.0f;
	float fps_spike_stddev_ms = 20.0f;
	int fps_min_spikes = 4;

	float score_decay_per_second = 0.02f;

	static AntiCheatConfig LoadFromFile(const std::string& filepath);
};
