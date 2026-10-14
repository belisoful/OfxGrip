// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
#include <ofxgrip/minihost/PropertySetImpl.hpp>

OFXGRIP_NAMESPACE_BEGIN
namespace minihost {

template <typename T>
OfxStatus PropSet::set(const std::string& name, int index, T value)
{
	std::lock_guard<std::mutex> lock(mutex_);
	if (index < 0) {
		return kOfxStatErrBadIndex;
	}
	if (readOnly_.count(name) != 0) {
		rejectedWrites.push_back(name);
		return kOfxStatErrValue;
	}
	auto& list = values_[name];
	if (static_cast<std::size_t>(index) >= list.size()) {
		list.resize(static_cast<std::size_t>(index) + 1, PropValue(T{}));
	}
	list[static_cast<std::size_t>(index)] = PropValue(std::move(value));
	return kOfxStatOK;
}

template <typename T>
OfxStatus PropSet::get(const std::string& name, int index, T& out) const
{
	std::lock_guard<std::mutex> lock(mutex_);
	if (withheld_.count(name) != 0) {
		return kOfxStatErrUnknown;
	}
	auto it = values_.find(name);
	if (it == values_.end()) {
		return kOfxStatErrUnknown;
	}
	if (index < 0 || static_cast<std::size_t>(index) >= it->second.size()) {
		return kOfxStatErrBadIndex;
	}
	const PropValue& v = it->second[static_cast<std::size_t>(index)];
	if constexpr (std::is_same_v<T, int>) {
		if (const int* i = std::get_if<int>(&v)) {
			out = *i;
			return kOfxStatOK;
		}
		if (const double* d = std::get_if<double>(&v)) {
			out = static_cast<int>(*d);
			return kOfxStatOK;
		}
	} else if constexpr (std::is_same_v<T, double>) {
		if (const double* d = std::get_if<double>(&v)) {
			out = *d;
			return kOfxStatOK;
		}
		if (const int* i = std::get_if<int>(&v)) {
			out = static_cast<double>(*i);
			return kOfxStatOK;
		}
	} else if constexpr (std::is_same_v<T, const char*>) {
		if (const std::string* s = std::get_if<std::string>(&v)) {
			out = s->c_str();
			return kOfxStatOK;
		}
	} else if constexpr (std::is_same_v<T, void*>) {
		if (void* const* p = std::get_if<void*>(&v)) {
			out = *p;
			return kOfxStatOK;
		}
	}
	return kOfxStatErrValue;
}

void PropSet::setInt(const std::string& name, int value, int index)
{
	set<int>(name, index, value);
}
void PropSet::setDouble(const std::string& name, double value, int index)
{
	set<double>(name, index, value);
}
void PropSet::setString(const std::string& name, const std::string& value, int index)
{
	set<std::string>(name, index, value);
}
void PropSet::setPointer(const std::string& name, void* value, int index)
{
	set<void*>(name, index, value);
}
void PropSet::setInts(const std::string& name, const std::vector<int>& values)
{
	std::lock_guard<std::mutex> lock(mutex_);
	auto& list = values_[name];
	list.clear();
	for (int v : values) {
		list.emplace_back(v);
	}
}
void PropSet::setDoubles(const std::string& name, const std::vector<double>& values)
{
	std::lock_guard<std::mutex> lock(mutex_);
	auto& list = values_[name];
	list.clear();
	for (double v : values) {
		list.emplace_back(v);
	}
}
void PropSet::setStrings(const std::string& name, const std::vector<std::string>& values)
{
	std::lock_guard<std::mutex> lock(mutex_);
	auto& list = values_[name];
	list.clear();
	for (const std::string& v : values) {
		list.emplace_back(v);
	}
}

bool PropSet::has(const std::string& name) const
{
	std::lock_guard<std::mutex> lock(mutex_);
	return withheld_.count(name) == 0 && values_.count(name) != 0;
}

int PropSet::dimension(const std::string& name) const
{
	std::lock_guard<std::mutex> lock(mutex_);
	if (withheld_.count(name) != 0) {
		return -1;
	}
	auto it = values_.find(name);
	return it == values_.end() ? -1 : static_cast<int>(it->second.size());
}

int PropSet::getInt(const std::string& name, int fallback, int index) const
{
	int v = fallback;
	return get<int>(name, index, v) == kOfxStatOK ? v : fallback;
}
double PropSet::getDouble(const std::string& name, double fallback, int index) const
{
	double v = fallback;
	return get<double>(name, index, v) == kOfxStatOK ? v : fallback;
}
std::string PropSet::getString(const std::string& name, const std::string& fallback, int index) const
{
	const char* v = nullptr;
	return get<const char*>(name, index, v) == kOfxStatOK && v != nullptr ? std::string(v) : fallback;
}
void* PropSet::getPointer(const std::string& name, int index) const
{
	void* v = nullptr;
	return get<void*>(name, index, v) == kOfxStatOK ? v : nullptr;
}
std::vector<int> PropSet::getInts(const std::string& name) const
{
	std::vector<int> out;
	const int n = dimension(name);
	for (int i = 0; i < n; ++i) {
		out.push_back(getInt(name, 0, i));
	}
	return out;
}
std::vector<double> PropSet::getDoubles(const std::string& name) const
{
	std::vector<double> out;
	const int n = dimension(name);
	for (int i = 0; i < n; ++i) {
		out.push_back(getDouble(name, 0.0, i));
	}
	return out;
}
std::vector<std::string> PropSet::getStrings(const std::string& name) const
{
	std::vector<std::string> out;
	const int n = dimension(name);
	for (int i = 0; i < n; ++i) {
		out.push_back(getString(name, std::string(), i));
	}
	return out;
}
std::vector<std::string> PropSet::names() const
{
	std::lock_guard<std::mutex> lock(mutex_);
	std::vector<std::string> out;
	for (const auto& [name, v] : values_) {
		out.push_back(name);
	}
	return out;
}

struct PropSuite {
	static PropSet* ps(OfxPropertySetHandle h) { return PropSet::from(h); }
	static OfxStatus setPointer(OfxPropertySetHandle h, const char* p, int i, void* v) { return h ? ps(h)->set<void*>(p, i, v) : kOfxStatErrBadHandle; }
	static OfxStatus setString(OfxPropertySetHandle h, const char* p, int i, const char* v) { return h ? ps(h)->set<std::string>(p, i, std::string(v ? v : "")) : kOfxStatErrBadHandle; }
	static OfxStatus setDouble(OfxPropertySetHandle h, const char* p, int i, double v) { return h ? ps(h)->set<double>(p, i, v) : kOfxStatErrBadHandle; }
	static OfxStatus setInt(OfxPropertySetHandle h, const char* p, int i, int v) { return h ? ps(h)->set<int>(p, i, v) : kOfxStatErrBadHandle; }
	static OfxStatus setPointerN(OfxPropertySetHandle h, const char* p, int n, void* const* v)
	{
		if (!h) {
			return kOfxStatErrBadHandle;
		}
		if (ps(h)->isReadOnly(p)) {
			ps(h)->rejectedWrites.push_back(p);
			return kOfxStatErrValue;
		}
		ps(h)->reset(p);
		for (int i = 0; i < n; ++i) {
			ps(h)->setPointer(p, v[i], i);
		}
		return kOfxStatOK;
	}
	static OfxStatus setStringN(OfxPropertySetHandle h, const char* p, int n, const char* const* v)
	{
		if (!h) {
			return kOfxStatErrBadHandle;
		}
		if (ps(h)->isReadOnly(p)) {
			ps(h)->rejectedWrites.push_back(p);
			return kOfxStatErrValue;
		}
		std::vector<std::string> values;
		for (int i = 0; i < n; ++i) {
			values.emplace_back(v[i] ? v[i] : "");
		}
		ps(h)->setStrings(p, values);
		return kOfxStatOK;
	}
	static OfxStatus setDoubleN(OfxPropertySetHandle h, const char* p, int n, const double* v)
	{
		if (!h) {
			return kOfxStatErrBadHandle;
		}
		if (ps(h)->isReadOnly(p)) {
			ps(h)->rejectedWrites.push_back(p);
			return kOfxStatErrValue;
		}
		ps(h)->setDoubles(p, std::vector<double>(v, v + n));
		return kOfxStatOK;
	}
	static OfxStatus setIntN(OfxPropertySetHandle h, const char* p, int n, const int* v)
	{
		if (!h) {
			return kOfxStatErrBadHandle;
		}
		if (ps(h)->isReadOnly(p)) {
			ps(h)->rejectedWrites.push_back(p);
			return kOfxStatErrValue;
		}
		ps(h)->setInts(p, std::vector<int>(v, v + n));
		return kOfxStatOK;
	}
	static OfxStatus getPointer(OfxPropertySetHandle h, const char* p, int i, void** v) { return h ? ps(h)->get<void*>(p, i, *v) : kOfxStatErrBadHandle; }
	static OfxStatus getString(OfxPropertySetHandle h, const char* p, int i, char** v)
	{
		if (!h) {
			return kOfxStatErrBadHandle;
		}
		const char* s = nullptr;
		const OfxStatus st = ps(h)->get<const char*>(p, i, s);
		*v = const_cast<char*>(s);
		return st;
	}
	static OfxStatus getDouble(OfxPropertySetHandle h, const char* p, int i, double* v) { return h ? ps(h)->get<double>(p, i, *v) : kOfxStatErrBadHandle; }
	static OfxStatus getInt(OfxPropertySetHandle h, const char* p, int i, int* v) { return h ? ps(h)->get<int>(p, i, *v) : kOfxStatErrBadHandle; }
	static OfxStatus getPointerN(OfxPropertySetHandle h, const char* p, int n, void** v)
	{
		for (int i = 0; i < n; ++i) {
			if (OfxStatus st = getPointer(h, p, i, v + i); st != kOfxStatOK) {
				return st;
			}
		}
		return kOfxStatOK;
	}
	static OfxStatus getStringN(OfxPropertySetHandle h, const char* p, int n, char** v)
	{
		for (int i = 0; i < n; ++i) {
			if (OfxStatus st = getString(h, p, i, v + i); st != kOfxStatOK) {
				return st;
			}
		}
		return kOfxStatOK;
	}
	static OfxStatus getDoubleN(OfxPropertySetHandle h, const char* p, int n, double* v)
	{
		for (int i = 0; i < n; ++i) {
			if (OfxStatus st = getDouble(h, p, i, v + i); st != kOfxStatOK) {
				return st;
			}
		}
		return kOfxStatOK;
	}
	static OfxStatus getIntN(OfxPropertySetHandle h, const char* p, int n, int* v)
	{
		for (int i = 0; i < n; ++i) {
			if (OfxStatus st = getInt(h, p, i, v + i); st != kOfxStatOK) {
				return st;
			}
		}
		return kOfxStatOK;
	}
	static OfxStatus reset(OfxPropertySetHandle h, const char* p)
	{
		if (!h) {
			return kOfxStatErrBadHandle;
		}
		ps(h)->reset(p);
		return kOfxStatOK;
	}
	static OfxStatus getDimension(OfxPropertySetHandle h, const char* p, int* count)
	{
		if (!h) {
			return kOfxStatErrBadHandle;
		}
		const int n = ps(h)->dimension(p);
		if (n < 0) {
			return kOfxStatErrUnknown;
		}
		*count = n;
		return kOfxStatOK;
	}
};

const OfxPropertySuiteV1* PropSet::suite() noexcept
{
	static const OfxPropertySuiteV1 s = {
	    &PropSuite::setPointer, &PropSuite::setString, &PropSuite::setDouble, &PropSuite::setInt,
	    &PropSuite::setPointerN, &PropSuite::setStringN, &PropSuite::setDoubleN, &PropSuite::setIntN,
	    &PropSuite::getPointer, &PropSuite::getString, &PropSuite::getDouble, &PropSuite::getInt,
	    &PropSuite::getPointerN, &PropSuite::getStringN, &PropSuite::getDoubleN, &PropSuite::getIntN,
	    &PropSuite::reset, &PropSuite::getDimension,
	};
	return &s;
}

}  // namespace minihost
OFXGRIP_NAMESPACE_END
