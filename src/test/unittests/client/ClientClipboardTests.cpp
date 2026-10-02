#include "client/Client.h"
#include "client/ServerProxy.h"
#include "inputleap/ClipboardChunk.h"
#include "inputleap/protocol_types.h"
#include "net/ISocketFactory.h"
#include "net/IDataSocket.h"
#include "net/IListenSocket.h"
#include "io/IStream.h"
#include "test/mock/inputleap/MockEventQueue.h"
#include "test/mock/inputleap/MockScreen.h"
#include <gtest/gtest.h>

namespace inputleap {
namespace {
class UnusedSocketFactory : public ISocketFactory {
public:
    std::unique_ptr<IDataSocket> create(IArchNetwork::EAddressFamily,
                                       ConnectionSecurityLevel) const override { return {}; }
    std::unique_ptr<IListenSocket> create_listen(IArchNetwork::EAddressFamily,
                                                ConnectionSecurityLevel) const override { return {}; }
};

class UnusedStream : public IStream, public EventTarget {
public:
    const EventTarget* get_event_target() const override { return this; }
    void close() override {}
    std::uint32_t read(void*, std::uint32_t) override { return 0; }
    void write(const void*, std::uint32_t) override {}
    void flush() override {}
    void shutdownInput() override {}
    void shutdownOutput() override {}
    bool isReady() const override { return true; }
    std::uint32_t getSize() const override { return 0; }
};
}

class ClientClipboardTests : public testing::Test {
protected:
    void verifyFailedReadCanBeRetried()
    {
        testing::NiceMock<MockEventQueue> events;
        testing::NiceMock<MockScreen> screen;
        UnusedSocketFactory factory;
        UnusedStream stream;
        ClientArgs args;
        Client client(&events, "test", NetworkAddress(), &factory, &screen, args);
        client.m_mock = true;
        ServerProxy server(&client, &stream, &events);
        client.m_server = &server;
        client.m_timeClipboard[kClipboardClipboard] = 0;
        client.m_sentClipboard[kClipboardClipboard] = false;
        client.m_dataClipboard[kClipboardClipboard] = "previous transfer";

        EXPECT_CALL(screen, getClipboard(kClipboardClipboard, testing::_))
            .WillOnce(testing::Return(false));
        EXPECT_CALL(events, add_event(testing::_)).Times(0);
        client.sendClipboard(kClipboardClipboard);
        EXPECT_FALSE(client.m_sentClipboard[kClipboardClipboard]);
        EXPECT_EQ(0, client.m_timeClipboard[kClipboardClipboard]);
        EXPECT_EQ("previous transfer", client.m_dataClipboard[kClipboardClipboard]);
        testing::Mock::VerifyAndClearExpectations(&events);

        EXPECT_CALL(screen, getClipboard(kClipboardClipboard, testing::_))
            .WillOnce([](ClipboardID, IClipboard* clipboard) {
                clipboard->open(1);
                clipboard->clear();
                clipboard->add(IClipboard::kText, "new copy");
                clipboard->close();
                return true;
            });
        std::string transmitted;
        EXPECT_CALL(events, add_event(testing::_)).WillRepeatedly([&](Event&& event) {
            if (event.getType() == EventType::CLIPBOARD_SENDING) {
                const auto& chunk = event.get_data_as<ClipboardChunk>();
                if (chunk.mark_ == kDataChunk) {
                    transmitted += chunk.data_;
                }
            }
        });
        client.sendClipboard(kClipboardClipboard);
        EXPECT_TRUE(client.m_sentClipboard[kClipboardClipboard]);
        Clipboard received;
        ASSERT_TRUE(received.unmarshall(transmitted, 0));
        received.open(0);
        EXPECT_EQ("new copy", received.get(IClipboard::kText));
        received.close();
    }
};

TEST_F(ClientClipboardTests, failedReadDoesNotSendOrConsumeNextTransfer)
{
    verifyFailedReadCanBeRetried();
}
} // namespace inputleap
