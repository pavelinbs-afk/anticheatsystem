#pragma once

#include <unordered_map>
#include <memory>
#include <cstdint>
#include "player_profile.h"
#include "config.h"

#ifndef AC_MAXPLAYERS
#define AC_MAXPLAYERS 64
#endif

class AimAnalyzer;
class WallhackDetector;
class MovementAnalyzer;
class StatisticsTracker;
class IntegrityChecker;
class SuspicionScorer;
class FpsDropDetector;
class GameFileScanner;

class AntiCheatCore {
public:
	static AntiCheatCore* GetInstance();

	bool Initialize();
	void Shutdown();

	// ClientConnect: reject before enter if integrity lockdown / prior integrity ban.
	bool ShouldRejectConnect(uint64_t steamID, char* rejectReason, size_t rejectLen);
	void OnPlayerConnect(int slot, uint64_t steamID, const char* name);
	void OnPlayerDisconnect(int slot, uint64_t steamID);
	void OnPlayerDeath(int attackerSlot, int victimSlot, bool headshot, bool thrusmoke, bool attackerblind, bool noscope, int penetrated);
	void OnPlayerHurt(int attackerSlot, int victimSlot, float damage);
	void OnWeaponFire(int shooterSlot);
	void OnGameFrame();
	void OnRoundStart();
	void OnRoundEnd();

	PlayerProfile* GetPlayerBySlot(int slot);
	PlayerProfile* GetPlayerProfile(uint64_t steamID);
	const AntiCheatConfig& GetConfig() const { return m_Config; }

private:
	AntiCheatCore() = default;

	static AntiCheatCore* s_Instance;

	AntiCheatConfig m_Config;
	std::unordered_map<uint64_t, std::shared_ptr<PlayerProfile>> m_PlayerProfiles;
	uint64_t m_SlotToSteam[AC_MAXPLAYERS]{};

	std::unique_ptr<AimAnalyzer> m_AimAnalyzer;
	std::unique_ptr<WallhackDetector> m_WallhackDetector;
	std::unique_ptr<MovementAnalyzer> m_MovementAnalyzer;
	std::unique_ptr<StatisticsTracker> m_StatisticsTracker;
	std::unique_ptr<IntegrityChecker> m_IntegrityChecker;
	std::unique_ptr<SuspicionScorer> m_SuspicionScorer;
	std::unique_ptr<FpsDropDetector> m_FpsDropDetector;
	std::unique_ptr<GameFileScanner> m_GameFileScanner;

	void SampleAllPlayers();
	void ProcessPlayer(PlayerProfile* profile);
	void CheckAndApplyActions(PlayerProfile* profile);
};
