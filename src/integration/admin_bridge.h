#pragma once

#include <cstdint>

// Discord/ban reason = detection tags (rage/aim/fps/…) from C++; AdminPlugin Bearer → backend.
void AdminBridge_ApplyBan(uint64_t steamId, const char* playerName, float suspicionScore = 0.0f, const char* mapName = nullptr, const char* detectReason = nullptr);
void AdminBridge_SendReport(uint64_t steamId, const char* playerName);
void AdminBridge_WarnAdmins(uint64_t steamId, const char* playerName, float score);
void AdminBridge_DiscordNotify(uint64_t steamId, const char* playerName, float suspicionScore, const char* mapName, bool resend, const char* detectReason = nullptr);
void AdminBridge_DiscordTest();
