#pragma once

#include <cstdint>

class CEntityInstance;

CEntityInstance* GetControllerBySlot(int iSlot);
CEntityInstance* GetPawnBySlot(int iSlot);
int GetPlayerTeamNum(int iSlot);
bool IsPlayerAlive(int iSlot);
bool SamplePlayerState(int iSlot, float* outPos, float* outAngles, float* outVel);
const char* GetPlayerNameSafe(int iSlot);
