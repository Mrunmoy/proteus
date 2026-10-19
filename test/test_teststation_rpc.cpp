// ── TestStation RPC over aether-lite (host, fake link) ──────────────────────
// Proves the manufacturing-test protocol end to end on the
// host with NO hardware: a real aether-lite-framed request is fed through an
// in-memory fake HAL, dispatched by the generated TestStation service into the
// portable OP handlers (over fake driver hooks), and the response frame is
// asserted byte-for-byte. This is the "host-test the framing/dispatch + the ops
// with a fake link first" step before it runs on metal.
//
// The device runs ops and returns RAW data — these tests assert the raw bytes on
// the wire; the PASS/FAIL JUDGMENT is the PC station's job (tools/mfg/gui).

extern "C"
{
#include "aether_lite_dispatch.h"
#include "aether_lite_framing.h"
#include "aether_lite_types.h"
#include "TestStation.h"   // generated aether-lite dispatch (proto/gen/aether_lite)
}

#include "app/TestAgent.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <vector>

namespace
{
    // ── Fake link: two in-memory byte queues the HAL reads/writes ──────────
    std::vector<uint8_t> g_rx;        // bytes the device will read (its input)
    size_t               g_rxPos = 0;
    std::vector<uint8_t> g_tx;        // bytes the device wrote (its output)

    size_t fakeRxRead(uint8_t* buf, size_t maxBytes, void* /*ctx*/)
    {
        size_t n = 0;
        while (n < maxBytes && g_rxPos < g_rx.size()) { buf[n++] = g_rx[g_rxPos++]; }
        return n;
    }

    int fakeTxWrite(const uint8_t* buf, size_t len, void* /*ctx*/)
    {
        g_tx.insert(g_tx.end(), buf, buf + len);
        return 0;
    }

    uint32_t fakeTick() { return 0; }

    // Known identity the device handler reports (asserted on the wire).
    constexpr uint8_t  kChipUid[8] = {0x96, 0x2a, 0x28, 0x56, 0x2d, 0x9f, 0x63, 0x7a};
    constexpr uint32_t kFwVersion  = 0x00010000u;   // 1.0.0
    constexpr uint32_t kProtoHash  = 0xE131D8F3u;

    // Little-endian field readers over the captured response bytes.
    uint16_t rd16(const std::vector<uint8_t>& v, size_t o)
    {
        return static_cast<uint16_t>(v[o] | (v[o + 1] << 8));
    }
    uint32_t rd32(const std::vector<uint8_t>& v, size_t o)
    {
        return static_cast<uint32_t>(v[o]) | (static_cast<uint32_t>(v[o + 1]) << 8)
             | (static_cast<uint32_t>(v[o + 2]) << 16) | (static_cast<uint32_t>(v[o + 3]) << 24);
    }

    // Wire offsets within a frame: [AA][55][len:4][header:24][payload][crc:4]
    constexpr size_t kHdr = 6;          // header starts after sync(2)+len(4)
    constexpr size_t kPld = kHdr + 24;  // payload starts after the 24B header

    // ── Fake driver hooks — record the op args, return scripted raw data ────
    bool     g_ledOn        = false;
    int      g_lcdPattern   = -1;
    uint32_t g_scriptedHeld = 0;
    uint32_t g_toneFreq     = 0;
    uint32_t g_toneMs       = 0;
    bool     g_cardPresent  = false;
    uint32_t g_cardFiles    = 0;

    bool     fakeSetLed(bool on)          { g_ledOn = on; return on; }
    bool     fakeLcdShowPattern(uint8_t p){ g_lcdPattern = p; return true; }
    uint32_t fakeScanButtons()            { return g_scriptedHeld; }
    bool     fakePlayTone(uint32_t f, uint32_t ms) { g_toneFreq = f; g_toneMs = ms; return true; }
    bool     fakeSdProbe(uint32_t* files)
    {
        *files = g_cardPresent ? g_cardFiles : 0u;
        return g_cardPresent;
    }
}

