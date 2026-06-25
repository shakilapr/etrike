/*
 * test_checksum_full.cpp
 *
 * Comprehensive checksum validation for SYNTREE protocol messages.
 *
 * The SYNTREE checksum is: XOR(bytes[0..6]) ^ 0xFF
 * This produces the property: XOR(bytes[0..7]) == 0xFF
 *
 * Test categories:
 *   1. Known-answer: fixed input bytes -> expected checksum
 *   2. Pack roundtrip: to_frame -> verify checksum byte
 *   3. Unpack roundtrip: from_frame -> verify checksum field == recomputed
 *   4. Boundary: extreme input values
 *   5. Invariant: XOR-sum of all 8 bytes always equals 0xFF
 *   6. Corruption detection: bit flips cause checksum mismatch
 */

#include <cstdio>
#include <cstdint>
#include <cstring>

#include "can/can_protocol.h"

static int failures = 0;

#define CHECK(cond, msg)                                 \
    do {                                                 \
        if (!(cond)) {                                   \
            printf("  FAIL: %s\n", msg);                 \
            failures++;                                  \
        }                                                \
    } while (0)

#define CHECK_EQ(a, b, msg)                              \
    do {                                                 \
        auto _a = (a);                                   \
        auto _b = (b);                                   \
        if (_a == _b) {                                  \
            /* pass */                                   \
        } else {                                         \
            printf("  FAIL: %s  (%d != %d)\n",           \
                   msg, (int)_a, (int)_b);               \
            failures++;                                  \
        }                                                \
    } while (0)

/* ---- Reference checksum implementation ---- */

static uint8_t compute_checksum_xor(const uint8_t raw[8]) {
    uint8_t cksum = 0;
    for (int i = 0; i < 7; ++i) cksum ^= raw[i];
    return cksum ^ 0xFF;
}

/* ---- Assert that XOR of all 8 bytes == 0xFF ---- */

static bool xor_sum_invariant(const uint8_t raw[8]) {
    uint8_t x = 0;
    for (int i = 0; i < 8; ++i) x ^= raw[i];
    return x == 0xFF;
}

/* ===================================================================
 *  Section 1: Known-answer tests (VcuSesReq, 0x169)
 * =================================================================== */

static void test_known_vector_ses_defaults() {
    printf("  Known vector: VcuSesReq defaults ...\n");

    // Factory defaults (angle=0, speed=328, all enables=1, rollcnt=0)
    uint8_t raw[8] = {};
    can::VcuSesReq r1{};
    r1.align_enable    = 1;
    r1.control_enable  = 1;
    r1.target_angle    = 0;
    r1.target_speed    = 328;       // 0x148
    r1.roll_cnt_enable = 1;
    r1.checksum_enable = 1;
    r1.rolling_counter = 0;
    r1.vehicle_speed   = 0;
    r1.pack(raw);

    // byte 0: 0x03    (align=1, ctrl=1)
    // byte 1: 0x00
    // byte 2: 0x00    (angle low)
    // byte 3: 0x00    (angle high)
    // byte 4: 0x48    (speed low: 0x148 & 0xFF)
    // byte 5: 0x07    (bit0=1, bit1=1, bits2-3=(0x148>>8)&3=1, rollcnt=0)
    // byte 6: 0x00
    // XOR(0x03,0x00,0x00,0x00,0x48,0x07,0x00) = 0x4C
    // cksum = 0x4C ^ 0xFF = 0xB3
    CHECK(raw[0] == 0x03, "SES known[0] = 0x03");
    CHECK(raw[1] == 0x00, "SES known[1] = 0x00");
    CHECK(raw[2] == 0x00, "SES known[2] = 0x00");
    CHECK(raw[3] == 0x00, "SES known[3] = 0x00");
    CHECK(raw[4] == 0x48, "SES known[4] = 0x48");
    CHECK(raw[5] == 0x07, "SES known[5] = 0x07");
    CHECK(raw[6] == 0x00, "SES known[6] = 0x00");
    CHECK(raw[7] == 0xB3, "SES known[7] = 0xB3 (XOR^0xFF)");

    CHECK(xor_sum_invariant(raw), "SES defaults XOR invariant 0xFF");
}

