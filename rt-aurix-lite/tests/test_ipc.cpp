// IPC abstraction tests — snapshot + SPSC channel.

#include <cstdio>
#include <cstdlib>

#include "ipc/snapshot.h"
#include "ipc/spsc_channel.h"
#include "ipc/messages.h"
#include "config/safety_config.h"  // kEstopReason*

namespace {

int g_failures = 0;

#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
            ++g_failures;                                                 \
        }                                                                 \
    } while (0)

using rta::Snapshot;
using rta::SpscChannel;

// Snapshot: latest-value publish/read.
void test_snapshot() {
    Snapshot<rta::Mode> snap;
    CHECK(snap.read() == rta::Mode::Manual);  // default
    snap.publish(rta::Mode::Auto);
    CHECK(snap.read() == rta::Mode::Auto);
    snap.publish(rta::Mode::Estop);
    CHECK(snap.read() == rta::Mode::Estop);
}

// Snapshot of a small struct.
void test_snapshot_struct() {
    struct S { std::int32_t a; std::uint8_t b; };
    static_assert(sizeof(S) <= 8, "test struct must fit in a word");
    Snapshot<S> snap;
    S s{7, 3};
    snap.publish(s);
    S out = snap.read();
    CHECK(out.a == 7);
    CHECK(out.b == 3);
}

// SPSC: FIFO order, full/empty detection.
void test_spsc_fifo() {
    SpscChannel<int, 4> ch;
    CHECK(ch.empty());
    CHECK(!ch.full());

    CHECK(ch.push(1));
    CHECK(ch.push(2));
    CHECK(ch.push(3));
    CHECK(ch.push(4));
    CHECK(ch.full());

    int v = 0;
    CHECK(!ch.push(5));  // full -> rejected
    CHECK(ch.pop(v)); CHECK(v == 1);
    CHECK(ch.pop(v)); CHECK(v == 2);
    CHECK(ch.pop(v)); CHECK(v == 3);
    CHECK(ch.pop(v)); CHECK(v == 4);
    CHECK(ch.empty());
    CHECK(!ch.pop(v));   // empty -> rejected
}

// SPSC wrap-around after full.
void test_spsc_wrap() {
    SpscChannel<int, 2> ch;
    CHECK(ch.push(10));
    CHECK(ch.push(20));
    int v;
    CHECK(ch.pop(v)); CHECK(v == 10);
    CHECK(ch.push(30));  // wrap
    CHECK(ch.pop(v)); CHECK(v == 20);
    CHECK(ch.pop(v)); CHECK(v == 30);
}

// Messages: typed event round-trip through a channel.
void test_messages() {
    using rta::EstopEvent;
    SpscChannel<EstopEvent, 4> ch;
    EstopEvent ev;
    ev.active = true;
    ev.reason = rta::kEstopReasonCanEstop;
    ev.obstacle_triggered = false;
    ev.valid = true;
    CHECK(ch.push(ev));
    EstopEvent out;
    CHECK(ch.pop(out));
    CHECK(out.active);
    CHECK(out.reason == rta::kEstopReasonCanEstop);
    CHECK(out.valid);
}

}  // namespace

int main() {
    test_snapshot();
    test_snapshot_struct();
    test_spsc_fifo();
    test_spsc_wrap();
    test_messages();
    if (g_failures) {
        std::printf("ipc: %d FAILURES\n", g_failures);
        return 1;
    }
    std::printf("ipc: all tests passed\n");
    return 0;
}
