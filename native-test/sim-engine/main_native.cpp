/**
 * sim-engine-native — CAN ECU simulation engine (IPC via stdin/stdout JSON-Lines).
 *
 * Reads:  JSON Lines from stdin  (frames, config, tick commands)
 * Writes: JSON Lines to   stdout (response frames, ECU state)
 *
 * Compiles the RT physics model from the actual firmware source.
 * HAL/FreeRTOS dependencies are stubbed out — only the pure-logic layer
 * (physics, steering math) is compiled. Safety monitor and full state
 * machines need the complete rt_state.h context; those will be added
 * incrementally as the stub layer matures.
 *
 * Build: cmake --build build3 --target sim_engine_native
 * Run:   echo '{"type":"tick","dt_ms":10}' | ./sim_engine_native
 */

#include <cstdio>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cmath>
#include <string>

#include "protocol/generated/cpp/etrike_protocol.hpp"

// ── Stub the minimal ESP-IDF / FreeRTOS types needed ──
struct QueueHandle_t_dummy {};
using QueueHandle_t = QueueHandle_t_dummy*;
#define ESP_LOGE(tag, fmt, ...)
#define ESP_LOGW(tag, fmt, ...)
#define ESP_LOGI(tag, fmt, ...)

int64_t g_sim_time_us = 0;

extern "C" {
    int64_t esp_timer_get_time() {
        return g_sim_time_us;
    }
    void set_sim_time_us(int64_t us) {
        g_sim_time_us = us;
    }
}

// Override esp_timer_get_time to return our simulated time
#undef esp_timer_get_time
extern "C" int64_t esp_timer_get_time();

// ── Include the real physics model (no FreeRTOS/ESP-IDF deps) ──
#include "config.h"
#include "physics_model.h"

// Stub globals needed by physics model
#include <atomic>
std::atomic<int32_t> g_brake_request_kpa{0};
std::atomic<uint32_t> g_obstacle_mm{UINT32_MAX};
std::atomic<int32_t> g_mtr_actual_speed_mmps{0};
std::atomic<int64_t> g_last_estop_sent_us{0};

// Define the physics model instance
rt::PhysicsModel g_physics;

// ── JSON helpers ──
static bool g_eof = false;

static std::string read_line() {
    if (g_eof) return "";
    std::string line;
    int c;
    while ((c = getchar()) != EOF && c != '\n') {
        if (c != '\r') line += static_cast<char>(c);
    }
    if (c == EOF) g_eof = true;
    return line;
}

static void write_json(const char* json) {
    fputs(json, stdout);
    fputc('\n', stdout);
    fflush(stdout);
}

static std::string json_get_string(const std::string& json, const char* key) {
    std::string search = "\"" + std::string(key) + "\":";
    auto pos = json.find(search);
    if (pos == std::string::npos) return "";
    pos += search.length();
    while (pos < json.length() && std::isspace(static_cast<unsigned char>(json[pos]))) ++pos;
    if (pos >= json.length() || json[pos] != '"') return "";
    ++pos;
    auto end = json.find('"', pos);
    if (end == std::string::npos) return "";
    return json.substr(pos, end - pos);
}

static std::string json_get_type(const std::string& json) {
    return json_get_string(json, "type");
}

static int json_get_int(const std::string& json, const char* key, int def = 0) {
    std::string search = "\"" + std::string(key) + "\":";
    auto pos = json.find(search);
    if (pos == std::string::npos) return def;
    pos += search.length();
    while (pos < json.length() && (json[pos] == ' ' || json[pos] == '\t')) pos++;
    std::string num;
    while (pos < json.length() && (isdigit(json[pos]) || json[pos] == '-'))
        num += json[pos++];
    return num.empty() ? def : std::stoi(num);
}

static bool json_get_byte_array(const std::string& json, const char* key,
                                std::array<std::uint8_t, 8>& out, std::size_t& size) {
    std::string search = "\"" + std::string(key) + "\":";
    auto pos = json.find(search);
    if (pos == std::string::npos) return false;
    pos += search.length();
    while (pos < json.length() && std::isspace(static_cast<unsigned char>(json[pos]))) ++pos;
    if (pos >= json.length() || json[pos] != '[') return false;
    ++pos;
    size = 0;
    while (pos < json.length() && json[pos] != ']') {
        while (pos < json.length() && (std::isspace(static_cast<unsigned char>(json[pos])) || json[pos] == ',')) ++pos;
        if (pos >= json.length() || json[pos] == ']') break;
        if (!std::isdigit(static_cast<unsigned char>(json[pos]))) return false;
        int value = 0;
        while (pos < json.length() && std::isdigit(static_cast<unsigned char>(json[pos]))) {
            value = value * 10 + (json[pos++] - '0');
        }
        if (value > 255 || size >= out.size()) return false;
        out[size++] = static_cast<std::uint8_t>(value);
    }
    return pos < json.length() && json[pos] == ']';
}