static void test_known_vector_ses_max_angle() {
    printf("  Known vector: VcuSesReq max angle + speed ...\n");

    can::VcuSesReq r{};
    r.align_enable    = 1;
    r.control_enable  = 1;
    r.target_angle    = 6000;       // 0x1770
    r.target_speed    = 525;        // 0x20D, max SYNTREE range
    r.roll_cnt_enable = 1;
    r.checksum_enable = 1;
    r.rolling_counter = 0xF;
    r.vehicle_speed   = 255;

    uint8_t raw[8] = {};
    r.pack(raw);

    uint8_t expected_cksum = compute_checksum_xor(raw);
    CHECK(raw[7] == expected_cksum, "SES max angle cksum matches XOR^0xFF");
    CHECK(xor_sum_invariant(raw), "SES max angle XOR invariant");

    // Verify a few critical bytes
    CHECK(raw[2] == 0x70, "SES angle low byte = 0x70");       // 0x1770 & 0xFF
    CHECK(raw[3] == 0x17, "SES angle high byte = 0x17");      // 0x1770 >> 8
    CHECK(raw[6] == 0xFF, "SES vehicle_speed = 0xFF");
}

/* ===================================================================
 *  Section 2: Known-answer tests (VcuSebReq, 0x7B9)
 * =================================================================== */

static void test_known_vector_seb_stroke_default() {
    printf("  Known vector: VcuSebReq stroke defaults ...\n");

    // Defaults: stroke=600, control_mode=0, rollcnt=1, enables=1
    can::VcuSebReq r{};
    r.align_enable    = 1;
    r.control_enable  = 1;
    r.control_mode    = 0;       // Stroke
    r.auto_brake      = 0;
    r.stroke_req      = 600;     // 0x0258
    r.roll_cnt_enable = 1;
    r.checksum_enable = 1;
    r.rolling_counter = 1;

    uint8_t raw[8] = {};
    r.pack(raw);

    // byte 0: 0x03    (align=1, ctrl=1, mode=0, auto=0)
    // byte 1: 0x00
    // byte 2: 0x58    (stroke low)
    // byte 3: 0x02    (stroke high)
    // byte 4: 0x00    (reserved)
    // byte 5: 0x00    (reserved)
    // byte 6: 0x13    (bit0=1, bit1=1, bits4-7=1 -> 0x01|0x02|0x10 = 0x13)
    // XOR(0x03,0x00,0x58,0x02,0x00,0x00,0x13) = 0x4A
    // cksum = 0x4A ^ 0xFF = 0xB5
    CHECK(raw[0] == 0x03, "SEB known[0] = 0x03");
    CHECK(raw[2] == 0x58, "SEB known[2] = 0x58");
    CHECK(raw[3] == 0x02, "SEB known[3] = 0x02");
    CHECK(raw[6] == 0x13, "SEB known[6] = 0x13");
    CHECK(raw[7] == 0xB5, "SEB known[7] = 0xB5");
    CHECK(xor_sum_invariant(raw), "SEB stroke default XOR invariant");
}

