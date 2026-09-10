#pragma once

#include <unordered_set>
#include <cstdint>
#include <cstddef>

// SteamIDs with AdminPlugin immunity >= 95 (Ст. Модератор+) — anticheat ignores them.
class StaffExemptList {
public:
	void Reload();
	void Tick(float curtime);
	bool IsExempt(uint64_t steamId) const;
	size_t Size() const { return m_exempt.size(); }

private:
	std::unordered_set<uint64_t> m_exempt;
	float m_nextAllowedReload = 0.0f;
	static constexpr float kReloadIntervalSec = 30.0f;
};
