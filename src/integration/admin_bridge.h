#pragma once

#include <cstdint>

// ServerCommand bridge to the AdminPlugin ban/report/warn commands.
// Ban reason = detection tags (rage/aim/fps/…) from C++.
void AdminBridge_ApplyBan(uint64_t steamId, const char* playerName, float suspicionScore = 0.0f, const char* mapName = nullptr, const char* detectReason = nullptr);
void AdminBridge_SendReport(uint64_t steamId, const char* playerName);
void AdminBridge_WarnAdmins(uint64_t steamId, const char* playerName, float score);

/// Retry pending ServerCommand bridges (ban/report/warn) — call from GameFrame.
void AdminBridge_Tick(float curtime);
