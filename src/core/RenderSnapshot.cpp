// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
#include <ofxgrip/core/RenderSnapshot.hpp>

#include <ofxgrip/core/Codec.hpp>

#include <cstring>

OFXGRIP_NAMESPACE_BEGIN

namespace {

// Stream layout (little-endian):
//   magic "OGSN", u16 codec version, u16 reserved
//   u8 section tags until End:
//     Info(1):    the RenderInfo fields in declaration order
//     License(2): u8
//     Value(3):   u32 parameter id, u8 kind, payload
//     Extra(4):   u16 key length, key bytes, u32 data length, data
//     End(0)
constexpr char kMagic[4] = {'O', 'G', 'S', 'N'};
enum Tag : std::uint8_t { TagEnd = 0, TagInfo = 1, TagLicense = 2, TagValue = 3, TagExtra = 4 };

}  // namespace

const Value* RenderSnapshot::value(ParameterId id) const noexcept
{
	auto it = values_.find(id);
	return it == values_.end() ? nullptr : &it->second;
}

double RenderSnapshot::number(ParameterId id, double fallback) const noexcept
{
	const Value* v = value(id);
	return v == nullptr ? fallback : v->toDouble(fallback);
}

int RenderSnapshot::integer(ParameterId id, int fallback) const noexcept
{
	const Value* v = value(id);
	return v == nullptr ? fallback : v->toInt(fallback);
}

bool RenderSnapshot::boolean(ParameterId id, bool fallback) const noexcept
{
	const Value* v = value(id);
	return v == nullptr ? fallback : v->toBool(fallback);
}

Point2D RenderSnapshot::point(ParameterId id, Point2D fallback) const noexcept
{
	const Value* v = value(id);
	if (const Point2D* p = v == nullptr ? nullptr : v->tryAs<Point2D>()) {
		return *p;
	}
	return fallback;
}

Point3D RenderSnapshot::point3D(ParameterId id, Point3D fallback) const noexcept
{
	const Value* v = value(id);
	if (const Point3D* p = v == nullptr ? nullptr : v->tryAs<Point3D>()) {
		return *p;
	}
	return fallback;
}

RGB RenderSnapshot::rgb(ParameterId id, RGB fallback) const noexcept
{
	const Value* v = value(id);
	if (v == nullptr) {
		return fallback;
	}
	if (const RGB* c = v->tryAs<RGB>()) {
		return *c;
	}
	if (const RGBA* c = v->tryAs<RGBA>()) {
		return {c->r, c->g, c->b};
	}
	return fallback;
}

RGBA RenderSnapshot::rgba(ParameterId id, RGBA fallback) const noexcept
{
	const Value* v = value(id);
	if (v == nullptr) {
		return fallback;
	}
	if (const RGBA* c = v->tryAs<RGBA>()) {
		return *c;
	}
	if (const RGB* c = v->tryAs<RGB>()) {
		return {c->r, c->g, c->b, 1.0};
	}
	return fallback;
}

std::string RenderSnapshot::string(ParameterId id, std::string fallback) const
{
	const Value* v = value(id);
	if (const std::string* s = v == nullptr ? nullptr : v->tryAs<std::string>()) {
		return *s;
	}
	return fallback;
}

const Bytes* RenderSnapshot::extra(std::string_view key) const noexcept
{
	auto it = extras_.find(std::string(key));
	return it == extras_.end() ? nullptr : &it->second;
}

Bytes RenderSnapshot::encode() const
{
	Bytes out;
	ByteWriter w(out);
	out.insert(out.end(), kMagic, kMagic + 4);
	w.u16(kCodecVersion);
	w.u16(0);

	w.u8(TagInfo);
	w.f64(info_.time);
	w.f64(info_.frameRate);
	w.f64(info_.renderScale.x);
	w.f64(info_.renderScale.y);
	w.u8(static_cast<std::uint8_t>(info_.field));
	w.u8(info_.draft ? 1 : 0);
	w.u8(info_.interactive ? 1 : 0);
	w.u8(info_.sequential ? 1 : 0);
	w.i32(info_.renderWindow.x1);
	w.i32(info_.renderWindow.y1);
	w.i32(info_.renderWindow.x2);
	w.i32(info_.renderWindow.y2);
	w.f64(info_.projectSize.x);
	w.f64(info_.projectSize.y);
	w.f64(info_.projectOffset.x);
	w.f64(info_.projectOffset.y);
	w.f64(info_.pixelAspect);
	w.str(info_.colourspace);
	w.str(info_.resolvePage);

	w.u8(TagLicense);
	w.u8(static_cast<std::uint8_t>(license_));

	for (const auto& [id, value] : values_) {
		w.u8(TagValue);
		w.u32(id);
		writeValue(w, value);
	}
	for (const auto& [key, data] : extras_) {
		w.u8(TagExtra);
		w.str(key);
		w.bytes(data);
	}
	w.u8(TagEnd);
	return out;
}

