#include "anticheat_core.h"
#include "modules/aim_analyzer.h"
#include "modules/wallhack_detector.h"
#include "modules/movement_analyzer.h"
#include "modules/statistics_tracker.h"
#include "modules/integrity_checker.h"
#include "modules/suspicion_scorer.h"
#include "modules/fps_drop_detector.h"
#include "modules/game_file_scanner.h"
#include "modules/shot_tracker.h"
#include "modules/network_safety.h"
#include "modules/combat_heuristics.h"
#include "modules/math_utils.h"
#include "integration/admin_bridge.h"
#include "integration/backend_client.h"
#include "integration/discord_webhook.h"
#include "players.h"
#include "plugin.h"
#include "staff_exempt.h"

#include <chrono>
#include <ctime>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <vector>
#include <cstring>
#include <string>
#include <inetchannelinfo.h>
#include <tier1/utlstring.h>

AntiCheatCore* AntiCheatCore::s_Instance = nullptr;

AntiCheatCore* AntiCheatCore::GetInstance()
{
	if (!s_Instance)
		s_Instance = new AntiCheatCore();
	return s_Instance;
}

bool AntiCheatCore::Initialize()
{
	m_Config = AntiCheatConfig::LoadFromFile("addons/anticheat/configs/anticheat_config.json");

	m_AimAnalyzer = std::make_unique<AimAnalyzer>();
	m_WallhackDetector = std::make_unique<WallhackDetector>();
	m_MovementAnalyzer = std::make_unique<MovementAnalyzer>();
	m_StatisticsTracker = std::make_unique<StatisticsTracker>();
	m_IntegrityChecker = std::make_unique<IntegrityChecker>();
	m_SuspicionScorer = std::make_unique<SuspicionScorer>();
	m_FpsDropDetector = std::make_unique<FpsDropDetector>();
	m_GameFileScanner = std::make_unique<GameFileScanner>();
	m_ShotTracker = std::make_unique<ShotTracker>();
	m_BackendClient = std::make_unique<BackendClient>();
	m_DiscordWebhook = std::make_unique<DiscordWebhook>();
	m_StaffExempt = std::make_unique<StaffExemptList>();
	m_NetworkSafety = std::make_unique<NetworkSafety>();
	m_CombatHeuristics = std::make_unique<CombatHeuristics>();
	m_StaffExempt->Reload();

	m_AimAnalyzer->SetConfig(m_Config.snap_angle_threshold, (float)m_Config.snap_time_threshold_ms, 3.0f, 0.5f);
	m_WallhackDetector->SetConfig(m_Config.wh_track_fov_deg, m_Config.wh_min_track_distance, m_Config.wh_streak_ticks);
	m_MovementAnalyzer->SetConfig(m_Config.speed_threshold, 800.0f);
	m_FpsDropDetector->SetConfig(m_Config.fps_max_frame_ms, m_Config.fps_spike_stddev_ms, m_Config.fps_min_spikes);
	m_SuspicionScorer->SetThresholds(m_Config.monitor_threshold, m_Config.warn_threshold,
		m_Config.report_threshold, m_Config.admin_warn_threshold, m_Config.ban_threshold,
		m_Config.score_decay_per_second, m_Config.fast_ban_threshold, m_Config.fast_ban_window_sec);
	m_ShotTracker->SetConfig(m_Config.shot_hit_window_sec, m_Config.shot_aim_fov_deg,
		m_Config.shot_min_hit_distance, m_Config.shot_min_shots_before_score);
	m_GameFileScanner->SetEnabled(m_Config.enable_game_file_scan);
	m_GameFileScanner->ScanAtStartup();
	// Backend bypass/IP check goes through AdminPlugin Bearer (no curl on game host).
	m_BackendClient->SetConfig(m_Config.backend_api_url, m_Config.backend_api_token, false);
	m_DiscordWebhook->SetWebhookUrl(m_Config.discord_webhook_url);
	AntiCheatConfig::WriteStatusFile(g_AntiCheatPlugin.GetVersion());

	std::memset(m_SlotToSteam, 0, sizeof(m_SlotToSteam));
	AC_Log("core initialized (shot_track=%d backend_via=AdminPlugin discord=%d staff_exempt=%d)",
		(int)m_Config.enable_shot_tracking,
		(int)m_DiscordWebhook->IsEnabled(),
		(int)(m_StaffExempt ? m_StaffExempt->Size() : 0));
	return true;
}

bool AntiCheatCore::IsStaffExempt(uint64_t steamId) const
{
	return m_StaffExempt && m_StaffExempt->IsExempt(steamId);
}

