#pragma once

#include "../player_profile.h"
#include <string>

class IntegrityChecker {
public:
    IntegrityChecker();
    ~IntegrityChecker();

    // Returns suspicion score delta
    float CheckIntegrity(PlayerProfile& player);

private:
    bool CheckConVars(PlayerProfile& player);
    bool DetectImpossibleState(PlayerProfile& player);
    bool VerifyTickConsistency(PlayerProfile& player);
    bool CheckNetworkAnomalies(PlayerProfile& player);
};
