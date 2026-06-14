// Phase 1: shared/can/can_protocol.h validation
// g++ -std=c++17 -I../../shared test_can_protocol.cpp -o test_can && ./test_can

#include <cstdio>
#include <cstring>
#include <cassert>
#include "can/can_protocol.h"

static int fails = 0;

#define CHECK(desc) printf("  %-48s ", desc)
#define OK          printf("PASS\n")
#define BAD(m)      do { printf("FAIL: %s\n", m); ++fails; } while(0)

static void hdr(const char* s) { printf("\n== %s ==\n", s); }

// ── ID uniqueness ──────────────────────────────────────────────────

static void test_id_uniqueness() {
    hdr("ID uniqueness");
    uint32_t low[] = {
        can::kIdSafetyEstop, can::kIdSysSafetySts, can::kIdSysDcdcCmd,
        can::kIdSysModeCmd, can::kIdSysThrottleSts, can::kIdVcuSesReq,
        can::kIdSesStatus, can::kIdRtDriveCmd, can::kIdRtBrakeCmd,
        can::kIdHostLightCmd, can::kIdSysDiagRpt, can::kIdVcuSebReq,
        can::kIdSebStatus, can::kIdRtHeartbeatLow, can::kIdSysHeartbeat
    };
    int nl = sizeof(low)/sizeof(low[0]);
    for (int i=0;i<nl;++i) for (int j=i+1;j<nl;++j) if (low[i]==low[j]) BAD("dup low");
    CHECK("15 low-level CAN IDs unique"); if (!fails) OK;

    uint32_t high[] = {
        can::kIdSafetyEstop, can::kIdSysSafetySts, can::kIdSysThrottleSts,
        can::kIdRtStateRpt, can::kIdRtPidRpt, can::kIdHostDriveCmd,
        can::kIdHostBrakeReq, can::kIdHostLightCmd, can::kIdRtObstacleRpt,
        can::kIdSysDiagRpt, can::kIdRtHeartbeatHigh, can::kIdJetsonHeartbeat
    };
    int nh = sizeof(high)/sizeof(high[0]);
    for (int i=0;i<nh;++i) for (int j=i+1;j<nh;++j) if (high[i]==high[j]) BAD("dup high");
    CHECK("12 high-level CAN IDs unique"); if (!fails) OK;
}

// ── Forwarding helpers ─────────────────────────────────────────────

static void test_forwarding() {
    hdr("Forwarding helpers");
    CHECK("is_estop_id(0x001) true");
    assert(can::is_estop_id(0x001)); OK;

    CHECK("low->high: 0x001,0x011,0x120,0x600 true");
    assert(can::is_forwarded_low_to_high(0x001)); assert(can::is_forwarded_low_to_high(0x011));
    assert(can::is_forwarded_low_to_high(0x120)); assert(can::is_forwarded_low_to_high(0x600)); OK;

    CHECK("low->high: 0x200,0x202,0x203 false");
    assert(!can::is_forwarded_low_to_high(0x200));
    assert(!can::is_forwarded_low_to_high(0x202));
    assert(!can::is_forwarded_low_to_high(0x203)); OK;

    CHECK("high->low: 0x001,0x302 true");
    assert(can::is_forwarded_high_to_low(0x001)); assert(can::is_forwarded_high_to_low(0x302)); OK;

    CHECK("high->low: 0x300 false (consumed)");
    assert(!can::is_forwarded_high_to_low(0x300)); OK;
}

// ── Round-trip: struct → frame → struct → frame, compare ──────────

template<typename P>
static void rt(const char* name, P orig, void (P::*to)(can::Frame&) const,
               P (*from)(const can::Frame&)) {
    CHECK(name);
    can::Frame f1; (orig.*to)(f1);
    P back = from(f1);
    can::Frame f2; (back.*to)(f2);
    if (memcmp(f1.data, f2.data, f1.dlc) || f1.id != f2.id || f1.dlc != f2.dlc) BAD("mismatch"); else OK;
}

