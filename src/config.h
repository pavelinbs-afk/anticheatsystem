#pragma once

#include <string>
#include <cstdint>

struct AntiCheatConfig {
	float snap_angle_threshold = 120.0f;
	int snap_time_threshold_ms = 50;
	float headshot_ratio_threshold = 0.85f;
	int min_kills_for_stats = 12;

	float speed_threshold = 520.0f;
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
	float wh_track_fov_deg = 3.0f;
	float wh_min_track_distance = 800.0f;
	int wh_streak_ticks = 96;

	// Client FPS hitch thresholds (GetRemoteFramerate) — only severe stalls
	float fps_max_frame_ms = 70.0f;
	float fps_spike_stddev_ms = 35.0f;
	int fps_min_spikes = 6;

	float score_decay_per_second = 0.05f;

	// Shot tracking (all weapon_fire, not only kills)
	bool enable_shot_tracking = true;
	float shot_hit_window_sec = 0.35f;
	float shot_aim_fov_deg = 1.25f;
	float shot_min_hit_distance = 350.0f;
	int shot_min_shots_before_score = 12;

	// Backend bypass/IP check → GET /api/cs2/anticheat/check (same logic as site /bypass)
	bool enable_backend_check = true;
	std::string backend_api_url = "https://perfecteam.ru";
	std::string backend_api_token = ""; // Bearer = CS2_FREEZE_SERVICE_TOKEN / CS_PLUGIN_SECRET

	static AntiCheatConfig LoadFromFile(const std::string& filepath);
};
