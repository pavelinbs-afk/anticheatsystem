#include "anticheat_core.h"
#include "modules/aim_analyzer.h"
#include "modules/wallhack_detector.h"
#include "modules/movement_analyzer.h"
#include "modules/statistics_tracker.h"
#include "modules/integrity_checker.h"
#include "modules/suspicion_scorer.h"
#include "modules/fps_drop_detector.h"
#include "modules/game_file_scanner.h"
#include "integration/admin_bridge.h"
#include "players.h"
#include "plugin.h"

#include <chrono>
#include <vector>
#include <cstring>
#include <inetchannelinfo.h>

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

	m_AimAnalyzer->SetConfig(m_Config.snap_angle_threshold, (float)m_Config.snap_time_threshold_ms, 2.0f, 0.5f);
	m_WallhackDetector->SetConfig(m_Config.wh_track_fov_deg, m_Config.wh_min_track_distance, m_Config.wh_streak_ticks);
	m_MovementAnalyzer->SetConfig(m_Config.speed_threshold, 250.0f);
	m_FpsDropDetector->SetConfig(m_Config.fps_max_frame_ms, m_Config.fps_spike_stddev_ms, m_Config.fps_min_spikes);
	m_SuspicionScorer->SetThresholds(m_Config.monitor_threshold, m_Config.warn_threshold,
		m_Config.report_threshold, m_Config.ban_threshold, m_Config.score_decay_per_second);
	m_GameFileScanner->SetEnabled(m_Config.enable_game_file_scan);
	m_GameFileScanner->ScanAtStartup();

	std::memset(m_SlotToSteam, 0, sizeof(m_SlotToSteam));
	AC_Log("core initialized");
	return true;
}

void AntiCheatCore::Shutdown()
{
	m_PlayerProfiles.clear();
	AC_Log("core shutdown");
}

bool AntiCheatCore::ShouldRejectConnect(uint64_t steamID, char* rejectReason, size_t rejectLen)
{
	if (!m_GameFileScanner || !m_Config.enable_game_file_scan)
		return false;

	const char* reason = nullptr;
	if (m_GameFileScanner->IsSteamIntegrityBanned(steamID))
		reason = "Banned: critical game integrity violation";
	else if (m_GameFileScanner->IsLockdownActive())
		reason = m_GameFileScanner->LockdownReason();

	if (!reason)
		return false;

	if (rejectReason && rejectLen > 0)
		V_strncpy(rejectReason, reason, (int)rejectLen);

	// Persist ban before they enter the server.
	AdminBridge_ApplyBan(steamID, "integrity");
	AC_LogCritical("reject connect steam=%llu reason=%s",
		(unsigned long long)steamID, reason);
	return true;
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

	CGlobalVars* gv = GetGlobals();
	profile->lastMoveTime = gv ? gv->curtime : 0.0f;

	m_PlayerProfiles[steamID] = profile;
	m_SlotToSteam[slot] = steamID;

	if (!m_GameFileScanner || !m_Config.enable_game_file_scan)
		return;

	IntegrityScanResult scan = m_GameFileScanner->ScanOnPlayerJoin(steamID, profile->name.c_str());
	if (!scan.criticalChange && !m_GameFileScanner->IsLockdownActive())
		return;

	profile->isBanned = true;
	profile->actionTakenBan = true;
	AdminBridge_ApplyBan(steamID, profile->name.c_str());
	AC_LogCritical("integrity ban on join steam=%llu name=%s reason=%s",
		(unsigned long long)steamID, profile->name.c_str(),
		scan.reason.empty() ? m_GameFileScanner->LockdownReason() : scan.reason.c_str());

	// Kick immediately — do not let them play while ban bridge runs.
	if (g_pEngine)
	{
		const char* kickMsg = "Critical game file/memory integrity violation";
		g_pEngine->KickClient(CPlayerSlot(slot), kickMsg, static_cast<ENetworkDisconnectionReason>(15));
	}
}