static void test_known_vector_seb_pressure() {
    printf("  Known vector: VcuSebReq pressure mode ...\n");

    can::VcuSebReq r{};
    r.align_enable    = 1;
    r.control_enable  = 1;
    r.control_mode    = 1;       // Pressure mode
    r.auto_brake      = 1;
    r.stroke_req      = 900;     // stroke still present for low byte
    r.pressure_req    = 75;      // 3.75 MPa
    r.roll_cnt_enable = 1;
    r.checksum_enable = 1;
    r.rolling_counter = 8;

    uint8_t raw[8] = {};
    r.pack(raw);

    uint8_t expected_cksum = compute_checksum_xor(raw);
    CHECK(raw[7] == expected_cksum, "SEB pressure cksum matches XOR^0xFF");
    CHECK(xor_sum_invariant(raw), "SEB pressure XOR invariant");

    // Pressure mode: byte 0 bit 2 set, byte 3 = pressure_req
    CHECK((raw[0] >> 2) & 1, "SEB pressure mode bit set");
    CHECK(raw[3] == 75, "SEB byte 3 = pressure_req 75");
}

/* ===================================================================
 *  Section 3: Pack roundtrip checksum validation
 * =================================================================== */

static void test_pack_checksum_ses() {
    printf("  Pack checksum: VcuSesReq all field variations ...\n");

    // Various target angles across the full range
    int32_t angles[] = {-7000, -300, 0, 300, 7000, 12000, 30000};
    for (auto angle : angles) {
        can::VcuSesReq r{};
        r.control_enable  = 1;
        r.checksum_enable = 1;
        r.target_angle    = angle;
        r.target_speed    = 328;

        can::Frame f{};
        r.to_frame(f);

        uint8_t expected = compute_checksum_xor(f.data);
        CHECK_EQ(f.data[7], expected, "SES pack checksum for angle");
    }
}

static void test_pack_checksum_seb() {
    printf("  Pack checksum: VcuSebReq all field variations ...\n");

    // Various stroke values
    uint16_t strokes[] = {0, 600, 1000, 30000, 60000, 65535};
    for (auto stroke : strokes) {
        can::VcuSebReq r{};
        r.control_enable  = 1;
        r.checksum_enable = 1;
        r.stroke_req      = stroke;
        r.roll_cnt_enable = 1;

        can::Frame f{};
        r.to_frame(f);

        uint8_t expected = compute_checksum_xor(f.data);
        CHECK_EQ(f.data[7], expected, "SEB pack checksum for stroke");
    }
}

/* ===================================================================
 *  Section 4: Unpack checksum validation
 * =================================================================== */

static void test_unpack_checksum_ses() {
    printf("  Unpack checksum: VcuSesReq field matches recomputed ...\n");

    for (int cnt = 0; cnt < 16; ++cnt) {
        can::VcuSesReq r{};
        r.control_enable  = 1;
        r.checksum_enable = 1;
        r.target_angle    = cnt * 1000;
        r.target_speed    = 300 + cnt * 10;
        r.rolling_counter = cnt;
        r.roll_cnt_enable = 1;

        can::Frame f{};
        r.to_frame(f);

        auto unpacked = can::VcuSesReq::unpack(f.data);
        uint8_t recomputed = compute_checksum_xor(f.data);
        CHECK_EQ(unpacked.checksum, recomputed,
                 "SES unpack checksum == recomputed");
        CHECK_EQ(unpacked.checksum, f.data[7],
                 "SES unpack checksum == wire byte");
    }
}

static void test_unpack_checksum_seb() {
    printf("  Unpack checksum: VcuSebReq field matches recomputed ...\n");

    for (int cnt = 0; cnt < 16; ++cnt) {
        can::VcuSebReq r{};
        r.control_enable  = 1;
        r.checksum_enable = 1;
        r.stroke_req      = 600 + cnt * 50;
        r.rolling_counter = cnt;
        r.roll_cnt_enable = 1;

        can::Frame f{};
        r.to_frame(f);

        auto unpacked = can::VcuSebReq::unpack(f.data);
        uint8_t recomputed = compute_checksum_xor(f.data);
        CHECK_EQ(unpacked.checksum, recomputed,
                 "SEB unpack checksum == recomputed");
        CHECK_EQ(unpacked.checksum, f.data[7],
                 "SEB unpack checksum == wire byte");
    }
}

/* ===================================================================
 *  Section 5: Boundary value tests
 * =================================================================== */

