// Phase R6: RT CAN gateway — forwarding categories test.
// Architecture.md §2.3: 3 categories of message handling.

#include <cstdio>
#include <cstring>
#include "can_rx_router.h"
#include "can/can_protocol.h"

static int fails = 0;
#define CHECK(desc) printf("  %-55s ", desc)
#define OK          printf("PASS\n")
#define BAD(m)      do { printf("FAIL: %s\n", m); ++fails; } while(0)
#define hdr(s)      printf("\n== %s ==\n", s)

static void test_forwarding_low_to_high() {
    hdr("Category 1: Transparent forward — Low→High");

    // 0x011 SYS_SAFETY_STS → forwarded to high
    {
        can::Frame f; f.id = 0x011; f.dlc = 2; f.put_u8(0, 0); f.put_u8(1, 1);
        can::Frame gw_high = {};
        rt::GatewayQueues q{}; q.gw_tx_high = &gw_high;
        rt::route_frame(f, false/*low bus*/, q);
        CHECK("0x011 on low → forwarded to high");
        if (gw_high.id == 0x011 && gw_high.dlc == 2) OK; else BAD("0x011 not forwarded");
    }

    // 0x120 SYS_THROTTLE_STS → forwarded to high
    {
        can::Frame f; f.id = 0x120; f.dlc = 2; f.put_i16(0, 1500);
        can::Frame gw_high = {};
        rt::GatewayQueues q{}; q.gw_tx_high = &gw_high;
        rt::route_frame(f, false, q);
        CHECK("0x120 on low → forwarded to high");
        if (gw_high.id == 0x120) OK; else BAD("0x120 not forwarded");
    }

    // 0x600 SYS_DIAG_RPT → forwarded to high
    {
        can::Frame f; f.id = 0x600; f.dlc = 8;
        can::Frame gw_high = {};
        rt::GatewayQueues q{}; q.gw_tx_high = &gw_high;
        rt::route_frame(f, false, q);
        CHECK("0x600 on low → forwarded to high");
        if (gw_high.id == 0x600) OK; else BAD("0x600 not forwarded");
    }
}

static void test_forwarding_high_to_low() {
    hdr("Category 1: Transparent forward — High→Low");

    // 0x302 HOST_LIGHT_CMD → forwarded to low
    {
        can::Frame f; f.id = 0x302; f.dlc = 1; f.put_u8(0, 0x0F);
        can::Frame gw_low = {};
        rt::GatewayQueues q{}; q.gw_tx_low = &gw_low;
        rt::route_frame(f, true/*high bus*/, q);
        CHECK("0x302 on high → forwarded to low");
        if (gw_low.id == 0x302) OK; else BAD("0x302 not forwarded");
    }

    // 0x300 is NOT forwarded high→low (consumed)
    {
        can::Frame f; f.id = 0x300; f.dlc = 8;
        can::Frame gw_low = {};
        rt::GatewayQueues q{}; q.gw_tx_low = &gw_low;
        rt::route_frame(f, true, q);
        CHECK("0x300 on high → NOT forwarded (consumed)");
        if (gw_low.id == 0) OK; else BAD("0x300 forwarded — should be consumed");
    }
}

static void test_estop_bidirectional() {
    hdr("Category 1: ESTOP bidirectional (0x001)");

    // 0x001 on low → estop flag set
    {
        bool got_estop = false;
        rt::GatewayQueues q{}; q.estop_flag = &got_estop;
        can::Frame f; f.id = 0x001;
        rt::route_frame(f, false, q);
        CHECK("0x001 on low → estop_flag=true");
        if (got_estop) OK; else BAD("estop not detected on low");
    }

    // 0x001 on high → estop flag set
    {
        bool got_estop = false;
        rt::GatewayQueues q{}; q.estop_flag = &got_estop;
        can::Frame f; f.id = 0x001;
        rt::route_frame(f, true, q);
        CHECK("0x001 on high → estop_flag=true");
        if (got_estop) OK; else BAD("estop not detected on high");
    }
}

