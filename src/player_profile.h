#pragma once

#include <string>
#include <cstdint>
#include <chrono>
#include <deque>
#include <map>
#include <vector>
#include <cmath>

// ============================================================
// Math types used throughout the anti-cheat system
// ============================================================
struct AcVec3 {
    float x, y, z;

    AcVec3() : x(0), y(0), z(0) {}
    AcVec3(float x, float y, float z) : x(x), y(y), z(z) {}

    AcVec3 operator+(const AcVec3& other) const { return {x + other.x, y + other.y, z + other.z}; }
    AcVec3 operator-(const AcVec3& other) const { return {x - other.x, y - other.y, z - other.z}; }
    AcVec3 operator*(float scalar) const { return {x * scalar, y * scalar, z * scalar}; }

    float Length() const { return std::sqrt(x * x + y * y + z * z); }
    float LengthSqr() const { return x * x + y * y + z * z; }

    void Normalize() {
        float len = Length();
        if (len > 0) { x /= len; y /= len; z /= len; }
    }
};

struct AcAngle {
    float pitch, yaw, roll;

    AcAngle() : pitch(0), yaw(0), roll(0) {}
    AcAngle(float p, float y, float r) : pitch(p), yaw(y), roll(r) {}
};

struct ShotRecord {
    float time = 0.0f;
    AcAngle angles;
    float snapDeg = 0.0f;
    float bestEnemyFov = 999.0f;
    uint64_t bestEnemySteam = 0;
    float bestEnemyDist = 0.0f;
    bool consumedHit = false;
};

// ============================================================
// Player Profile — unified data structure for all modules
// ============================================================
struct PlayerProfile {
    // === Identification ===
    uint64_t steamId = 0;
    std::string name;
    std::string ipAddress;

    // === Time Tracking ===
    std::chrono::steady_clock::time_point connectionTime;
    std::chrono::steady_clock::time_point lastActivityTime;
    float lastMoveTime = 0.0f;

    // === Position & Movement ===
    AcVec3 position;
    AcVec3 lastPosition;
    AcVec3 velocity;
    std::deque<AcVec3> positionHistory;

    // === View Angles (last 128 ticks) ===
    AcAngle viewAngles;
    AcAngle lastViewAngles;
    std::deque<AcAngle> angleHistory;
    std::deque<float> angleDeltaHistory;

    // === Movement State ===
    bool isCrouching = false;
    bool inAir = false;

    // === Pre-Aim Tracking ===
    float timeCrosshairOnEnemy = 0.0f;
    bool isLookingAtEnemy = false;
    struct PreAimData {
        std::chrono::steady_clock::time_point lastTargetAcquiredTime;
        int preAimSuspicionCount = 0;
    } preAimData;

    // === Combat Statistics (current session) ===
    int kills = 0;
    int deaths = 0;
    int assists = 0;
    int headshots = 0;
    float totalDamage = 0.0f;
    int shotsFired = 0;
    int shotsHit = 0;
    int roundsPlayed = 0;

    // === Computed Metrics (cached) ===
    float kdRatio = 0.0f;
    float headshotPercentage = 0.0f;
    float accuracy = 0.0f;
    float adr = 0.0f;

    // === Per-Round Stats ===
    std::map<int, int> killsPerRound;

    // === Historical Data (from MySQL) ===
    float previousSessionKD = 0.0f;
    float previousSessionHSRatio = 0.0f;

    // === Suspicion / Flagging ===
    float suspicionScore = 0.0f;
    std::map<std::string, float> suspicionBreakdown;
    bool isFlagged = false;
    bool isBanned = false;
    bool actionTakenReport = false;
    bool actionTakenAdminWarn = false;
    bool actionTakenBan = false;
    int slot = -1;

    // Client frametime / FPS hitch tracking (from INetChannelInfo::GetRemoteFramerate)
    int fpsDropStreak = 0;
    float lastClientFrameMs = 0.0f;

    // Wallhack / pre-aim tracking (FOV lock on enemy)
    int wallAimStreak = 0;
    uint64_t wallAimTargetSteam = 0;

    // Movement / spawn guards (team change, death, join → ignore speed for a bit)
    int lastTeamNum = 0;
    float movementIgnoreUntil = 0.0f;
    bool samplesValid = false;
    bool statsFlaggedThisSession = false;
    int untrustedAngleHits = 0;

    // Recent weapon_fire samples for hit matching (advanced aim tracking)
    std::deque<ShotRecord> recentShots;

    // === Timestamps ===
    std::chrono::system_clock::time_point lastViolationTime;

    // === Helpers ===
    float GetKD() const {
        return deaths > 0 ? static_cast<float>(kills) / deaths : static_cast<float>(kills);
    }
    float GetHSPercent() const {
        return kills > 0 ? (static_cast<float>(headshots) / kills) * 100.0f : 0.0f;
    }
    float GetAccuracy() const {
        return shotsFired > 0 ? (static_cast<float>(shotsHit) / shotsFired) * 100.0f : 0.0f;
    }
    float GetADR() const {
        return roundsPlayed > 0 ? totalDamage / roundsPlayed : 0.0f;
    }
};

// === Shared structs ===
struct HistoricalStats {
    float mean_kd = 1.0f;
    float std_dev_kd = 0.5f;
    float mean_hs = 40.0f;
    float std_dev_hs = 15.0f;
    float mean_acc = 30.0f;
    float std_dev_acc = 10.0f;
};

struct BanInfo {
    int id = 0;
    uint64_t steamId = 0;
    std::string reason;
    std::string bannedAt;
    std::string expiresAt;
    bool isActive = false;
    bool isDeferred = false;
    std::string deferredUntil;
};

struct SessionData {
    std::string sessionStart;
    std::string sessionEnd;
    int kills = 0;
    int deaths = 0;
    int headshots = 0;
    int damage = 0;
    int rounds = 0;
    float finalScore = 0.0f;
};

struct GameEvent {
    std::string eventName;
    uint64_t attackerSteamId = 0;
    uint64_t victimSteamId = 0;
    bool headshot = false;
    float damage = 0.0f;
};
