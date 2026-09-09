#pragma once

#include <cstdint>

void AdminBridge_ApplyBan(uint64_t steamId, const char* playerName);
void AdminBridge_SendReport(uint64_t steamId, const char* playerName);
void AdminBridge_WarnAdmins(uint64_t steamId, const char* playerName, float score);
