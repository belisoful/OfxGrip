// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
#include <ofxgrip/core/UrlWhitelist.hpp>

#include <ofxgrip/core/Codec.hpp>

#include <algorithm>

OFXGRIP_NAMESPACE_BEGIN

namespace {

constexpr std::string_view kMagic = "OGUW";
constexpr std::uint32_t kVersion = 1;
constexpr std::uint32_t kMaximumPatterns = 4096;

constexpr char lower(char c) noexcept
{
	return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
}

constexpr bool isSpaceOrControl(char c) noexcept
{
	const unsigned char u = static_cast<unsigned char>(c);
	return u <= 0x20 || u == 0x7F;
}

std::string_view trimmed(std::string_view text) noexcept
{
	while (!text.empty() && isSpaceOrControl(text.front())) {
		text.remove_prefix(1);
	}
	while (!text.empty() && isSpaceOrControl(text.back())) {
		text.remove_suffix(1);
	}
	return text;
}

}  // namespace

bool globMatches(std::string_view glob, std::string_view text) noexcept
{
	std::size_t g = 0;
	std::size_t t = 0;
	std::size_t star = std::string_view::npos;
	std::size_t resume = 0;
	while (t < text.size()) {
		if (g < glob.size() && glob[g] == '*') {
			star = g++;
			resume = t;
		} else if (g < glob.size() && (glob[g] == '?' || lower(glob[g]) == lower(text[t]))) {
			++g;
			++t;
		} else if (star != std::string_view::npos) {
			// The last star takes one more character.
			g = star + 1;
			t = ++resume;
		} else {
			return false;
		}
	}
	while (g < glob.size() && glob[g] == '*') {
		++g;
	}
	return g == glob.size();
}

std::string_view urlHost(std::string_view url) noexcept
{
	const std::size_t scheme = url.find("://");
	if (scheme == std::string_view::npos || scheme == 0) {
		return {};
	}
	// A scheme holds letters, digits, '+', '-', and '.'; anything else puts the "://" in a path.
	for (const char c : url.substr(0, scheme)) {
		const bool letter = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
		const bool other = (c >= '0' && c <= '9') || c == '+' || c == '-' || c == '.';
		if (!letter && !other) {
			return {};
		}
	}
	std::string_view authority = url.substr(scheme + 3);
	// A backslash ends the authority too: browsers read it as a slash.
	authority = authority.substr(0, authority.find_first_of("/?#\\"));
	if (const std::size_t at = authority.rfind('@'); at != std::string_view::npos) {
		authority.remove_prefix(at + 1);
	}
	if (!authority.empty() && authority.front() == '[') {
		const std::size_t close = authority.find(']');
		return close == std::string_view::npos ? std::string_view{} : authority.substr(0, close + 1);
	}
	return authority.substr(0, authority.find(':'));
}

UrlWhitelist::UrlWhitelist(std::initializer_list<std::string_view> patterns)
{
	for (const std::string_view pattern : patterns) {
		add(pattern);
	}
}

UrlWhitelist::UrlWhitelist(std::span<const std::string> patterns)
{
	setPatterns(patterns);
}

UrlWhitelist UrlWhitelist::allowAll()
{
	return UrlWhitelist{"*"};
}

UrlWhitelist UrlWhitelist::defaultVideo()
{
	return UrlWhitelist{"youtube.com", "youtu.be", "rumble.com", "odysee.com", "bitchute.com", "brighteon.com", "gumroad.com"};
}

bool UrlWhitelist::add(std::string_view pattern)
{
	const std::string_view normalized = trimmed(pattern);
	if (normalized.empty() || contains(normalized)) {
		return false;
	}
	patterns_.emplace_back(normalized);
	return true;
}

bool UrlWhitelist::remove(std::string_view pattern)
{
	const std::string_view normalized = trimmed(pattern);
	const auto it = std::find(patterns_.begin(), patterns_.end(), normalized);
	if (it == patterns_.end()) {
		return false;
	}
	patterns_.erase(it);
	return true;
}

bool UrlWhitelist::contains(std::string_view pattern) const
{
	const std::string_view normalized = trimmed(pattern);
	return !normalized.empty() && std::find(patterns_.begin(), patterns_.end(), normalized) != patterns_.end();
}

void UrlWhitelist::setPatterns(std::span<const std::string> patterns)
{
	patterns_.clear();
	for (const std::string& pattern : patterns) {
		add(pattern);
	}
}

bool UrlWhitelist::matches(std::string_view url) const noexcept
{
	if (url.empty() || std::any_of(url.begin(), url.end(), isSpaceOrControl)) {
		return false;
	}
	const std::string_view host = urlHost(url);
	for (const std::string& pattern : patterns_) {
		if (globMatches(pattern, url)) {
			return true;
		}
		// The label-boundary suffixes of the host: www.youtube.com, youtube.com, com.
		std::string_view suffix = host;
		while (!suffix.empty()) {
			if (globMatches(pattern, suffix)) {
				return true;
			}
			const std::size_t dot = suffix.find('.');
			if (dot == std::string_view::npos) {
				break;
			}
			suffix.remove_prefix(dot + 1);
		}
	}
	return false;
}

bool UrlWhitelist::allowsAll() const noexcept
{
	return std::find(patterns_.begin(), patterns_.end(), "*") != patterns_.end();
}

Bytes UrlWhitelist::encode() const
{
	Bytes out;
	ByteWriter w(out);
	w.raw(kMagic);
	w.u32(kVersion);
	w.u32(static_cast<std::uint32_t>(patterns_.size()));
	for (const std::string& pattern : patterns_) {
		w.str(pattern);
	}
	return out;
}

Error UrlWhitelist::decode(std::span<const std::uint8_t> bytes, UrlWhitelist& out)
{
	out = UrlWhitelist{};
	const auto malformed = []() { return Error::make(ErrorCode::Codec, "The bytes are not a URL whitelist"); };
	ByteReader r(bytes);
	std::uint32_t version = 0;
	std::uint32_t count = 0;
	if (!r.magic(kMagic) || !r.u32(version) || !r.u32(count)) {
		return malformed();
	}
	if (version != kVersion) {
		return Error::make(ErrorCode::Codec, "The URL whitelist has a version this build does not read").with("version", std::to_string(version));
	}
	if (count > kMaximumPatterns) {
		return malformed();
	}
	UrlWhitelist decoded;
	for (std::uint32_t i = 0; i < count; ++i) {
		std::string pattern;
		if (!r.str(pattern)) {
			return malformed();
		}
		decoded.add(pattern);
	}
	out = std::move(decoded);
	return Error::none();
}

OFXGRIP_NAMESPACE_END
