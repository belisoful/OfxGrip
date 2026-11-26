// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
// The glob matcher, the host of a URL, the allow-list's editing and matching, and its codec.
#include <ofxgrip/core/UrlWhitelist.hpp>

#include <gtest/gtest.h>

using namespace ofxgrip;

TEST(UrlWhitelist, GlobMatching)
{
	EXPECT_TRUE(globMatches("", ""));
	EXPECT_FALSE(globMatches("", "a"));
	EXPECT_TRUE(globMatches("*", ""));
	EXPECT_TRUE(globMatches("*", "anything at all"));
	EXPECT_TRUE(globMatches("a?c", "abc"));
	EXPECT_FALSE(globMatches("a?c", "ac"));
	EXPECT_FALSE(globMatches("a?c", "abbc"));
	EXPECT_TRUE(globMatches("a*c", "ac"));
	EXPECT_TRUE(globMatches("a*c", "abbbc"));
	EXPECT_FALSE(globMatches("a*c", "abbbd"));
	EXPECT_TRUE(globMatches("*.example.com", "www.example.com"));
	EXPECT_FALSE(globMatches("*.example.com", "example.com"));
	EXPECT_TRUE(globMatches("https://*.example.com/*", "https://cdn.example.com/a/b?c=d"));
	EXPECT_TRUE(globMatches("a*b*c", "a--b--b--c"));
	EXPECT_FALSE(globMatches("a*b*c", "a--b--b--"));
	EXPECT_TRUE(globMatches("**a**", "bab"));
	// Case is ignored for ASCII letters, and every other character is literal.
	EXPECT_TRUE(globMatches("YouTube.COM", "youtube.com"));
	EXPECT_FALSE(globMatches("a.c", "abc"));
	EXPECT_TRUE(globMatches("a[b]+(c)", "a[b]+(c)"));
	// A long run of stars stays fast.
	EXPECT_FALSE(globMatches("*a*a*a*a*a*a*a*a*b", std::string(2000, 'a')));
}

TEST(UrlWhitelist, HostOfAUrl)
{
	EXPECT_EQ(urlHost("https://www.youtube.com/watch?v=1"), "www.youtube.com");
	EXPECT_EQ(urlHost("https://youtube.com"), "youtube.com");
	EXPECT_EQ(urlHost("http://example.com:8080/a"), "example.com");
	EXPECT_EQ(urlHost("https://example.com?x=1"), "example.com");
	EXPECT_EQ(urlHost("https://example.com#top"), "example.com");
	// The host is what follows the last '@': a user name cannot pose as the host.
	EXPECT_EQ(urlHost("https://youtube.com@evil.example/"), "evil.example");
	EXPECT_EQ(urlHost("https://user:pass@example.com:443/"), "example.com");
	EXPECT_EQ(urlHost("https://youtube.com\\@evil.example/"), "youtube.com");
	EXPECT_EQ(urlHost("https://[2001:db8::1]:8443/x"), "[2001:db8::1]");
	EXPECT_EQ(urlHost("https://[2001:db8::1/x"), "");
	EXPECT_EQ(urlHost("file:///Users/a/b.html"), "");
	EXPECT_EQ(urlHost("youtube.com/watch"), "");
	EXPECT_EQ(urlHost("mailto:someone@example.com"), "");
	EXPECT_EQ(urlHost("://example.com"), "");
	EXPECT_EQ(urlHost("/path?next=https://example.com"), "");
	EXPECT_EQ(urlHost(""), "");
}

TEST(UrlWhitelist, EditsTheList)
{
	UrlWhitelist list;
	EXPECT_TRUE(list.patterns().empty());
	EXPECT_TRUE(list.add("  youtube.com \n"));
	EXPECT_FALSE(list.add("youtube.com"));
	EXPECT_FALSE(list.add("   "));
	EXPECT_TRUE(list.add("*.example.com"));
	EXPECT_EQ(list.patterns(), (std::vector<std::string>{"youtube.com", "*.example.com"}));
	EXPECT_TRUE(list.contains(" youtube.com"));
	EXPECT_FALSE(list.contains("vimeo.com"));
	EXPECT_FALSE(list.contains(""));
	EXPECT_TRUE(list.remove("youtube.com "));
	EXPECT_FALSE(list.remove("youtube.com"));
	const std::vector<std::string> replacement{"a.com", "", "b.com", "a.com"};
	list.setPatterns(replacement);
	EXPECT_EQ(list.patterns(), (std::vector<std::string>{"a.com", "b.com"}));
	EXPECT_EQ(UrlWhitelist(replacement), list);
	list.clear();
	EXPECT_EQ(list, UrlWhitelist());
	EXPECT_EQ(UrlWhitelist::defaultVideo().patterns().size(), 7u);
	EXPECT_TRUE(UrlWhitelist::defaultVideo().contains("youtu.be"));
}

