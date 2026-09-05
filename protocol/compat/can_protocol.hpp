#pragma once

#include <cstdint>
#include <string_view>

#include "protocol/codecs/seb.hpp"
#include "protocol/codecs/ses.hpp"
#include "protocol/compat/transport.hpp"
#include "protocol/compat/e2e.hpp"
#include "protocol/generated/cpp/etrike_protocol.hpp"

namespace etrike {
namespace protocol {
namespace compat {

enum class Bus : std::uint8_t { High = 0, Low = 1, Powertrain = 2 };
enum class Mode : std::uint8_t { Manual = 0, Auto = 1, Estop = 2 };
enum class Gear : std::uint8_t { N = 0, D = 1, S = 2, R = 3 };

inline constexpr const char* mode_name(Mode mode) noexcept {
    switch (mode) {
        case Mode::Manual: return "MANUAL";
        case Mode::Auto: return "AUTO";
        case Mode::Estop: return "ESTOP";
    }
    return "?";
}

inline constexpr const char* gear_name(Gear gear) noexcept {
    switch (gear) {
        case Gear::N: return "N";
        case Gear::D: return "D";
        case Gear::S: return "S";
        case Gear::R: return "R";
    }
    return "?";
}

// Application protocol IDs come from generated message definitions.
inline constexpr std::uint32_t kIdSafetyEstop = generated::SafetyEstop::kId;
inline constexpr std::uint32_t kIdSysSafetySts = generated::SysSafetySts::kId;
inline constexpr std::uint32_t kIdSysModeCmd = generated::SysModeCmd::kId;
inline constexpr std::uint32_t kIdSysPwrCmd = generated::SysPwrCmd::kId;
inline constexpr std::uint32_t kIdHmiModeReq = generated::HmiModeReq::kId;
inline constexpr std::uint32_t kIdHmiPwrReq = generated::HmiPwrReq::kId;
inline constexpr std::uint32_t kIdSysThrottleSts = generated::SysThrottleSts::kId;
inline constexpr std::uint32_t kIdRtMotionRpt = generated::RtMotionRpt::kId;
inline constexpr std::uint32_t kIdRtDriveCmd = generated::RtDriveCmd::kId;
inline constexpr std::uint32_t kIdRtBrakeCmd = generated::RtBrakeCmd::kId;
inline constexpr std::uint32_t kIdMtrMotorFbk = generated::MtrMotorFbk::kId;
inline constexpr std::uint32_t kIdRtStateRpt = generated::RtStateRpt::kId;
inline constexpr std::uint32_t kIdRtPidRpt = generated::RtPidRpt::kId;
inline constexpr std::uint32_t kIdHostDriveCmd = generated::HostDriveCmd::kId;
inline constexpr std::uint32_t kIdHostBrakeReq = generated::HostBrakeReq::kId;
inline constexpr std::uint32_t kIdHostLightCmd = generated::HostLightCmd::kId;
inline constexpr std::uint32_t kIdHostSteerCmd = generated::HostSteerCmd::kId;
inline constexpr std::uint32_t kIdSteerDiag = generated::SteerDiag::kId;
inline constexpr std::uint32_t kIdBrakeDiag = generated::BrakeDiag::kId;
inline constexpr std::uint32_t kIdHostObstacleDist = generated::HostObstacleDist::kId;
inline constexpr std::uint32_t kIdSysDiagRpt = generated::SysDiagRpt::kId;
inline constexpr std::uint32_t kIdHostHeartbeat = generated::HostHeartbeat::kId;
inline constexpr std::uint32_t kIdRtHeartbeat = generated::RtHeartbeat::kId;
inline constexpr std::uint32_t kIdSysHeartbeat = generated::SysHeartbeat::kId;
inline constexpr std::uint32_t kIdPwtDcdcCmd = generated::PwtDcdcCmd::kId;

// Custom vendor IDs come from the selected custom codec implementations.
inline constexpr std::uint32_t kIdVcuSesReq = codecs::ses::kCommandId;
inline constexpr std::uint32_t kIdSesStatus = codecs::ses::kStatusId;
inline constexpr std::uint32_t kIdSesErrInfo = codecs::ses::kErrorInfoId;
inline constexpr std::uint32_t kIdSesVersion = codecs::ses::kVersionId;
inline constexpr std::uint32_t kIdSesTest = codecs::ses::kTestId;
inline constexpr std::uint32_t kIdVcuSebReq = codecs::seb::kCommandId;
inline constexpr std::uint32_t kIdSebStatus = codecs::seb::kStatusId;
inline constexpr std::uint32_t kIdSebErrInfo = codecs::seb::kErrorInfoId;
inline constexpr std::uint32_t kIdSebVersion = codecs::seb::kVersionId;
inline constexpr std::uint32_t kIdSebTest = codecs::seb::kTestId;

inline constexpr std::string_view bus_name(Bus bus) noexcept {
    switch (bus) {
        case Bus::High: return "high";
        case Bus::Low: return "low";
        case Bus::Powertrain: return "powertrain";
    }
    return {};
}

inline bool is_forwarded(std::uint32_t id, bool extended, Bus from, Bus to) noexcept {
    const std::string_view from_name = bus_name(from);
    const std::string_view to_name = bus_name(to);
    for (const RouteMetadata& route : kRoutes) {
        if (route.from_bus != from_name || route.to_bus != to_name ||
            route.semantics != RouteSemantics::SameFrame)
            continue;
        for (const MessageMetadata& message : kMessages) {
            if (message.key == route.message && message.bus == route.from_bus &&
                message.id == id && message.extended == extended)
                return true;
        }
    }
    return false;
}

inline bool is_forwarded_low_to_high(std::uint32_t id) noexcept {
    return is_forwarded(id, false, Bus::Low, Bus::High);
}

inline bool is_forwarded_high_to_low(std::uint32_t id) noexcept {
    return is_forwarded(id, false, Bus::High, Bus::Low);
}

inline bool is_known_frame_on_bus(std::uint32_t id, bool extended,
                                  std::uint8_t dlc, Bus bus) noexcept {
    const std::string_view name = bus_name(bus);
    for (const MessageMetadata& message : kMessages) {
        if (message.bus == name && message.id == id
            && message.extended == extended && message.dlc == dlc) {
            return true;
        }
    }
    return false;
}

inline bool is_estop_id(std::uint32_t id) noexcept {
    return id == kIdSafetyEstop;
}

}  // namespace compat
}  // namespace protocol
}  // namespace etrike

