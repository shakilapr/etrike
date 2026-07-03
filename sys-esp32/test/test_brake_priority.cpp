// g++ -std=c++17 -DTESTING -I. -I../src -I../../shared -I../../shared/can \
//     test_brake_priority.cpp -o test_bp && ./test_bp
//
// Verifies: brake_control.h — priority ordering (F1), auto_brake bit (F2).

#include <cstdio>
#include <cstdint>
#include "config.h"
#include "shared_config.h"
#include "can/can_protocol.h"
#include "brake_control.h"

static int pass=0, fail=0;
#define CHECK(cond) do { if(cond){pass++;}else{fail++;fprintf(stderr,"FAIL %s:%d\n",__FILE__,__LINE__);} } while(0)

// Valid 0x721 status: byte0=0x01 (aligned), bytes2-3=stroke_raw=600 (0mm)
static uint8_t seb_ok[8] = {0x01, 0, 0x58, 0x02, 0, 0, 0, 0};
// No 0x721 received yet → status_byte0=0xFF, stroke doesn't matter
static constexpr uint8_t  kNoStatus  = 0xFF;
static constexpr uint16_t kDefaultStroke = 600;
static inline uint16_t seb_stroke(const uint8_t* d) { return d[2] | (uint16_t(d[3]) << 8); }

// Bootstrap brake control to ACTIVE state via the DEGRADED path
static void bootstrap_active(sys::BrakeControl& bc) {
    // Run 110 ticks at 50 Hz = 2.2s — exceeds BOOT_WAIT(500ms) + LISTEN_SYNC(2s timeout)
    can::VcuSebReq unused;
    for (int i = 0; i < 110; ++i) {
        bc.tick(false, false, 0, can::Mode::Manual, kNoStatus, kDefaultStroke, unused);
    }
    // Now feed valid 0x721 to recover from DEGRADED → ACTIVE
    can::VcuSebReq dummy;
    bc.tick(false, false, 0, can::Mode::Manual, seb_ok[0], seb_stroke(seb_ok), dummy);
}

int main() {
    printf("\n=== Brake Control: Priority & auto_brake ===\n\n");
    using namespace sys;
    using namespace can;

    // ── F1: Priority ordering ──────────────────────────────────────────
    printf("-- Priority: ESTOP > lever > CAN pressure > release --\n");
    {
        BrakeControl bc;
        bc.init();
        bootstrap_active(bc);

        VcuSebReq out;

        // Test 1: ESTOP → max stroke 27mm → raw = (27+30)/0.05 = 1140
        bc.tick(false, true, 5000, Mode::Estop, seb_ok[0], seb_stroke(seb_ok), out);
        CHECK(out.control_mode == 0);     // Stroke Mode
        CHECK(out.stroke_req == 1140);    // 27mm
        CHECK(out.auto_brake == 0);       // ESTOP is NOT automated
        CHECK(out.pressure_req == 0);

        // Test 2: lever + brake_kpa>0 → LEVER WINS (15mm → raw 900)
        bc.tick(true, false, 2000, Mode::Auto, seb_ok[0], seb_stroke(seb_ok), out);
        CHECK(out.control_mode == 0);     // Stroke Mode (lever override)
        CHECK(out.stroke_req == 900);     // 15mm → (15+30)/0.05 = 900
        CHECK(out.auto_brake == 0);       // manual braking
        CHECK(out.pressure_req == 0);

        // Test 3: brake_kpa>0 without lever → Pressure Mode
        bc.tick(false, false, 2000, Mode::Auto, seb_ok[0], seb_stroke(seb_ok), out);
        CHECK(out.control_mode == 1);     // Pressure Mode
        CHECK(out.pressure_req > 0);      // kPa→raw conversion
        CHECK(out.auto_brake == 1);       // AUTO + CAN pressure = automated
        CHECK(out.stroke_req == 600);     // hold at 0mm

        // Test 4: no lever, no estop, no brake_kpa → release (0mm → raw 600)
        bc.tick(false, false, 0, Mode::Manual, seb_ok[0], seb_stroke(seb_ok), out);
        CHECK(out.control_mode == 0);     // Stroke Mode
        CHECK(out.stroke_req == 600);     // 0mm
        CHECK(out.auto_brake == 0);

        // Test 5: lever in MANUAL mode → 15mm
        bc.tick(true, false, 0, Mode::Manual, seb_ok[0], seb_stroke(seb_ok), out);
        CHECK(out.control_mode == 0);
        CHECK(out.stroke_req == 900);
        CHECK(out.auto_brake == 0);
    }

    // ── F2: auto_brake bit per mode ────────────────────────────────────
    printf("-- auto_brake bit --\n");
    {
        BrakeControl bc;
        bc.init();
        bootstrap_active(bc);
        VcuSebReq out;

        // AUTO + CAN pressure → auto_brake = 1
        bc.tick(false, false, 4000, Mode::Auto, seb_ok[0], seb_stroke(seb_ok), out);
        CHECK(out.auto_brake == 1);

        // MANUAL + lever → auto_brake = 0
        bc.tick(true, false, 0, Mode::Manual, seb_ok[0], seb_stroke(seb_ok), out);
        CHECK(out.auto_brake == 0);

        // AUTO + lever override → auto_brake = 0
        bc.tick(true, false, 2000, Mode::Auto, seb_ok[0], seb_stroke(seb_ok), out);
        CHECK(out.auto_brake == 0);

        // ESTOP → auto_brake = 0
        bc.tick(false, true, 0, Mode::Estop, seb_ok[0], seb_stroke(seb_ok), out);
        CHECK(out.auto_brake == 0);
    }

    // ── kPa → SEB raw conversion ───────────────────────────────────────
    printf("-- kPa -> SEB pressure raw --\n");
    {
        BrakeControl bc;
        bc.init();
        bootstrap_active(bc);
        VcuSebReq out;

        // 5000 kPa = 5 MPa → raw = 5000*0.02 = 100
        bc.tick(false, false, 5000, Mode::Auto, seb_ok[0], seb_stroke(seb_ok), out);
        CHECK(out.pressure_req == 100);

        // 2500 kPa → (2500+25)/50 = 50
        bc.tick(false, false, 2500, Mode::Auto, seb_ok[0], seb_stroke(seb_ok), out);
        CHECK(out.pressure_req == 50);

        // 100 kPa → (100+25)/50 = 2
        bc.tick(false, false, 100, Mode::Auto, seb_ok[0], seb_stroke(seb_ok), out);
        CHECK(out.pressure_req == 2);

        // 25000 kPa absurd → clamped to 100
        bc.tick(false, false, 25000, Mode::Auto, seb_ok[0], seb_stroke(seb_ok), out);
        CHECK(out.pressure_req == 100);
    }

    // ── Rolling counter ─────────────────────────────────────────────────
    printf("-- Rolling counter --\n");
    {
        BrakeControl bc;
        bc.init();
        bootstrap_active(bc);
        VcuSebReq out;

        bc.tick(false, false, 0, Mode::Manual, seb_ok[0], seb_stroke(seb_ok), out);
        uint8_t prev = out.rolling_counter;
        for (int i = 0; i < 20; ++i) {
            bc.tick(false, false, 0, Mode::Manual, seb_ok[0], seb_stroke(seb_ok), out);
            CHECK(out.rolling_counter == ((prev + 1) & 0x0F));
            prev = out.rolling_counter;
        }
    }

    printf("\n=== Results: %d passed, %d failed ===\n", pass, fail);
    return fail ? 1 : 0;
}