// ── Main ──
int main() {
    write_json("{\"type\":\"state\",\"ecu\":\"rt\",\"healthy\":true,\"uptime_ms\":0}");
    rt::DriveCmd commanded_drive{};

    while (!g_eof) {
        std::string line = read_line();
        if (line.empty()) continue;

        std::string msg_type = json_get_type(line);

        if (msg_type == "frame") {
            // A small JSON-Lines CAN ingress for software-in-the-loop clients.
            // Decode with the generated production protocol codec, not a local layout.
            const std::string id = json_get_string(line, "id");
            std::array<std::uint8_t, 8> data{};
            std::size_t length = 0;
            if (id == "0x300" && json_get_byte_array(line, "data", data, length)) {
                etrike::protocol::Frame frame = etrike::protocol::Frame::standard(0x300u, static_cast<std::uint8_t>(length));
                frame.data = data;
                etrike::protocol::generated::HostDriveCmd host{};
                if (etrike::protocol::generated::decode(frame.view(), host) == etrike::protocol::CodecStatus::Ok) {
                    commanded_drive.speed_mmps = host.speed_mmps;
                    commanded_drive.yaw_rate_mrad_s = host.yaw_rate_mrad_s;
                    write_json("{\"type\":\"ack\",\"id\":\"0x300\",\"name\":\"HOST_DRIVE_CMD\"}");
                } else {
                    write_json("{\"type\":\"error\",\"code\":\"invalid_host_drive_cmd\"}");
                }
            }
        } else if (msg_type == "tick") {
            int dt_ms = json_get_int(line, "dt_ms", 10);
            g_sim_time_us += int64_t(dt_ms) * 1000;

            // Run physics: resolve a drive command
            rt::DriveCmd cmd = commanded_drive;
            // Keep explicit scalar input for standalone simulator trace tools.
            if (line.find("\"speed_mmps\"") != std::string::npos)
                cmd.speed_mmps = json_get_int(line, "speed_mmps", cmd.speed_mmps);
            if (line.find("\"yaw_mrad_s\"") != std::string::npos)
                cmd.yaw_rate_mrad_s = json_get_int(line, "yaw_mrad_s", cmd.yaw_rate_mrad_s);

            rt::ResolvedSetpoint sp{};
            bool ok = g_physics.resolve(cmd, sp);

            char buf[512];

            // Dynamic steering limit
            float limit_deg = rt::compute_dynamic_limit(static_cast<float>(cmd.speed_mmps));
            float follow_thr = rt::compute_following_error_threshold(static_cast<float>(cmd.speed_mmps));

            // Physics output as state
            snprintf(buf, sizeof(buf),
                "{\"type\":\"state\",\"ecu\":\"rt\","
                "\"healthy\":true,\"uptime_ms\":%lld,"
                "\"physics\":{"
                "\"motor_speed_mmps\":%d,"
                "\"steer_angle_mdeg\":%d,"
                "\"steer_valid\":%s,"
                "\"reversing\":%s,"
                "\"gear\":%d,"
                "\"steer_limit_deg\":%.1f,"
                "\"follow_error_threshold_deg\":%.1f"
                "}}",
                (long long)(g_sim_time_us / 1000),
                sp.motor_speed_mmps,
                sp.steer_angle_mdeg,
                sp.steer_valid ? "true" : "false",
                sp.reversing ? "true" : "false",
                sp.cmd_gear,
                limit_deg,
                follow_thr);
            write_json(buf);

            // Motor command frame (0x204)
            if (ok) {
                etrike::protocol::generated::RtDriveCmd command{};
                command.motor_speed_mmps = sp.motor_speed_mmps;
                command.gear = sp.cmd_gear;
                etrike::protocol::Frame frame{};
                if (etrike::protocol::generated::encode(command, frame) !=
                    etrike::protocol::CodecStatus::Ok) {
                    continue;
                }
                snprintf(buf, sizeof(buf),
                    "{\"type\":\"frame\",\"bus\":\"low\",\"id\":\"0x204\",\"dlc\":5,"
                    "\"data\":[%d,%d,%d,%d,%d],\"name\":\"RT_DRIVE_CMD\"}",
                    frame.data[0], frame.data[1], frame.data[2], frame.data[3], frame.data[4]);
                write_json(buf);
            }
        }
    }
    return 0;
}
