#include "integrity_checker.h"

IntegrityChecker::IntegrityChecker() = default;
IntegrityChecker::~IntegrityChecker() = default;

bool IntegrityChecker::CheckConVars(PlayerProfile&) { return false; }
bool IntegrityChecker::DetectImpossibleState(PlayerProfile&) { return false; }
bool IntegrityChecker::VerifyTickConsistency(PlayerProfile&) { return false; }
bool IntegrityChecker::CheckNetworkAnomalies(PlayerProfile&) { return false; }

float IntegrityChecker::CheckIntegrity(PlayerProfile& player)
{
	(void)player;
	// Placeholders disabled — returning positive scores without real checks would false-ban.
	return 0.0f;
}