void AntiCheatCore::AddSoftScore(uint64_t steamId, const char* module, float score, const char* reason, const PlayerProfile* netProfile)
{
	if (score <= 0.0f || !m_SuspicionScorer)
		return;
	if (m_Config.enable_network_safety && m_NetworkSafety && netProfile &&
		m_NetworkSafety->ShouldVetoSoftDetections(*netProfile))
	{
		AC_LogDebug("network veto +%.1f [%s] steam=%llu ping=%.0f jitter=%.0f (soft detect skipped)",
			score, module, (unsigned long long)steamId,
			netProfile->lastPingMs, netProfile->lastJitterMs);
		return;
	}
	m_SuspicionScorer->AddScore(steamId, module, score, reason);
}

void AntiCheatCore::Shutdown()
{
	m_PlayerProfiles.clear();
	AC_Log("core shutdown");
}

void AntiCheatCore::PrintStatus() const
{
	int tracked = (int)m_PlayerProfiles.size();
	int slots = 0;
	for (int i = 0; i < AC_MAXPLAYERS; ++i)
	{
		if (m_SlotToSteam[i])
			++slots;
	}

	Msg("  core        : OK\n");
	Msg("  players     : tracked=%d slots_mapped=%d staff_exempt=%zu\n",
		tracked, slots, m_StaffExempt ? m_StaffExempt->Size() : 0);
	Msg("  discord     : via AdminPlugin Bearer → backend /api/cs2/anticheat/ban-notify\n");
	Msg("  discord_local: %s (legacy local curl unused on ban)\n",
		(m_DiscordWebhook && m_DiscordWebhook->IsEnabled()) ? m_DiscordWebhook->GetStatusLabel() : "off");
	Msg("  debug_log   : %s\n", AC_IsDebugLogEnabled() ? "ON" : "OFF");
	Msg("  backend     : %s via AdminPlugin Bearer → /api/cs2/anticheat/check\n",
		m_Config.enable_backend_check ? "ON" : "OFF");
	Msg("  confidence  : monitor=%.0f warn=%.0f report=%.0f admin_warn=%.0f ban=%.0f fast=%.0f/%.0fs (watch, 2-channel ban)\n",
		m_Config.monitor_threshold, m_Config.warn_threshold, m_Config.report_threshold,
		m_Config.admin_warn_threshold, m_Config.ban_threshold,
		m_Config.fast_ban_threshold, m_Config.fast_ban_window_sec);
	Msg("  ban         : days=%d reason=\"%s\"\n",
		m_Config.ban_duration_days, m_Config.ban_reason.c_str());
	Msg("  modules     : aim=%d wh=%d move=%d stats=%d fps=%d shots=%d combat=%d netsafe=%d server_filescan=%d (client files N/A)\n",
		(int)m_Config.enable_aim_detection,
		(int)m_Config.enable_wallhack_detection,
		(int)m_Config.enable_movement_detection,
		(int)m_Config.enable_stats_tracking,
		(int)m_Config.enable_fps_drop_detection,
		(int)m_Config.enable_shot_tracking,
		(int)m_Config.enable_combat_heuristics,
		(int)m_Config.enable_network_safety,
		(int)m_Config.enable_game_file_scan);
	Msg("  subsystems  : aim=%d wh=%d move=%d stats=%d fps=%d shot=%d combat=%d net=%d scorer=%d scan=%d backend=%d discord=%d staff=%d\n",
		m_AimAnalyzer ? 1 : 0,
		m_WallhackDetector ? 1 : 0,
		m_MovementAnalyzer ? 1 : 0,
		m_StatisticsTracker ? 1 : 0,
		m_FpsDropDetector ? 1 : 0,
		m_ShotTracker ? 1 : 0,
		m_CombatHeuristics ? 1 : 0,
		m_NetworkSafety ? 1 : 0,
		m_SuspicionScorer ? 1 : 0,
		m_GameFileScanner ? 1 : 0,
		m_BackendClient ? 1 : 0,
		m_DiscordWebhook ? 1 : 0,
		m_StaffExempt ? 1 : 0);
}

void AntiCheatCore::RequestDiscordWebhookTest()
{
	AC_Log("ac_webhook_test: via AdminPlugin → backend Discord");
	AdminBridge_DiscordTest();
}

bool AntiCheatCore::RequestDiscordWebhookResend(uint64_t steamId, const char* playerName)
{
	if (steamId == 0)
	{
		AC_Log("ac_webhook_resend: invalid steamid");
		return false;
	}

	const char* map = "resend";
	if (CGlobalVars* gv = GetGlobals())
	{
		const char* m = STRING(gv->mapname);
		if (m && m[0])
			map = m;
	}
	const std::string detectReason = m_SuspicionScorer
		? m_SuspicionScorer->GetBanReasonTags(steamId)
		: std::string("detect");
	AdminBridge_DiscordNotify(steamId, playerName, 0.0f, map, true, detectReason.c_str());
	return true;
}

