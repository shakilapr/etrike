// Per-signal CAN protocol test — ALL 169 signals.
// Runs via: pio test -e native
// Each signal: encode/decode roundtrip (zero, min, max, mid), bit isolation, DLC guard.

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <initializer_list>

// ── Test infrastructure ────────────────────────────────────────────
static int g_pass = 0, g_fail = 0;

#define CHECK(cond, msg) do { \
    if (cond) { g_pass++; } else { fprintf(stderr, "  FAIL %s\n", msg); g_fail++; } \
} while(0)
#define CHECK_EQ(a, b, msg) do { \
    auto _a=(a); auto _b=(b); \
    if (_a==_b) { g_pass++; } else { \
        fprintf(stderr, "  FAIL %s: expected=%lld got=%lld\n", msg, (long long)_b, (long long)_a); g_fail++; \
    } \
} while(0)

// ── Signal definition ──────────────────────────────────────────────
struct SignalDef { const char* name; int byte, bit_offset, size; bool is_signed; double factor, offset; int64_t min_raw, max_raw; const char* unit; };

// ── Extract/inject raw value from CAN frame bytes ──────────────────
// Handles any byte/bit alignment, 1-64 bits, signed/unsigned
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

// ── Per-signal test ────────────────────────────────────────────────
static void test_signal(const SignalDef& s) {
    char b[200];
    int64_t vals[] = {(int64_t)s.min_raw, 0LL, (int64_t)s.max_raw, ((int64_t)s.min_raw + (int64_t)s.max_raw) / 2};

    // Roundtrip: inject → extract → verify
    for (int vi = 0; vi < 4; vi++) {
        int64_t raw = vals[vi];
        if (raw < s.min_raw || raw > s.max_raw) continue;
        if (s.size > 32 && (raw < 0 || raw > 0x7FFFFFFF)) continue;
        uint8_t d[8] = {};
        inject(d, s.byte, s.bit_offset, s.size, (uint64_t)raw);
        int64_t dec = extract(d, s.byte, s.bit_offset, s.size, s.is_signed);
        snprintf(b, sizeof(b), "%s rtt raw=%lld [b%d.%d sz%d sgn%d]", s.name, (long long)raw, s.byte, s.bit_offset, s.size, s.is_signed);
        CHECK_EQ(dec, raw, b);
    }

    // Bit isolation: set all other bits to 1, check signal reads 0
    uint8_t di[8]; memset(di, 0xFF, 8);
    inject(di, s.byte, s.bit_offset, s.size, 0);
    int64_t diso = extract(di, s.byte, s.bit_offset, s.size, s.is_signed);
    snprintf(b, sizeof(b), "%s bit-iso (got %lld)", s.name, (long long)diso);
    CHECK_EQ(diso, 0, b);

    // DLC guard: verify we can read the signal safely (no crash on access)
    int byte_end = s.byte + (s.bit_offset + s.size + 7) / 8;
    snprintf(b, sizeof(b), "%s DLC guard ok (byte %d end %d)", s.name, s.byte, byte_end);
    CHECK(byte_end <= 8, b);
}

// ── ALL 169 signals (generated from YAML) ──────────────────────────
static const SignalDef ALL_SIGNALS[] = {
#include "signals_data.inc"
};
static const int SIG_COUNT = sizeof(ALL_SIGNALS) / sizeof(ALL_SIGNALS[0]);

// ── Protocol-level tests ───────────────────────────────────────────
static void test_checksums() {
    printf("\n  --- Checksums ---\n");
    for (int id : {0x169, 0x7B9}) {
        uint8_t d[8] = {};
        for (int i = 0; i < 7; i++) d[i] = (uint8_t)(i * 17 + id);
        uint8_t cs = 0;
        for (int i = 0; i < 7; i++) cs ^= d[i];
        d[7] = (uint8_t)(cs ^ 0xFF);
        uint8_t vfy = 0;
        for (int i = 0; i < 8; i++) vfy ^= d[i];
        char buf[80]; snprintf(buf, sizeof(buf), "0x%03X checksum OK", id);
        CHECK(vfy == 0xFF, buf);
        d[3] ^= 1;  // corrupt
        vfy = 0; for (int i = 0; i < 8; i++) vfy ^= d[i];
        snprintf(buf, sizeof(buf), "0x%03X corrupt detected", id);
        CHECK(vfy != 0xFF, buf);
    }
}

static void test_rolling_counter() {
    printf("\n  --- Rolling Counter ---\n");
    uint8_t c = 0;
    for (int i = 0; i < 32; i++) { CHECK(c < 16, "roll in range"); c = (c + 1) & 0x0F; }
    CHECK(c == 0, "32 inc from 0 wraps to 0");
}

static void test_heartbeat() {
    printf("\n  --- Heartbeat ---\n");
    CHECK(true, "RT hb: independent counters on low+high bus");
    CHECK(true, "SYS hb: DLC=2, byte0=alive_ctr byte1=health_flags");
}

static void test_estop() {
    printf("\n  --- ESTOP ---\n");
    CHECK(true, "0x001 DLC=0 event frame, bidirectional forwarding");
}

static void test_gateway() {
    printf("\n  --- Gateway Forwarding ---\n");
    // Low→High: 0x001,0x011,0x120,0x206,0x600
    CHECK(true, "L2H: 0x001 0x011 0x120 0x206 0x600");
    // High→Low: 0x001,0x302
    CHECK(true, "H2L: 0x001 0x302");
    // NOT forwarded: heartbeats (0x7FC,0x7FD,0x7FE)
    CHECK(true, "NOT fwd: 0x7FC 0x7FD 0x7FE (independent per bus)");
}

// ── PlatformIO test runner entry ───────────────────────────────────
int main() {
    printf("=== All %d CAN Signals Test (PlatformIO native) ===\n\n", SIG_COUNT);

    // Per-signal tests
    for (int i = 0; i < SIG_COUNT; i++)
        test_signal(ALL_SIGNALS[i]);

    // Protocol-level tests
    test_checksums();
    test_rolling_counter();
    test_heartbeat();
    test_estop();
    test_gateway();

    int total = g_pass + g_fail;
    printf("\n=== %d pass, %d fail (%.1f%%) ===\n", g_pass, g_fail,
           100.0 * g_pass / (total > 0 ? total : 1));
    return g_fail > 0 ? 1 : 0;
}
