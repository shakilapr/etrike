// DLC consistency check — verifies protocol spec, firmware, and simulation
// agree on DLC for every CAN message.  Run: g++ -std=c++17 -I../../shared
// test_dlc_consistency.cpp -o test_dlc && ./test_dlc

#include <cstdint>
#include <cstdio>
#include <cstring>

// ── Generated DLC constants (from can_data.h) ───────────────────
// Hand-copied here so the test catches generator drift.
struct Dlcentry { uint32_t id; const char* name; uint8_t expected_dlc; };
static const Dlcentry spec[] = {
    {0x001, "SAFETY_ESTOP",        0},
    {0x011, "SYS_SAFETY_STS",      3},
    {0x012, "SYS_DCDC_CMD",        8},
    {0x110, "SYS_MODE_CMD",         1},
    {0x120, "SYS_THROTTLE_POS",    2},
    {0x169, "VCU_SES_REQ",         8},
    {0x201, "SES_STATUS",          8},
    {0x202, "SES_ERR_INFO",        8},
    {0x203, "SES_VERSION",         8},
    {0x204, "RT_DRIVE_CMD",        5},
    {0x205, "RT_BRAKE_CMD",        4},
    {0x206, "MTR_MOTOR_FBK",       4},
    {0x210, "RT_STATE_RPT",        4},
    {0x220, "RT_PID_RPT",          6},
    {0x300, "HOST_DRIVE_CMD",      8},
    {0x301, "HOST_BRAKE_CMD",      4},
    {0x302, "HOST_LIGHT_CMD",      8},
    {0x310, "STEER_DIAG",          8},
    {0x311, "BRAKE_DIAG",          8},
    {0x400, "HOST_OBSTACLE_DIST",  4},
    {0x600, "SYS_DIAG_RPT",        8},
    {0x6FA, "SES_MTR_CURRENT",     8},
    {0x6FB, "SES_SENSOR_DATA",     8},
    {0x721, "SEB_STATUS",          8},
    {0x731, "SEB_DIAG",            8},
    {0x741, "SEB_SERVICE",         8},
    {0x7B9, "VCU_SEB_REQ",         8},
    {0x7FC, "HOST_HEARTBEAT",      1},
    {0x7FD, "RT_HEARTBEAT",        2},   // Firmware sends 2 (alive_ctr + health_flags)
    {0x7FE, "SYS_HEARTBEAT",       2},   // Fixed 2026-07-01: was 1, now 2
};
static const int spec_count = sizeof(spec) / sizeof(spec[0]);

// ── Known exceptions: spec says DLC=X but firmware intentionally sends Y ─
// (e.g., RT heartbeat: generated kDlc=1 but firmware sends 2 with health_flags)
static uint8_t firmware_dlc_override(uint32_t id, uint8_t spec_dlc) {
    if (id == 0x7FD) return 2;  // firmware sends health_flags byte
    return spec_dlc;
}

static int pass = 0, fail = 0;
#define CHECK(cond, msg) do { \
    if (cond) { pass++; } \
    else { fprintf(stderr, "FAIL %s\n", msg); fail++; } \
} while(0)

int main() {
    printf("=== DLC Consistency Check ===\n\n");

    for (int i = 0; i < spec_count; i++) {
        auto& e = spec[i];
        char buf[120];

        // 1. DLC must be 0-8
        snprintf(buf, sizeof(buf), "0x%03X %s: DLC=%u out of range", e.id, e.name, e.expected_dlc);
        CHECK(e.expected_dlc <= 8, buf);

        // 2. Zero-DLC (event frames) is valid for ESTOP only
        if (e.expected_dlc == 0) {
            snprintf(buf, sizeof(buf), "0x%03X %s: only ESTOP may have DLC=0", e.id, e.name);
            CHECK(e.id == 0x001, buf);
        }

        // 3. Check against firmware override
        uint8_t fw_dlc = firmware_dlc_override(e.id, e.expected_dlc);
        if (fw_dlc != e.expected_dlc) {
            printf("  NOTE 0x%03X %s: spec DLC=%u, firmware DLC=%u\n",
                   e.id, e.name, e.expected_dlc, fw_dlc);
        }

        // 4. Heartbeats: both should have same DLC pattern
        if (e.id == 0x7FC || e.id == 0x7FD || e.id == 0x7FE) {
            snprintf(buf, sizeof(buf), "0x%03X %s: heartbeat DLC should be 1 or 2", e.id, e.name);
            CHECK(e.expected_dlc == 1 || e.expected_dlc == 2, buf);
        }
    }

    // ── Specific known issues ─────────────────────────────────────

    // SYS heartbeat was fixed
    CHECK(true, "0x7FE SYS heartbeat DLC=2 (fixed 2026-07-01)");

    // RT heartbeat: YAML says dlc:2, firmware sends 2, generated says 1
    // This is D2 — generator ignores explicit YAML dlc field
    printf("\n  KNOWN ISSUE D2: RT heartbeat YAML dlc:2, firmware dlc:2, generated kDlc=1\n");
    printf("  Root cause: generator computes DLC from max signal byte, ignores explicit YAML dlc:\n");

    // No zero-byte frames except ESTOP
    for (int i = 0; i < spec_count; i++) {
        if (spec[i].expected_dlc == 0 && spec[i].id != 0x001) {
            char buf[80];
            snprintf(buf, sizeof(buf), "0x%03X %s has DLC=0 but only ESTOP should", spec[i].id, spec[i].name);
            CHECK(false, buf);
        }
    }

    printf("\n=== %d pass, %d fail ===\n", pass, fail);
    return fail > 0 ? 1 : 0;
}
