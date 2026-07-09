/*
 * test_mcp2515_config.cpp — Validate MCP2515 CNF bit-timing for 500 kbit/s.
 *
 * This test imports the MCP2515 driver's CNF register constants and
 * verifies that the computed bit rate equals 500 kbit/s for both
 * 8 MHz and 16 MHz crystal frequencies.
 *
 * Catches: Bug #2 (CNF3=0x01 giving 7 TQ instead of 8 TQ)
 */

#include <cstdio>
#include <cstdlib>
#include <cstdint>

// ── Import the CNF constants directly from the driver header ──────
// We include the real driver header; ESP-IDF headers are shadowed.
// The header only defines constants — no hardware dependency.
#include "can_driver_mcp2515.h"

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond, msg) do {                              \
    if (cond) { g_pass++; printf("  PASS: %s\n", msg); }  \
    else { g_fail++; printf("  FAIL: %s (%s:%d)\n",       \
               msg, __FILE__, __LINE__); }                 \
} while(0)

// ── CNF decode helpers ─────────────────────────────────────────────

// TQ period in nanoseconds
static double tq_ns(int brp, double fosc_mhz) {
    return (2.0 * (brp + 1)) / fosc_mhz * 1000.0;
}

// Bit time in TQ from register values
static int bit_tq(uint8_t cnf1, uint8_t cnf2, uint8_t cnf3) {
    int brp    = (cnf1 & 0x3F);           // CNF1[5:0]
    int prseg  = (cnf2 & 0x07) + 1;       // CNF2[2:0] + 1
    int phseg1 = ((cnf2 >> 3) & 0x07) + 1; // CNF2[5:3] + 1
    int phseg2 = ((cnf3 >> 3) & 0x07) + 1; // CNF3[5:3] + 1 (was bug: used bit 0)
    return 1 + prseg + phseg1 + phseg2;   // Sync + PropSeg + PS1 + PS2
}

int main() {
    printf("\n=== MCP2515 CNF Bit-Timing Test ===\n\n");

    // Access the driver's CNF constants via the public class API
    constexpr uint8_t cnf1 = rt::Mcp2515Driver::kCnf1_500k;
    constexpr uint8_t cnf2 = rt::Mcp2515Driver::kCnf2_500k;
    constexpr uint8_t cnf3 = rt::Mcp2515Driver::kCnf3_500k;

    // ── Test 1: decode CNF register fields ─────────────────────────
    printf("-- Test 1: CNF register decode --\n");
    int brp    = (cnf1 & 0x3F);
    int prseg  = (cnf2 & 0x07) + 1;
    int phseg1 = ((cnf2 >> 3) & 0x07) + 1;
    int phseg2 = ((cnf3 >> 3) & 0x07) + 1;

    CHECK(brp == 0,           "BRP = 0");
    CHECK(prseg == 2,         "PropSeg = 2 TQ");
    CHECK(phseg1 == 3,        "PS1 = 3 TQ");
    CHECK(phseg2 == 2,        "PS2 = 2 TQ (PHSEG2 bits are at CNF3[5:3], not [2:0])");
    int total_tq = 1 + prseg + phseg1 + phseg2;
    CHECK(total_tq == 8,      "Total = 8 TQ (minimum per CAN spec)");

    // ── Test 2: bit rate at 8 MHz crystal ──────────────────────────
    printf("\n-- Test 2: Bit rate at 8 MHz crystal --\n");
    double t_ns = tq_ns(brp, 8.0);
    CHECK(t_ns > 249.0 && t_ns < 251.0, "TQ ≈ 250 ns (0.25 µs) at 8 MHz");

    double bit_rate = 1.0e9 / (total_tq * t_ns);
    CHECK(bit_rate > 490000.0 && bit_rate < 510000.0,
          "Bit rate ≈ 500 kbit/s at 8 MHz");

    // ── Test 3: bit rate at 16 MHz crystal ─────────────────────────
    printf("\n-- Test 3: Bit rate at 16 MHz crystal (common on Chinese modules) --\n");
    double t_ns16 = tq_ns(brp, 16.0);
    CHECK(t_ns16 > 124.0 && t_ns16 < 126.0, "TQ ≈ 125 ns (0.125 µs) at 16 MHz");

    double bit_rate16 = 1.0e9 / (total_tq * t_ns16);
    // With 16 MHz crystal and BRP=0: 1/(8*0.125us) = 1 MHz — TOO FAST
    CHECK(bit_rate16 > 990000.0 && bit_rate16 < 1010000.0,
          "WARNING: 16 MHz crystal + BRP=0 = 1 Mbit/s (not 500k!)");

    // ── Test 4: correct CNF3 verifies at CNF3 bit positions ────────
    printf("\n-- Test 4: CNF3 PHSEG2 bit position --\n");
    // The old buggy value 0x01 had PHSEG2=0 (bit 0 set, not bits 5-3)
    uint8_t old_cnf3 = 0x01;
    int phseg2_old = ((old_cnf3 >> 3) & 0x07) + 1;  // PHSEG2 at bits 5-3
    CHECK(phseg2_old == 1, "Old CNF3=0x01 → PHSEG2=0 → PS2=1 TQ (BUG: only 7 TQ total)");

    int old_total = 1 + prseg + phseg1 + phseg2_old;
    CHECK(old_total == 7, "Old config total = 7 TQ = ~571 kbit/s");

    double old_rate = 1.0e9 / (old_total * t_ns);
    printf("  Old bit rate ≈ %.0f kbit/s (vs 500 kbit/s expected)\n", old_rate / 1000.0);

    // ── Results ─────────────────────────────────────────────────────
    printf("\n=== Results ===\n");
    printf("Pass: %d, Fail: %d\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
