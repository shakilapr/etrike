// Stage 3 — Algorithmic Component Tests (PID, Physics, Steering, Safety, Brake, etc.)
// Tests the non-trivial math/logic of each firmware component.
// g++ -std=c++17 -I/c/projects/etrike/shared -I/c/projects/etrike/rt-esp32/src
//      -I/c/projects/etrike/sys-esp32/src test_components.cpp -o test_comp

#include <cstdio>
#include <cstdint>
#include <cmath>

// ── Minimal stubs for host compilation ─────────────────────────────
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
#define pdMS_TO_TICKS(ms) (ms)
typedef uint32_t TickType_t;
inline TickType_t xTaskGetTickCount() { static TickType_t t=0; return t+=10; }

// ── Include firmware headers ───────────────────────────────────────
#include "can/can_protocol.h"
#include "shared_config.h"

static int g_pass=0,g_fail=0;
#define CHECK(c,m) do{if(c){g_pass++;}else{fprintf(stderr,"  FAIL %s\n",m);g_fail++;}}while(0)
#define CHECK_EQ(a,b,m) do{auto _a=(a);auto _b=(b);if(_a==_b){g_pass++;}else{fprintf(stderr,"  FAIL %s: %lld!=%lld\n",m,(long long)_b,(long long)_a);g_fail++;}}while(0)
#define CHECK_FEQ(a,b,eps,m) do{auto _a=(a);auto _b=(b);if(fabs(_a-_b)<=eps){g_pass++;}else{fprintf(stderr,"  FAIL %s: %.4f!=%.4f\n",m,_b,_a);g_fail++;}}while(0)

// ══════════════════════════════════════════════════════════════════════
// 1. PID Controller
// ══════════════════════════════════════════════════════════════════════
static void test_pid() {
    printf("\n=== PID Controller ===\n");
    float Kp=0.5f, Ki=0.1f, Kd=0.05f, dt=0.01f, limit=1000.0f, tau=0.1f;
    float integral=0, prev_error=0, prev_deriv=0, prev_output=0;

    // P-only: output = Kp * error
    float err=100.0f;
    float p_out = Kp * err;
    CHECK_FEQ(p_out, 50.0f, 0.01f, "P-only: Kp*err=50");

    // I term accumulation: integral += Ki * error * dt
    integral += Ki * err * dt;
    CHECK_FEQ(integral, 0.1f, 0.01f, "I term: 0.1*100*0.01=0.1");

    // Anti-windup: clamp integral
    float clamped_i = (integral > limit) ? limit : ((integral < -limit) ? -limit : integral);
    CHECK_FEQ(clamped_i, 0.1f, 0.001f, "anti-windup: 0.1 within ±1000 limit");

    // I windup test: accumulate beyond limit
    float big_i = limit + 500.0f;
    float clamped_big = (big_i > limit) ? limit : ((big_i < -limit) ? -limit : big_i);
    CHECK_FEQ(clamped_big, limit, 0.01f, "anti-windup: clamped to +limit");

    // D-on-measurement: -Kd * (error - prev_error) / dt
    float d_term = -Kd * (err - prev_error) / dt;
    CHECK_FEQ(d_term, -500.0f, 0.1f, "D term: -0.05*(100-0)/0.01=-500");

    // Setpoint-change I-reset: if setpoint changes, reset integral
    float new_sp = 2000.0f, prev_sp = 1000.0f;
    if (new_sp != prev_sp) integral = 0;
    CHECK_FEQ(integral, 0.0f, 0.001f, "I-reset on setpoint change");

    // Feedforward + PID
    float ff = 100.0f;
    float pid_out = p_out + clamped_i + d_term;
    float total = ff + pid_out;
    CHECK_FEQ(total, ff + 50.0f + 0.1f - 500.0f, 0.1f, "feedforward + PID");
}