static void test_roundtrips() {
    hdr("Round-trip: struct -> frame -> struct -> frame");
    rt("SysSafetySts {estop, hb_ok}", can::SysSafetySts{true,false},
       &can::SysSafetySts::to_frame, can::SysSafetySts::from_frame);
    rt("SysModeCmd {Auto}", can::SysModeCmd{1},
       &can::SysModeCmd::to_frame, can::SysModeCmd::from_frame);
    rt("SysThrottleSts {1500}", can::SysThrottleSts{1500},
       &can::SysThrottleSts::to_frame, can::SysThrottleSts::from_frame);
    rt("RtDriveCmd {2000,D}", can::RtDriveCmd{2000, uint8_t(can::Gear::D)},
       &can::RtDriveCmd::to_frame, can::RtDriveCmd::from_frame);
    rt("RtBrakeCmd {5000 kPa}", can::RtBrakeCmd{5000},
       &can::RtBrakeCmd::to_frame, can::RtBrakeCmd::from_frame);
    rt("HostLightCmd {all}", can::HostLightCmd{true,true,false,true},
       &can::HostLightCmd::to_frame, can::HostLightCmd::from_frame);
    rt("HostDriveCmd {2500,-500}", can::HostDriveCmd{2500,-500},
       &can::HostDriveCmd::to_frame, can::HostDriveCmd::from_frame);
    rt("HostBrakeReq {8000}", can::HostBrakeReq{8000},
       &can::HostBrakeReq::to_frame, can::HostBrakeReq::from_frame);
    rt("RtObstacleRpt {1500}", can::RtObstacleRpt{1500},
       &can::RtObstacleRpt::to_frame, can::RtObstacleRpt::from_frame);
    rt("RtDriveCmd {-300}", can::RtDriveCmd{-300,0},
       &can::RtDriveCmd::to_frame, can::RtDriveCmd::from_frame);
}

// ── DLC checks ─────────────────────────────────────────────────────

static void test_dlc() {
    hdr("DLC correctness");
    can::Frame f;
    can::SysSafetySts{}.to_frame(f);   CHECK("SysSafetySts DLC=2");  if (f.dlc==2) OK; else BAD("dlc");
    can::SysModeCmd{}.to_frame(f);     CHECK("SysModeCmd DLC=1");    if (f.dlc==1) OK; else BAD("dlc");
    can::SysThrottleSts{}.to_frame(f); CHECK("SysThrottleSts DLC=2");if (f.dlc==2) OK; else BAD("dlc");
    can::RtDriveCmd{}.to_frame(f);     CHECK("RtDriveCmd DLC=5");    if (f.dlc==5) OK; else BAD("dlc");
    can::RtBrakeCmd{}.to_frame(f);     CHECK("RtBrakeCmd DLC=4");    if (f.dlc==4) OK; else BAD("dlc");
    can::HostLightCmd{}.to_frame(f);   CHECK("HostLightCmd DLC=1");  if (f.dlc==1) OK; else BAD("dlc");
    can::HostDriveCmd{}.to_frame(f);   CHECK("HostDriveCmd DLC=8");  if (f.dlc==8) OK; else BAD("dlc");
    can::HostBrakeReq{}.to_frame(f);   CHECK("HostBrakeReq DLC=4");  if (f.dlc==4) OK; else BAD("dlc");
    can::RtObstacleRpt{}.to_frame(f);  CHECK("RtObstacleRpt DLC=4"); if (f.dlc==4) OK; else BAD("dlc");
    can::RtStateRpt{}.to_frame(f);     CHECK("RtStateRpt DLC=3");    if (f.dlc==3) OK; else BAD("dlc");
    can::SysDiagRpt{}.to_frame(f);     CHECK("SysDiagRpt DLC=8");    if (f.dlc==8) OK; else BAD("dlc");
    can::VcuSesReq s; s.align_enable=s.control_enable=s.roll_cnt_enable=s.checksum_enable=1;
    s.control_mode=1; s.to_frame(f);
    CHECK("VcuSesReq DLC=8"); if (f.dlc==8) OK; else BAD("dlc");
    can::VcuSebReq b; b.control_mode=1; b.to_frame(f);
    CHECK("VcuSebReq DLC=8"); if (f.dlc==8) OK; else BAD("dlc");
}

