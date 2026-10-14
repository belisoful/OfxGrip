// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
#include <ofxgrip/core/RenderSnapshot.hpp>

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

class Writer {
public:
	explicit Writer(Bytes& out) : out_(out) {}
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

private:
	Bytes& out_;
};

class Reader {
public:
	explicit Reader(std::span<const std::uint8_t> in) : in_(in) {}
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
	bool done() const { return pos_ >= in_.size(); }

private:
	std::span<const std::uint8_t> in_;
	std::size_t pos_{0};
};

void writeValue(Writer& w, const Value& v)
{
	w.u8(static_cast<std::uint8_t>(v.kind()));
	switch (v.kind()) {
	case ValueKind::Empty:
		break;
	case ValueKind::Bool:
		w.u8(v.as<bool>() ? 1 : 0);
		break;
	case ValueKind::Int:
		w.i32(v.as<int>());
		break;
	case ValueKind::Double:
		w.f64(v.as<double>());
		break;
	case ValueKind::Point2D:
		w.f64(v.as<Point2D>().x);
		w.f64(v.as<Point2D>().y);
		break;
	case ValueKind::Point3D:
		w.f64(v.as<Point3D>().x);
		w.f64(v.as<Point3D>().y);
		w.f64(v.as<Point3D>().z);
		break;
	case ValueKind::RGB:
		w.f64(v.as<RGB>().r);
		w.f64(v.as<RGB>().g);
		w.f64(v.as<RGB>().b);
		break;
	case ValueKind::RGBA:
		w.f64(v.as<RGBA>().r);
		w.f64(v.as<RGBA>().g);
		w.f64(v.as<RGBA>().b);
		w.f64(v.as<RGBA>().a);
		break;
	case ValueKind::String:
		w.str(v.as<std::string>());
		break;
	case ValueKind::Bytes:
		w.bytes(v.as<Bytes>());
		break;
	}
}

bool readValue(Reader& r, Value& out)
{
	std::uint8_t kind = 0;
	if (!r.u8(kind)) {
		return false;
	}
	switch (static_cast<ValueKind>(kind)) {
	case ValueKind::Empty:
		out = Value();
		return true;
	case ValueKind::Bool: {
		std::uint8_t b = 0;
		if (!r.u8(b)) {
			return false;
		}
		out = Value(b != 0);
		return true;
	}
	case ValueKind::Int: {
		std::int32_t i = 0;
		if (!r.i32(i)) {
			return false;
		}
		out = Value(static_cast<int>(i));
		return true;
	}
	case ValueKind::Double: {
		double d = 0;
		if (!r.f64(d)) {
			return false;
		}
		out = Value(d);
		return true;
	}
	case ValueKind::Point2D: {
		Point2D p;
		if (!r.f64(p.x) || !r.f64(p.y)) {
			return false;
		}
		out = Value(p);
		return true;
	}
	case ValueKind::Point3D: {
		Point3D p;
		if (!r.f64(p.x) || !r.f64(p.y) || !r.f64(p.z)) {
			return false;
		}
		out = Value(p);
		return true;
	}
	case ValueKind::RGB: {
		RGB c;
		if (!r.f64(c.r) || !r.f64(c.g) || !r.f64(c.b)) {
			return false;
		}
		out = Value(c);
		return true;
	}
	case ValueKind::RGBA: {
		RGBA c;
		if (!r.f64(c.r) || !r.f64(c.g) || !r.f64(c.b) || !r.f64(c.a)) {
			return false;
		}
		out = Value(c);
		return true;
	}
	case ValueKind::String: {
		std::string s;
		if (!r.str(s)) {
			return false;
		}
		out = Value(std::move(s));
		return true;
	}
	case ValueKind::Bytes: {
		Bytes b;
		if (!r.bytes(b)) {
			return false;
		}
		out = Value(std::move(b));
		return true;
	}
	}
	return false;
}

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
	Writer w(out);
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
	Reader r(bytes.subspan(4));
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