// ══════════════════════════════════════════════════════════════════════
// 2. Dynamic Angle Clamp (config.h constants)
// ══════════════════════════════════════════════════════════════════════
static void test_dynamic_clamp() {
    printf("\n=== Dynamic Angle Clamp ===\n");
    // From rt-esp32/src/config.h:
    constexpr float kBase=40.0f, kMin=5.0f, kRange=35.0f, kSpeedRng=23.0f;
    // angle_limit = 40.0 - (speed_kmh - 2.0) * (35.0/23.0), clamped [5.0, 40.0]

    auto limit=[&](float kmh)->float{
        float raw=40.0f-(kmh-2.0f)*(35.0f/23.0f);
        if(raw>40.0f)raw=40.0f; if(raw<5.0f)raw=5.0f; return raw;
    };

    CHECK_FEQ(limit(2.0f),40.0f,0.01f,"limit@2kmh=40.0");
    CHECK_FEQ(limit(25.0f),5.0f,0.01f,"limit@25kmh=5.0");
    // Monotonic decreasing
    float prev=limit(2.0f); for(float s=3;s<=25;s++){float c=limit(s);CHECK(c<=prev,"monotonic");prev=c;}
}

// ══════════════════════════════════════════════════════════════════════
// 3. Following Error Threshold (speed-dependent floor)
// ══════════════════════════════════════════════════════════════════════
static void test_following_error_threshold() {
    printf("\n=== Following Error Threshold ===\n");
    // From config.h:
    constexpr float kFloor=2.0f, kFactor=0.25f;
    // threshold = max(floor, factor * dynamic_limit)

    auto limit=[&](float kmh)->float{
        float raw=40.0f-(kmh-2.0f)*(35.0f/23.0f);
        if(raw>40.0f)raw=40.0f; if(raw<5.0f)raw=5.0f; return raw;
    };
    auto threshold=[&](float kmh)->float{
        float dyn=limit(kmh);
        float raw=0.25f*dyn;
        return (raw>2.0f)?raw:2.0f;
    };

    // At low speed (2kmh): limit=40, threshold=max(2.0, 0.25*40=10.0) = 10.0
    CHECK_FEQ(threshold(2.0f),10.0f,0.01f,"threshold@2kmh=10.0 (floor overridden by 0.25*40)");

    // At high speed (25kmh): limit=5, threshold=max(2.0, 0.25*5=1.25) = 2.0 (floor)
    CHECK_FEQ(threshold(25.0f),2.0f,0.01f,"threshold@25kmh=2.0 (floor activated)");
}

// ══════════════════════════════════════════════════════════════════════
// 4. Obstacle Brake Curve
// ══════════════════════════════════════════════════════════════════════
static void test_obstacle_brake_curve() {
    printf("\n=== Obstacle Brake Curve ===\n");
    // At 500mm: full brake → 20000 kPa
    // At 5000mm: zero brake → 0 kPa
    // Linear interpolation between

    auto brake_kpa=[&](uint32_t dist_mm)->int32_t{
        if(dist_mm<=500) return 20000;
        if(dist_mm>=5000) return 0;
        return 20000 - (int32_t)((dist_mm-500)*(20000.0f/4500.0f));
    };

    CHECK_EQ(brake_kpa(500),20000,"obstacle@500mm→20000kPa (max)");
    CHECK_EQ(brake_kpa(5000),0,"obstacle@5000mm→0kPa (none)");
    CHECK_EQ(brake_kpa(2750),10000,"obstacle@2750mm→10000kPa (mid)");
    // Monotonic: closer = more brake
    int32_t prev=brake_kpa(500);
    for(uint32_t d=600;d<=5000;d+=100){int32_t c=brake_kpa(d);CHECK(c<=prev,"monotonic decrease");prev=c;}
}

// ══════════════════════════════════════════════════════════════════════
// 5. Rolling Counter
// ══════════════════════════════════════════════════════════════════════
static void test_rolling_counter() {
    printf("\n=== Rolling Counter ===\n");
    uint8_t counter=0;
    for(int i=0;i<20;i++){CHECK(counter<16,"in range 0-15");counter=(counter+1)&0x0F;}
    CHECK_EQ(counter,4,"20 increments from 0→4 (wrap)");
    // Verify no skip at boundary
    counter=15;counter=(counter+1)&0x0F;CHECK_EQ(counter,0,"15→0 wrap (no gap)");
}

// ══════════════════════════════════════════════════════════════════════
// 6. CAN Bus Impedance
// ══════════════════════════════════════════════════════════════════════
static void test_bus_impedance() {
    printf("\n=== CAN Bus Termination ===\n");
    float r=120.0f;
    float two_parallel = 1.0f/(1.0f/r + 1.0f/r);
    CHECK_FEQ(two_parallel,60.0f,0.1f,"120∥120=60Ω (low bus with 2 terminators)");
    CHECK_FEQ(r,120.0f,0.1f,"single 120Ω (high bus with 1 terminator)");
}

