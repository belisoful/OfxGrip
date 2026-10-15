// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
#include <ofxgrip/core/Codec.hpp>

OFXGRIP_NAMESPACE_BEGIN

void writeValue(ByteWriter& w, const Value& v)
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
	case ValueKind::Blob:
		w.bytes(v.as<Bytes>());
		break;
	}
}

bool readValue(ByteReader& r, Value& out)
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
	case ValueKind::Blob: {
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

OFXGRIP_NAMESPACE_END
