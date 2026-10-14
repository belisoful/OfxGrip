// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
/*!
	@file  PropertySetImpl.hpp
	@brief The mini host's property set: a map from name to a typed, indexed value list, and the
	       `OfxPropertySuiteV1` over it.

	Introduced in OfxGrip 0.1.0. A property set may be marked read-only for a name so a test can
	imitate a host that rejects a write (Q-OFX-ANY-014), and a name may be withheld so a read
	fails as it does on a host that lacks the property (Q-OFX-ANY-015).
*/
#pragma once

#include <ofxgrip/core/Types.hpp>

#include "ofxCore.h"
#include "ofxProperty.h"

#include <map>
#include <mutex>
#include <set>
#include <string>
#include <variant>
#include <vector>

OFXGRIP_NAMESPACE_BEGIN
namespace minihost {

/*! One property value. Strings are stored, and the pointer the suite returns stays valid until
    the property changes. */
using PropValue = std::variant<int, double, std::string, void*>;

class PropSet {
public:
	PropSet() = default;
	PropSet(const PropSet&) = delete;
	PropSet& operator=(const PropSet&) = delete;

	void setInt(const std::string& name, int value, int index = 0);
	void setDouble(const std::string& name, double value, int index = 0);
	void setString(const std::string& name, const std::string& value, int index = 0);
	void setPointer(const std::string& name, void* value, int index = 0);
	void setInts(const std::string& name, const std::vector<int>& values);
	void setDoubles(const std::string& name, const std::vector<double>& values);
	void setStrings(const std::string& name, const std::vector<std::string>& values);
	void reset(const std::string& name) { values_.erase(name); }

	bool has(const std::string& name) const;
	int dimension(const std::string& name) const;
	int getInt(const std::string& name, int fallback = 0, int index = 0) const;
	double getDouble(const std::string& name, double fallback = 0.0, int index = 0) const;
	std::string getString(const std::string& name, const std::string& fallback = {}, int index = 0) const;
	void* getPointer(const std::string& name, int index = 0) const;
	std::vector<int> getInts(const std::string& name) const;
	std::vector<double> getDoubles(const std::string& name) const;
	std::vector<std::string> getStrings(const std::string& name) const;
	std::vector<std::string> names() const;

	/*! Marks a name read-only; the suite answers `kOfxStatErrValue` to writes. */
	void makeReadOnly(const std::string& name) { readOnly_.insert(name); }
	/*! Hides a name; the suite answers `kOfxStatErrUnknown` to reads of it. */
	void withhold(const std::string& name) { withheld_.insert(name); }
	bool isReadOnly(const std::string& name) const { return readOnly_.count(name) != 0; }
	bool isWithheld(const std::string& name) const { return withheld_.count(name) != 0; }
	/*! Records the names of rejected writes, for tests. */
	std::vector<std::string> rejectedWrites;

	OfxPropertySetHandle handle() noexcept { return reinterpret_cast<OfxPropertySetHandle>(this); }
	static PropSet* from(OfxPropertySetHandle handle) noexcept { return reinterpret_cast<PropSet*>(handle); }

	/*! The suite. Function pointers dispatch to the handle's `PropSet`. */
	static const OfxPropertySuiteV1* suite() noexcept;

private:
	friend struct PropSuite;
	mutable std::mutex mutex_;
	std::map<std::string, std::vector<PropValue>> values_;
	std::set<std::string> readOnly_;
	std::set<std::string> withheld_;
	template <typename T>
	OfxStatus set(const std::string& name, int index, T value);
	template <typename T>
	OfxStatus get(const std::string& name, int index, T& out) const;
};

}  // namespace minihost
OFXGRIP_NAMESPACE_END