int AntiCheatCore::RequestDiscordWebhookResendFromBansFile()
{

	std::string gameDir;
	if (g_pEngine)
	{
		CBufferStringN<512> gd;
		g_pEngine->GetGameDir(gd);
		if (gd.Get() && gd.Get()[0])
			gameDir = gd.Get();
	}
	for (char& c : gameDir)
	{
		if (c == '\\')
			c = '/';
	}

	std::vector<std::string> candidates;
	if (!gameDir.empty())
	{
		candidates.push_back(gameDir + "/addons/counterstrikesharp/configs/plugins/AdminPlugin/banned_steamids.json");
		candidates.push_back(gameDir + "/csgo/addons/counterstrikesharp/configs/plugins/AdminPlugin/banned_steamids.json");
	}
	candidates.push_back("addons/counterstrikesharp/configs/plugins/AdminPlugin/banned_steamids.json");

	std::string json;
	std::string used;
	for (const std::string& path : candidates)
	{
		std::ifstream f(path);
		if (!f)
			continue;
		std::ostringstream ss;
		ss << f.rdbuf();
		json = ss.str();
		used = path;
		break;
	}

	if (used.empty())
	{
		AC_Log("ac_webhook_resend_all: banned_steamids.json not found");
		return 0;
	}

	auto JsonGetStringField = [](const std::string& obj, const char* key) -> std::string
	{
		std::string needle = std::string("\"") + key + "\"";
		size_t k = obj.find(needle);
		if (k == std::string::npos)
			return {};
		size_t colon = obj.find(':', k + needle.size());
		if (colon == std::string::npos)
			return {};
		size_t q1 = obj.find('"', colon + 1);
		if (q1 == std::string::npos)
			return {};
		size_t q2 = q1 + 1;
		std::string out;
		while (q2 < obj.size() && obj[q2] != '"')
		{
			if (obj[q2] == '\\' && q2 + 1 < obj.size())
			{
				out.push_back(obj[q2 + 1]);
				q2 += 2;
				continue;
			}
			out.push_back(obj[q2++]);
		}
		return out;
	};

	auto TrimAscii = [](std::string s) -> std::string
	{
		while (!s.empty() && (s.front() == ' ' || s.front() == '\t'))
			s.erase(s.begin());
		while (!s.empty() && (s.back() == ' ' || s.back() == '\t'))
			s.pop_back();
		return s;
	};

	auto EqualsIgnoreCaseAscii = [](const std::string& a, const std::string& b) -> bool
	{
		if (a.size() != b.size())
			return false;
		for (size_t i = 0; i < a.size(); ++i)
		{
			unsigned char ca = static_cast<unsigned char>(a[i]);
			unsigned char cb = static_cast<unsigned char>(b[i]);
			if (ca >= 'A' && ca <= 'Z')
				ca = static_cast<unsigned char>(ca - 'A' + 'a');
			if (cb >= 'A' && cb <= 'Z')
				cb = static_cast<unsigned char>(cb - 'A' + 'a');
			if (ca != cb)
				return false;
		}
		return true;
	};

	auto IsAnticheatAdminName = [&](const std::string& raw) -> bool
	{
		const std::string admin = TrimAscii(raw);
		if (admin.empty())
			return false;
		// Exact labels used historically / in AdminPlugin.
		if (admin == "Античит система")
			return true;
		if (EqualsIgnoreCaseAscii(admin, "Anti-Cheat System"))
			return true;
		if (EqualsIgnoreCaseAscii(admin, "anticheat"))
			return true;
		return false;
	};

	int queued = 0;
	size_t pos = 0;
	while (pos < json.size())
	{
		size_t objStart = json.find('{', pos);
		if (objStart == std::string::npos)
			break;
		size_t objEnd = json.find('}', objStart);
		if (objEnd == std::string::npos)
			break;
		std::string obj = json.substr(objStart, objEnd - objStart + 1);
		pos = objEnd + 1;

		std::string adminName = JsonGetStringField(obj, "AdminName");
		if (adminName.empty())
			adminName = JsonGetStringField(obj, "adminName");
		if (adminName.empty())
			adminName = JsonGetStringField(obj, "admin");
		if (!IsAnticheatAdminName(adminName))
			continue;

		std::string sidStr = JsonGetStringField(obj, "steamId64");
		if (sidStr.empty())
			sidStr = JsonGetStringField(obj, "SteamId64");
		uint64_t sid = sidStr.empty() ? 0 : std::strtoull(sidStr.c_str(), nullptr, 10);
		if (sid == 0)
			continue;

		std::string nick = JsonGetStringField(obj, "name");
		if (nick.empty())
			nick = "?";

		std::string fileReason = JsonGetStringField(obj, "reason");
		if (fileReason.empty())
			fileReason = JsonGetStringField(obj, "Reason");
		const char* reasonArg = fileReason.empty() ? nullptr : fileReason.c_str();
		AdminBridge_DiscordNotify(sid, nick.c_str(), 0.0f, "resend", true, reasonArg);
		++queued;
	}

	AC_Log("ac_webhook_resend_all: file=%s queued=%d via AdminPlugin→backend (AdminName Anti-Cheat System|Античит система|anticheat)",
		used.c_str(), queued);
	return queued;
}

