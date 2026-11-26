// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
/*!
	@file  UrlWhitelist.hpp
	@brief An ordered allow-list of URL glob patterns, for a control that loads web content.

	Introduced in OfxGrip 0.5.0. Each pattern is a glob: `?` matches one character, `*` matches
	any run of characters, and every other character is literal. Matching ignores ASCII case.
	A URL is allowed when a pattern matches its whole text or a label-boundary suffix of its
	host. The pattern `youtube.com` therefore allows the host `www.youtube.com` and refuses
	`eviltube.com`. An empty list blocks every URL. The single pattern `*` allows every URL.
*/
#pragma once

#include <ofxgrip/core/Error.hpp>
#include <ofxgrip/core/Types.hpp>
#include <ofxgrip/core/Value.hpp>

#include <cstdint>
#include <initializer_list>
#include <span>
#include <string>
#include <string_view>
#include <vector>

OFXGRIP_NAMESPACE_BEGIN

/*! Whether a glob matches the whole of a text, ignoring ASCII case. */
bool globMatches(std::string_view glob, std::string_view text) noexcept;

/*! The host of an absolute URL: the text after `://` and any `user@` part, up to a port, a
    path, a query, or a fragment. An IPv6 literal keeps its brackets. Returns an empty view
    for a URL with no authority. */
std::string_view urlHost(std::string_view url) noexcept;

/*! The allow-list. */
class UrlWhitelist {
public:
	/*! An empty list, which blocks every URL. */
	UrlWhitelist() = default;
	/*! A list of the patterns, in order. Surrounding white space is trimmed, and an empty or
	    repeated pattern is dropped. */
	UrlWhitelist(std::initializer_list<std::string_view> patterns);
	explicit UrlWhitelist(std::span<const std::string> patterns);
	/*! A list holding only `*`. */
	static UrlWhitelist allowAll();
	/*! A list of common video-hosting domains: youtube.com, youtu.be, rumble.com, odysee.com,
	    bitchute.com, brighteon.com, gumroad.com. Each is a bare domain, so its subdomains are
	    included. */
	static UrlWhitelist defaultVideo();

	const std::vector<std::string>& patterns() const noexcept { return patterns_; }
	/*! Appends a pattern. Returns false when the trimmed pattern is empty or present. */
	bool add(std::string_view pattern);
	/*! Removes a pattern. Returns false when it is not present. */
	bool remove(std::string_view pattern);
	/*! Whether the trimmed pattern is present. */
	bool contains(std::string_view pattern) const;
	/*! Replaces the whole list. */
	void setPatterns(std::span<const std::string> patterns);
	/*! Empties the list. */
	void clear() noexcept { patterns_.clear(); }

	/*! Whether a pattern allows the URL. An empty URL, and a URL with white space or a
	    control character, is refused. */
	bool matches(std::string_view url) const noexcept;
	/*! Whether the list holds the pattern `*`. */
	bool allowsAll() const noexcept;

	/*! The list as bytes: the same bytes on every platform. */
	Bytes encode() const;
	/*! Reads a list `encode` wrote. Other bytes leave `out` empty and return an error. */
	static Error decode(std::span<const std::uint8_t> bytes, UrlWhitelist& out);

	bool operator==(const UrlWhitelist&) const = default;

private:
	std::vector<std::string> patterns_;
};

OFXGRIP_NAMESPACE_END
