#include <cstdio>
#include "can/can_protocol.h"
int main() {
    can::Frame f; f.id = 0x300; f.dlc = 8;
    f.put_i32(0, 2000);
    f.put_u8(4, 0); f.put_u8(5, 1); f.put_u8(6, 0x90);
    f.put_u8(7, uint8_t(can::Gear::D));
    printf("byte4=%d byte5=%d byte6=%d byte7=%d\n", f.u8_at(4), f.u8_at(5), f.u8_at(6), f.u8_at(7));
    auto cmd = can::HostDriveCmd::from_frame(f);
    printf("speed=%d yaw=%d gear=%d\n", cmd.speed_mmps, cmd.yaw_rate_mrad_s, cmd.gear);
    return 0;
}