std::string AntiCheatCore::GetClientIpForSlot(int slot)
{
	if (!g_pEngine || slot < 0)
		return {};
	INetChannelInfo* net = g_pEngine->GetPlayerNetInfo(CPlayerSlot(slot));
	if (!net)
		return {};
	const char* addr = net->GetAddress();
	if (!addr || !*addr)
		return {};

	// Formats: "1.2.3.4:27005" or "[::1]:27005"
	std::string s(addr);
	if (!s.empty() && s.front() == '[')
	{
		size_t end = s.find(']');
		if (end != std::string::npos)
			return s.substr(1, end - 1);
	}
	size_t colon = s.rfind(':');
	if (colon != std::string::npos)
		s.resize(colon);
	return s;
}

std::vector<PlayerProfile*> AntiCheatCore::CollectEnemies(PlayerProfile* profile)
{
	std::vector<PlayerProfile*> enemies;
	if (!profile)
		return enemies;
	const int team = GetPlayerTeamNum(profile->slot);
	for (auto& [sid, other] : m_PlayerProfiles)
	{
		(void)sid;
		if (other.get() == profile)
			continue;
		if (!IsPlayerAlive(other->slot))
			continue;
		int t2 = GetPlayerTeamNum(other->slot);
		if (team >= 2 && t2 >= 2 && team != t2)
			enemies.push_back(other.get());
	}
	return enemies;
}

void AntiCheatCore::QueueBackendCheck(PlayerProfile* profile)
{
	if (!profile || !m_Config.enable_backend_check)
		return;

	std::string ip = profile->ipAddress.empty() ? GetClientIpForSlot(profile->slot) : profile->ipAddress;
	if (profile->ipAddress.empty() && !ip.empty())
		profile->ipAddress = ip;

	CGlobalVars* gv = GetGlobals();
	const float now = gv ? gv->curtime : 0.0f;
	AdminBridge_BackendCheck(profile->steamId, ip.c_str(), profile->name.c_str());

	if (ip.empty())
	{
		// IP often arrives a beat late — keep sticky retries until we have it.
		profile->backendCheckPending = true;
		profile->backendCheckDone = false;
		profile->backendCheckAttempts = 1;
		profile->backendCheckNextAt = now + 2.0f;
	}
	else
	{
		profile->backendCheckDone = true;
		profile->backendCheckPending = false;
	}
}

void AntiCheatCore::ProcessBackendResults()
{
	// Kick/report handled by AdminPlugin css_anticheat_backend_check (Bearer → backend).
}

void AntiCheatCore::OnPlayerConnect(int slot, uint64_t steamID, const char* name)
{
	if (slot < 0 || slot >= AC_MAXPLAYERS || steamID == 0)
		return;

	auto profile = std::make_shared<PlayerProfile>();
	profile->steamId = steamID;
	profile->slot = slot;
	profile->name = name ? name : "";
	profile->connectionTime = std::chrono::steady_clock::now();
	profile->lastActivityTime = profile->connectionTime;
	profile->ipAddress = GetClientIpForSlot(slot);

	CGlobalVars* gv = GetGlobals();
	profile->lastMoveTime = gv ? gv->curtime : 0.0f;
	profile->movementIgnoreUntil = profile->lastMoveTime + 5.0f;
	profile->samplesValid = false;
	profile->lastTeamNum = GetPlayerTeamNum(slot);

	// Do NOT kick from sticky local cache — backend check decides after unban.
	// Local MarkBanned only blocks duplicate ApplyBan from scoring this uptime.
	if (m_SuspicionScorer->IsAlreadyBanned(steamID))
	{
		AC_Log("connect steam=%llu had local ban flag — waiting backend re-check (no kick)",
			(unsigned long long)steamID);
	}

	m_PlayerProfiles[steamID] = profile;
	m_SlotToSteam[slot] = steamID;

	if (IsStaffExempt(steamID))
	{
		AC_Log("connect steam=%llu staff exempt (ст. модератор+) — AC ignore",
			(unsigned long long)steamID);
		return;
	}

	QueueBackendCheck(profile.get());
}

void AntiCheatCore::OnPlayerDisconnect(int slot, uint64_t steamID)
{
	uint64_t sid = steamID ? steamID : (slot >= 0 && slot < AC_MAXPLAYERS ? m_SlotToSteam[slot] : 0);
	if (slot >= 0 && slot < AC_MAXPLAYERS)
		m_SlotToSteam[slot] = 0;
	if (sid)
	{
		if (m_SuspicionScorer)
			m_SuspicionScorer->SetAdminCheckHold(sid, false);
		m_PlayerProfiles.erase(sid);
	}
}

PlayerProfile* AntiCheatCore::GetPlayerBySlot(int slot)
{
	if (slot < 0 || slot >= AC_MAXPLAYERS)
		return nullptr;
	uint64_t sid = m_SlotToSteam[slot];
	return sid ? GetPlayerProfile(sid) : nullptr;
}