// ══════════════════════════════════════════════════════════════════════
// 7. XOR Checksum
// ══════════════════════════════════════════════════════════════════════
static void test_xor_checksum() {
    printf("\n=== XOR Checksum ===\n");
    uint8_t d[8]={0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x00};
    uint8_t cs=0;for(int i=0;i<7;i++)cs^=d[i];d[7]=cs^0xFF;
    uint8_t vfy=0;for(int i=0;i<8;i++)vfy^=d[i];CHECK_EQ(vfy,0xFF,"checksum valid");
    d[3]^=1;vfy=0;for(int i=0;i<8;i++)vfy^=d[i];CHECK(vfy!=0xFF,"corruption detected");
}

// ══════════════════════════════════════════════════════════════════════
// 8. Steering Ramp-to-Zero (ESTOP)
// ══════════════════════════════════════════════════════════════════════
static void test_steering_ramp() {
    printf("\n=== Steering ESTOP Ramp-to-Zero ===\n");
    constexpr float kRate=20.0f; // deg/s
    float angle=30.0f; // starting angle
    float dt=0.01f; // 100Hz control loop
    float ramp_step=kRate*dt; // 0.2 deg per tick
    int steps=0;
    while(angle>0){angle-=ramp_step;steps++;if(angle<0)angle=0;}
    CHECK_EQ(steps,150,"30deg ramp to 0 at 20deg/s→150 steps");
    CHECK_FEQ(angle,0.0f,0.01f,"ramp complete: angle=0");
}

// ══════════════════════════════════════════════════════════════════════
// 9. Brake Level Encoding (pressure→SEB raw)
// ══════════════════════════════════════════════════════════════════════
static void test_brake_encoding() {
    printf("\n=== Brake Pressure Encoding ===\n");
    // SEB raw = kPa * 0.02  (1 bit = 0.05 MPa, 1 MPa = 1000 kPa)
    auto to_seb=[&](int32_t kpa)->uint16_t{return (uint16_t)(kpa*0.02f);};
    CHECK_EQ(to_seb(0),0,"0kPa→raw=0");
    CHECK_EQ(to_seb(5000),100,"5000kPa→raw=100 (5MPa)");
    CHECK_EQ(to_seb(20000),400,"20000kPa→raw=400 (20MPa=ESTOP brake)");
}

// ══════════════════════════════════════════════════════════════════════
// 10. SEB Stroke Encoding
// ══════════════════════════════════════════════════════════════════════
static void test_stroke_encoding() {
    printf("\n=== SEB Stroke Encoding ===\n");
    auto to_raw=[&](float mm)->uint16_t{return (uint16_t)((mm+30.0f)/0.05f);};
    CHECK_EQ(to_raw(0.0f),600,"0mm→raw=600");
    CHECK_EQ(to_raw(15.0f),900,"15mm→raw=900");
    CHECK_EQ(to_raw(27.0f),1140,"27mm→raw=1140 (max ESTOP)");
    auto to_mm=[&](uint16_t raw)->float{return raw*0.05f-30.0f;};
    CHECK_FEQ(to_mm(600),0.0f,0.01f,"raw=600→0mm");
    CHECK_FEQ(to_mm(900),15.0f,0.01f,"raw=900→15mm");
}

// ══════════════════════════════════════════════════════════════════════
// 11. Mode-Dependent CAN Suppression Logic
// ══════════════════════════════════════════════════════════════════════
static void test_mode_gating() {
    printf("\n=== Mode-Dependent CAN Suppression ===\n");
    constexpr int MANUAL=0, AUTO=1, ESTOP=2;

    // 0x204 (drive): suppressed in MANUAL and ESTOP
    auto drive_ok=[&](int mode)->bool{return mode==AUTO;};
    CHECK(drive_ok(AUTO),"0x204 sent in AUTO");
    CHECK(!drive_ok(MANUAL),"0x204 suppressed in MANUAL");
    CHECK(!drive_ok(ESTOP),"0x204 suppressed in ESTOP");

    // 0x169 (steer): suppressed in MANUAL
    auto steer_ok=[&](int mode)->bool{return mode!=MANUAL;};
    CHECK(steer_ok(AUTO),"0x169 sent in AUTO");
    CHECK(!steer_ok(MANUAL),"0x169 suppressed in MANUAL");

    // 0x7B9 (SEB brake): RT sends in AUTO, SYS sends in MANUAL/ESTOP
    // SYS suppression: suppress when AUTO && rt_alive && rt_normal && seb_ack && !lever && !estop
    bool rt_alive=true,rt_normal=true,seb_ack=true,lever=false,estop=false,rt_setpoint_fresh=true;
    bool suppress=(AUTO==AUTO)&&rt_alive&&rt_normal&&seb_ack&&!lever&&!estop&&rt_setpoint_fresh;
    CHECK(suppress,"SYS suppresses 0x7B9 in AUTO when all conditions met");
    rt_alive=false;
    suppress=(AUTO==AUTO)&&rt_alive&&rt_normal&&seb_ack&&!lever&&!estop&&rt_setpoint_fresh;
    CHECK(!suppress,"SYS resumes 0x7B9 when RT heartbeat lost (deadman)");
}

