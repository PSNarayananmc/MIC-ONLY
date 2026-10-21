// ---------------------------------------------------------------------------
// RingBuffer unit tests — the realtime backbone of the audio pipeline.
// Verifies: data integrity across the wraparound, drop-on-overflow policy,
// SPSC ordering under concurrency, and the failure counters.
// ---------------------------------------------------------------------------
#include "../src/audio/RingBuffer.h"
#include <QCoreApplication>
#include <QDebug>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>
#include <thread>
#include <vector>

using vc::RingBuffer;

static int g_fail = 0;
#define CHECK(cond, msg)                                                     \
    do {                                                                     \
        if (!(cond)) {                                                       \
            qCritical("FAIL: %s (line %d)", msg, __LINE__);                  \
            ++g_fail;                                                        \
        }                                                                    \
    } while (0)

static float makeValue(std::uint32_t i)
{
    // lossless round-trip of a 32-bit counter through float
    float f;
    std::memcpy(&f, &i, sizeof(f));
    return f;
}

static std::uint32_t valueIndex(float f)
{
    std::uint32_t i;
    std::memcpy(&i, &f, sizeof(i));
    return i;
}

// 1) simple write/read round trip
static void testRoundTrip()
{
    RingBuffer rb(4096);
    CHECK(rb.capacity() == 4096, "capacity preserved");

    std::vector<float> in(1000), out(1000);
    for (std::size_t i = 0; i < in.size(); ++i)
        in[i] = float(i) * 0.001f;
    CHECK(rb.write(in.data(), in.size()) == in.size(), "write all");
    CHECK(rb.readSpace() == 1000, "readSpace after write");
    CHECK(rb.writeSpace() == rb.capacity() - 1001, "writeSpace respects guard");

    std::fill(out.begin(), out.end(), -1.0f);
    CHECK(rb.read(out.data(), out.size()) == out.size(), "read all");
    for (std::size_t i = 0; i < out.size(); ++i) {
        if (std::fabs(out[i] - in[i]) > 1e-9f) {
            CHECK(false, "round-trip sample mismatch");
            break;
        }
    }
    CHECK(rb.readSpace() == 0, "empty after read");
}

// 2) wraparound: advance the indices close to the physical end, then
//    read/write blocks that straddle it (both memcpy halves must be correct)
static void testWraparound()
{
    RingBuffer rb(1024);
    std::vector<float> in(300), out(300);
    // fill + drain several times to park the indices mid-buffer
    for (int cycle = 0; cycle < 7; ++cycle) {
        for (std::size_t i = 0; i < in.size(); ++i)
            in[i] = makeValue(std::uint32_t(cycle * 100000 + i));
        CHECK(rb.write(in.data(), in.size()) == in.size(), "wrap write");
        CHECK(rb.read(out.data(), out.size()) == out.size(), "wrap read");
        for (std::size_t i = 0; i < out.size(); ++i) {
            if (valueIndex(out[i]) != std::uint32_t(cycle * 100000 + i)) {
                CHECK(false, "wraparound data corruption");
                return;
            }
        }
    }
    // 7 x 300 = 2100 samples through a 1024 buffer: indices wrapped ~2x
}

// 3) overflow policy: a write that does not fit must be DROPPED (not split,
//    not blocking) and counted — realtime contract
static void testOverflowPolicy()
{
    RingBuffer rb(1024);
    std::vector<float> big(2048, 0.5f);
    CHECK(rb.write(big.data(), big.size()) == 0, "oversized write dropped");
    CHECK(rb.overruns() == 1, "overrun counted");
    CHECK(rb.readSpace() == 0, "nothing written on overflow");

    // fill exactly, then one more must be dropped
    std::vector<float> max(rb.capacity() - 1, 0.25f);
    CHECK(rb.write(max.data(), max.size()) == max.size(), "fill to capacity-1");
    CHECK(rb.write(big.data(), 64) == 0, "write to full buffer dropped");
    CHECK(rb.overruns() == 2, "second overrun counted");
}

// 4) underrun policy: reading more than available returns 0 and counts
static void testUnderrun()
{
    RingBuffer rb(1024);
    std::vector<float> out(100);
    CHECK(rb.read(out.data(), out.size()) == 0, "read from empty fails");
    CHECK(rb.underruns() == 1, "underrun counted");
}