Error RenderSnapshot::decode(std::span<const std::uint8_t> bytes, RenderSnapshot& out)
{
	auto fail = [](const char* why) { return Error::make(ErrorCode::Codec, "Render snapshot is unreadable", why); };
	if (bytes.size() < 8 || std::memcmp(bytes.data(), kMagic, 4) != 0) {
		return fail("bad magic");
	}
	ByteReader r(bytes.subspan(4));
	std::uint16_t version = 0;
	std::uint16_t reserved = 0;
	if (!r.u16(version) || !r.u16(reserved)) {
		return fail("truncated header");
	}
	if (version > kCodecVersion) {
		return fail("newer codec version");
	}
	RenderSnapshot result;
	for (;;) {
		std::uint8_t tag = 0;
		if (!r.u8(tag)) {
			return fail("missing end tag");
		}
		if (tag == TagEnd) {
			break;
		}
		switch (tag) {
		case TagInfo: {
			RenderInfo& i = result.info_;
			std::uint8_t field = 0;
			std::uint8_t draft = 0;
			std::uint8_t interactive = 0;
			std::uint8_t sequential = 0;
			if (!r.f64(i.time) || !r.f64(i.frameRate) || !r.f64(i.renderScale.x) || !r.f64(i.renderScale.y) || !r.u8(field)
			    || !r.u8(draft) || !r.u8(interactive) || !r.u8(sequential) || !r.i32(i.renderWindow.x1) || !r.i32(i.renderWindow.y1)
			    || !r.i32(i.renderWindow.x2) || !r.i32(i.renderWindow.y2) || !r.f64(i.projectSize.x) || !r.f64(i.projectSize.y)
			    || !r.f64(i.projectOffset.x) || !r.f64(i.projectOffset.y) || !r.f64(i.pixelAspect) || !r.str(i.colourspace)
			    || !r.str(i.resolvePage)) {
				return fail("truncated info");
			}
			if (field > static_cast<std::uint8_t>(Field::Doubled)) {
				return fail("bad field");
			}
			i.field = static_cast<Field>(field);
			i.draft = draft != 0;
			i.interactive = interactive != 0;
			i.sequential = sequential != 0;
			break;
		}
		case TagLicense: {
			std::uint8_t state = 0;
			if (!r.u8(state)) {
				return fail("truncated license");
			}
			if (state > static_cast<std::uint8_t>(LicenseState::Unlicensed)) {
				return fail("bad license state");
			}
			result.license_ = static_cast<LicenseState>(state);
			break;
		}
		case TagValue: {
			std::uint32_t id = 0;
			Value value;
			if (!r.u32(id) || !readValue(r, value)) {
				return fail("truncated value");
			}
			result.values_[id] = std::move(value);
			break;
		}
		case TagExtra: {
			std::string key;
			Bytes data;
			if (!r.str(key) || !r.bytes(data)) {
				return fail("truncated extra");
			}
			result.extras_[std::move(key)] = std::move(data);
			break;
		}
		default:
			return fail("unknown tag");
		}
	}
	out = std::move(result);
	return Error::none();
}

RenderSnapshotBuilder& RenderSnapshotBuilder::set(ParameterId id, Value value)
{
	snapshot_.values_[id] = std::move(value);
	return *this;
}

RenderSnapshotBuilder& RenderSnapshotBuilder::setExtra(std::string key, Bytes data)
{
	snapshot_.extras_[std::move(key)] = std::move(data);
	return *this;
}

RenderSnapshotBuilder& RenderSnapshotBuilder::setInfo(RenderInfo info)
{
	snapshot_.info_ = std::move(info);
	return *this;
}

RenderSnapshotBuilder& RenderSnapshotBuilder::setLicense(LicenseState state)
{
	snapshot_.license_ = state;
	return *this;
}

OFXGRIP_NAMESPACE_END
