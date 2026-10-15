// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
/*!
	@file  Digest.hpp
	@brief SHA-256 and HMAC-SHA256, for license files and other signed records.

	Introduced in OfxGrip 0.4.0. FIPS 180-4 and RFC 2104, implemented here so the core depends on
	no crypto library. The functions are deterministic on every platform and are tested against
	the published vectors.
*/
#pragma once

#include <ofxgrip/core/Types.hpp>
#include <ofxgrip/core/Value.hpp>

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

OFXGRIP_NAMESPACE_BEGIN

/*! A SHA-256 digest. */
using Sha256Digest = std::array<std::uint8_t, 32>;

/*! The SHA-256 of a byte span. */
Sha256Digest sha256(std::span<const std::uint8_t> data) noexcept;
/*! The SHA-256 of a string's bytes. */
Sha256Digest sha256(std::string_view text) noexcept;
/*! The HMAC-SHA256 of a message under a key. */
Sha256Digest hmacSha256(std::span<const std::uint8_t> key, std::span<const std::uint8_t> message) noexcept;
Sha256Digest hmacSha256(std::string_view key, std::string_view message) noexcept;
/*! The lower-case hexadecimal text of a digest. */
std::string hexDigest(const Sha256Digest& digest);
/*! Whether two digests match, compared in constant time. */
bool digestsEqual(const Sha256Digest& a, const Sha256Digest& b) noexcept;
/*! Parses hexadecimal text into a digest. Returns false for anything but 64 hex digits. */
bool parseHexDigest(std::string_view text, Sha256Digest& out) noexcept;

OFXGRIP_NAMESPACE_END
