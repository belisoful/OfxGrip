// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
#include <ofxgrip/core/Value.hpp>

#include <cmath>
#include <sstream>

OFXGRIP_NAMESPACE_BEGIN

double Value::toDouble(double fallback) const noexcept
{
	switch (kind()) {
	case ValueKind::Bool:
		return as<bool>() ? 1.0 : 0.0;
	case ValueKind::Int:
		return static_cast<double>(as<int>());
	case ValueKind::Double:
		return as<double>();
	default:
		return fallback;
	}
}

int Value::toInt(int fallback) const noexcept
{
	switch (kind()) {
	case ValueKind::Bool:
		return as<bool>() ? 1 : 0;
	case ValueKind::Int:
		return as<int>();
	case ValueKind::Double: {
		const double d = as<double>();
		if (std::isnan(d) || d > 2147483647.0 || d < -2147483648.0) {
			return fallback;
		}
		return static_cast<int>(d);
	}
	default:
		return fallback;
	}
}

bool Value::toBool(bool fallback) const noexcept
{
	switch (kind()) {
	case ValueKind::Bool:
		return as<bool>();
	case ValueKind::Int:
		return as<int>() != 0;
	case ValueKind::Double:
		return as<double>() != 0.0;
	case ValueKind::String: {
		const std::string& s = as<std::string>();
		if (s == "true" || s == "1" || s == "yes") {
			return true;
		}
		if (s == "false" || s == "0" || s == "no") {
			return false;
		}
		return fallback;
	}
	default:
		return fallback;
	}
}

std::string Value::toString() const
{
	std::ostringstream out;
	switch (kind()) {
	case ValueKind::Empty:
		out << "<empty>";
		break;
	case ValueKind::Bool:
		out << (as<bool>() ? "true" : "false");
		break;
	case ValueKind::Int:
		out << as<int>();
		break;
	case ValueKind::Double:
		out << as<double>();
		break;
	case ValueKind::Point2D:
		out << "(" << as<Point2D>().x << ", " << as<Point2D>().y << ")";
		break;
	case ValueKind::Point3D:
		out << "(" << as<Point3D>().x << ", " << as<Point3D>().y << ", " << as<Point3D>().z << ")";
		break;
	case ValueKind::RGB:
		out << "rgb(" << as<RGB>().r << ", " << as<RGB>().g << ", " << as<RGB>().b << ")";
		break;
	case ValueKind::RGBA:
		out << "rgba(" << as<RGBA>().r << ", " << as<RGBA>().g << ", " << as<RGBA>().b << ", " << as<RGBA>().a << ")";
		break;
	case ValueKind::String:
		out << '"' << as<std::string>() << '"';
		break;
	case ValueKind::Bytes:
		out << "<" << as<Bytes>().size() << " bytes>";
		break;
	}
	return out.str();
}

std::string_view toString(ValueKind kind) noexcept
{
	switch (kind) {
	case ValueKind::Empty:
		return "empty";
	case ValueKind::Bool:
		return "bool";
	case ValueKind::Int:
		return "int";
	case ValueKind::Double:
		return "double";
	case ValueKind::Point2D:
		return "point2d";
	case ValueKind::Point3D:
		return "point3d";
	case ValueKind::RGB:
		return "rgb";
	case ValueKind::RGBA:
		return "rgba";
	case ValueKind::String:
		return "string";
	case ValueKind::Bytes:
		return "bytes";
	}
	return "empty";
}

OFXGRIP_NAMESPACE_END
