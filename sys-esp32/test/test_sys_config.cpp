// Phase 3: sys-esp32/src/config.h validation
// g++ -std=c++17 -I../src -I../../shared test_sys_config.cpp -o test_sys_config && ./test_sys_config

#include <cstdio>
#include "config.h"

static int fails = 0;
#define CHECK(d) printf("  %-48s ", d)
#define OK       printf("PASS\n")
#define BAD(m)   do { printf("FAIL: %s\n", m); ++fails; } while(0)

int main() {
    printf("Phase 3: sys-esp32/src/config.h\n\n");

    printf("== GPIO uniqueness (no pin conflicts on SYS ESP32) ==\n");
    int gpios[] = {
        sys::kCanTxGpio, sys::kCanRxGpio,
        sys::kEstopGpio, sys::kBrakeLeverGpio, sys::kStartBtnGpio, sys::kModeBtnGpio,
        sys::kThrottleI2cSda, sys::kThrottleI2cScl,
        sys::kGearDSense, sys::kGearSSense, sys::kGearRSense,
        sys::kGearDOut, sys::kGearSOut, sys::kGearROut,
        sys::kSwitchLeftTurn, sys::kSwitchRightTurn, sys::kSwitchHeadlight,
        sys::kLightLeftTurn, sys::kLightRightTurn, sys::kLightBrake, sys::kLightHead,
        sys::kBulbAuto, sys::kBulbManual, sys::kPower12vRelay,
        sys::kWdtToggleGpio,
    };
    int n = sizeof(gpios)/sizeof(gpios[0]);
    for (int i=0;i<n;++i) for (int j=i+1;j<n;++j) if (gpios[i]==gpios[j]) BAD("dup GPIO");
    CHECK("26 GPIOs unique on SYS ESP32"); if (!fails) OK;

    printf("\n== Constant sanity ==\n");
    CHECK("CAN bitrate 500k");       if (sys::kCanBitrateHz==500000) OK; else BAD("bitrate");
    CHECK("throttle dead zone >0");  if (sys::kThrottleDeadZone>0) OK; else BAD("deadzone");
    CHECK("throttle DAC max 4095");  if (sys::kThrottleDacMaxVal==4095) OK; else BAD("dac");
    CHECK("max speed 3000");         if (sys::kThrottleMaxSpeedMmps==3000) OK; else BAD("speed");
    CHECK("debounce 500ms");         if (sys::kDebounceMs==500) OK; else BAD("debounce");
    CHECK("HB interval 500ms");      if (sys::kHeartbeatIntervalMs==500) OK; else BAD("hb interval");
    CHECK("HB timeout 1000ms");      if (sys::kHeartbeatTimeoutMs==1000) OK; else BAD("hb timeout");
    CHECK("startup grace 3000ms");   if (sys::kStartupGracePeriodMs==3000) OK; else BAD("grace");
    CHECK("brake cmd 50Hz");         if (sys::kBrakeCmdRateHz==50) OK; else BAD("brake rate");
    CHECK("brake boot wait 500ms");  if (sys::kBrakeBootWaitMs==500) OK; else BAD("brake boot");
    CHECK("manual stroke < max");    if (sys::kBrakeManualStroke < sys::kBrakeMaxStroke) OK; else BAD("brake stroke");
    CHECK("turn blink 500/500");     if (sys::kTurnBlinkOnMs==500 && sys::kTurnBlinkOffMs==500) OK; else BAD("blink");
    CHECK("safety check 20Hz");      if (sys::kSafetyCheckHz==20) OK; else BAD("safety");
    CHECK("gear check 50Hz");        if (sys::kGearCheckHz==50) OK; else BAD("gear");

    printf("\n== I2C address ==\n");
    CHECK("MCP4725 addr 0x60");      if (sys::kThrottleDacI2cAddr==0x60) OK; else BAD("i2c");

    printf("\n  Result: %d failures\n", fails);
    return fails ? 1 : 0;
}