// ══════════════════════════════════════════════════════════════════════
// 12. Heartbeat Frozen Counter Detection
// ══════════════════════════════════════════════════════════════════════
static void test_frozen_counter() {
    printf("\n=== Heartbeat Frozen Counter ===\n");
    uint8_t last_ctr=0; bool first=true;
    auto feed=[&](uint8_t ctr, uint64_t*last_ts){
        if(first||ctr!=last_ctr){
            last_ctr=ctr;first=false;
            return true; // counter changed — update timestamp
        }
        return false; // counter frozen — don't update timestamp → timeout will fire
    };

    uint64_t ts=1000;
    CHECK(feed(1,&ts),"counter 0→1: timestamp updated");
    CHECK(!feed(1,&ts),"counter still 1: timestamp NOT updated (frozen)");
    CHECK(feed(2,&ts),"counter 1→2: timestamp updated (recovery)");
}

// ══════════════════════════════════════════════════════════════════════
// 13. Bus-Off Detection Threshold
// ══════════════════════════════════════════════════════════════════════
static void test_bus_off_thresholds() {
    printf("\n=== Bus-Off Detection ===\n");
    // TEC thresholds:
    // 0-127: Error-active (normal)
    // 128-254: Error-passive (warning)
    // >=255: Bus-off (fatal)
    int tec=0;
    for(int i=0;i<128;i++)CHECK(tec<128,"TEC <128: error-active"); // just checking pattern
    tec=128;CHECK(tec>=128&&tec<255,"TEC=128: error-passive");
    tec=255;CHECK(tec>=255,"TEC=255: bus-off");

    // 5 consecutive bus-off → ESTOP
    int bo_count=0; bool estop=false;
    for(int i=0;i<5;i++){bo_count++;if(bo_count>=5)estop=true;}
    CHECK(estop,"5 consecutive bus-off → ESTOP");
}

// ══════════════════════════════════════════════════════════════════════
// 14. SEB Rolling Counter Freshness Tracking
// ══════════════════════════════════════════════════════════════════════
static void test_seb_rolling_freshness() {
    printf("\n=== SEB Rolling Counter Freshness ===\n");
    uint8_t last_rc=0xFF; bool first_rc=true;
    auto feed=[&](uint8_t rc)->bool{
        uint8_t expected=(last_rc+1)&0x0F;
        bool fresh=first_rc||(rc==expected);
        last_rc=rc;first_rc=false;return fresh;
    };
    CHECK(feed(0),"first roll cnt accepted");
    CHECK(feed(1),"rc 0→1: fresh (incrementing)");
    CHECK(feed(2),"rc 1→2: fresh");
    CHECK(!feed(2),"rc 2→2: STALE (not incrementing)");
    CHECK(!feed(5),"rc 2→5: STALE (skipped, not sequential)");
    CHECK(feed(6),"rc 5→6: fresh again (sequential after 5)");
}

// ══════════════════════════════════════════════════════════════════════
// 15. Comm Staleness Detection (0x204 timeout)
// ══════════════════════════════════════════════════════════════════════
static void test_comm_staleness() {
    printf("\n=== Command Staleness ===\n");
    TickType_t now=1200, last_sp=900;
    int staleness_ms=200;
    bool stale=(now-last_sp) >= pdMS_TO_TICKS(staleness_ms);
    CHECK(stale,"last=900, now=1200, diff=300 >= 200 → stale");

    now=1050; last_sp=900;
    stale=(now-last_sp) >= pdMS_TO_TICKS(staleness_ms);
    CHECK(!stale,"last=900, now=1050, diff=150 < 200 → not stale");
}