PlayerProfile* AntiCheatCore::GetPlayerProfile(uint64_t steamID)
{
	auto it = m_PlayerProfiles.find(steamID);
	return it != m_PlayerProfiles.end() ? it->second.get() : nullptr;
}

void AntiCheatCore::OnPlayerDeath(int attackerSlot, int victimSlot, bool headshot, bool thrusmoke, bool attackerblind, bool noscope, int penetrated)
{
	PlayerProfile* attacker = GetPlayerBySlot(attackerSlot);
	PlayerProfile* victim = GetPlayerBySlot(victimSlot);
	if (attacker && IsStaffExempt(attacker->steamId))
		attacker = nullptr;
	if (attacker && victim && attacker->steamId != victim->steamId)
	{
		attacker->kills++;
		attacker->killsThisRound++;
		if (headshot)
			attacker->headshots++;

		float pos[3]{}, ang[3]{}, vel[3]{};
		if (SamplePlayerState(attacker->slot, pos, ang, vel))
		{
			attacker->viewAngles = AcAngle(ang[0], ang[1], ang[2]);
			attacker->position = AcVec3(pos[0], pos[1], pos[2]);
		}
		if (SamplePlayerState(victim->slot, pos, ang, vel))
			victim->position = AcVec3(pos[0], pos[1], pos[2]);

		const float dist = VectorDistance(attacker->position, victim->position);

		// Kill-time aim is a secondary signal; primary aim scoring is on all shots/hits.
		float aimScore = m_AimAnalyzer->OnPlayerShoot(*attacker, *victim, headshot, dist);
		if (aimScore > 0.0f)
			AddSoftScore(attacker->steamId, "AimAnalyzer", aimScore, "Combat aim anomaly", attacker);

		if (m_Config.enable_wallhack_detection || m_Config.enable_smoke_detection)
		{
			CombatKillFlags flags;
			flags.headshot = headshot;
			flags.thrusmoke = thrusmoke && m_Config.enable_smoke_detection;
			flags.attackerblind = attackerblind && m_Config.enable_wallhack_detection;
			flags.noscope = noscope;
			flags.penetrated = m_Config.enable_wallhack_detection ? penetrated : 0;
			flags.distance = dist;

			float whScore = m_WallhackDetector->OnCombatKill(*attacker, *victim, flags);
			if (whScore > 0.0f)
			{
				const char* reason = "Wallbang kill";
				if (thrusmoke)
					reason = "Smoke kill";
				else if (attackerblind)
					reason = "Blind kill";
				else if (attacker->wallAimTargetSteam == victim->steamId &&
					attacker->wallAimStreak >= m_Config.wh_streak_ticks)
					reason = "Prefire kill";
				AddSoftScore(attacker->steamId, "WallhackDetector", whScore, reason, attacker);
			}
		}
	}
	if (victim)
	{
		victim->deaths++;
		CGlobalVars* gv = GetGlobals();
		victim->movementIgnoreUntil = (gv ? gv->curtime : 0.0f) + 3.0f;
		victim->samplesValid = false;
		victim->wallAimStreak = 0;
	}
}

void AntiCheatCore::OnPlayerHurt(int attackerSlot, int victimSlot, float damage, int hitgroup)
{
	PlayerProfile* attacker = GetPlayerBySlot(attackerSlot);
	PlayerProfile* victim = GetPlayerBySlot(victimSlot);
	if (!attacker || !victim || attacker->steamId == victim->steamId)
		return;
	if (IsStaffExempt(attacker->steamId))
		return;

	attacker->totalDamage += damage;
	attacker->shotsHit++;

	// Fresh sample at event time — GameFrame angles can be a tick stale.
	float pos[3]{}, ang[3]{}, vel[3]{};
	if (SamplePlayerState(attacker->slot, pos, ang, vel))
	{
		attacker->lastViewAngles = attacker->viewAngles;
		attacker->viewAngles = AcAngle(ang[0], ang[1], ang[2]);
		attacker->position = AcVec3(pos[0], pos[1], pos[2]);
		attacker->velocity = AcVec3(vel[0], vel[1], vel[2]);
	}
	if (SamplePlayerState(victim->slot, pos, ang, vel))
	{
		victim->position = AcVec3(pos[0], pos[1], pos[2]);
	}

	CGlobalVars* gv = GetGlobals();
	const float curtime = gv ? gv->curtime : 0.0f;

	if (m_Config.enable_shot_tracking && m_ShotTracker)
	{
		float score = m_ShotTracker->OnPlayerHurt(*attacker, *victim, damage, hitgroup, curtime);
		if (score > 0.0f)
			AddSoftScore(attacker->steamId, "ShotTracker", score, "Shot aim anomaly", attacker);
	}

	if (m_Config.enable_combat_heuristics && m_CombatHeuristics)
	{
		float hScore = m_CombatHeuristics->OnPlayerHurt(*attacker, *victim, curtime);
		if (hScore > 0.0f)
			AddSoftScore(attacker->steamId, "CombatHeuristics", hScore, "Trigger/accuracy", attacker);
	}

	if (m_Config.enable_wallhack_detection && m_WallhackDetector)
	{
		const float dist = VectorDistance(attacker->position, victim->position);
		CombatKillFlags whFlags;
		whFlags.headshot = (hitgroup == 1);
		whFlags.distance = dist;
		// Hurt events lack smoke/pen flags; score distant FOV-lock prefire hits.
		float whScore = m_WallhackDetector->OnCombatHurt(*attacker, *victim, whFlags, hitgroup == 1);
		if (whScore > 0.0f)
		{
			const char* reason = (attacker->wallAimTargetSteam == victim->steamId) ? "Prefire hit" : "Wallhack hit";
			AddSoftScore(attacker->steamId, "WallhackDetector", whScore, reason, attacker);
		}
	}
}

