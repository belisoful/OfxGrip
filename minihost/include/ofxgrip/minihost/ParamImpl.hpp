// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
/*!
	@file  ParamImpl.hpp
	@brief The mini host's parameters: typed values per dimension with keyframes and linear
	       interpolation, and the `OfxParameterSuiteV1` over them.

	Introduced in OfxGrip 0.1.0. Doubles interpolate linearly between keyframes; integers,
	booleans, choices, and strings hold until the next keyframe. A parameter whose descriptor
	says it does not animate keeps a single value.
*/
#pragma once

#include <ofxgrip/core/Types.hpp>
#include <ofxgrip/minihost/PropertySetImpl.hpp>

#include "ofxParam.h"

#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

OFXGRIP_NAMESPACE_BEGIN
namespace minihost {

/*! A parameter, descriptor or instance. */
class Param {
public:
	Param(std::string name, std::string type);
	const std::string& name() const noexcept { return name_; }
	const std::string& type() const noexcept { return type_; }
	PropSet& props() noexcept { return props_; }
	/*! The number of numeric dimensions, or 0 for strings. */
	int dimensions() const noexcept;
	bool isInteger() const noexcept;
	bool isString() const noexcept;
	bool animates() const;

	/*! Copies the descriptor's properties and default into a fresh instance parameter. */
	std::unique_ptr<Param> instantiate() const;

	std::vector<double> numericValueAt(Time time) const;
	std::string stringValueAt(Time time) const;
	void setNumericValue(const std::vector<double>& value);
	void setNumericValueAt(Time time, const std::vector<double>& value);
	void setStringValue(const std::string& value);
	void setStringValueAt(Time time, const std::string& value);
	int keyCount() const;
	std::vector<Time> keyTimes() const;
	void deleteKey(Time time);
	void deleteAllKeys();

	OfxParamHandle handle() noexcept { return reinterpret_cast<OfxParamHandle>(this); }
	static Param* from(OfxParamHandle h) noexcept { return reinterpret_cast<Param*>(h); }

private:
	std::string name_;
	std::string type_;
	PropSet props_;
	mutable std::mutex mutex_;
	std::vector<double> numeric_;
	std::string text_;
	std::map<Time, std::vector<double>> numericKeys_;
	std::map<Time, std::string> textKeys_;
};

/*! A parameter set, descriptor or instance. */
class ParamSet {
public:
	PropSet& props() noexcept { return props_; }
	Param* define(const std::string& type, const std::string& name);
	Param* find(const std::string& name) const noexcept;
	const std::vector<std::unique_ptr<Param>>& params() const noexcept { return params_; }
	/*! Instantiates every descriptor parameter into a new set. */
	std::unique_ptr<ParamSet> instantiate() const;
	int editDepth{0};
	std::vector<std::string> editNames;

	OfxParamSetHandle handle() noexcept { return reinterpret_cast<OfxParamSetHandle>(this); }
	static ParamSet* from(OfxParamSetHandle h) noexcept { return reinterpret_cast<ParamSet*>(h); }

	/*! The suite. `paramCopy` honors the `copyIsNoOp` switch to imitate Q-RESOLVE-006. */
	static const OfxParameterSuiteV1* suite() noexcept;
	static bool copyIsNoOp;

private:
	PropSet props_;
	std::vector<std::unique_ptr<Param>> params_;
};

}  // namespace minihost
OFXGRIP_NAMESPACE_END
