#include <unity.h>
#include <cstdint>
#include <atomic>

// Model the TWAI and MCP2515 recovery state machine invariants from can_driver_twai.h and can_driver_mcp2515.h
enum class TwaiHealth : uint8_t { Active, Warning, Passive, BusOff };

struct MockTwaiStateMachine {
    TwaiHealth state = TwaiHealth::Active;
    uint16_t tec = 0;
    uint16_t rec = 0;
    bool recovery_in_progress = false;
    uint32_t recovery_attempts = 0;
    bool tx_admitted = true;

    void record_tx_error() {
        tec += 8;
        if (tec >= 256) {
            state = TwaiHealth::BusOff;
            tx_admitted = false;
        } else if (tec >= 128) {
            state = TwaiHealth::Passive;
        } else if (tec >= 96) {
            state = TwaiHealth::Warning;
        }
    }

    bool initiate_recovery() {
        if (state != TwaiHealth::BusOff) return false;
        recovery_in_progress = true;
        recovery_attempts++;
        return true;
    }

    void complete_recovery() {
        if (recovery_in_progress) {
            recovery_in_progress = false;
            state = TwaiHealth::Active;
            tec = 0;
            rec = 0;
            tx_admitted = true;
        }
    }
};

enum class McpMode : uint8_t {
    Normal = 0x00,
    Sleep = 0x20,
    Loopback = 0x40,
    ListenOnly = 0x60,
    Configuration = 0x80
};

struct MockMcp2515StateMachine {
    McpMode mode = McpMode::Configuration;
    bool initialized = false;
    bool spi_healthy = true;
    bool bus_off = false;

    bool init() {
        if (!spi_healthy) return false;
        mode = McpMode::Normal;
        initialized = true;
        return true;
    }

    bool can_transmit() const {
        return initialized && spi_healthy && !bus_off && (mode == McpMode::Normal || mode == McpMode::Loopback);
    }

    void trigger_spi_fault() {
        spi_healthy = false;
    }

    void clear_spi_fault() {
        spi_healthy = true;
    }
};

void setUp(void) {}
void tearDown(void) {}

void test_twai_bus_off_recovery(void) {
    MockTwaiStateMachine twai{};
    TEST_ASSERT_EQUAL(TwaiHealth::Active, twai.state);
    TEST_ASSERT_TRUE(twai.tx_admitted);

    // Accumulate errors until Bus-Off
    for (int i = 0; i < 35; ++i) {
        twai.record_tx_error();
    }
    TEST_ASSERT_EQUAL(TwaiHealth::BusOff, twai.state);
    TEST_ASSERT_FALSE(twai.tx_admitted);

    // Initiate recovery
    TEST_ASSERT_TRUE(twai.initiate_recovery());
    TEST_ASSERT_TRUE(twai.recovery_in_progress);
    TEST_ASSERT_EQUAL_UINT32(1, twai.recovery_attempts);

    // Recovery finishes -> returns to Active
    twai.complete_recovery();
    TEST_ASSERT_EQUAL(TwaiHealth::Active, twai.state);
    TEST_ASSERT_TRUE(twai.tx_admitted);
    TEST_ASSERT_EQUAL_UINT16(0, twai.tec);
}

void test_mcp2515_spi_fault(void) {
    MockMcp2515StateMachine mcp{};
    TEST_ASSERT_TRUE(mcp.init());
    TEST_ASSERT_TRUE(mcp.can_transmit());

    // SPI fault disables transmission
    mcp.trigger_spi_fault();
    TEST_ASSERT_FALSE(mcp.can_transmit());

    // Recovering SPI allows transmission again
    mcp.clear_spi_fault();
    TEST_ASSERT_TRUE(mcp.can_transmit());

    // ListenOnly mode must block transmission
    mcp.mode = McpMode::ListenOnly;
    TEST_ASSERT_FALSE(mcp.can_transmit());
}

extern "C" void app_main() {
    UNITY_BEGIN();
    RUN_TEST(test_twai_bus_off_recovery);
    RUN_TEST(test_mcp2515_spi_fault);
    UNITY_END();
}

#if defined(HOST_BUILD) || defined(NATIVE_TEST_ENV) || !defined(ESP_PLATFORM)
int main(int argc, char **argv) {
    app_main();
    return 0;
}
#endif
