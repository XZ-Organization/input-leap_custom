// Clipboard regressions run in a private, non-interactive window station.
// Never use the user's clipboard, even when a test fails.
#include "platform/MSWindowsScreen.h"
#include "platform/MSWindowsClipboard.h"
#include "inputleap/Clipboard.h"
#include "inputleap/Screen.h"
#include "inputleap/option_types.h"
#include "server/PrimaryClient.h"
#include "test/mock/inputleap/MockEventQueue.h"
#include <gtest/gtest.h>
#include <functional>
#include <thread>
#include <future>
#include "test/mock/inputleap/ClipboardTimerPump.h"

namespace inputleap {
namespace {

void withIsolatedClipboard(const std::function<void()>& test)
{
    HWINSTA original = GetProcessWindowStation();
    HWINSTA station = CreateWindowStationW(nullptr, 0, WINSTA_ALL_ACCESS, nullptr);
    ASSERT_NE(nullptr, station);
    if (!SetProcessWindowStation(station)) {
        CloseWindowStation(station);
        FAIL() << "Cannot isolate clipboard";
    }
    HDESK desktop = CreateDesktopW(L"ClipboardTests", nullptr, nullptr, 0,
                                  GENERIC_ALL, nullptr);
    if (desktop != nullptr) {
        std::thread worker([&]() {
            ASSERT_TRUE(SetThreadDesktop(desktop));
            test();
        });
        worker.join();
    }
    EXPECT_TRUE(SetProcessWindowStation(original));
    if (desktop != nullptr) {
        CloseDesktop(desktop);
    }
    CloseWindowStation(station);
    ASSERT_NE(nullptr, desktop);
}

class ClipboardScreen {
public:
    ClipboardScreen()
    {
        if (MSWindowsScreen::getWindowInstance() == nullptr) {
            MSWindowsScreen::init(GetModuleHandle(nullptr));
        }
        screen = std::make_unique<MSWindowsScreen>(true, true, false, &events);
        window = CreateWindowExW(0, L"STATIC", L"clipboard test owner", 0,
                                 0, 0, 0, 0, HWND_MESSAGE, nullptr, nullptr, nullptr);
    }
    ~ClipboardScreen()
    {
        DestroyWindow(window);
        screen.reset();
        // The production process creates one screen. Its destructor tries
        // to unregister the class before destroying the drop window.
        UnregisterClassA("InputLeap", GetModuleHandle(nullptr));
    }

    void copyLocally(const std::string& text)
    {
        ASSERT_NE(nullptr, window);
        MSWindowsClipboard clipboard(window);
        ASSERT_TRUE(clipboard.open(0));
        EXPECT_TRUE(clipboard.emptyUnowned());
        clipboard.add(IClipboard::kText, text);
        clipboard.close();
    }

    std::string text()
    {
        Clipboard clipboard;
        if (!screen->getClipboard(kClipboardClipboard, &clipboard)) {
            ADD_FAILURE() << "Cannot read isolated clipboard";
            return {};
        }
        clipboard.open(0);
        auto result = clipboard.get(IClipboard::kText);
        clipboard.close();
        return result;
    }