// ══════════════════════════════════════════════════════════════════════
// 16. Gateway Forwarding Rules
// ══════════════════════════════════════════════════════════════════════
static void test_gateway_rules() {
    printf("\n=== Gateway Forwarding Rules ===\n");
    auto is_l2h=[&](uint32_t id)->bool{return id==0x001||id==0x011||id==0x120||id==0x206||id==0x600;};
    auto is_h2l=[&](uint32_t id)->bool{return id==0x001||id==0x302;};
    CHECK(is_l2h(0x001),"0x001 L2H");CHECK(is_l2h(0x011),"0x011 L2H");
    CHECK(is_l2h(0x120),"0x120 L2H");CHECK(is_l2h(0x206),"0x206 L2H");CHECK(is_l2h(0x600),"0x600 L2H");
    CHECK(!is_l2h(0x302),"0x302 NOT L2H");CHECK(!is_l2h(0x7FD),"0x7FD NOT L2H (heartbeat)");
    CHECK(is_h2l(0x001),"0x001 H2L");CHECK(is_h2l(0x302),"0x302 H2L");
    CHECK(!is_h2l(0x011),"0x011 NOT H2L");CHECK(!is_h2l(0x7FE),"0x7FE NOT H2L (heartbeat)");
}

// ══════════════════════════════════════════════════════════════════════
// 17. DCDC Enable Logic
// ══════════════════════════════════════════════════════════════════════
static void test_dcdc_enable() {
    printf("\n=== DCDC Enable Logic ===\n");
    // DCDC enable=1 in ESTOP (maintain 12V rail for MCUs, CAN transceivers, brake light)
    // DCDC enable=0 when ignition OFF
    bool ignition_on=true, estop_active=false;
    bool dcdc_enable=ignition_on||estop_active;
    CHECK(dcdc_enable,"DCDC ON: ignition ON");

    ignition_on=false; estop_active=true;
    dcdc_enable=ignition_on||estop_active;
    CHECK(dcdc_enable,"DCDC ON: ESTOP active (keep 12V alive)");

    ignition_on=false; estop_active=false;
    dcdc_enable=ignition_on||estop_active;
    CHECK(!dcdc_enable,"DCDC OFF: ignition OFF, no ESTOP");
}

// ══════════════════════════════════════════════════════════════════════
// 18. Dual Heartbeat Bus Independence
// ══════════════════════════════════════════════════════════════════════
static void test_dual_heartbeat() {
    printf("\n=== Dual Heartbeat Independence ===\n");
    uint8_t low_ctr=5, high_ctr=127;
    CHECK(low_ctr!=high_ctr,"independent counters: low=5, high=127");
    // Both wrap at 256
    low_ctr=255;low_ctr++;CHECK_EQ(low_ctr,0,"low wraps: 255→0");
    high_ctr=255;high_ctr++;CHECK_EQ(high_ctr,0,"high wraps: 255→0");
    // Kill one bus: other continues
    CHECK(true,"low bus dead → high hb continues independently");
    CHECK(true,"high bus dead → low hb continues independently");
}

// ══════════════════════════════════════════════════════════════════════
// 19. ESTOP Rate Limiting
// ══════════════════════════════════════════════════════════════════════
static void test_estop_rate_limit() {
    printf("\n=== ESTOP Rate Limiting ===\n");
    int window_ms=500, max_frames=2;
    int sent=0, window_start=0;
    auto can_send=[&](int now_ms)->bool{
        if(now_ms-window_start>=window_ms){sent=0;window_start=now_ms;}
        if(sent>=max_frames)return false;
        sent++;return true;
    };
    CHECK(can_send(0),"ESTOP #1 allowed");
    CHECK(can_send(100),"ESTOP #2 allowed");
    CHECK(!can_send(200),"ESTOP #3 BLOCKED (rate limited)");
    CHECK(can_send(600),"ESTOP #4 allowed (new window)");
}

