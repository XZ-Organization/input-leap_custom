#include "platform/MSWindowsClipboardHTMLConverter.h"
#include <gtest/gtest.h>
#include <cstring>

namespace inputleap {

TEST(MSWindowsClipboardHTMLSafetyTests, outOfBoundsFragmentIsRejectedWithoutThrowing)
{
    MSWindowsClipboardHTMLConverter converter;
    for (const auto& html : {
             "StartFragment:999\r\nEndFragment:1000\r\nx",
             "StartFragment:40\r\nEndFragment:999\r\nx",
             "StartFragment:999999999999999999999999\r\nEndFragment:9999999999999999999999999\r\nx"}) {
        const auto size = std::strlen(html) + 1;
        HGLOBAL data = GlobalAlloc(GMEM_MOVEABLE, size);
        ASSERT_NE(nullptr, data);
        void* memory = GlobalLock(data);
        ASSERT_NE(nullptr, memory);
        std::memcpy(memory, html, size);
        GlobalUnlock(data);
        EXPECT_NO_THROW(EXPECT_TRUE(converter.toIClipboard(data).empty()));
        GlobalFree(data);
    }
}

TEST(MSWindowsClipboardHTMLSafetyTests, formattedUnicodeAndNewlinesRoundTrip)
{
    MSWindowsClipboardHTMLConverter converter;
    const std::string html = u8"<b>\uD55C\uAE00</b>\n<p>second line</p>";
    HGLOBAL data = converter.fromIClipboard(html);
    ASSERT_NE(nullptr, data);
    EXPECT_EQ(html, converter.toIClipboard(data));
    GlobalFree(data);
}
} // namespace inputleap
