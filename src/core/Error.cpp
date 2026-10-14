// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
#include <ofxgrip/core/Error.hpp>

OFXGRIP_NAMESPACE_BEGIN

std::string_view toString(ErrorCode code) noexcept
{
	switch (code) {
	case ErrorCode::None:
		return "None";
	case ErrorCode::Failed:
		return "Failed";
	case ErrorCode::Fatal:
		return "Fatal";
	case ErrorCode::Unknown:
		return "Unknown";
	case ErrorCode::MissingHostFeature:
		return "MissingHostFeature";
	case ErrorCode::Unsupported:
		return "Unsupported";
	case ErrorCode::Exists:
		return "Exists";
	case ErrorCode::Format:
		return "Format";
	case ErrorCode::Memory:
		return "Memory";
	case ErrorCode::BadHandle:
		return "BadHandle";
	case ErrorCode::BadIndex:
		return "BadIndex";
	case ErrorCode::Value:
		return "Value";
	case ErrorCode::Cancelled:
		return "Cancelled";
	case ErrorCode::InvalidManifest:
		return "InvalidManifest";
	case ErrorCode::InvalidParameter:
		return "InvalidParameter";
	case ErrorCode::NotFound:
		return "NotFound";
	case ErrorCode::WrongPhase:
		return "WrongPhase";
	case ErrorCode::Codec:
		return "Codec";
	case ErrorCode::HostCall:
		return "HostCall";
	case ErrorCode::Render:
		return "Render";
	}
	return "Unknown";
}

std::string Error::describe() const
{
	std::string text(toString(code_));
	if (!message_.empty()) {
		text += ": ";
		text += message_;
	}
	if (!developerMessage_.empty()) {
		text += " (";
		text += developerMessage_;
		text += ")";
	}
	for (const auto& [key, value] : context_) {
		text += " [";
		text += key;
		text += "=";
		text += value;
		text += "]";
	}
	return text;
}

OFXGRIP_NAMESPACE_END