void AntiCheatCore::OnPlayerDisconnect(int slot, uint64_t steamID)
{
	uint64_t sid = steamID ? steamID : (slot >= 0 && slot < AC_MAXPLAYERS ? m_SlotToSteam[slot] : 0);
	if (slot >= 0 && slot < AC_MAXPLAYERS)
		m_SlotToSteam[slot] = 0;
	if (sid)
		m_PlayerProfiles.erase(sid);
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
	if (attacker && victim && attacker->steamId != victim->steamId)
	{
		attacker->kills++;
		if (headshot)
			attacker->headshots++;

		const float dist = VectorDistance(attacker->position, victim->position);

		float aimScore = m_AimAnalyzer->OnPlayerShoot(*attacker, *victim, headshot, dist);
		if (aimScore > 0.0f)
			m_SuspicionScorer->AddScore(attacker->steamId, "AimAnalyzer", aimScore, "Combat aim anomaly");

		if (m_Config.enable_wallhack_detection || m_Config.enable_smoke_detection)
		{
			CombatKillFlags flags;
			flags.headshot = headshot;
			flags.thrusmoke = thrusmoke && m_Config.enable_smoke_detection;
			flags.attackerblind = attackerblind && m_Config.enable_wallhack_detection;
			flags.noscope = noscope;
			flags.penetrated = m_Config.enable_wallhack_detection ? penetrated : 0;
			flags.distance = dist;

			// Smoke kills always go through OnCombatKill when smoke detection is on.
			float whScore = m_WallhackDetector->OnCombatKill(*attacker, *victim, flags);
			if (whScore > 0.0f)
			{
				const char* reason = thrusmoke ? "Smoke kill" : (attackerblind ? "Blind kill" : "Wallbang/prefire");
				m_SuspicionScorer->AddScore(attacker->steamId, "WallhackDetector", whScore, reason);
			}
		}
	}
	if (victim)
		victim->deaths++;
}

void AntiCheatCore::OnPlayerHurt(int attackerSlot, int victimSlot, float damage)
{
	PlayerProfile* attacker = GetPlayerBySlot(attackerSlot);
	PlayerProfile* victim = GetPlayerBySlot(victimSlot);
	if (attacker && victim && attacker->steamId != victim->steamId)
	{
		attacker->totalDamage += damage;
		attacker->shotsHit++;
	}
}

void AntiCheatCore::OnWeaponFire(int shooterSlot)
{
	if (auto* p = GetPlayerBySlot(shooterSlot))
		p->shotsFired++;
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
		profile->inAir = false;
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
	m_SuspicionScorer->DecayScores(deltaTime);

	if (m_GameFileScanner && m_Config.enable_game_file_scan)
	{
		CGlobalVars* gv = GetGlobals();
		m_GameFileScanner->Tick(gv ? gv->curtime : 0.0f);
	}

	for (auto& [steamID, profile] : m_PlayerProfiles)
	{
		(void)steamID;
		ProcessPlayer(profile.get());
	}
}

void AntiCheatCore::OnRoundStart()
{
	for (auto& [steamID, profile] : m_PlayerProfiles)
	{
		(void)steamID;
		profile->roundsPlayed++;
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

	float scoreDelta = 0.0f;

	if (m_Config.enable_aim_detection)
	{
		scoreDelta = m_AimAnalyzer->Analyze(*profile, 0.015f);
		if (scoreDelta > 0.0f)
			m_SuspicionScorer->AddScore(profile->steamId, "AimAnalyzer", scoreDelta, "Aim anomaly");
	}

	if (m_Config.enable_wallhack_detection)
	{
		std::vector<PlayerProfile*> enemies;
		for (auto& [sid, other] : m_PlayerProfiles)
		{
			(void)sid;
			if (other.get() == profile)
				continue;
			int t1 = GetPlayerTeamNum(profile->slot);
			int t2 = GetPlayerTeamNum(other->slot);
			if (t1 >= 2 && t2 >= 2 && t1 != t2)
				enemies.push_back(other.get());
		}
		scoreDelta = m_WallhackDetector->Analyze(*profile, enemies);
		if (scoreDelta > 0.0f)
			m_SuspicionScorer->AddScore(profile->steamId, "WallhackDetector", scoreDelta, "Visibility anomaly");
	}

	if (m_Config.enable_movement_detection)
	{
		scoreDelta = m_MovementAnalyzer->Analyze(*profile, 0.015f);
		if (scoreDelta > 0.0f)
			m_SuspicionScorer->AddScore(profile->steamId, "MovementAnalyzer", scoreDelta, "Movement anomaly");
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
				m_SuspicionScorer->AddScore(profile->steamId, "FpsDropDetector", scoreDelta, "Client FPS hitch");
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
	ScorerAction action = m_SuspicionScorer->EvaluatePlayer(*profile);
	// Scores never printed to the player — only server log via SuspicionScorer.

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

		// If score jumped past report threshold straight to 50+, still file a ticket once.
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
		AdminBridge_ApplyBan(profile->steamId, profile->name.c_str());
		AC_Log("auto-ban steam=%llu name=%s score=%.1f (continued after admin warn)",
			(unsigned long long)profile->steamId, profile->name.c_str(),
			m_SuspicionScorer->GetScore(profile->steamId));
	}
}