// ── Port-level identity handlers (the test stands in for src/port/rp2350-test) ─
extern "C" int TestStation_handleGetIdentity(Identity* id)
{
    std::memcpy(id->chipUid, kChipUid, 8);
    id->fwVersion = kFwVersion;
    id->protoHash = kProtoHash;
    return AL_SUCCESS;
}

extern "C" int TestStation_handleGetDeviceInfo(DevInfo* info)
{
    info->present    = 1;
    info->serialized = 0;
    info->hwRev      = 1;
    info->vid        = 0x2E8A;
    info->pid        = 0x4001;
    std::strncpy(info->model,   "RP2350-PiZero", sizeof(info->model) - 1);
    std::strncpy(info->variant, "GamePi13",      sizeof(info->variant) - 1);
    info->serial[0] = '\0';
    return AL_SUCCESS;
}

class TestStationRpc : public ::testing::Test
{
protected:
    al_hal_t m_hal{};

    void SetUp() override
    {
        g_rx.clear(); g_rxPos = 0; g_tx.clear();
        m_hal.rx_read     = fakeRxRead;
        m_hal.tx_write    = fakeTxWrite;
        m_hal.get_tick_ms = fakeTick;
        m_hal.user_ctx    = nullptr;
        al_init(&m_hal);
        ASSERT_EQ(TestStation_register(), AL_SUCCESS);

        g_ledOn = false; g_lcdPattern = -1; g_scriptedHeld = 0;
        g_toneFreq = 0; g_toneMs = 0; g_cardPresent = false; g_cardFiles = 0;

        app::TestAgentHooks hooks{};
        hooks.setLed         = fakeSetLed;
        hooks.lcdShowPattern = fakeLcdShowPattern;
        hooks.scanButtons    = fakeScanButtons;
        hooks.playTone       = fakePlayTone;
        hooks.sdProbe        = fakeSdProbe;
        app::testAgentInit(hooks);
    }

    // Build a real aether-lite request frame and place it on the device's RX.
    void injectRequest(uint32_t messageId, uint32_t seq,
                       const uint8_t* payload = nullptr, uint32_t payloadLen = 0)
    {
        al_frame_header_t hdr{};
        hdr.version       = AL_PROTOCOL_VERSION;
        hdr.flags         = AL_FRAME_REQUEST;
        hdr.service_id    = TEST_STATION_SERVICE_ID;
        hdr.message_id    = messageId;
        hdr.seq           = seq;
        hdr.payload_bytes = payloadLen;

        g_tx.clear();
        ASSERT_EQ(al_frame_send(&hdr, payload, payloadLen), 0);  // frames into g_tx
        g_rx    = g_tx;   // hand the framed request to the device's RX
        g_rxPos = 0;
        g_tx.clear();     // capture the response separately
    }

    // Common response-header assertions; returns the success/aux code.
    int32_t respHeader(uint32_t messageId, uint32_t seq)
    {
        EXPECT_EQ(g_tx[0], 0xAA);
        EXPECT_EQ(g_tx[1], 0x55);
        EXPECT_EQ(rd16(g_tx, kHdr + 2),  AL_FRAME_RESPONSE);
        EXPECT_EQ(rd32(g_tx, kHdr + 4),  TEST_STATION_SERVICE_ID);
        EXPECT_EQ(rd32(g_tx, kHdr + 8),  messageId);
        EXPECT_EQ(rd32(g_tx, kHdr + 12), seq);
        return static_cast<int32_t>(rd32(g_tx, kHdr + 20));
    }
};

// ── Identity + transport robustness ─────────────────────────────────────────

