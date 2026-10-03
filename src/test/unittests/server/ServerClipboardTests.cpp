#include "test/mock/inputleap/MockScreen.h"
#include "test/mock/inputleap/ClipboardTimerPump.h"
#include "server/Server.h"
#include "server/PrimaryClient.h"
#include <gtest/gtest.h>

namespace inputleap {
namespace {
class MemoryPeer : public PrimaryClient {
public:
    explicit MemoryPeer(const std::string& name) : PrimaryClient(name, nullptr) {}
    std::function<void()> onLeave;
    bool readable = true;
    Clipboard local;
    bool dirty[kClipboardEnd] = {};
    const EventTarget* get_event_target() const override { return this; }
    KeyModifierMask getToggleMask() const override { return 0; }
    bool leave() override { if (onLeave) onLeave(); return true; }
    void enter(std::int32_t, std::int32_t, std::uint32_t, KeyModifierMask, bool) override {}
    bool getClipboard(ClipboardID, IClipboard* dst) const override {
        return readable && Clipboard::copy(dst, &local);
    }
    void setClipboard(ClipboardID id, const IClipboard* src) override {
        if (dirty[id]) { dirty[id] = false; Clipboard::copy(&local, src); }
    }
    void grabClipboard(ClipboardID id) override { dirty[id] = true; }
    void setClipboardDirty(ClipboardID id, bool value) override { dirty[id] = value; }
};
void putText(Clipboard& clipboard, const std::string& value)
{
    clipboard.open(0); clipboard.clear(); clipboard.add(IClipboard::kText, value); clipboard.close();
}
std::string readText(const Clipboard& clipboard)
{
    clipboard.open(0); auto value = clipboard.get(IClipboard::kText); clipboard.close(); return value;
}
}

class ServerClipboardTests : public testing::Test {
protected:
    ServerClipboardTests() : timers(events), primary("PC1"), peer("PC2")
    {
        ON_CALL(events, add_event(testing::_)).WillByDefault([](Event&& e) { Event::deleteData(e); });
        config.addScreen("PC1"); config.addScreen("PC2");
        server.m_config = &config;
        server.m_primaryClient = &primary;
        server.m_active = &primary;
        server.m_events = &events;
        server.m_switchScreen = nullptr;
        server.m_enableClipboard = true;
        server.m_maximumClipboardSize = 1024 * 1024;
        server.m_seqNum = 1;
        server.m_clients = {{"PC1", &primary}, {"PC2", &peer}};
        server.m_clientSet = {&primary, &peer};
        putText(primary.local, "new PC1 copy");
        putText(peer.local, "previous PC2 copy");
        for (ClipboardID id = 0; id < kClipboardEnd; ++id) {
            auto& state = server.m_clipboards[id];
            state.m_clipboardOwner = "PC2";
            state.m_clipboardSeqNum = 1;
            putText(state.m_clipboard, "previous PC2 copy");
            state.m_clipboardData = state.m_clipboard.marshall();
        }
    }
    void grab(MemoryPeer& who)
    {
        for (ClipboardID id = 0; id < kClipboardEnd; ++id) {
            IScreen::ClipboardInfo info;
            info.m_id = id; info.m_sequenceNumber = server.m_seqNum;
            Event event(EventType::CLIPBOARD_GRABBED, &who,
                        create_event_data<IScreen::ClipboardInfo>(info));
            server.handle_clipboard_grabbed(event, &who);
            Event::deleteData(event);
        }
    }
    void switchToPeer() { server.switchScreen(&peer, 10, 10, false); }
    void disableSharing() { server.m_enableClipboard = false; }
    testing::NiceMock<MockEventQueue> events;
    ClipboardTimerPump timers;
    Config config;
    MemoryPeer primary, peer;
    Server server;
};

TEST_F(ServerClipboardTests, latePrimaryGrabSendsCopyToAlreadyActivePeer)
{
    switchToPeer();
    grab(primary); // queued ownership notification delivered after leave()
    EXPECT_EQ("new PC1 copy", readText(peer.local));
}

TEST_F(ServerClipboardTests, primaryGrabBeforeSwitchStillSendsCopy)
{
    grab(primary);
    switchToPeer();
    EXPECT_EQ("new PC1 copy", readText(peer.local));
}

TEST_F(ServerClipboardTests, failedPrimaryReadRetriesWithoutAnotherScreenSwitch)
{
    grab(primary);
    primary.readable = false;
    switchToPeer();
    primary.readable = true;
    ASSERT_TRUE(timers.fire());
    EXPECT_EQ("new PC1 copy", readText(peer.local));
    EXPECT_TRUE(timers.empty());
}

TEST_F(ServerClipboardTests, primaryReadRetryDoesNotOverrideNewRemoteOwner)
{
    grab(primary);
    primary.readable = false;
    switchToPeer();
    grab(peer);
    primary.readable = true;
    while (timers.fire()) {}
    EXPECT_NE("new PC1 copy", readText(peer.local));
}

TEST_F(ServerClipboardTests, primaryReadRetriesAreBounded)
{
    grab(primary);
    primary.readable = false;
    switchToPeer();
    int attempts = 0;
    while (attempts < 25 && timers.fire()) ++attempts;
    EXPECT_GT(attempts, 0);
    EXPECT_TRUE(timers.empty());
}

TEST_F(ServerClipboardTests, pendingReadDoesNotSendAfterSharingDisabled)
{
    grab(primary);
    primary.readable = false;
    switchToPeer();
    disableSharing();
    primary.readable = true;
    ASSERT_TRUE(timers.fire());
    EXPECT_NE("new PC1 copy", readText(peer.local));
    EXPECT_TRUE(timers.empty());
}
}
