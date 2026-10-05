#pragma once

#include <cstdint>

// Discord/ban reason = detection tags (rage/aim/fps/…) from C++; AdminPlugin Bearer → backend.
void AdminBridge_ApplyBan(uint64_t steamId, const char* playerName, float suspicionScore = 0.0f, const char* mapName = nullptr, const char* detectReason = nullptr);
void AdminBridge_SendReport(uint64_t steamId, const char* playerName);
void AdminBridge_WarnAdmins(uint64_t steamId, const char* playerName, float score);
void AdminBridge_DiscordNotify(uint64_t steamId, const char* playerName, float suspicionScore, const char* mapName, bool resend, const char* detectReason = nullptr);
void AdminBridge_DiscordTest();
/// Bypass/IP check via AdminPlugin Bearer → GET /api/cs2/anticheat/check (no curl on game host).
void AdminBridge_BackendCheck(uint64_t steamId, const char* clientIp, const char* playerName);

/// Retry pending ServerCommand bridges (ban/report/warn/backend) — call from GameFrame.
void AdminBridge_Tick(float curtime);