TEST(UrlWhitelist, AllowsByWholeTextOrHostSuffix)
{
	const UrlWhitelist video = UrlWhitelist::defaultVideo();
	EXPECT_TRUE(video.matches("https://youtube.com/watch?v=1"));
	EXPECT_TRUE(video.matches("https://www.youtube.com/watch?v=1"));
	EXPECT_TRUE(video.matches("https://M.YouTube.com/"));
	EXPECT_TRUE(video.matches("https://youtu.be/abc"));
	// A look-alike host is refused, and so is a host that only starts with the domain.
	EXPECT_FALSE(video.matches("https://eviltube.com/"));
	EXPECT_FALSE(video.matches("https://notyoutube.com/"));
	EXPECT_FALSE(video.matches("https://youtube.com.evil.example/"));
	EXPECT_FALSE(video.matches("https://youtube.com@evil.example/"));
	EXPECT_FALSE(video.matches("https://evil.example/?next=youtube.com"));
	EXPECT_FALSE(video.matches("youtube.com/watch"));
	EXPECT_FALSE(video.matches(""));
	EXPECT_FALSE(video.matches("https://youtube.com/a b"));
	EXPECT_FALSE(video.matches("https://youtube.com/\n"));
	EXPECT_FALSE(video.allowsAll());

	EXPECT_FALSE(UrlWhitelist().matches("https://youtube.com/"));
	const UrlWhitelist all = UrlWhitelist::allowAll();
	EXPECT_TRUE(all.allowsAll());
	EXPECT_TRUE(all.matches("https://anything.example/x"));
	EXPECT_TRUE(all.matches("file:///a/b.html"));
	EXPECT_FALSE(all.matches(""));

	// A pattern with a scheme matches the whole text.
	const UrlWhitelist exact{"https://docs.example.com/*", "*.cdn.example"};
	EXPECT_TRUE(exact.matches("https://docs.example.com/guide"));
	EXPECT_FALSE(exact.matches("http://docs.example.com/guide"));
	EXPECT_TRUE(exact.matches("https://a.cdn.example/file.js"));
	EXPECT_FALSE(exact.matches("https://cdn.example/file.js"));
	// A top-level label alone is a suffix too.
	EXPECT_TRUE(UrlWhitelist{"example"}.matches("https://a.b.example/"));
}

TEST(UrlWhitelist, Codec)
{
	const UrlWhitelist list{"youtube.com", "https://docs.example.com/*"};
	const Bytes bytes = list.encode();
	UrlWhitelist decoded;
	Error e = UrlWhitelist::decode(bytes, decoded);
	ASSERT_TRUE(e.ok()) << e.describe();
	EXPECT_EQ(decoded, list);
	e = UrlWhitelist::decode(UrlWhitelist().encode(), decoded);
	ASSERT_TRUE(e.ok()) << e.describe();
	EXPECT_TRUE(decoded.patterns().empty());
	decoded = list;
	for (std::size_t length = 0; length < bytes.size(); ++length) {
		EXPECT_EQ(UrlWhitelist::decode(std::span<const std::uint8_t>(bytes.data(), length), decoded).code(), ErrorCode::Codec) << "length " << length;
		EXPECT_TRUE(decoded.patterns().empty());
	}
	Bytes future = bytes;
	future[4] = 2;
	EXPECT_EQ(UrlWhitelist::decode(future, decoded).code(), ErrorCode::Codec);
	Bytes tooMany = UrlWhitelist().encode();
	tooMany[10] = 0xFF;
	EXPECT_EQ(UrlWhitelist::decode(tooMany, decoded).code(), ErrorCode::Codec);
}
