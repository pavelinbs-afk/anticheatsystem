#pragma once

#include <cstdint>

void AdminBridge_ApplyBan(uint64_t steamId, const char* playerName);
void AdminBridge_SendReport(uint64_t steamId, const char* playerName);