static void test_checksum_all_zero() {
    printf("  Boundary: all-zero payload ...\n");

    // VcuSesReq with everything zero
    can::VcuSesReq r{};
    // Defaults: target_angle=0, target_speed=328 -- need to zero
    r.target_speed = 0;

    can::Frame f{};
    r.to_frame(f);

    uint8_t expected = compute_checksum_xor(f.data);
    CHECK_EQ(f.data[7], expected, "SES all-zero cksum");
    CHECK(xor_sum_invariant(f.data), "SES all-zero XOR invariant");
}

static void test_checksum_all_ff() {
    printf("  Boundary: max-value payload ...\n");

    // VcuSesReq with max values
    {
        can::VcuSesReq r{};
        r.control_enable  = 1;
        r.target_angle    = 32767;       // max int16
        r.target_speed    = 1023;        // max 10-bit
        r.roll_cnt_enable = 1;
        r.checksum_enable = 1;
        r.rolling_counter = 0xF;
        r.vehicle_speed   = 255;

        can::Frame f{};
        r.to_frame(f);

        uint8_t expected = compute_checksum_xor(f.data);
        CHECK_EQ(f.data[7], expected, "SES max-value cksum");
        CHECK(xor_sum_invariant(f.data), "SES max-value XOR invariant");
    }

    // VcuSebReq with max values
    {
        can::VcuSebReq r{};
        r.align_enable    = 1;
        r.control_enable  = 1;
        r.control_mode    = 1;           // Pressure mode
        r.auto_brake      = 1;
        r.stroke_req      = 65535;
        r.pressure_req    = 255;
        r.roll_cnt_enable = 1;
        r.checksum_enable = 1;
        r.rolling_counter = 0xF;

        can::Frame f{};
        r.to_frame(f);

        uint8_t expected = compute_checksum_xor(f.data);
        CHECK_EQ(f.data[7], expected, "SEB max-value cksum");
        CHECK(xor_sum_invariant(f.data), "SEB max-value XOR invariant");
    }
}

/* ===================================================================
 *  Section 6: XOR invariant for every rolling counter value
 * =================================================================== */

static void test_xor_invariant_all_rollcnt() {
    printf("  Invariant: XOR[0..7] == 0xFF across all rollcnt ...\n");

    for (uint8_t cnt = 0; cnt < 16; ++cnt) {
        // VcuSesReq
        {
            can::VcuSesReq r{};
            r.control_enable  = 1;
            r.checksum_enable = 1;
            r.target_angle    = cnt * 2000;
            r.target_speed    = 328 + cnt;
            r.rolling_counter = cnt;
            r.roll_cnt_enable = 1;

            can::Frame f{};
            r.to_frame(f);
            CHECK(xor_sum_invariant(f.data), "SES XOR invariant rollcnt");
        }

        // VcuSebReq
        {
            can::VcuSebReq r{};
            r.control_enable  = 1;
            r.checksum_enable = 1;
            r.stroke_req      = 600 + cnt * 100;
            r.rolling_counter = cnt;
            r.roll_cnt_enable = 1;

            can::Frame f{};
            r.to_frame(f);
            CHECK(xor_sum_invariant(f.data), "SEB XOR invariant rollcnt");
        }
    }
}

/* ===================================================================
 *  Section 7: Corruption detection
 * =================================================================== */

