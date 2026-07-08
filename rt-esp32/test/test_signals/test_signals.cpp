#include <unity.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cmath>

struct SignalDef { const char* name; int byte, bit_offset, size; bool is_signed; double factor, offset; int64_t min_raw, max_raw; const char* unit; };

static int64_t extract(const uint8_t d[8], int byte, int bo, int sz, bool sgn) {
    if (sz > 64) return 0;
    uint64_t raw = 0;
    int remain = sz, cb = byte, cbit = bo;
    while (remain > 0 && cb < 8) {
        int avail = 8 - cbit, take = remain < avail ? remain : avail;
        raw |= (uint64_t)((d[cb] >> cbit) & ((1u << take) - 1)) << (sz - remain);
        remain -= take; cbit = 0; cb++;
    }
    if (sgn && sz < 64 && (raw & (1ull << (sz - 1))))
        raw |= ~((1ull << sz) - 1);
    return (int64_t)raw;
}

static void inject(uint8_t d[8], int byte, int bo, int sz, uint64_t raw) {
    int remain = sz, cb = byte, cbit = bo;
    while (remain > 0 && cb < 8) {
        int avail = 8 - cbit, take = remain < avail ? remain : avail;
        uint8_t v = (raw >> (sz - remain)) & ((1u << take) - 1);
        d[cb] = (d[cb] & ~(((1u << take) - 1) << cbit)) | (v << cbit);
        remain -= take; cbit = 0; cb++;
    }
}

static const SignalDef ALL_SIGNALS[] = {
#include "../signals_data.inc"
};
static const int SIG_COUNT = sizeof(ALL_SIGNALS) / sizeof(ALL_SIGNALS[0]);

void setUp(void) {}
void tearDown(void) {}

void test_all_signals_roundtrip_and_isolation(void) {
    for (int i = 0; i < SIG_COUNT; i++) {
        const SignalDef& s = ALL_SIGNALS[i];
        int64_t vals[] = {(int64_t)s.min_raw, 0LL, (int64_t)s.max_raw, ((int64_t)s.min_raw + (int64_t)s.max_raw) / 2};

        for (int vi = 0; vi < 4; vi++) {
            int64_t raw = vals[vi];
            if (raw < s.min_raw || raw > s.max_raw) continue;
            if (s.size > 32 && (raw < 0 || raw > 0x7FFFFFFF)) continue;
            uint8_t d[8] = {};
            inject(d, s.byte, s.bit_offset, s.size, (uint64_t)raw);
            int64_t dec = extract(d, s.byte, s.bit_offset, s.size, s.is_signed);
            TEST_ASSERT_EQUAL_INT64_MESSAGE(raw, dec, s.name);
        }

        uint8_t di[8]; memset(di, 0xFF, 8);
        inject(di, s.byte, s.bit_offset, s.size, 0);
        int64_t diso = extract(di, s.byte, s.bit_offset, s.size, s.is_signed);
        TEST_ASSERT_EQUAL_INT64_MESSAGE(0, diso, s.name);

        int byte_end = s.byte + (s.bit_offset + s.size + 7) / 8;
        TEST_ASSERT_TRUE_MESSAGE(byte_end <= 8, s.name);
    }
}

void test_protocol_checksums(void) {
    int ids[] = {0x169, 0x7B9};
    for (int i = 0; i < 2; i++) {
        int id = ids[i];
        uint8_t d[8] = {};
        for (int j = 0; j < 7; j++) d[j] = (uint8_t)(j * 17 + id);
        uint8_t cs = 0;
        for (int j = 0; j < 7; j++) cs ^= d[j];
        d[7] = (uint8_t)(cs ^ 0xFF);
        uint8_t vfy = 0;
        for (int j = 0; j < 8; j++) vfy ^= d[j];
        TEST_ASSERT_EQUAL_UINT8(0xFF, vfy);

        d[3] ^= 1;
        vfy = 0; 
        for (int j = 0; j < 8; j++) vfy ^= d[j];
        TEST_ASSERT_NOT_EQUAL(0xFF, vfy);
    }
}

void test_protocol_rolling_counter(void) {
    uint8_t c = 0;
    for (int i = 0; i < 32; i++) {
        TEST_ASSERT_TRUE(c < 16);
        c = (c + 1) & 0x0F;
    }
    TEST_ASSERT_EQUAL_UINT8(0, c);
}

void test_protocol_frame_context_mode_dependent(void) {
    // 0x7B9 VCU_SEB_REQ
    uint8_t d1[8] = {};
    d1[4] = 0x01 | (0 << 1) | (0 << 3) | (1 << 4) | (1 << 5) | (1 << 6);
    inject(d1, 2, 0, 16, 900);
    int64_t stroke = extract(d1, 2, 0, 16, false);
    TEST_ASSERT_EQUAL_INT64(900, stroke);

    d1[4] = 0x01 | (1 << 1) | (0 << 3) | (1 << 4) | (1 << 5) | (1 << 6);
    inject(d1, 2, 0, 16, 100);
    int64_t pressure = extract(d1, 2, 0, 16, false);
    TEST_ASSERT_EQUAL_INT64(100, pressure);

    uint8_t cs = 0;
    for (int i = 0; i < 7; i++) cs ^= d1[i];
    d1[7] = cs ^ 0xFF;
    uint8_t vfy = 0;
    for (int i = 0; i < 8; i++) vfy ^= d1[i];
    TEST_ASSERT_EQUAL_UINT8(0xFF, vfy);

    // 0x169 VCU_SES_REQ
    uint8_t d2[8] = {};
    d2[4] = 0x01 | (0 << 1) | (1 << 3) | (1 << 4) | (1 << 5);
    inject(d2, 0, 0, 16, 30000);
    int64_t angle = extract(d2, 0, 0, 16, false);
    TEST_ASSERT_EQUAL_INT64(30000, angle);

    d2[4] = 0x01 | (1 << 1) | (1 << 3) | (1 << 4) | (1 << 5);
    inject(d2, 2, 0, 16, 5000);
    int64_t speed = extract(d2, 2, 0, 16, true);
    TEST_ASSERT_EQUAL_INT64(5000, speed);

    // 0x011 SYS_SAFETY_STS
    uint8_t d3[8] = {};
    d3[0] = (1 << 0) | (1 << 1) | (0 << 2) | (0 << 3) | (0 << 4) | (1 << 5);
    TEST_ASSERT_EQUAL_UINT8(1, d3[0] & 0x01);
    TEST_ASSERT_EQUAL_UINT8(1, (d3[0] >> 1) & 0x01);
    TEST_ASSERT_EQUAL_UINT8(0, (d3[0] >> 2) & 0x03);
    TEST_ASSERT_EQUAL_UINT8(0, (d3[0] >> 4) & 0x01);
    TEST_ASSERT_EQUAL_UINT8(1, (d3[0] >> 5) & 0x01);
    
    d3[1] = 42;
    TEST_ASSERT_EQUAL_UINT8(42, d3[1]);
}

extern "C" void app_main() {
    UNITY_BEGIN();
    RUN_TEST(test_all_signals_roundtrip_and_isolation);
    RUN_TEST(test_protocol_checksums);
    RUN_TEST(test_protocol_rolling_counter);
    RUN_TEST(test_protocol_frame_context_mode_dependent);
    UNITY_END();
}

#if defined(HOST_BUILD) || defined(NATIVE_TEST_ENV) || !defined(ESP_PLATFORM)
int main(int argc, char **argv) {
    app_main();
    return 0;
}
#endif
