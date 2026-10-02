// Clipboard regressions run in a private, non-interactive window station.
// Never use the user's clipboard, even when a test fails.
#include "platform/MSWindowsScreen.h"
#include "platform/MSWindowsClipboard.h"
#include "inputleap/Clipboard.h"
#include "test/mock/inputleap/MockEventQueue.h"
#include <gtest/gtest.h>
#include <functional>
#include <thread>

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