static void test_corruption_detection() {
    printf("  Corruption: bit flips cause checksum mismatch ...\n");

    // Pack a frame, then flip a single bit in a data byte.
    // The checksum should fail to match the flipped data.
    can::VcuSesReq r{};
    r.control_enable  = 1;
    r.checksum_enable = 1;
    r.target_angle    = 5000;
    r.target_speed    = 400;
    r.rolling_counter = 5;
    r.roll_cnt_enable = 1;

    can::Frame f{};
    r.to_frame(f);

    uint8_t original_checksum = f.data[7];
    uint8_t recomputed = compute_checksum_xor(f.data);
    CHECK_EQ(original_checksum, recomputed, "Corruption: baseline checksum matches");

    // Flip bit 0 of byte 2 (target_angle LSB)
    f.data[2] ^= 0x01;
    uint8_t corrupted_recomputed = compute_checksum_xor(f.data);
    CHECK(corrupted_recomputed != original_checksum,
          "Corruption: bit flip changes checksum");

    // Restore and verify re-match
    f.data[2] ^= 0x01;
    uint8_t restored = compute_checksum_xor(f.data);
    CHECK_EQ(restored, original_checksum, "Corruption: restore matches");
}

/* ===================================================================
 *  Section 8: to_frame/from_frame checksum roundtrip
 * =================================================================== */

static void test_frame_roundtrip_checksum() {
    printf("  Frame roundtrip: to_frame + from_frame preserves checksum ...\n");

    // Full to_frame -> from_frame cycle for VcuSesReq
    {
        can::VcuSesReq r{};
        r.align_enable    = 1;
        r.control_enable  = 1;
        r.target_angle    = 7500;
        r.target_speed    = 480;
        r.roll_cnt_enable = 1;
        r.checksum_enable = 1;
        r.rolling_counter = 11;
        r.vehicle_speed   = 30;

        can::Frame f{};
        r.to_frame(f);

        auto unpacked = can::VcuSesReq::unpack(f.data);
        uint8_t recomputed = compute_checksum_xor(f.data);
        CHECK_EQ(unpacked.checksum, recomputed,
                 "SES frame roundtrip cksum matches");
        CHECK_EQ(unpacked.checksum, f.data[7],
                 "SES frame roundtrip cksum == wire");
        CHECK(xor_sum_invariant(f.data),
              "SES frame roundtrip XOR invariant");
    }

    // Full to_frame -> from_frame cycle for VcuSebReq
    {
        can::VcuSebReq r{};
        r.align_enable    = 1;
        r.control_enable  = 1;
        r.control_mode    = 0;          // Stroke mode
        r.stroke_req      = 5000;
        r.roll_cnt_enable = 1;
        r.checksum_enable = 1;
        r.rolling_counter = 9;

        can::Frame f{};
        r.to_frame(f);

        auto unpacked = can::VcuSebReq::unpack(f.data);
        uint8_t recomputed = compute_checksum_xor(f.data);
        CHECK_EQ(unpacked.checksum, recomputed,
                 "SEB frame roundtrip cksum matches");
        CHECK_EQ(unpacked.checksum, f.data[7],
                 "SEB frame roundtrip cksum == wire");
        CHECK(xor_sum_invariant(f.data),
              "SEB frame roundtrip XOR invariant");
    }
}

/* =================================================================== */

int main() {
    printf("=== SYNTREE Checksum Full Tests ===\n\n");

    printf("--- Known-answer tests ---\n");
    test_known_vector_ses_defaults();
    test_known_vector_ses_max_angle();
    test_known_vector_seb_stroke_default();
    test_known_vector_seb_pressure();

    printf("\n--- Pack checksum ---\n");
    test_pack_checksum_ses();
    test_pack_checksum_seb();

    printf("\n--- Unpack checksum ---\n");
    test_unpack_checksum_ses();
    test_unpack_checksum_seb();

    printf("\n--- Boundary values ---\n");
    test_checksum_all_zero();
    test_checksum_all_ff();

    printf("\n--- XOR invariant ---\n");
    test_xor_invariant_all_rollcnt();

    printf("\n--- Corruption detection ---\n");
    test_corruption_detection();

    printf("\n--- Frame roundtrip ---\n");
    test_frame_roundtrip_checksum();

    printf("\n=== %s: %d failures ===\n",
           failures == 0 ? "ALL PASS" : "SOME FAILED", failures);
    return failures;
}