TEST_F(TestStationRpc, GetIdentityRoundTrip)
{
    injectRequest(TEST_STATION_GET_IDENTITY, 0x1234);
    ASSERT_EQ(al_poll(), 1);

    EXPECT_EQ(respHeader(TEST_STATION_GET_IDENTITY, 0x1234), AL_SUCCESS);
    EXPECT_EQ(rd32(g_tx, kHdr + 16), 16u);               // payload_bytes
    for (int i = 0; i < 8; ++i) { EXPECT_EQ(g_tx[kPld + i], kChipUid[i]) << "byte " << i; }
    EXPECT_EQ(rd32(g_tx, kPld + 8),  kFwVersion);
    EXPECT_EQ(rd32(g_tx, kPld + 12), kProtoHash);
}

TEST_F(TestStationRpc, GetDeviceInfoReportsTheTypeBlock)
{
    injectRequest(TEST_STATION_GET_DEVICE_INFO, 0x2222);
    ASSERT_EQ(al_poll(), 1);

    EXPECT_EQ(respHeader(TEST_STATION_GET_DEVICE_INFO, 0x2222), AL_SUCCESS);
    EXPECT_EQ(rd32(g_tx, kHdr + 16), 67u);               // packed DeviceInfo size
    // Layout: present(1) serialized(1) hwRev(2) vid(2) pid(2) model[17] variant[17] serial[25]
    EXPECT_EQ(g_tx[kPld + 0], 1u);                       // present
    EXPECT_EQ(g_tx[kPld + 1], 0u);                       // serialized
    EXPECT_EQ(rd16(g_tx, kPld + 2), 1u);                 // hwRev
    EXPECT_EQ(rd16(g_tx, kPld + 4), 0x2E8Au);            // vid
    EXPECT_EQ(rd16(g_tx, kPld + 6), 0x4001u);            // pid
    EXPECT_STREQ(reinterpret_cast<const char*>(g_tx.data() + kPld + 8),  "RP2350-PiZero");
    EXPECT_STREQ(reinterpret_cast<const char*>(g_tx.data() + kPld + 25), "GamePi13");
    EXPECT_EQ(g_tx[kPld + 42], 0u);                      // serial empty (not serialized)
}

TEST_F(TestStationRpc, UnknownMethodReturnsInvalidMethod)
{
    injectRequest(/*messageId=*/0x99u, /*seq=*/7);
    ASSERT_EQ(al_poll(), 1);

    EXPECT_EQ(rd16(g_tx, kHdr + 2),  AL_FRAME_RESPONSE);
    EXPECT_EQ(rd32(g_tx, kHdr + 12), 7u);                       // seq echoed
    EXPECT_EQ(rd32(g_tx, kHdr + 16), 0u);                       // no payload
    EXPECT_EQ(static_cast<int32_t>(rd32(g_tx, kHdr + 20)),
              AL_ERR_INVALID_METHOD);
}

TEST_F(TestStationRpc, GarbageBytesAreResynced)
{
    injectRequest(TEST_STATION_GET_IDENTITY, 0x55);
    std::vector<uint8_t> framed = g_rx;                 // the good frame
    g_rx.clear();
    g_rx.insert(g_rx.end(), {0x00, 0xAA, 0x12, 0xFF}); // junk + a false sync
    g_rx.insert(g_rx.end(), framed.begin(), framed.end());
    g_rxPos = 0;

    ASSERT_EQ(al_poll(), 1);
    EXPECT_EQ(rd16(g_tx, kHdr + 2), AL_FRAME_RESPONSE);
    EXPECT_EQ(rd32(g_tx, kHdr + 12), 0x55u);
}

// ── Hardware ops: device runs the driver, returns RAW data (no verdict) ─────

