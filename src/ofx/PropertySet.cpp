// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
#include <ofxgrip/ofx/PropertySet.hpp>
#include <ofxgrip/ofx/Suites.hpp>

OFXGRIP_NAMESPACE_BEGIN

std::optional<int> PropertySet::getInt(const char* name, int index) const
{
	if (!valid()) {
		return std::nullopt;
	}
	int value = 0;
	if (suite_->propGetInt(handle_, name, index, &value) != kOfxStatOK) {
		return std::nullopt;
	}
	return value;
}

std::optional<double> PropertySet::getDouble(const char* name, int index) const
{
	if (!valid()) {
		return std::nullopt;
	}
	double value = 0;
	if (suite_->propGetDouble(handle_, name, index, &value) != kOfxStatOK) {
		return std::nullopt;
	}
	return value;
}

std::optional<std::string> PropertySet::getString(const char* name, int index) const
{
	if (!valid()) {
		return std::nullopt;
	}
	char* value = nullptr;
	if (suite_->propGetString(handle_, name, index, &value) != kOfxStatOK || value == nullptr) {
		return std::nullopt;
	}
	return std::string(value);
}

std::optional<void*> PropertySet::getPointer(const char* name, int index) const
{
	if (!valid()) {
		return std::nullopt;
	}
	void* value = nullptr;
	if (suite_->propGetPointer(handle_, name, index, &value) != kOfxStatOK) {
		return std::nullopt;
	}
	return value;
}

std::vector<int> PropertySet::getInts(const char* name) const
{
	std::vector<int> out;
	const int n = dimension(name);
	for (int i = 0; i < n; ++i) {
		if (auto v = getInt(name, i)) {
			out.push_back(*v);
		}
	}
	return out;
}

std::vector<double> PropertySet::getDoubles(const char* name) const
{
	std::vector<double> out;
	const int n = dimension(name);
	for (int i = 0; i < n; ++i) {
		if (auto v = getDouble(name, i)) {
			out.push_back(*v);
		}
	}
	return out;
}

std::vector<std::string> PropertySet::getStrings(const char* name) const
{
	std::vector<std::string> out;
	const int n = dimension(name);
	for (int i = 0; i < n; ++i) {
		if (auto v = getString(name, i)) {
			out.push_back(*v);
		}
	}
	return out;
}

int PropertySet::dimension(const char* name) const
{
	if (!valid()) {
		return -1;
	}
	int count = 0;
	if (suite_->propGetDimension(handle_, name, &count) != kOfxStatOK) {
		return -1;
	}
	return count;
}

namespace {

Error check(OfxStatus status, const char* name)
{
	if (status == kOfxStatOK) {
		return Error::none();
	}
	Error e = statusToError(status, "property write failed");
	e.with("property", name);
	return e;
}

}  // namespace

Error PropertySet::setInt(const char* name, int value, int index) const
{
	if (!valid()) {
		return Error::make(ErrorCode::BadHandle, "No property set");
	}
	return check(suite_->propSetInt(handle_, name, index, value), name);
}

Error PropertySet::setDouble(const char* name, double value, int index) const
{
	if (!valid()) {
		return Error::make(ErrorCode::BadHandle, "No property set");
	}
	return check(suite_->propSetDouble(handle_, name, index, value), name);
}

Error PropertySet::setString(const char* name, const std::string& value, int index) const
{
	if (!valid()) {
		return Error::make(ErrorCode::BadHandle, "No property set");
	}
	return check(suite_->propSetString(handle_, name, index, value.c_str()), name);
}

Error PropertySet::setPointer(const char* name, void* value, int index) const
{
	if (!valid()) {
		return Error::make(ErrorCode::BadHandle, "No property set");
	}
	return check(suite_->propSetPointer(handle_, name, index, value), name);
}

Error PropertySet::setInts(const char* name, const std::vector<int>& values) const
{
	if (!valid()) {
		return Error::make(ErrorCode::BadHandle, "No property set");
	}
	return check(suite_->propSetIntN(handle_, name, static_cast<int>(values.size()), values.data()), name);
}

Error PropertySet::setDoubles(const char* name, const std::vector<double>& values) const
{
	if (!valid()) {
		return Error::make(ErrorCode::BadHandle, "No property set");
	}
	return check(suite_->propSetDoubleN(handle_, name, static_cast<int>(values.size()), values.data()), name);
}

Error PropertySet::setStrings(const char* name, const std::vector<std::string>& values) const
{
	if (!valid()) {
		return Error::make(ErrorCode::BadHandle, "No property set");
	}
	std::vector<const char*> pointers;
	pointers.reserve(values.size());
	for (const std::string& s : values) {
		pointers.push_back(s.c_str());
	}
	return check(suite_->propSetStringN(handle_, name, static_cast<int>(pointers.size()), pointers.data()), name);
}

bool PropertySet::trySetString(const char* name, const std::string& value, std::vector<std::string>* rejected) const
{
	const bool ok = setString(name, value).ok();
	if (!ok && rejected != nullptr) {
		rejected->emplace_back(name);
	}
	return ok;
}

bool PropertySet::trySetInt(const char* name, int value, std::vector<std::string>* rejected) const
{
	const bool ok = setInt(name, value).ok();
	if (!ok && rejected != nullptr) {
		rejected->emplace_back(name);
	}
	return ok;
}

bool PropertySet::trySetDouble(const char* name, double value, std::vector<std::string>* rejected) const
{
	const bool ok = setDouble(name, value).ok();
	if (!ok && rejected != nullptr) {
		rejected->emplace_back(name);
	}
	return ok;
}

OFXGRIP_NAMESPACE_END