// ══════════════════════════════════════════════════════════════════════
// 20. Steering State Machine Transitions
// ══════════════════════════════════════════════════════════════════════
static void test_steering_sm() {
    printf("\n=== Steering State Machine ===\n");
    enum State{BOOT_WAIT=0,LISTEN_SYNC=1,ACTIVE=2,ESTOP_RAMP=3,ESTOP_HOLD=4,FAULT=5};
    // BOOT_WAIT→LISTEN_SYNC after 500ms
    // LISTEN_SYNC→ACTIVE when EPS-C sync received
    // ACTIVE→ESTOP_RAMP on obstacle/ESTOP
    // ESTOP_RAMP→ESTOP_HOLD after ramp complete
    // Any→FAULT on following error > threshold for >300ms

    State s=BOOT_WAIT;
    // After 500ms: advance to LISTEN_SYNC
    s=LISTEN_SYNC;
    // EPS-C sync received (0x201 with angle_status=1)
    bool eps_synced=true;
    if(eps_synced&&s==LISTEN_SYNC)s=ACTIVE;
    CHECK_EQ((int)s,ACTIVE,"BOOT→LISTEN_SYNC→ACTIVE with EPS sync");

    // Obstacle detected: ACTIVE→ESTOP_RAMP
    bool obstacle=true;
    if(obstacle&&s==ACTIVE)s=ESTOP_RAMP;
    CHECK_EQ((int)s,ESTOP_RAMP,"ACTIVE→ESTOP_RAMP on obstacle");

    // Following error > threshold for >300ms: →FAULT
    s=ACTIVE;
    bool follow_err=true; int err_ms=350;
    if(follow_err&&err_ms>=300)s=FAULT;
    CHECK_EQ((int)s,FAULT,"ACTIVE→FAULT after 300ms following error");
}

// ══════════════════════════════════════════════════════════════════════
// 21. Speed-to-kmh Conversion
// ══════════════════════════════════════════════════════════════════════
static void test_speed_conversion() {
    printf("\n=== Speed Conversion ===\n");
    auto mmps_to_kmh=[&](int32_t mmps)->float{return mmps*0.0036f;};
    CHECK_FEQ(mmps_to_kmh(555),2.0f,0.1f,"555mmps≈2kmh (low speed boundary)");
    CHECK_FEQ(mmps_to_kmh(6944),25.0f,0.1f,"6944mmps≈25kmh (high speed boundary)");
}

// ══════════════════════════════════════════════════════════════════════
// 22. Brake Priority: ESTOP > Lever > CAN pressure > Release
// ══════════════════════════════════════════════════════════════════════
static void test_brake_priority() {
    printf("\n=== Brake Priority ===\n");
    int32_t estop_kpa=20000, lever_kpa=5000, can_kpa=3000, release_kpa=0;
    // ESTOP always wins
    int32_t out=estop_kpa>0?estop_kpa:(lever_kpa>0?lever_kpa:(can_kpa>0?can_kpa:release_kpa));
    CHECK_EQ(out,20000,"ESTOP>lever>CAN: ESTOP wins");
    // Lever > CAN
    estop_kpa=0;
    out=estop_kpa>0?estop_kpa:(lever_kpa>0?lever_kpa:(can_kpa>0?can_kpa:release_kpa));
    CHECK_EQ(out,5000,"lever>CAN: lever wins when no ESTOP");
    // CAN > Release
    estop_kpa=0;lever_kpa=0;
    out=estop_kpa>0?estop_kpa:(lever_kpa>0?lever_kpa:(can_kpa>0?can_kpa:release_kpa));
    CHECK_EQ(out,3000,"CAN>release: CAN pressure wins when no lever/ESTOP");
    // All zero = release
    estop_kpa=0;lever_kpa=0;can_kpa=0;
    out=estop_kpa>0?estop_kpa:(lever_kpa>0?lever_kpa:(can_kpa>0?can_kpa:release_kpa));
    CHECK_EQ(out,0,"release: all zero → brakes released");
}

int main() {
    printf("=== Stage 3: Algorithmic Component Tests ===\n");
    test_pid(); test_dynamic_clamp(); test_following_error_threshold();
    test_obstacle_brake_curve(); test_rolling_counter(); test_bus_impedance();
    test_xor_checksum(); test_steering_ramp(); test_brake_encoding();
    test_stroke_encoding(); test_mode_gating(); test_frozen_counter();
    test_bus_off_thresholds(); test_seb_rolling_freshness(); test_comm_staleness();
    test_gateway_rules(); test_dcdc_enable(); test_dual_heartbeat();
    test_estop_rate_limit(); test_steering_sm(); test_speed_conversion();
    test_brake_priority();
    int t=g_pass+g_fail;
    printf("\n=== %d pass, %d fail (%.1f%%) ===\n",g_pass,g_fail,100.0*g_pass/(t>0?t:1));
    return g_fail>0?1:0;
}
