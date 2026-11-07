#pragma once
#include <cstdint>

// At most one layout search per frame, 20/s globally and 4/s per player slot.
// A bad first player cannot monopolize every search opportunity.
struct BoneScanBudget {
    bool used = false;
    uint64_t next_global_ms = 0;
    uint64_t next_slot_ms[65] = {};
    void BeginFrame() { used = false; }
    bool Acquire(uint64_t now, int slot) {
        if (slot < 0 || slot >= 65 || used || now < next_global_ms || now < next_slot_ms[slot])
            return false;
        used = true;
        next_global_ms = now + 50;
        next_slot_ms[slot] = now + 250;
        return true;
    }
};
