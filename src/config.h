#pragma once

#include <string>
#include <cstdint>

struct AntiCheatConfig {
	float snap_angle_threshold = 55.0f;
	int snap_time_threshold_ms = 50;
	float headshot_ratio_threshold = 0.85f;
	int min_kills_for_stats = 12;

	float speed_threshold = 520.0f;
	bool enable_bhop_detection = false;

	float monitor_threshold = 15.0f;
	float warn_threshold = 25.0f;
	float report_threshold = 35.0f;
	float admin_warn_threshold = 50.0f; // HTML warn to admins
	float ban_threshold = 55.0f;        // ban after warn + continued detections
	float fast_ban_threshold = 60.0f;   // after warn, reach this within fast_ban_window_sec → ban
	float fast_ban_window_sec = 60.0f;

	int ban_duration_days = 45;
	std::string ban_reason = "Использование читов (Античит система)";
	int ban_countdown_seconds = 10;
	// Legacy local Discord URL (unused for auto-ban). Discord goes via AdminPlugin → backend.
	std::string discord_webhook_url = "";

	// Verbose console spam (staff reload, file scan ok, score ticks, shot traces).
	// Important events (ban/report/warn/critical) always use AC_Log / AC_LogCritical.
	bool enable_debug_log = false;

	bool enable_aim_detection = true;
	bool enable_wallhack_detection = true;
	bool enable_smoke_detection = true;
	bool enable_skill_spike_detection = true;
	bool enable_stats_tracking = true;
	bool enable_movement_detection = true;
	bool enable_fps_drop_detection = false;
	// Optional: watch SERVER gamedir (anticheat.so / gameinfo.gi on the dedicated host).
	// Cannot read client files — CS2 dedicated has no access to player gameinfo.gi.
	bool enable_game_file_scan = false;

	// WH / smoke — long distant FOV lock only (avoid accidental peek aim)
	float wh_track_fov_deg = 3.5f;
	float wh_min_track_distance = 500.0f;
	int wh_streak_ticks = 240; // ~3.75s @64 tick (3.5–4s window)

	// Client FPS hitch thresholds (GetRemoteFramerate) — only severe stalls
	float fps_max_frame_ms = 70.0f;
	float fps_spike_stddev_ms = 35.0f;
	int fps_min_spikes = 6;

	float score_decay_per_second = 0.05f;

	// Shot tracking (all weapon_fire, not only kills)
	bool enable_shot_tracking = true;
	bool enable_combat_heuristics = true; // trigger / aimlock / doubletap / inhuman acc (CS2AC-inspired)
	bool enable_network_safety = true;    // veto soft detections on bad net
	float shot_hit_window_sec = 0.45f;
	float shot_aim_fov_deg = 3.5f;
	float shot_min_hit_distance = 200.0f;
	int shot_min_shots_before_score = 3;

	// Bypass/IP check via AdminPlugin Bearer → GET /api/cs2/anticheat/check (not local curl).
	bool enable_backend_check = true;
	std::string backend_api_url = "https://perfecteam.ru"; // legacy unused (AdminPlugin freeze API base)
	std::string backend_api_token = ""; // legacy unused

	static AntiCheatConfig LoadFromFile(const std::string& filepath);
	/// Writes addons/anticheat/configs/anticheat_status.json (version for AdminPlugin Discord footer).
	static void WriteStatusFile(const char* version);
};
