#pragma once

#include <cstdint>

// Ban reason is read by AdminPlugin from anticheat_config.json (ban.reason).
void AdminBridge_ApplyBan(uint64_t steamId, const char* playerName);
void AdminBridge_SendReport(uint64_t steamId, const char* playerName);
void AdminBridge_WarnAdmins(uint64_t steamId, const char* playerName, float score);
