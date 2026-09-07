// RT WP3 — DiagnosticManager (Phase B) reporting path.
// Verifies the RT global reporter (rt::diag()) and the 0x621 (RT_DIAG_EVENT_RPT)
// wire encoding/decoding used by main.cpp's drain. The individual detector raise
// sites live inside ESP-IDF-dependent firmware files and are covered by the
// firmware build; this test exercises the reporter + codec path that is host-safe.
#include <unity.h>

#include "diag_rt.h"
#include "protocol/compat/can.hpp"

using namespace etrike::diagnostics;

void setUp(void) {}
void tearDown(void) {}

static int g_failures = 0;

void test_rt_diag_singleton_raises_and_drains() {
    auto& d = rt::diag();
    d.clear_all();

    d.raise(DiagId::RtSteerFollowingError, 123);
    d.raise(DiagId::RtCanBusOff, 0x0A05);
    d.raise(DiagId::RtSesL3Fault, 0x0300);

    int n = 0;
    DiagReport r{};
    while (d.pop_pending_report(r)) {
        ++n;
        can::gen::RtDiagEventRpt out{};
        out.diag_id = static_cast<std::uint16_t>(r.id);
        out.state = static_cast<std::uint8_t>(r.state);
        out.occurrence_count = r.occurrence_count;
        out.report_counter = r.report_counter;
        out.flags = r.flags;
        out.snapshot_data = r.snapshot_data;
        can::Frame fr{};
        TEST_ASSERT_EQUAL(can::gen::CodecStatus::Ok, can::gen::encode_rt_diag_event_rpt(out, fr));
        TEST_ASSERT_EQUAL(0x621u, fr.id);
        TEST_ASSERT_EQUAL(8, fr.dlc);
    }
    TEST_ASSERT_EQUAL(3, n);

    // Re-raise of an already-active diagnostic must NOT enqueue a second report.
    d.raise(DiagId::RtSteerFollowingError, 200);
    TEST_ASSERT_FALSE(d.pop_pending_report(r));
}

void test_rt_diag_replay_rebroadcasts_active() {
    auto& d = rt::diag();
    d.clear_all();
    d.raise(DiagId::RtSysHeartbeatTimeout, 1);  // latching per registry meta
    DiagReport r{};
    TEST_ASSERT_TRUE(d.pop_pending_report(r));   // first drain
    TEST_ASSERT_FALSE(d.pop_pending_report(r));  // nothing else pending
    d.replay_active_set();
    TEST_ASSERT_TRUE(d.pop_pending_report(r));   // replay re-broadcasts active set
}

extern "C" void app_main() {
    UNITY_BEGIN();
    RUN_TEST(test_rt_diag_singleton_raises_and_drains);
    RUN_TEST(test_rt_diag_replay_rebroadcasts_active);
    UNITY_END();
}

#if defined(HOST_BUILD) || defined(NATIVE_TEST_ENV) || !defined(ESP_PLATFORM)
int main(int argc, char** argv) {
    (void)argc; (void)argv;
    app_main();
    return 0;
}
#endif
