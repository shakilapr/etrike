// Gateway forwarding rules — verifies firmware matches spec.
// Run: g++ -std=c++17 -I../../shared test_gateway_forwarding.cpp -o test_gw && ./test_gw

#include <cstdint>
#include <cstdio>
#include <set>
#include <cassert>

// ── Forwarding rules from can_protocol.h (hand-copied — test catches drift) ─
static bool is_forwarded_low_to_high(uint32_t id) {
    return id == 0x001 || id == 0x011 || id == 0x120 || id == 0x206 || id == 0x600;
}
static bool is_forwarded_high_to_low(uint32_t id) {
    return id == 0x001 || id == 0x302;
}

// ── IDs that appear on both buses but MUST NOT be forwarded ─
static bool is_not_forwarded(uint32_t id) {
    // Heartbeats appear on both buses independently — NOT bridged
    return id == 0x7FD || id == 0x7FE || id == 0x7FC;
}

// ── All known CAN IDs ─
static const uint32_t all_ids[] = {
    0x001, 0x006, 0x011, 0x012, 0x110, 0x120, 0x169,
    0x201, 0x202, 0x203, 0x204, 0x205, 0x206, 0x210, 0x220,
    0x300, 0x301, 0x302, 0x310, 0x311, 0x400,
    0x600, 0x6FA, 0x6FB,
    0x721, 0x731, 0x741, 0x7B9,
    0x7FC, 0x7FD, 0x7FE,
};

static int pass = 0, fail = 0;
#define CHECK(cond, msg) do { \
    if (cond) { pass++; } \
    else { fprintf(stderr, "FAIL [%s]\n", msg); fail++; } \
} while(0)

int main() {
    printf("=== Gateway Forwarding Rules ===\n\n");

    // ── Low→High forwarding ──────────────────────────────────────
    printf("--- Low→High ---\n");
    std::set<uint32_t> expected_l2h = {0x001, 0x011, 0x120, 0x206, 0x600};
    for (auto id : all_ids) {
        bool should = expected_l2h.count(id) > 0;
        bool actual = is_forwarded_low_to_high(id);
        if (should != actual) {
            char buf[80];
            snprintf(buf, sizeof(buf), "Low→High: 0x%03X should=%d actual=%d", id, should, actual);
            CHECK(false, buf);
        } else { pass++; }
    }

    // ── High→Low forwarding ──────────────────────────────────────
    printf("--- High→Low ---\n");
    std::set<uint32_t> expected_h2l = {0x001, 0x302};
    for (auto id : all_ids) {
        bool should = expected_h2l.count(id) > 0;
        bool actual = is_forwarded_high_to_low(id);
        if (should != actual) {
            char buf[80];
            snprintf(buf, sizeof(buf), "High→Low: 0x%03X should=%d actual=%d", id, should, actual);
            CHECK(false, buf);
        } else { pass++; }
    }

    // ── Not-forwarded check ──────────────────────────────────────
    printf("--- NOT Forwarded ---\n");
    for (auto id : all_ids) {
        if (is_not_forwarded(id)) {
            // Must NOT be in either forwarding set
            bool in_l2h = expected_l2h.count(id) > 0;
            bool in_h2l = expected_h2l.count(id) > 0;
            char buf[80];
            snprintf(buf, sizeof(buf), "0x%03X is_not_forwarded but in L2H=%d H2L=%d", id, in_l2h, in_h2l);
            CHECK(!in_l2h && !in_h2l, buf);
        }
    }

    // ── ESTOP (0x001) must be forwarded BOTH ways ────────────────
    CHECK(is_forwarded_low_to_high(0x001), "ESTOP must forward low→high");
    CHECK(is_forwarded_high_to_low(0x001), "ESTOP must forward high→low");

    // ── Heartbeats must NOT be forwarded ─────────────────────────
    CHECK(!is_forwarded_low_to_high(0x7FD), "RT heartbeat NOT forwarded low→high");
    CHECK(!is_forwarded_high_to_low(0x7FD), "RT heartbeat NOT forwarded high→low");
    CHECK(!is_forwarded_low_to_high(0x7FE), "SYS heartbeat NOT forwarded");
    CHECK(!is_forwarded_high_to_low(0x7FE), "SYS heartbeat NOT forwarded");

    // ── 0x302 is asymmetric: high→low only ───────────────────────
    CHECK(!is_forwarded_low_to_high(0x302), "0x302 NOT low→high");
    CHECK(is_forwarded_high_to_low(0x302), "0x302 IS high→low");

    printf("\n=== %d pass, %d fail ===\n", pass, fail);
    return fail > 0 ? 1 : 0;
}
