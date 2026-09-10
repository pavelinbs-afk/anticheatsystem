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
#include "integration/admin_bridge.h"
#include "integration/backend_client.h"
#include "players.h"
#include "plugin.h"
#include "staff_exempt.h"

#include <chrono>
#include <vector>
#include <cstring>
#include <string>
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
	m_ShotTracker = std::make_unique<ShotTracker>();
	m_BackendClient = std::make_unique<BackendClient>();
	m_StaffExempt = std::make_unique<StaffExemptList>();
	m_StaffExempt->Reload();

	m_AimAnalyzer->SetConfig(m_Config.snap_angle_threshold, (float)m_Config.snap_time_threshold_ms, 3.0f, 0.5f);
	m_WallhackDetector->SetConfig(m_Config.wh_track_fov_deg, m_Config.wh_min_track_distance, m_Config.wh_streak_ticks);
	m_MovementAnalyzer->SetConfig(m_Config.speed_threshold, 800.0f);
	m_FpsDropDetector->SetConfig(m_Config.fps_max_frame_ms, m_Config.fps_spike_stddev_ms, m_Config.fps_min_spikes);
	m_SuspicionScorer->SetThresholds(m_Config.monitor_threshold, m_Config.warn_threshold,
		m_Config.report_threshold, m_Config.ban_threshold, m_Config.score_decay_per_second);
	m_ShotTracker->SetConfig(m_Config.shot_hit_window_sec, m_Config.shot_aim_fov_deg,
		m_Config.shot_min_hit_distance, m_Config.shot_min_shots_before_score);
	m_GameFileScanner->SetEnabled(m_Config.enable_game_file_scan);
	m_GameFileScanner->ScanAtStartup();
	m_BackendClient->SetConfig(m_Config.backend_api_url, m_Config.backend_api_token, m_Config.enable_backend_check);

	std::memset(m_SlotToSteam, 0, sizeof(m_SlotToSteam));
	AC_Log("core initialized (shot_track=%d backend=%d staff_exempt=%d)",
		(int)m_Config.enable_shot_tracking, (int)m_BackendClient->IsEnabled(),
		(int)(m_StaffExempt ? m_StaffExempt->Size() : 0));
	return true;
}

bool AntiCheatCore::IsStaffExempt(uint64_t steamId) const
{
	return m_StaffExempt && m_StaffExempt->IsExempt(steamId);
}

void AntiCheatCore::Shutdown()
{
	m_PlayerProfiles.clear();
	AC_Log("core shutdown");
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
	if (!profile || !m_BackendClient || !m_BackendClient->IsEnabled())
		return;

	BackendCheckRequest req;
	req.steamId = profile->steamId;
	req.slot = profile->slot;
	req.playerName = profile->name;
	req.clientIp = profile->ipAddress.empty() ? GetClientIpForSlot(profile->slot) : profile->ipAddress;
	if (profile->ipAddress.empty() && !req.clientIp.empty())
		profile->ipAddress = req.clientIp;
	m_BackendClient->RequestCheck(req);
}

