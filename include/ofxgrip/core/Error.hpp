// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
/*!
	@file  Error.hpp
	@brief The neutral error value, the status codes it carries, and the exception that carries
	       it through C++ code until the C boundary.

	Introduced in OfxGrip 0.1.0. An `Error` holds a code, a message for the user, a message for
	the developer, and context tags added while the error propagates. The OFX adapter maps the
	code to an `OfxStatus` at its single catch boundary. Cancellation is a separate outcome
	(`ErrorCode::Cancelled`) that the adapter never reports as a failure.
*/
#pragma once

#include <ofxgrip/core/Types.hpp>

#include <exception>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

OFXGRIP_NAMESPACE_BEGIN

/*! The outcome of an operation. The first group mirrors the OpenFX status codes one to one; the
    second group names OfxGrip's own failures and maps to `Failed` at the host boundary. */
enum class ErrorCode : std::uint16_t {
	None = 0,
	Failed,
	Fatal,
	Unknown,
	MissingHostFeature,
	Unsupported,
	Exists,
	Format,
	Memory,
	BadHandle,
	BadIndex,
	Value,
	Cancelled,

	InvalidManifest = 100,
	InvalidParameter,
	NotFound,
	WrongPhase,
	Codec,
	HostCall,
	Render,
};

/*! The name of an error code. */
std::string_view toString(ErrorCode code) noexcept;

/*! An error value. An `Error` with code `None` is success; `ok()` and the boolean conversion
    test for that. */
class Error {
public:
	Error() = default;
	Error(ErrorCode code, std::string message, std::string developerMessage = {})
	: code_(code), message_(std::move(message)), developerMessage_(std::move(developerMessage))
	{
	}

	/*! Success. */
	static Error none() noexcept { return {}; }
	/*! An error with a code and a user message. */
	static Error make(ErrorCode code, std::string message, std::string developerMessage = {})
	{
		return Error(code, std::move(message), std::move(developerMessage));
	}
	/*! The cancellation outcome. */
	static Error cancelled() { return Error(ErrorCode::Cancelled, "Cancelled"); }

	bool ok() const noexcept { return code_ == ErrorCode::None; }
	explicit operator bool() const noexcept { return !ok(); }
	bool isCancellation() const noexcept { return code_ == ErrorCode::Cancelled; }

	ErrorCode code() const noexcept { return code_; }
	const std::string& message() const noexcept { return message_; }
	const std::string& developerMessage() const noexcept { return developerMessage_; }
	const std::vector<std::pair<std::string, std::string>>& context() const noexcept { return context_; }

	/*! Adds a context tag, such as `("parameter", "gain")`, and returns the error for chaining. */
	Error& with(std::string key, std::string value)
	{
		context_.emplace_back(std::move(key), std::move(value));
		return *this;
	}

	/*! One line with the code, the messages, and the context tags, for logs. */
	std::string describe() const;

private:
	ErrorCode code_{ErrorCode::None};
	std::string message_;
	std::string developerMessage_;
	std::vector<std::pair<std::string, std::string>> context_;
};

/*! The exception OfxGrip throws inside C++ code. The adapter's catch boundary turns it back into
    an `Error`. */
class OfxGripException : public std::exception {
public:
	explicit OfxGripException(Error error) : error_(std::move(error)), what_(error_.describe()) {}
	const Error& error() const noexcept { return error_; }
	const char* what() const noexcept override { return what_.c_str(); }

private:
	Error error_;
	std::string what_;
};

/*! A value or an error. */
template <typename T>
class Result {
public:
	Result(T value) : value_(std::move(value)) {}  // NOLINT(google-explicit-constructor)
	Result(Error error) : error_(std::move(error)) {}  // NOLINT(google-explicit-constructor)

	bool ok() const noexcept { return error_.ok(); }
	explicit operator bool() const noexcept { return ok(); }
	const Error& error() const noexcept { return error_; }
	T& value() { return value_; }
	const T& value() const { return value_; }
	T valueOr(T fallback) const { return ok() ? value_ : std::move(fallback); }

private:
	T value_{};
	Error error_;
};

OFXGRIP_NAMESPACE_END