void AntiCheatCore::OnWeaponFire(int shooterSlot)
{
	PlayerProfile* shooter = GetPlayerBySlot(shooterSlot);
	if (!shooter)
	{
		static int s_miss = 0;
		if (shooterSlot >= 0 && s_miss < 15)
		{
			++s_miss;
			AC_Log("weapon_fire slot=%d but no profile (connect/xuid?)", shooterSlot);
		}
		return;
	}
	if (IsStaffExempt(shooter->steamId))
		return;

	float pos[3]{}, ang[3]{}, vel[3]{};
	if (SamplePlayerState(shooter->slot, pos, ang, vel))
	{
		shooter->lastViewAngles = shooter->viewAngles;
		shooter->viewAngles = AcAngle(ang[0], ang[1], ang[2]);
		shooter->lastPosition = shooter->position;
		shooter->position = AcVec3(pos[0], pos[1], pos[2]);
		shooter->velocity = AcVec3(vel[0], vel[1], vel[2]);
	}

	if (!m_Config.enable_shot_tracking || !m_ShotTracker)
	{
		shooter->shotsFired++;
		return;
	}

	CGlobalVars* gv = GetGlobals();
	const float curtime = gv ? gv->curtime : 0.0f;
	auto enemies = CollectEnemies(shooter);
	float score = m_ShotTracker->OnWeaponFire(*shooter, curtime, enemies);
	if (score > 0.0f)
		AddSoftScore(shooter->steamId, "ShotTracker", score, "Rage aim snap", shooter);

	if (m_Config.enable_combat_heuristics && m_CombatHeuristics)
	{
		float hScore = m_CombatHeuristics->OnWeaponFire(*shooter, curtime, enemies);
		if (hScore > 0.0f)
			AddSoftScore(shooter->steamId, "CombatHeuristics", hScore, "Trigger/aimlock/doubletap", shooter);
	}
}

void AntiCheatCore::SampleAllPlayers()
{
	for (auto& [steamID, profile] : m_PlayerProfiles)
	{
		(void)steamID;
		float pos[3]{}, ang[3]{}, vel[3]{};
		if (!SamplePlayerState(profile->slot, pos, ang, vel))
			continue;

		profile->lastPosition = profile->position;
		profile->position = AcVec3(pos[0], pos[1], pos[2]);
		profile->lastViewAngles = profile->viewAngles;
		profile->viewAngles = AcAngle(ang[0], ang[1], ang[2]);
		profile->velocity = AcVec3(vel[0], vel[1], vel[2]);

		const float dPitch = AngleDifference(profile->viewAngles.pitch, profile->lastViewAngles.pitch);
		const float dYaw = AngleDifference(profile->viewAngles.yaw, profile->lastViewAngles.yaw);
		const float snap = std::sqrt(dPitch * dPitch + dYaw * dYaw);
		profile->angleDeltaHistory.push_back(snap);
		while (profile->angleDeltaHistory.size() > 48)
			profile->angleDeltaHistory.pop_front();

		// Air heuristic: vertical velocity
		profile->inAir = std::fabs(profile->velocity.z) > 20.0f;

		if (profile->ipAddress.empty())
			profile->ipAddress = GetClientIpForSlot(profile->slot);

		// Sticky backend-check: retry when IP arrives or AdminPlugin may have been late.
		if (m_Config.enable_backend_check && profile->backendCheckPending && !profile->backendCheckDone)
		{
			CGlobalVars* gv = GetGlobals();
			const float now = gv ? gv->curtime : 0.0f;
			if (now >= profile->backendCheckNextAt)
			{
				const bool haveIp = !profile->ipAddress.empty();
				AdminBridge_BackendCheck(profile->steamId, profile->ipAddress.c_str(), profile->name.c_str());
				profile->backendCheckAttempts++;
				if (haveIp || profile->backendCheckAttempts >= 5)
				{
					profile->backendCheckDone = true;
					profile->backendCheckPending = false;
				}
				else
				{
					profile->backendCheckNextAt = now + 2.0f;
				}
			}
		}
	}
}