void AntiCheatCore::ProcessBackendResults()
{
	if (!m_BackendClient)
		return;

	std::vector<BackendCheckResult> results;
	m_BackendClient->PollResults(results);
	for (const BackendCheckResult& r : results)
	{
		if (!r.ok)
		{
			AC_Log("backend check failed steam=%llu err=%s",
				(unsigned long long)r.steamId, r.rawError.c_str());
			continue;
		}

		PlayerProfile* profile = GetPlayerProfile(r.steamId);
		if (!profile)
			profile = GetPlayerBySlot(r.slot);

		// Backend is source of truth after unban: clear sticky local ban cache.
		if (!r.steamBanned && !(r.shouldEnforce && r.exactIpLinked > 0))
		{
			if (m_SuspicionScorer->IsAlreadyBanned(r.steamId))
			{
				m_SuspicionScorer->ClearBanned(r.steamId);
				AC_Log("backend: steam=%llu clean — cleared local ban cache",
					(unsigned long long)r.steamId);
			}
			if (profile)
			{
				profile->isBanned = false;
				profile->actionTakenBan = false;
			}
			if (r.prefixIpLinked > 0)
			{
				AC_Log("backend: /24 IP link (log only) steam=%llu links=%d",
					(unsigned long long)r.steamId, r.prefixIpLinked);
			}
			continue;
		}

		if (r.steamBanned)
		{
			// Active ban on backend — kick only, never re-issue ApplyBan.
			if (profile)
			{
				profile->isBanned = true;
				profile->actionTakenBan = true;
			}
			m_SuspicionScorer->MarkBanned(r.steamId);
			AC_Log("backend: active ban steam=%llu — kick only (no re-ban)",
				(unsigned long long)r.steamId);
			if (g_pEngine && profile && profile->slot >= 0)
			{
				g_pEngine->KickClient(CPlayerSlot(profile->slot),
					"Active ban (Perfect.Team)",
					static_cast<ENetworkDisconnectionReason>(15));
			}
			continue;
		}

		// Exact IP shared with another active ban — report only (no auto-ban).
		// Auto-banning alts by IP caused false bans after unbans / shared NAT.
		if (r.shouldEnforce && r.exactIpLinked > 0)
		{
			AC_LogCritical("backend IP link steam=%llu ip=%s reason=%s (report only, no auto-ban)",
				(unsigned long long)r.steamId, r.clientIp.c_str(), r.enforceReason.c_str());
			if (profile && !profile->actionTakenReport)
			{
				profile->actionTakenReport = true;
				AdminBridge_SendReport(r.steamId, profile->name.c_str());
			}
			else if (!profile)
			{
				AdminBridge_SendReport(r.steamId, r.playerName.c_str());
			}
		}
	}
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

	if (m_GameFileScanner && m_Config.enable_game_file_scan)
		m_GameFileScanner->ScanOnPlayerJoin(steamID, profile->name.c_str());

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
	if (attacker && IsStaffExempt(attacker->steamId))
		attacker = nullptr;
	if (attacker && victim && attacker->steamId != victim->steamId)
	{
		attacker->kills++;
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

			float whScore = m_WallhackDetector->OnCombatKill(*attacker, *victim, flags);
			if (whScore > 0.0f)
			{
				const char* reason = thrusmoke ? "Smoke kill" : (attackerblind ? "Blind kill" : "Wallbang/prefire");
				m_SuspicionScorer->AddScore(attacker->steamId, "WallhackDetector", whScore, reason);
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

	if (!m_Config.enable_shot_tracking || !m_ShotTracker)
		return;

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
	float score = m_ShotTracker->OnPlayerHurt(*attacker, *victim, damage, hitgroup, curtime);
	if (score > 0.0f)
		m_SuspicionScorer->AddScore(attacker->steamId, "ShotTracker", score, "Shot aim anomaly");
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
		m_SuspicionScorer->AddScore(shooter->steamId, "ShotTracker", score, "Rage aim snap");
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

		if (profile->ipAddress.empty())
			profile->ipAddress = GetClientIpForSlot(profile->slot);
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
		profile->movementIgnoreUntil = now + 3.0f;
		profile->samplesValid = false;
		profile->wallAimStreak = 0;
		profile->wallAimTargetSteam = 0;
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

	if (m_Config.enable_aim_detection && alive && team >= 2)
	{
		scoreDelta = m_AimAnalyzer->Analyze(*profile, 0.015f);
		if (scoreDelta > 0.0f)
			m_SuspicionScorer->AddScore(profile->steamId, "AimAnalyzer", scoreDelta, "Aim anomaly");
	}

	if (m_Config.enable_wallhack_detection && alive && team >= 2)
		m_WallhackDetector->Analyze(*profile, CollectEnemies(profile));

	if (m_Config.enable_movement_detection)
	{
		scoreDelta = m_MovementAnalyzer->Analyze(*profile, 0.015f, curtime, team, alive);
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
		AdminBridge_ApplyBan(profile->steamId, profile->name.c_str());
		AC_Log("auto-ban steam=%llu name=%s score=%.1f (continued after admin warn)",
			(unsigned long long)profile->steamId, profile->name.c_str(),
			m_SuspicionScorer->GetScore(profile->steamId));
	}
}