    testing::NiceMock<MockEventQueue> events;
    std::unique_ptr<MSWindowsScreen> screen;
    HWND window;
};

class ClipboardLock {
public:
    ClipboardLock() : releaseFuture(release.get_future()), worker([this]() {
        bool ok = OpenClipboard(nullptr) != FALSE;
        acquired.set_value(ok);
        releaseFuture.wait();
        if (ok) CloseClipboard();
    }) { EXPECT_TRUE(acquired.get_future().get()); }
    ~ClipboardLock() { release.set_value(); worker.join(); }
private:
    std::promise<bool> acquired;
    std::promise<void> release;
    std::future<void> releaseFuture;
    std::thread worker;
};

Clipboard remoteText(const std::string& text)
{
    Clipboard data;
    data.open(0); data.add(IClipboard::kText, text); data.close();
    return data;
}
}

TEST(MSWindowsClipboardSafetyTests, primaryDoesNotReplayFinishedAsyncDelivery)
{
    // Successful, superseded and exhausted retries all consume one delivery.
    for (int outcome = 0; outcome < 3; ++outcome) {
        withIsolatedClipboard([outcome]() {
            SCOPED_TRACE(outcome);
            ClipboardScreen fixture;
            ClipboardTimerPump timers(fixture.events);
            auto* platform = fixture.screen.get();
            Screen screen(std::move(fixture.screen), &fixture.events);
            PrimaryClient primary("primary", &screen);
            auto remote = remoteText("remote B");
            primary.setClipboardDirty(kClipboardClipboard, true);
            {
                ClipboardLock lock;
                primary.setClipboard(kClipboardClipboard, &remote);
                if (outcome == 2) {
                    for (int i = 0; i < 25 && timers.fire(); ++i) {}
                }
            }
            if (outcome == 0) ASSERT_TRUE(timers.fire());
            fixture.copyLocally("new local C");
            if (outcome == 1) ASSERT_TRUE(timers.fire());
            EXPECT_TRUE(timers.empty());
            primary.setClipboard(kClipboardClipboard, &remote);
            Clipboard actual;
            ASSERT_TRUE(platform->getClipboard(kClipboardClipboard, &actual));
            actual.open(0);
            EXPECT_EQ("new local C", actual.get(IClipboard::kText));
            actual.close();
            auto next = remoteText("next remote D");
            primary.setClipboardDirty(kClipboardClipboard, true);
            primary.setClipboard(kClipboardClipboard, &next);
            ASSERT_TRUE(platform->getClipboard(kClipboardClipboard, &actual));
            actual.open(0);
            EXPECT_EQ("next remote D", actual.get(IClipboard::kText));
            actual.close();
        });
    }
}

TEST(MSWindowsClipboardSafetyTests, sharingOptionsCancelPendingApply)
{
    for (auto option : {kOptionClipboardSharing, kOptionClipboardSharingSize}) {
        withIsolatedClipboard([option]() {
            SCOPED_TRACE(option);
            ClipboardScreen fixture;
            ClipboardTimerPump timers(fixture.events);
            auto* platform = fixture.screen.get();
            fixture.copyLocally("local");
            Screen screen(std::move(fixture.screen), &fixture.events);
            auto remote = remoteText("pending remote");
            {
                ClipboardLock lock;
                screen.setClipboard(kClipboardClipboard, &remote);
                // Unrelated options must not discard a pending delivery.
                screen.setOptions(OptionsList{kOptionScreenSaverSync, 0});
                EXPECT_FALSE(timers.empty());
                screen.setOptions(OptionsList{option, 0});
            }
            EXPECT_FALSE(timers.fire());
            EXPECT_TRUE(timers.empty());
            Clipboard actual;
            ASSERT_TRUE(platform->getClipboard(kClipboardClipboard, &actual));
            actual.open(0);
            EXPECT_EQ("local", actual.get(IClipboard::kText));
            actual.close();
            screen.setOptions(OptionsList{kOptionClipboardSharing, 1,
                                         kOptionClipboardSharingSize, 1024});
            auto next = remoteText("fresh remote");
            { ClipboardLock lock; screen.setClipboard(kClipboardClipboard, &next); }
            ASSERT_TRUE(timers.fire());
            ASSERT_TRUE(platform->getClipboard(kClipboardClipboard, &actual));
            actual.open(0);
            EXPECT_EQ("fresh remote", actual.get(IClipboard::kText));
            actual.close();
        });
    }
}

TEST(MSWindowsClipboardSafetyTests, lockedApplyRetriesAfterUnlock)
{
    withIsolatedClipboard([]() {
        ClipboardScreen fixture;
        ClipboardTimerPump timers(fixture.events);
        fixture.copyLocally("old");
        auto remote = remoteText("new");
        {
            ClipboardLock lock;
            EXPECT_FALSE(fixture.screen->setClipboard(kClipboardClipboard, &remote));
            EXPECT_FALSE(fixture.screen->setClipboard(kClipboardSelection, &remote));
        }
        ASSERT_TRUE(timers.fire());
        EXPECT_EQ("new", fixture.text());
        EXPECT_TRUE(timers.empty());
    });
}

TEST(MSWindowsClipboardSafetyTests, retryPreservesNewerLocalCopyEvenWithoutNotification)
{
    withIsolatedClipboard([]() {
        ClipboardScreen fixture;
        ClipboardTimerPump timers(fixture.events);
        auto remote = remoteText("old remote");
        { ClipboardLock lock; fixture.screen->setClipboard(kClipboardClipboard, &remote); }
        fixture.copyLocally("new local");
        ASSERT_TRUE(timers.fire());
        EXPECT_EQ("new local", fixture.text());
        EXPECT_TRUE(timers.empty());
    });
}

TEST(MSWindowsClipboardSafetyTests, newestRemotePayloadReplacesPendingRetry)
{
    withIsolatedClipboard([]() {
        ClipboardScreen fixture;
        ClipboardTimerPump timers(fixture.events);
        auto first = remoteText("first"), latest = remoteText("latest");
        {
            ClipboardLock lock;
            fixture.screen->setClipboard(kClipboardClipboard, &first);
            fixture.screen->setClipboard(kClipboardClipboard, &latest);
        }
        ASSERT_TRUE(timers.fire());
        EXPECT_EQ("latest", fixture.text());
        EXPECT_TRUE(timers.empty());
    });
}

TEST(MSWindowsClipboardSafetyTests, applyRetriesStopAfterBudgetAndOnRemoteGrab)
{
    withIsolatedClipboard([]() {
        ClipboardScreen fixture;
        ClipboardTimerPump timers(fixture.events);
        auto remote = remoteText("remote");
        ClipboardLock lock;
        fixture.screen->setClipboard(kClipboardClipboard, &remote);
        int attempts = 0;
        while (attempts < 25 && timers.fire()) ++attempts;
        EXPECT_GT(attempts, 0);
        EXPECT_TRUE(timers.empty());
        fixture.screen->setClipboard(kClipboardClipboard, &remote);
        fixture.screen->setClipboard(kClipboardClipboard, nullptr);
        EXPECT_TRUE(timers.empty());
    });
}

TEST(MSWindowsClipboardSafetyTests, writeFailureIsReportedAndOwnSequenceAllowsRetry)
{
    withIsolatedClipboard([]() {
        ClipboardScreen fixture;
        fixture.copyLocally("old");
        auto remote = remoteText("new");
        class FailOnceFacade : public MSWindowsClipboardFacade {
        public:
            bool write(HANDLE data, UINT format) override {
                if (fail) { fail = false; GlobalFree(data); return false; }
                return MSWindowsClipboardFacade::write(data, format);
            }
            bool fail = true;
        } facade;
        MSWindowsClipboard clipboard(fixture.window);
        clipboard.setFacade(facade);
        DWORD sequence = GetClipboardSequenceNumber();
        EXPECT_EQ(MSWindowsClipboard::CopyResult::Unavailable, clipboard.copyFrom(remote, &sequence));
        EXPECT_EQ(MSWindowsClipboard::CopyResult::Success, clipboard.copyFrom(remote, &sequence));
        EXPECT_EQ("new", fixture.text());
    });
}

TEST(MSWindowsClipboardSafetyTests, disableCancelsPendingClipboardApply)
{
    withIsolatedClipboard([]() {
        ClipboardScreen fixture;
        ClipboardTimerPump timers(fixture.events);
        auto remote = remoteText("remote");
        { ClipboardLock lock; fixture.screen->setClipboard(kClipboardClipboard, &remote); }
        EXPECT_FALSE(timers.empty());
        fixture.screen->disable();
        EXPECT_TRUE(timers.empty());
    });
}

TEST(MSWindowsClipboardSafetyTests, grabPreservesLocalContents)
{
    withIsolatedClipboard([]() {
        ClipboardScreen fixture;
        fixture.copyLocally("keep this copy");
        ASSERT_TRUE(fixture.screen->setClipboard(kClipboardClipboard, nullptr));
        EXPECT_EQ("keep this copy", fixture.text());
    });
}

TEST(MSWindowsClipboardSafetyTests, lockedReadReportsFailureWithoutClearingDestination)
{
    withIsolatedClipboard([]() {
        ClipboardScreen fixture;
        Clipboard destination;
        destination.open(0);
        destination.add(IClipboard::kText, "previous copy");
        destination.close();
        ASSERT_TRUE(OpenClipboard(fixture.window));
        std::thread reader([&]() {
            EXPECT_FALSE(fixture.screen->getClipboard(kClipboardClipboard, &destination));
        });
        reader.join();
        CloseClipboard();
        destination.open(0);
        EXPECT_EQ("previous copy", destination.get(IClipboard::kText));
        destination.close();
    });
}

TEST(MSWindowsClipboardSafetyTests, emptyOrUnsupportedTransferPreservesLocalContents)
{
    withIsolatedClipboard([]() {
        ClipboardScreen fixture;
        fixture.copyLocally("keep this copy");
        Clipboard remote;
        fixture.screen->setClipboard(kClipboardClipboard, &remote);
        EXPECT_EQ("keep this copy", fixture.text());
        remote.open(0);
        remote.add(IClipboard::kPNG, "unsupported on Windows");
        remote.close();
        fixture.screen->setClipboard(kClipboardClipboard, &remote);
        EXPECT_EQ("keep this copy", fixture.text());
    });
}

TEST(MSWindowsClipboardSafetyTests, eachLocalCopyAfterGrabIsDetectedButRemoteCopyIsNotEchoed)
{
    withIsolatedClipboard([]() {
        ClipboardScreen fixture;
        fixture.copyLocally("initial");
        fixture.screen->setClipboard(kClipboardClipboard, nullptr);
        int notifications = 0;
        EXPECT_CALL(fixture.events, add_event(testing::_))
            .WillRepeatedly([&](Event&& event) {
                if (event.getType() == EventType::CLIPBOARD_GRABBED) {
                    ++notifications;
                }
            });
        fixture.screen->checkClipboards();
        EXPECT_EQ(0, notifications);
        fixture.copyLocally("first");
        fixture.screen->checkClipboards();
        EXPECT_EQ(2, notifications);
        fixture.screen->checkClipboards();
        EXPECT_EQ(2, notifications);
        fixture.copyLocally("second");
        fixture.screen->checkClipboards();
        EXPECT_EQ(4, notifications);
        Clipboard remote;
        remote.open(0);
        remote.add(IClipboard::kText, "remote");
        remote.close();
        EXPECT_TRUE(fixture.screen->setClipboard(kClipboardClipboard, &remote));
        fixture.screen->checkClipboards();
        EXPECT_EQ(4, notifications);
        EXPECT_EQ("remote", fixture.text());
    });
}

} // namespace inputleap