// ── SYNTREE little-endian pack/unpack ──────────────────────────────

static void test_syntree() {
    hdr("SYNTREE little-endian pack/unpack");
    uint8_t r[8];

    CHECK("VcuSesReq round-trip (angle=455, speed=100, cnt=7)");
    can::VcuSesReq s; s.align_enable=1; s.control_enable=1; s.control_mode=1;
    s.target_angle=455; s.target_speed=100;
    s.roll_cnt_enable=1; s.checksum_enable=1; s.rolling_counter=7;
    s.pack(r); auto b = can::VcuSesReq::unpack(r);
    if (b.target_angle==455 && b.target_speed==100 && b.rolling_counter==7
        && b.control_mode==1 && b.align_enable==1 && b.control_enable==1) OK; else BAD("mismatch");

    CHECK("VcuSesReq checksum non-zero");
    if (r[7]!=0) OK; else BAD("cksum zero");

    CHECK("VcuSesReq zero angle (straight)");
    s.target_angle=0; s.pack(r); b=can::VcuSesReq::unpack(r);
    if (b.target_angle==0) OK; else BAD("zero angle lost");

    CHECK("VcuSebReq 0mm stroke raw=600");
    can::VcuSebReq sb; sb.control_mode=1; sb.stroke_req=600; sb.rolling_counter=3;
    sb.pack(r); auto bb=can::VcuSebReq::unpack(r);
    if (bb.stroke_req==600 && bb.rolling_counter==3) OK; else BAD("0mm mismatch");

    CHECK("VcuSebReq 15mm stroke raw=900");
    sb.stroke_req=900; sb.pack(r); bb=can::VcuSebReq::unpack(r);
    if (bb.stroke_req==900) OK; else BAD("15mm mismatch");

    CHECK("VcuSebReq 27mm (ESTOP max) raw=1140");
    sb.stroke_req=1140; sb.pack(r); bb=can::VcuSebReq::unpack(r);
    if (bb.stroke_req==1140) OK; else BAD("27mm mismatch");

    CHECK("VcuSebReq Pressure Mode");
    sb.control_mode=2; sb.pressure_req=50; sb.pack(r); bb=can::VcuSebReq::unpack(r);
    if (bb.control_mode==2 && bb.pressure_req==50) OK; else BAD("pressure mode");
}

// ── Priority ordering ──────────────────────────────────────────────

static void test_priority() {
    hdr("Priority ordering (lower ID = higher priority)");
    CHECK("0x001 < 0x011 < 0x110 < 0x120 < 0x202 < 0x600 < 0x7FD");
    bool ok = can::kIdSafetyEstop < can::kIdSysSafetySts
           && can::kIdSysSafetySts < can::kIdSysModeCmd
           && can::kIdSysModeCmd < can::kIdSysThrottleSts
           && can::kIdSysThrottleSts < can::kIdRtDriveCmd
           && can::kIdRtDriveCmd < can::kIdSysDiagRpt
           && can::kIdSysDiagRpt < can::kIdRtHeartbeatLow;
    if (ok) OK; else BAD("order broken");
}

// ───────────────────────────────────────────────────────────────────

int main() {
    printf("Phase 1: shared/can/can_protocol.h\n");

    test_id_uniqueness();
    test_forwarding();
    test_roundtrips();
    test_dlc();
    test_syntree();
    test_priority();

    printf("\n  Result: %d failures\n", fails);
    return fails ? 1 : 0;
}