static void test_category2_consume() {
    hdr("Category 2: Consume — 0x300, 0x301");

    // 0x300 on high → cmd parsed
    {
        can::HostDriveCmd cmd_storage;
        rt::GatewayQueues q{}; q.cmd = &cmd_storage;
        can::Frame f; f.id = 0x300; f.dlc = 8;
        f.put_i32(0, 2000);           // speed
        f.put_u8(4, 0); f.put_u8(5, 1); f.put_u8(6, 0x90);  // yaw=400
        f.put_u8(7, uint8_t(can::Gear::D));
        rt::route_frame(f, true, q);
        CHECK("0x300 on high → parsed into HostDriveCmd");
        if (cmd_storage.speed_mmps == 2000 && cmd_storage.yaw_rate_mrad_s == 400
            && cmd_storage.gear == uint8_t(can::Gear::D)) OK;
        else {
            printf("FAIL: sp=%d yaw=%d gear=%d\n",
                   cmd_storage.speed_mmps, cmd_storage.yaw_rate_mrad_s, cmd_storage.gear);
            BAD("0x300 parse");
        }
    }

    // 0x301 on high → brake kPa stored
    {
        int32_t brake_kpa = 0;
        rt::GatewayQueues q{}; q.brake_req_kpa = &brake_kpa;
        can::Frame f; f.id = 0x301; f.dlc = 4;
        f.put_i32(0, 8000);
        rt::route_frame(f, true, q);
        CHECK("0x301 on high → brake_kpa=8000");
        if (brake_kpa == 8000) OK; else BAD("0x301 parse");
    }

    // 0x301 ignored on low bus
    {
        int32_t brake_kpa = 0;
        rt::GatewayQueues q{}; q.brake_req_kpa = &brake_kpa;
        can::Frame f; f.id = 0x301; f.dlc = 4;
        f.put_i32(0, 8000);
        rt::route_frame(f, false/*low*/, q);
        CHECK("0x301 on low → ignored (not high bus)");
        if (brake_kpa == 0) OK; else BAD("0x301 should be ignored on low");
    }
}

static void test_category3_local() {
    hdr("Category 3: Bus-local (never forwarded)");

    // 0x110 SYS_MODE_CMD — consume mode
    {
        uint8_t mode_val = 0xFF;
        rt::GatewayQueues q{}; q.mode_from_sys = &mode_val;
        can::Frame f; f.id = 0x110; f.dlc = 1; f.put_u8(0, uint8_t(can::Mode::Auto));
        rt::route_frame(f, false, q);
        CHECK("0x110 on low → mode_from_sys=Auto");
        if (mode_val == uint8_t(can::Mode::Auto)) OK; else BAD("0x110 mode");
    }

    // 0x200 VCU_SES_REQ is NOT forwarded (local)
    {
        can::Frame gw_high = {};
        rt::GatewayQueues q{}; q.gw_tx_high = &gw_high;
        can::Frame f; f.id = 0x200; f.dlc = 8;
        rt::route_frame(f, false, q);
        CHECK("0x200 on low → NOT forwarded to high (local)");
        if (gw_high.id == 0) OK; else BAD("0x200 forwarded — should be local");
    }

    // 0x202 RT_DRIVE_CMD is NOT forwarded
    {
        can::Frame gw_high = {};
        rt::GatewayQueues q{}; q.gw_tx_high = &gw_high;
        can::Frame f; f.id = 0x202; f.dlc = 5;
        rt::route_frame(f, false, q);
        CHECK("0x202 on low → NOT forwarded (local)");
        if (gw_high.id == 0) OK; else BAD("0x202 forwarded — should be local");
    }

    // 0x201 SES_STATUS — consumed by steering
    {
        int16_t steer_angle = 0;
        rt::GatewayQueues q{}; q.steer_feedback_angle = &steer_angle;
        can::Frame f; f.id = 0x201; f.dlc = 8;
        f.data[2] = 0xC7; f.data[3] = 0x01;  // 0x01C7 = 455 raw (45.5°)
        rt::route_frame(f, false, q);
        CHECK("0x201 on low → steer_feedback_angle=455");
        if (steer_angle == 455) OK; else BAD("0x201 angle");
    }
}

int main() {
    printf("Phase R6: RT CAN Gateway — forwarding categories\n");

    test_forwarding_low_to_high();
    test_forwarding_high_to_low();
    test_estop_bidirectional();
    test_category2_consume();
    test_category3_local();

    printf("\n  Result: %d failures\n", fails);
    return fails ? 1 : 0;
}
