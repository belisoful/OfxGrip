// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
/*!
	@file  Codec.hpp
	@brief The little-endian byte writer and reader every OfxGrip binary format shares, and the
	       `Value` encoding.

	Introduced in OfxGrip 0.2.0. The render snapshot, the meta store, and the instance state blob
	write the same primitive encodings, so one codec exists for them: unsigned integers in
	little-endian byte order, doubles as their IEEE 754 bits, strings and byte strings with a
	32-bit length prefix. A reader never reads past its input; every read reports failure instead.
*/
#pragma once

#include <ofxgrip/core/Types.hpp>
#include <ofxgrip/core/Value.hpp>

#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <string_view>

OFXGRIP_NAMESPACE_BEGIN

/*! Appends primitives to a byte vector. */
class ByteWriter {
public:
	explicit ByteWriter(Bytes& out) : out_(out) {}
	void u8(std::uint8_t v) { out_.push_back(v); }
	void u16(std::uint16_t v)
	{
		u8(static_cast<std::uint8_t>(v & 0xFF));
		u8(static_cast<std::uint8_t>(v >> 8));
	}
	void u32(std::uint32_t v)
	{
		u16(static_cast<std::uint16_t>(v & 0xFFFF));
		u16(static_cast<std::uint16_t>(v >> 16));
	}
	void i32(std::int32_t v) { u32(static_cast<std::uint32_t>(v)); }
	void f64(double v)
	{
		std::uint64_t bits = 0;
		std::memcpy(&bits, &v, sizeof bits);
		u32(static_cast<std::uint32_t>(bits & 0xFFFFFFFFu));
		u32(static_cast<std::uint32_t>(bits >> 32));
	}
	void str(std::string_view s)
	{
		u32(static_cast<std::uint32_t>(s.size()));
		out_.insert(out_.end(), s.begin(), s.end());
	}
	void bytes(const Bytes& b)
	{
		u32(static_cast<std::uint32_t>(b.size()));
		out_.insert(out_.end(), b.begin(), b.end());
	}
	/*! Appends raw bytes without a length prefix, for magic numbers. */
	void raw(std::string_view s) { out_.insert(out_.end(), s.begin(), s.end()); }

private:
	Bytes& out_;
};

/*! Reads primitives from a byte span. Every read returns false at the end of the input. */
class ByteReader {
public:
	explicit ByteReader(std::span<const std::uint8_t> in) : in_(in) {}
	bool u8(std::uint8_t& v)
	{
		if (pos_ + 1 > in_.size()) {
			return false;
		}
		v = in_[pos_++];
		return true;
	}
	bool u16(std::uint16_t& v)
	{
		std::uint8_t a = 0;
		std::uint8_t b = 0;
		if (!u8(a) || !u8(b)) {
			return false;
		}
		v = static_cast<std::uint16_t>(a | (b << 8));
		return true;
	}
	bool u32(std::uint32_t& v)
	{
		std::uint16_t a = 0;
		std::uint16_t b = 0;
		if (!u16(a) || !u16(b)) {
			return false;
		}
		v = static_cast<std::uint32_t>(a) | (static_cast<std::uint32_t>(b) << 16);
		return true;
	}
	bool i32(std::int32_t& v)
	{
		std::uint32_t u = 0;
		if (!u32(u)) {
			return false;
		}
		v = static_cast<std::int32_t>(u);
		return true;
	}
	bool f64(double& v)
	{
		std::uint32_t lo = 0;
		std::uint32_t hi = 0;
		if (!u32(lo) || !u32(hi)) {
			return false;
		}
		const std::uint64_t bits = static_cast<std::uint64_t>(lo) | (static_cast<std::uint64_t>(hi) << 32);
		std::memcpy(&v, &bits, sizeof v);
		return true;
	}
	bool str(std::string& s)
	{
		std::uint32_t n = 0;
		if (!u32(n) || pos_ + n > in_.size()) {
			return false;
		}
		s.assign(reinterpret_cast<const char*>(in_.data() + pos_), n);
		pos_ += n;
		return true;
	}
	bool bytes(Bytes& b)
	{
		std::uint32_t n = 0;
		if (!u32(n) || pos_ + n > in_.size()) {
			return false;
		}
		b.assign(in_.begin() + static_cast<std::ptrdiff_t>(pos_), in_.begin() + static_cast<std::ptrdiff_t>(pos_ + n));
		pos_ += n;
		return true;
	}
	/*! Consumes `expected` when the next bytes match it. */
	bool magic(std::string_view expected)
	{
		if (pos_ + expected.size() > in_.size()) {
			return false;
		}
		if (std::memcmp(in_.data() + pos_, expected.data(), expected.size()) != 0) {
			return false;
		}
		pos_ += expected.size();
		return true;
	}
	bool done() const noexcept { return pos_ >= in_.size(); }
	std::size_t position() const noexcept { return pos_; }

private:
	std::span<const std::uint8_t> in_;
	std::size_t pos_{0};
};

/*! Writes a value as its kind byte followed by its payload. */
void writeValue(ByteWriter& writer, const Value& value);
/*! Reads a value written by `writeValue`. Returns false for a truncated stream or an unknown kind. */
bool readValue(ByteReader& reader, Value& out);

OFXGRIP_NAMESPACE_END
