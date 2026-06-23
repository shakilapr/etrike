// Phase 2: rt-esp32/src/config.h validation
// g++ -std=c++17 -I../src -I../../shared test_rt_config.cpp -o test_rt_config && ./test_rt_config

#include <cstdio>
#include "config.h"
#include "can/can_protocol.h"

static int fails = 0;
#define CHECK(desc) printf("  %-48s ", desc)
#define OK          printf("PASS\n")
#define BAD(m)      do { printf("FAIL: %s\n", m); ++fails; } while(0)

static void test_gpio_uniqueness() {
    printf("== GPIO uniqueness (no pin conflicts on RT ESP32) ==\n");
    int gpios[] = {
        // CAN low
        rt::kCanLowTxGpio, rt::kCanLowRxGpio,
        // SPI (MCP2515 high CAN)
        rt::kSpiSckGpio, rt::kSpiMosiGpio, rt::kSpiMisoGpio,
        rt::kSpiCsGpio, rt::kMcpIntGpio,
        // Encoders
        rt::kEncRearMotorA, rt::kEncRearMotorB,
        rt::kEncFrontWheelA, rt::kEncFrontWheelB,
        rt::kEncRearLeftA, rt::kEncRearLeftB,
        rt::kEncRearRightA, rt::kEncRearRightB,
        // Sensors
        rt::kObstacleTrigGpio, rt::kObstacleEchoGpio,
        rt::kImuSdaGpio, rt::kImuSclGpio,
        // WDT
        rt::kWdtToggleGpio,
    };
    int n = sizeof(gpios)/sizeof(gpios[0]);
    for (int i=0;i<n;++i)
        for (int j=i+1;j<n;++j)
            if (gpios[i] == gpios[j]) BAD("duplicate GPIO");
    CHECK("all GPIOs unique on RT ESP32"); if (!fails) OK;
}

static void test_constants() {
    printf("\n== Constant sanity ==\n");
    CHECK("wheelbase > 0");           if (shared::kWheelbaseMM > 0) OK; else BAD("wheelbase");
    CHECK("max speed fwd > rev");     if (shared::kMaxSpeedFwdMmps > shared::kMaxSpeedRevMmps) OK; else BAD("speed");
    CHECK("obstacle stop < clear");   if (shared::kObstacleStopMM < shared::kObstacleClearMM) OK; else BAD("obstacle");
    CHECK("HB timeout Sys < Jetson"); if (rt::kHeartbeatTimeoutMsSys < shared::kHeartbeatTimeoutMsJetson) OK; else BAD("hb timeout");
    CHECK("control loop 100 Hz");     if (rt::kControlLoopHz == 100) OK; else BAD("loop rate");
    CHECK("CAN bitrate 500k");        if (rt::kCanLowBitrateHz == 500000 && rt::kCanHighBitrateHz == 500000) OK; else BAD("bitrate");
    CHECK("steer boot wait > 0");     if (rt::kSteerBootWaitMs > 0) OK; else BAD("boot wait");
    CHECK("following error > 0");     if (rt::kSteerFollowingErrDeg > 0) OK; else BAD("follow err");
}

static void test_can_id_prefixes() {
    printf("\n== CAN ID prefix sanity (using can:: namespace) ==\n");
    // Safety IDs (0x001) should be lowest
    CHECK("estop is lowest ID");
    bool ok = (can::kIdSafetyEstop < can::kIdSysSafetySts)
           && (can::kIdSysSafetySts < can::kIdSysModeCmd)
           && (can::kIdSyntreeEpsCmd < can::kIdSysDiagRpt);
    if (ok) OK; else BAD("priority order");
}

static void test_r2_new_constants() {
    printf("\n== R2: new constants ==\n");

    CHECK("PID gains defined and positive");
    if (rt::kPidKp > 0.0f && rt::kPidKi > 0.0f && rt::kPidKd > 0.0f) OK; else BAD("PID gains");

    if (rt::kSteerLimitDeg > 0.0f && rt::kSteerLimitDeg <= 78.0f) OK; else BAD("steer limit");


}

int main() {
    printf("Phase 2: rt-esp32/src/config.h\n\n");
    test_gpio_uniqueness();
    test_constants();
    test_can_id_prefixes();
    test_r2_new_constants();
    printf("\n  Result: %d failures\n", fails);
    return fails ? 1 : 0;
}
