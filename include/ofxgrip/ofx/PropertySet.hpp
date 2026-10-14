// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
/*!
	@file  PropertySet.hpp
	@brief A typed, tolerant wrapper over an OpenFX property set.

	Introduced in OfxGrip 0.1.0. Reads return an `std::optional` so a missing property is a value,
	never an exception. Writes return an `Error`. The tolerant writes used for extension properties
	record their outcome so the host profile can report which extensions a host accepts
	(Q-OFX-ANY-014).
*/
#pragma once

#include <ofxgrip/core/Error.hpp>
#include <ofxgrip/core/Types.hpp>

#include "ofxCore.h"
#include "ofxProperty.h"

#include <optional>
#include <string>
#include <vector>

OFXGRIP_NAMESPACE_BEGIN

class PropertySet {
public:
	PropertySet() = default;
	PropertySet(const OfxPropertySuiteV1* suite, OfxPropertySetHandle handle) : suite_(suite), handle_(handle) {}

	bool valid() const noexcept { return suite_ != nullptr && handle_ != nullptr; }
	OfxPropertySetHandle handle() const noexcept { return handle_; }
	const OfxPropertySuiteV1* suite() const noexcept { return suite_; }

	std::optional<int> getInt(const char* name, int index = 0) const;
	std::optional<double> getDouble(const char* name, int index = 0) const;
	std::optional<std::string> getString(const char* name, int index = 0) const;
	std::optional<void*> getPointer(const char* name, int index = 0) const;
	int getInt(const char* name, int fallback, int index) const { return getInt(name, index).value_or(fallback); }
	double getDouble(const char* name, double fallback, int index) const { return getDouble(name, index).value_or(fallback); }
	std::string getString(const char* name, const std::string& fallback, int index) const { return getString(name, index).value_or(fallback); }
	std::vector<int> getInts(const char* name) const;
	std::vector<double> getDoubles(const char* name) const;
	std::vector<std::string> getStrings(const char* name) const;
	/*! The dimension, or -1 when the property is missing. */
	int dimension(const char* name) const;
	bool has(const char* name) const { return dimension(name) >= 0; }

	Error setInt(const char* name, int value, int index = 0) const;
	Error setDouble(const char* name, double value, int index = 0) const;
	Error setString(const char* name, const std::string& value, int index = 0) const;
	Error setPointer(const char* name, void* value, int index = 0) const;
	Error setInts(const char* name, const std::vector<int>& values) const;
	Error setDoubles(const char* name, const std::vector<double>& values) const;
	Error setStrings(const char* name, const std::vector<std::string>& values) const;

	/*! A write whose failure is recorded, not reported. Returns whether the host accepted it. */
	bool trySetString(const char* name, const std::string& value, std::vector<std::string>* rejected = nullptr) const;
	bool trySetInt(const char* name, int value, std::vector<std::string>* rejected = nullptr) const;
	bool trySetDouble(const char* name, double value, std::vector<std::string>* rejected = nullptr) const;

private:
	const OfxPropertySuiteV1* suite_{nullptr};
	OfxPropertySetHandle handle_{nullptr};
};

OFXGRIP_NAMESPACE_END