void AntiCheatCore::OnGameFrame()
{
	static auto lastTick = std::chrono::steady_clock::now();
	auto now = std::chrono::steady_clock::now();
	float deltaTime = std::chrono::duration<float>(now - lastTick).count();
	lastTick = now;
	if (deltaTime <= 0.0f || deltaTime > 1.0f)
		deltaTime = 0.015f;

	SampleAllPlayers();
	{
		CGlobalVars* gvBridge = GetGlobals();
		AdminBridge_Tick(gvBridge ? gvBridge->curtime : 0.0f);
	}
	ProcessBackendResults();
	m_SuspicionScorer->DecayScores(deltaTime);

	CGlobalVars* gvTick = GetGlobals();
	if (m_StaffExempt)
		m_StaffExempt->Tick(gvTick ? gvTick->curtime : 0.0f);

	if (m_GameFileScanner && m_Config.enable_game_file_scan)
	{
		CGlobalVars* gv = GetGlobals();
		m_GameFileScanner->Tick(gv ? gv->curtime : 0.0f);
	}

	CGlobalVars* gv = GetGlobals();
	const float curtime = gv ? gv->curtime : 0.0f;
	for (auto& [steamID, profile] : m_PlayerProfiles)
	{
		(void)steamID;
		if (m_ShotTracker && m_Config.enable_shot_tracking)
			m_ShotTracker->FlushStale(*profile, curtime);
		ProcessPlayer(profile.get());
	}
}

void AntiCheatCore::OnRoundStart()
{
	CGlobalVars* gv = GetGlobals();
	const float now = gv ? gv->curtime : 0.0f;
	for (auto& [steamID, profile] : m_PlayerProfiles)
	{
		(void)steamID;
		profile->roundsPlayed++;
		profile->killsThisRound = 0;
		profile->movementIgnoreUntil = now + 3.0f;
		profile->samplesValid = false;
		profile->wallAimStreak = 0;
		profile->wallAimTargetSteam = 0;
		profile->wallTrackScoreTicks = 0;
		profile->movementSpeedStreak = 0;
		profile->pendingRageSnapDeg = 0.0f;
		profile->pendingRageSnapUntil = 0.0f;
		profile->pendingRageSnapTarget = 0;
		while (profile->recentShots.size() > 8)
			profile->recentShots.pop_front();
	}
}

void AntiCheatCore::OnRoundEnd()
{
	if (!m_Config.enable_skill_spike_detection)
		return;
	for (auto& [steamID, profile] : m_PlayerProfiles)
	{
		(void)steamID;
		float spike = m_StatisticsTracker->CompareWithHistory(*profile);
		if (spike > 0.0f)
			m_SuspicionScorer->AddScore(profile->steamId, "StatisticsTracker", spike, "Skill spike");
	}
}

void AntiCheatCore::ProcessPlayer(PlayerProfile* profile)
{
	if (!profile || profile->isBanned || profile->actionTakenBan)
		return;
	if (IsStaffExempt(profile->steamId))
		return;

	const bool alive = IsPlayerAlive(profile->slot);
	const int team = GetPlayerTeamNum(profile->slot);
	CGlobalVars* gv = GetGlobals();
	const float curtime = gv ? gv->curtime : 0.0f;

	float scoreDelta = 0.0f;

	if (g_pEngine && m_Config.enable_network_safety && m_NetworkSafety)
	{
		INetChannelInfo* net = g_pEngine->GetPlayerNetInfo(CPlayerSlot(profile->slot));
		if (net && !net->IsTimingOut())
			m_NetworkSafety->Sample(*profile, net);
	}

	auto enemies = (alive && team >= 2) ? CollectEnemies(profile) : std::vector<PlayerProfile*>{};
	if (m_Config.enable_combat_heuristics && m_CombatHeuristics && alive && team >= 2)
		m_CombatHeuristics->OnTick(*profile, enemies, curtime);

	if (m_Config.enable_aim_detection && alive && team >= 2)
	{
		scoreDelta = m_AimAnalyzer->Analyze(*profile, 0.015f);
		if (scoreDelta > 0.0f)
			AddSoftScore(profile->steamId, "AimAnalyzer", scoreDelta, "Aim anomaly", profile);
	}

	if (m_Config.enable_wallhack_detection && alive && team >= 2)
	{
		scoreDelta = m_WallhackDetector->Analyze(*profile, enemies);
		if (scoreDelta > 0.0f)
			AddSoftScore(profile->steamId, "WallhackDetector", scoreDelta, "Wall track", profile);
	}

	if (m_Config.enable_movement_detection)
	{
		scoreDelta = m_MovementAnalyzer->Analyze(*profile, 0.015f, curtime, team, alive);
		if (scoreDelta > 0.0f)
			AddSoftScore(profile->steamId, "MovementAnalyzer", scoreDelta, "Movement anomaly", profile);
	}

	if (m_Config.enable_fps_drop_detection && g_pEngine)
	{
		INetChannelInfo* net = g_pEngine->GetPlayerNetInfo(CPlayerSlot(profile->slot));
		if (net && !net->IsTimingOut())
		{
			float frameTime = 0.0f;
			float frameStd = 0.0f;
			float frameStartStd = 0.0f;
			net->GetRemoteFramerate(&frameTime, &frameStd, &frameStartStd);
			scoreDelta = m_FpsDropDetector->Analyze(*profile, frameTime, frameStd);
			if (scoreDelta > 0.0f)
				AddSoftScore(profile->steamId, "FpsDropDetector", scoreDelta, "Client FPS hitch", profile);
		}
	}

	if (m_Config.enable_stats_tracking)
	{
		scoreDelta = m_StatisticsTracker->AnalyzeSession(*profile);
		if (scoreDelta > 0.0f)
			m_SuspicionScorer->AddScore(profile->steamId, "StatisticsTracker", scoreDelta, "High statistics");
	}

	scoreDelta = m_IntegrityChecker->CheckIntegrity(*profile);
	if (scoreDelta > 0.0f)
		m_SuspicionScorer->AddScore(profile->steamId, "IntegrityChecker", scoreDelta, "Integrity");

	CheckAndApplyActions(profile);
}