// This namespace is the narrow source-compatible surface needed by existing
// drivers. Payload types remain visibly generated or custom.
namespace can {
using Frame = ::etrike::protocol::Frame;
using FrameView = ::etrike::protocol::FrameView;
using Bus = ::etrike::protocol::compat::Bus;
using Mode = ::etrike::protocol::compat::Mode;
using Gear = ::etrike::protocol::compat::Gear;
namespace gen = ::etrike::protocol::generated;
namespace custom = ::etrike::protocol::codecs;
namespace e2e = ::etrike::protocol::e2e;

using ::etrike::protocol::compat::frame_view;
using ::etrike::protocol::compat::from_protocol_frame;
using ::etrike::protocol::compat::gear_name;
using ::etrike::protocol::compat::is_estop_id;
using ::etrike::protocol::compat::is_forwarded;
using ::etrike::protocol::compat::is_forwarded_high_to_low;
using ::etrike::protocol::compat::is_forwarded_low_to_high;
using ::etrike::protocol::compat::is_known_frame_on_bus;
using ::etrike::protocol::compat::mode_name;
using ::etrike::protocol::compat::to_protocol_frame;

using ::etrike::protocol::compat::kIdBrakeDiag;
using ::etrike::protocol::compat::kIdHmiModeReq;
using ::etrike::protocol::compat::kIdHmiPwrReq;
using ::etrike::protocol::compat::kIdHostBrakeReq;
using ::etrike::protocol::compat::kIdHostDriveCmd;
using ::etrike::protocol::compat::kIdHostHeartbeat;
using ::etrike::protocol::compat::kIdHostLightCmd;
using ::etrike::protocol::compat::kIdHostObstacleDist;
using ::etrike::protocol::compat::kIdHostSteerCmd;
using ::etrike::protocol::compat::kIdMtrMotorFbk;
using ::etrike::protocol::compat::kIdPwtDcdcCmd;
using ::etrike::protocol::compat::kIdRtBrakeCmd;
using ::etrike::protocol::compat::kIdRtDriveCmd;
using ::etrike::protocol::compat::kIdRtHeartbeat;
using ::etrike::protocol::compat::kIdRtMotionRpt;
using ::etrike::protocol::compat::kIdRtPidRpt;
using ::etrike::protocol::compat::kIdRtStateRpt;
using ::etrike::protocol::compat::kIdSafetyEstop;
using ::etrike::protocol::compat::kIdSebErrInfo;
using ::etrike::protocol::compat::kIdSebStatus;
using ::etrike::protocol::compat::kIdSebTest;
using ::etrike::protocol::compat::kIdSebVersion;
using ::etrike::protocol::compat::kIdSesErrInfo;
using ::etrike::protocol::compat::kIdSesStatus;
using ::etrike::protocol::compat::kIdSesTest;
using ::etrike::protocol::compat::kIdSesVersion;
using ::etrike::protocol::compat::kIdSteerDiag;
using ::etrike::protocol::compat::kIdSysDiagRpt;
using ::etrike::protocol::compat::kIdSysHeartbeat;
using ::etrike::protocol::compat::kIdSysModeCmd;
using ::etrike::protocol::compat::kIdSysPwrCmd;
using ::etrike::protocol::compat::kIdSysSafetySts;
using ::etrike::protocol::compat::kIdSysThrottleSts;
using ::etrike::protocol::compat::kIdVcuSebReq;
using ::etrike::protocol::compat::kIdVcuSesReq;

inline constexpr std::uint32_t kIdRtHeartbeatLow = kIdRtHeartbeat;
inline constexpr std::uint32_t kIdRtHeartbeatHigh = kIdRtHeartbeat;
inline constexpr std::uint32_t kIdSbwCmd = kIdVcuSesReq;
inline constexpr std::uint32_t kIdSbwStatus = kIdSesStatus;
inline constexpr std::uint32_t kIdSbwErrInfo = kIdSesErrInfo;
inline constexpr std::uint32_t kIdSbwVersion = kIdSesVersion;
inline constexpr std::uint32_t kIdSbwTest = kIdSesTest;
inline constexpr std::uint32_t kIdBbwCmd = kIdVcuSebReq;
inline constexpr std::uint32_t kIdBbwStatus = kIdSebStatus;
inline constexpr std::uint32_t kIdBbwErrInfo = kIdSebErrInfo;
inline constexpr std::uint32_t kIdBbwVersion = kIdSebVersion;
inline constexpr std::uint32_t kIdBbwTest = kIdSebTest;
}  // namespace can
