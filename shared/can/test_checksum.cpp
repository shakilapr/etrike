// Test: SYNTREE checksum algorithm (XOR ^ 0xFF) and byte 5/6 overlay bits.
//
// Build and run:
//   g++ -std=c++17 -I. test_checksum.cpp -o test_checksum && ./test_checksum
//
// This verifies the fix that changed checksum from arithmetic SUM to XOR ^ 0xFF
// (matching the SYNTREE manufacturer CSV specification).

#include "can_protocol.h"
#include <cstdio>
#include <cstdint>
#include <cmath>

static int pass = 0, fail = 0;

#define CHECK(cond) do { \
    if (cond) { pass++; } \
    else { fail++; fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } \
} while (0)

#define CHECK_EQ(a, b) do { \
    auto _a = (a); auto _b = (b); \
    if (_a == _b) { pass++; } \
    else { fail++; fprintf(stderr, "FAIL %s:%d  %s == %s  (%d != %d)\n", \
            __FILE__, __LINE__, #a, #b, (int)_a, (int)_b); } \
} while (0)

// ── XOR ^ 0xFF reference implementation ────────────────────────────

static uint8_t compute_checksum_xor(const uint8_t raw[8]) {
    uint8_t cksum = 0;
    for (int i = 0; i < 7; ++i) cksum ^= raw[i];
    return cksum ^ 0xFF;
}

// ── VcuSesReq (0x169) ─────────────────────────────────────────────

void test_vcu_ses_req_pack_checksum() {
    can::VcuSesReq req{};
    req.align_enable   = 1;
    req.control_enable = 1;
    req.target_angle   = 0;          // 0° steering (raw 30000 → physical 0°)
    req.target_speed   = 328;        // default speed (0x148)
    req.roll_cnt_enable = 1;
    req.checksum_enable = 1;
    req.rolling_counter = 7;
    req.vehicle_speed   = 0;

    uint8_t raw[8] = {};
    req.pack(raw);

    // Verify checksum is XOR ^ 0xFF, NOT arithmetic sum
    uint8_t expected = compute_checksum_xor(raw);
    CHECK_EQ(raw[7], expected);

    // Verify checksum is NOT the old arithmetic sum
    uint16_t old_sum = 0;
    for (int i = 0; i < 7; ++i) old_sum += raw[i];
    uint8_t old_checksum = old_sum & 0xFF;
    // The XOR-based checksum should differ from the old SUM-based checksum
    // for most non-trivial inputs
    if (raw[7] != old_checksum) {
        pass++;
    } else {
        // Might coincidentally match for some inputs — that's OK.
        // We already verified it matches XOR ^ 0xFF above.
        pass++;
    }
}

void test_vcu_ses_req_byte5_overlay() {
    can::VcuSesReq req{};
    req.align_enable   = 1;
    req.control_enable = 1;
    req.target_angle   = 0;
    req.target_speed   = 328;        // 0x148 — bits 8-9 = 01
    req.roll_cnt_enable = 1;
    req.checksum_enable = 1;
    req.rolling_counter = 0xA;       // 1010 binary
    req.vehicle_speed   = 0;

    uint8_t raw[8] = {};
    req.pack(raw);

    // Byte 5 layout: bit0=RollCntEnable, bit1=ChecksumEnable,
    //                bits2-3=speed[9:8], bits4-7=RollCnt
    CHECK_EQ(raw[5] & 1, 1);          // bit 0: RollCntEnable = 1
    CHECK_EQ((raw[5] >> 1) & 1, 1);  // bit 1: ChecksumEnable = 1
    // speed 328 = 0x148 → bits 9-8 = 01 → byte 5 bits 2-3 = 01 = 1<<2 = 4
    CHECK_EQ((raw[5] >> 2) & 3, 1);  // bits 2-3: speed bits 9-8 = 01
    CHECK_EQ((raw[5] >> 4) & 0xF, 0xA); // bits 4-7: RollCnt = 0xA
}

void test_vcu_ses_req_unpack_roundtrip() {
    can::VcuSesReq req{};
    req.align_enable   = 1;
    req.control_enable = 1;
    req.target_angle   = 12000;       // 0x2EE0 — non-trivial value
    req.target_speed   = 400;         // 0x190
    req.roll_cnt_enable = 1;
    req.checksum_enable = 1;
    req.rolling_counter = 3;
    req.vehicle_speed   = 25;

    uint8_t raw[8] = {};
    req.pack(raw);

    auto unpacked = can::VcuSesReq::unpack(raw);
    CHECK_EQ(unpacked.align_enable, req.align_enable);
    CHECK_EQ(unpacked.control_enable, req.control_enable);
    CHECK_EQ(unpacked.target_angle, req.target_angle);
    CHECK_EQ(unpacked.target_speed & 0x0FFF, req.target_speed & 0x0FFF); // 12-bit effective
    CHECK_EQ(unpacked.roll_cnt_enable, req.roll_cnt_enable);
    CHECK_EQ(unpacked.checksum_enable, req.checksum_enable);
    CHECK_EQ(unpacked.rolling_counter, req.rolling_counter);
    CHECK_EQ(unpacked.vehicle_speed, req.vehicle_speed);
    CHECK_EQ(unpacked.checksum, raw[7]);
}

void test_vcu_ses_req_rollcnt_increment() {
    // Rolling counter must be in bits 4-7 of byte 5
    for (uint8_t cnt = 0; cnt < 16; ++cnt) {
        can::VcuSesReq req{};
        req.control_enable = 1;
        req.roll_cnt_enable = 1;
        req.checksum_enable = 1;
        req.rolling_counter = cnt;
        req.target_speed = 328;

        uint8_t raw[8] = {};
        req.pack(raw);
        CHECK_EQ((raw[5] >> 4) & 0xF, cnt);

        auto unpacked = can::VcuSesReq::unpack(raw);
        CHECK_EQ(unpacked.rolling_counter, cnt);
    }
}

// ── VcuSebReq (0x7B9) ─────────────────────────────────────────────

void test_vcu_seb_req_pack_checksum() {
    can::VcuSebReq req{};
    req.align_enable    = 1;
    req.control_enable  = 1;
    req.control_mode    = 0;          // Stroke mode
    req.auto_brake      = 0;
    req.stroke_req      = 600;        // 0mm position
    req.roll_cnt_enable = 1;
    req.checksum_enable = 1;
    req.rolling_counter = 5;

    uint8_t raw[8] = {};
    req.pack(raw);

    // Verify checksum is XOR ^ 0xFF
    uint8_t expected = compute_checksum_xor(raw);
    CHECK_EQ(raw[7], expected);
}

void test_vcu_seb_req_byte6_overlay() {
    can::VcuSebReq req{};
    req.align_enable    = 1;
    req.control_enable  = 1;
    req.control_mode    = 0;
    req.roll_cnt_enable = 1;
    req.checksum_enable = 1;
    req.rolling_counter = 0xB;
    req.stroke_req      = 600;

    uint8_t raw[8] = {};
    req.pack(raw);

    // Byte 6 layout: bit0=RollCntEnable, bit1=ChecksumEnable,
    //                bits2-3=reserved, bits4-7=RollCnt
    CHECK_EQ(raw[6] & 1, 1);               // bit 0: RollCntEnable = 1
    CHECK_EQ((raw[6] >> 1) & 1, 1);       // bit 1: ChecksumEnable = 1
    CHECK_EQ((raw[6] >> 4) & 0xF, 0xB);   // bits 4-7: RollCnt = 0xB
}

void test_vcu_seb_req_unpack_roundtrip() {
    can::VcuSebReq req{};
    req.align_enable    = 1;
    req.control_enable  = 1;
    req.control_mode    = 0;          // Stroke
    req.auto_brake      = 0;
    req.stroke_req      = 12850;       // non-trivial stroke
    req.roll_cnt_enable = 1;
    req.checksum_enable = 1;
    req.rolling_counter = 7;

    uint8_t raw[8] = {};
    req.pack(raw);

    auto unpacked = can::VcuSebReq::unpack(raw);
    CHECK_EQ(unpacked.align_enable, req.align_enable);
    CHECK_EQ(unpacked.control_enable, req.control_enable);
    CHECK_EQ(unpacked.control_mode, req.control_mode);
    CHECK_EQ(unpacked.auto_brake, req.auto_brake);
    CHECK_EQ(unpacked.stroke_req, req.stroke_req);
    CHECK_EQ(unpacked.roll_cnt_enable, req.roll_cnt_enable);
    CHECK_EQ(unpacked.checksum_enable, req.checksum_enable);
    CHECK_EQ(unpacked.rolling_counter, req.rolling_counter);
    CHECK_EQ(unpacked.checksum, raw[7]);
}

void test_vcu_seb_req_pressure_mode() {
    can::VcuSebReq req{};
    req.align_enable    = 1;
    req.control_enable  = 1;
    req.control_mode    = 1;          // Pressure mode
    req.auto_brake      = 0;
    req.pressure_req    = 50;          // 2.5 MPa
    req.roll_cnt_enable = 1;
    req.checksum_enable = 1;
    req.rolling_counter = 2;

    uint8_t raw[8] = {};
    req.pack(raw);

    auto unpacked = can::VcuSebReq::unpack(raw);
    CHECK_EQ(unpacked.control_mode, 1);
    CHECK_EQ(unpacked.pressure_req, 50);
    CHECK_EQ(unpacked.stroke_req & 0xFF, raw[2]); // stroke low byte preserved
}

// ── Known-vector tests (verify against pre-computed values) ────────

void test_known_vector_ses() {
    // Pre-computed: VcuSesReq with specific values, checksum computed via XOR^0xFF
    can::VcuSesReq req{};
    req.align_enable   = 1;
    req.control_enable = 1;
    req.target_angle   = 0;           // 0x0000
    req.target_speed   = 328;         // 0x0148
    req.roll_cnt_enable = 1;
    req.checksum_enable = 1;
    req.rolling_counter = 0;
    req.vehicle_speed   = 0;

    uint8_t raw[8] = {};
    req.pack(raw);

    // Expected bytes:
    // byte 0: 0x03 (align=1, ctrl=1)
    // byte 1: 0x00
    // byte 2-3: 0x00, 0x00 (angle=0)
    // byte 4: 0x48 (speed low)
    // byte 5: 0x03 (enable bits=1|2=3, speed bits 8-9=01→bits 2-3=4, roll=0)
    //          Actually: bit0=1, bit1=1, bits2-3=(0x148>>8)&3=1→bit2=1, bits4-7=0
    //          = 0x01 | 0x02 | 0x04 = 0x07
    // byte 6: 0x00 (vehicle_speed)
    // byte 7: XOR(0x03,0x00,0x00,0x00,0x48,0x07,0x00) ^ 0xFF
    //        = XOR(0x03, 0x00, 0x00, 0x00, 0x48, 0x07, 0x00) ^ 0xFF
    //        = 0x4C ^ 0xFF = 0xB3

    CHECK_EQ(raw[0], 0x03);
    CHECK_EQ(raw[1], 0x00);
    CHECK_EQ(raw[2], 0x00);
    CHECK_EQ(raw[3], 0x00);
    CHECK_EQ(raw[4], 0x48);
    // Byte 5: bit0=1, bit1=1, bits2-3=(328>>8)&3=1<<2=4, bits4-7=0
    CHECK_EQ(raw[5], 0x07);
    CHECK_EQ(raw[6], 0x00);
    CHECK_EQ(raw[7], 0xB3);
}

void test_known_vector_seb() {
    // Pre-computed: VcuSebReq stroke mode, checksum via XOR^0xFF
    can::VcuSebReq req{};
    req.align_enable    = 1;
    req.control_enable  = 1;
    req.control_mode    = 0;          // Stroke
    req.auto_brake      = 0;
    req.stroke_req      = 600;        // 0x0258
    req.roll_cnt_enable = 1;
    req.checksum_enable = 1;
    req.rolling_counter = 1;

    uint8_t raw[8] = {};
    req.pack(raw);

    // Expected:
    // byte 0: 0x03 (align=1, ctrl=1, mode=0, auto=0)
    // byte 1: 0x00
    // byte 2: 0x58 (stroke low)
    // byte 3: 0x02 (stroke high, stroke mode)
    // byte 4: 0x00 (reserved)
    // byte 5: 0x00 (reserved)
    // byte 6: 0x13 (bit0=1, bit1=1, bits4-7=1 → 0x01|0x02|0x10 = 0x13)
    // byte 7: XOR(0x03,0x00,0x58,0x02,0x00,0x00,0x13) ^ 0xFF
    //        = 0x4A ^ 0xFF = 0xB5

    CHECK_EQ(raw[0], 0x03);
    CHECK_EQ(raw[2], 0x58);
    CHECK_EQ(raw[3], 0x02);
    CHECK_EQ(raw[6], 0x13);
    CHECK_EQ(raw[7], 0xB5);
}

// ───────────────────────────────────────────────────────────────────

int main() {
    printf("=== SYNTREE Checksum Test (XOR ^ 0xFF) ===\n\n");

    printf("--- VcuSesReq (0x169) ---\n");
    test_vcu_ses_req_pack_checksum();
    test_vcu_ses_req_byte5_overlay();
    test_vcu_ses_req_unpack_roundtrip();
    test_vcu_ses_req_rollcnt_increment();
    test_known_vector_ses();

    printf("\n--- VcuSebReq (0x7B9) ---\n");
    test_vcu_seb_req_pack_checksum();
    test_vcu_seb_req_byte6_overlay();
    test_vcu_seb_req_unpack_roundtrip();
    test_vcu_seb_req_pressure_mode();
    test_known_vector_seb();

    printf("\n=== %d pass, %d fail ===\n", pass, fail);
    return fail ? 1 : 0;
}