TEST_F(TestStationRpc, SetLedDrivesTheLedAndEchoesTheLevel)
{
    const uint8_t on[1] = {1};
    injectRequest(TEST_STATION_SET_LED, 0x10, on, sizeof(on));
    ASSERT_EQ(al_poll(), 1);

    EXPECT_TRUE(g_ledOn);                                // driver was driven
    EXPECT_EQ(respHeader(TEST_STATION_SET_LED, 0x10), AL_SUCCESS);
    ASSERT_EQ(rd32(g_tx, kHdr + 16), 1u);
    EXPECT_EQ(g_tx[kPld], 1u);                           // applied == on

    const uint8_t off[1] = {0};
    injectRequest(TEST_STATION_SET_LED, 0x11, off, sizeof(off));
    ASSERT_EQ(al_poll(), 1);
    EXPECT_FALSE(g_ledOn);
    EXPECT_EQ(g_tx[kPld], 0u);
}

TEST_F(TestStationRpc, LcdShowPatternForwardsThePatternToTheDriver)
{
    const uint8_t pattern[1] = {2};                      // green
    injectRequest(TEST_STATION_LCD_SHOW_PATTERN, 0x20, pattern, sizeof(pattern));
    ASSERT_EQ(al_poll(), 1);

    EXPECT_EQ(g_lcdPattern, 2);                          // forwarded raw
    EXPECT_EQ(respHeader(TEST_STATION_LCD_SHOW_PATTERN, 0x20), AL_SUCCESS);
    EXPECT_EQ(g_tx[kPld], 1u);                           // drawn
}

TEST_F(TestStationRpc, ScanButtonsReturnsTheRawHeldBitmask)
{
    g_scriptedHeld = 0x0805;                              // UP|LEFT|... arbitrary bits
    injectRequest(TEST_STATION_SCAN_BUTTONS, 0x30);
    ASSERT_EQ(al_poll(), 1);

    EXPECT_EQ(respHeader(TEST_STATION_SCAN_BUTTONS, 0x30), AL_SUCCESS);
    ASSERT_EQ(rd32(g_tx, kHdr + 16), 4u);
    EXPECT_EQ(rd32(g_tx, kPld), 0x0805u);                // raw bitmask, no judging
}

TEST_F(TestStationRpc, PlayToneForwardsFreqAndDuration)
{
    uint8_t req[8];
    const uint32_t freq = 440u, ms = 250u;
    std::memcpy(req + 0, &freq, 4);
    std::memcpy(req + 4, &ms, 4);
    injectRequest(TEST_STATION_PLAY_TONE, 0x40, req, sizeof(req));
    ASSERT_EQ(al_poll(), 1);

    EXPECT_EQ(g_toneFreq, 440u);
    EXPECT_EQ(g_toneMs, 250u);
    EXPECT_EQ(respHeader(TEST_STATION_PLAY_TONE, 0x40), AL_SUCCESS);
    EXPECT_EQ(g_tx[kPld], 1u);                           // started
}

TEST_F(TestStationRpc, SdProbeReportsAbsentWithoutACard)
{
    injectRequest(TEST_STATION_SD_PROBE, 0x50);
    ASSERT_EQ(al_poll(), 1);

    EXPECT_EQ(respHeader(TEST_STATION_SD_PROBE, 0x50), AL_SUCCESS);
    ASSERT_EQ(rd32(g_tx, kHdr + 16), 5u);                // present(1) + files(4)
    EXPECT_EQ(g_tx[kPld], 0u);                           // not mounted
    EXPECT_EQ(rd32(g_tx, kPld + 1), 0u);
}

TEST_F(TestStationRpc, SdProbeReportsTheRootEntryCountWithACard)
{
    g_cardPresent = true;
    g_cardFiles   = 12;
    injectRequest(TEST_STATION_SD_PROBE, 0x51);
    ASSERT_EQ(al_poll(), 1);

    EXPECT_EQ(respHeader(TEST_STATION_SD_PROBE, 0x51), AL_SUCCESS);
    EXPECT_EQ(g_tx[kPld], 1u);                           // mounted + read
    EXPECT_EQ(rd32(g_tx, kPld + 1), 12u);                // raw count, no judging
}