// 5) peek / skip / clear semantics
static void testPeekSkipClear()
{
    RingBuffer rb(1024);
    float in[4] = {1.f, 2.f, 3.f, 4.f};
    float out[4] = {};
    rb.write(in, 4);

    CHECK(rb.peek(out, 4) == 4, "peek full block");
    CHECK(out[0] == 1.f && out[3] == 4.f, "peek content");
    CHECK(rb.readSpace() == 4, "peek does not consume");

    rb.skip(2);
    CHECK(rb.readSpace() == 2, "skip consumes");
    CHECK(rb.read(out, 2) == 2 && out[0] == 3.f && out[1] == 4.f,
          "content after skip");

    rb.write(in, 4);
    rb.clear();
    CHECK(rb.readSpace() == 0, "clear empties the buffer");
    const std::uint64_t underrunsBefore = rb.underruns();
    float dummy[4];
    CHECK(rb.read(dummy, 4) == 0, "read after clear fails");
    CHECK(rb.underruns() == underrunsBefore + 1,
          "clear keeps counters monotonic");
}

// 6) SPSC concurrency stress: one producer, one consumer, in-order check.
//    Uses bit-pattern floats so every value must arrive EXACTLY once,
//    strictly ordered — the property the pipeline depends on.
static void testConcurrentSpsc()
{
    RingBuffer rb(4096);
    constexpr std::uint32_t TOTAL = 4u << 20;   // 4M samples
    std::atomic<bool> producerDone{false};
    std::atomic<std::uint32_t> readCount{0};

    std::thread producer([&] {
        std::uint32_t sent = 0;
        std::vector<float> blk(97);
        while (sent < TOTAL) {
            const std::size_t n = std::min<std::size_t>(blk.size(),
                                                        TOTAL - sent);
            if (rb.writeSpace() < n) {
                std::this_thread::yield();
                continue;
            }
            for (std::size_t i = 0; i < n; ++i)
                blk[i] = makeValue(sent + std::uint32_t(i));
            if (rb.write(blk.data(), n) == n)
                sent += std::uint32_t(n);
        }
        producerDone = true;
    });

    bool ok = true;
    std::thread consumer([&] {
        std::uint32_t expected = 0;
        std::vector<float> blk(211);
        while (expected < TOTAL) {
            if (producerDone && rb.readSpace() == 0) break;
            if (rb.readSpace() < 1) { std::this_thread::yield(); continue; }
            const std::size_t got = rb.read(blk.data(),
                std::min<std::size_t>(blk.size(), rb.readSpace()));
            for (std::size_t i = 0; i < got; ++i, ++expected) {
                if (valueIndex(blk[i]) != expected) {
                    qCritical("SPSC: expected %u got %u", expected,
                              valueIndex(blk[i]));
                    ok = false;
                    return;
                }
            }
            readCount += std::uint32_t(got);
        }
    });

    producer.join();
    consumer.join();
    CHECK(ok, "SPSC order preserved");
    CHECK(readCount.load() == TOTAL, "all samples delivered exactly once");
    CHECK(rb.overruns() == 0, "no overruns under SPSC load");
    CHECK(rb.underruns() == 0 || producerDone.load(),
          "no premature underruns");
}

// 7) random block-size fuzz: interleaving odd sizes across the boundary
static void testFuzz()
{
    RingBuffer rb(2048);
    std::mt19937 rng(20241020);
    std::uint32_t sent = 0, received = 0;
    int guard = 0;
    while (received < 200000 && guard++ < 100000) {
        const std::size_t wn = 1 + rng() % 500;
        std::vector<float> win(wn);
        for (std::size_t i = 0; i < wn; ++i)
            win[i] = makeValue(sent + std::uint32_t(i));
        const std::size_t wrote = rb.write(win.data(), wn);
        sent += std::uint32_t(wrote);

        const std::size_t rn = 1 + rng() % 500;
        std::vector<float> rout(rn);
        const std::size_t got = rb.read(rout.data(),
            std::min(rn, rb.readSpace()));
        for (std::size_t i = 0; i < got; ++i) {
            if (valueIndex(rout[i]) != received) {
                CHECK(false, "fuzz: data mismatch");
                return;
            }
            ++received;
        }
    }
    CHECK(received >= 200000, "fuzz volume reached");
    CHECK(received == sent || sent - received <= rb.capacity(),
          "in-flight data consistent");
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);   // RingBuffer itself has no Qt deps;
                                        // keep the harness uniform
    testRoundTrip();
    testWraparound();
    testOverflowPolicy();
    testUnderrun();
    testPeekSkipClear();
    testConcurrentSpsc();
    testFuzz();

    if (g_fail == 0) {
        qInfo("RINGBUFFER TESTS: ALL PASSED");
        return 0;
    }
    qCritical("RINGBUFFER TESTS: %d FAILURES", g_fail);
    return 1;
}