void AntiCheatCore::CheckAndApplyActions(PlayerProfile* profile)
{
	if (!profile || profile->actionTakenBan)
		return;
	if (IsStaffExempt(profile->steamId))
		return;

	// Local MarkBanned = already issued ApplyBan this uptime → never call it again.
	if (m_SuspicionScorer->IsAlreadyBanned(profile->steamId))
		return;

	ScorerAction action = m_SuspicionScorer->EvaluatePlayer(*profile);

	if (action == ScorerAction::REPORT && !profile->actionTakenReport)
	{
		profile->actionTakenReport = true;
		AdminBridge_SendReport(profile->steamId, profile->name.c_str());
		AC_Log("auto-report steam=%llu name=%s score=%.1f",
			(unsigned long long)profile->steamId, profile->name.c_str(),
			m_SuspicionScorer->GetScore(profile->steamId));
	}

	if (action == ScorerAction::ADMIN_WARN && !profile->actionTakenAdminWarn)
	{
		profile->actionTakenAdminWarn = true;
		const float score = m_SuspicionScorer->GetScore(profile->steamId);
		AdminBridge_WarnAdmins(profile->steamId, profile->name.c_str(), score);
		AC_Log("admin-warn steam=%llu name=%s score=%.1f (HTML to admins, ban deferred until more score)",
			(unsigned long long)profile->steamId, profile->name.c_str(), score);

		if (!profile->actionTakenReport)
		{
			profile->actionTakenReport = true;
			AdminBridge_SendReport(profile->steamId, profile->name.c_str());
			AC_Log("auto-report (with admin-warn) steam=%llu name=%s score=%.1f",
				(unsigned long long)profile->steamId, profile->name.c_str(), score);
		}
	}

	if (action == ScorerAction::BAN && !profile->actionTakenBan)
	{
		profile->actionTakenBan = true;
		profile->isBanned = true;
		m_SuspicionScorer->MarkBanned(profile->steamId);
		const float banScore = m_SuspicionScorer->GetScore(profile->steamId);
		const std::string detectReason = m_SuspicionScorer->GetBanReasonTags(profile->steamId);
		const char* mapName = nullptr;
		if (CGlobalVars* gv = GetGlobals())
		{
			const char* map = STRING(gv->mapname);
			if (map && map[0])
				mapName = map;
		}
		// Discord: AdminPlugin Bearer → backend /api/cs2/anticheat/ban-notify (no local curl).
		AdminBridge_ApplyBan(profile->steamId, profile->name.c_str(), banScore, mapName, detectReason.c_str());

		AC_Log("auto-ban steam=%llu name=%s score=%.1f reason=%s (discord via AdminPlugin→backend)",
			(unsigned long long)profile->steamId, profile->name.c_str(), banScore, detectReason.c_str());
	}
}

void AntiCheatCore::SetAdminCheckHold(uint64_t steamId, bool hold)
{
	if (!steamId || !m_SuspicionScorer)
		return;

	m_SuspicionScorer->SetAdminCheckHold(steamId, hold);
	if (!hold)
		return;

	m_SuspicionScorer->RetractIssuedBan(steamId);
	if (PlayerProfile* p = GetPlayerProfile(steamId))
	{
		p->actionTakenBan = false;
		p->isBanned = false;
	}
	AC_Log("admin-check hold ON steam=%llu (autoban paused; fast-ban retracted if queued)",
		(unsigned long long)steamId);
}
