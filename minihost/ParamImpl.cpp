// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
#include <ofxgrip/minihost/ParamImpl.hpp>

#include <cstdarg>
#include <cstring>

OFXGRIP_NAMESPACE_BEGIN
namespace minihost {

bool ParamSet::copyIsNoOp = false;

Param::Param(std::string name, std::string type) : name_(std::move(name)), type_(std::move(type))
{
	props_.setString(kOfxPropName, name_);
	props_.setString(kOfxParamPropType, type_);
	props_.setString(kOfxPropType, kOfxTypeParameter);
	props_.setInt(kOfxParamPropSecret, 0);
	props_.setInt(kOfxParamPropEnabled, 1);
	props_.setInt(kOfxParamPropPersistant, 1);
	props_.setInt(kOfxParamPropEvaluateOnChange, 1);
	props_.setInt(kOfxParamPropCanUndo, 1);
	props_.setInt(kOfxParamPropAnimates, isString() || type_ == kOfxParamTypeBoolean || type_ == kOfxParamTypeChoice || type_ == kOfxParamTypeGroup || type_ == kOfxParamTypePage || type_ == kOfxParamTypePushButton ? 0 : 1);
	props_.setInt(kOfxParamPropIsAnimating, 0);
	props_.setInt(kOfxParamPropIsAutoKeying, 0);
	numeric_.assign(static_cast<std::size_t>(dimensions()), 0.0);
}

int Param::dimensions() const noexcept
{
	if (type_ == kOfxParamTypeDouble || type_ == kOfxParamTypeInteger || type_ == kOfxParamTypeBoolean || type_ == kOfxParamTypeChoice) {
		return 1;
	}
	if (type_ == kOfxParamTypeDouble2D || type_ == kOfxParamTypeInteger2D) {
		return 2;
	}
	if (type_ == kOfxParamTypeDouble3D || type_ == kOfxParamTypeInteger3D || type_ == kOfxParamTypeRGB) {
		return 3;
	}
	if (type_ == kOfxParamTypeRGBA) {
		return 4;
	}
	return 0;
}

bool Param::isInteger() const noexcept
{
	return type_ == kOfxParamTypeInteger || type_ == kOfxParamTypeInteger2D || type_ == kOfxParamTypeInteger3D || type_ == kOfxParamTypeBoolean || type_ == kOfxParamTypeChoice;
}

bool Param::isString() const noexcept
{
	return type_ == kOfxParamTypeString || type_ == kOfxParamTypeCustom;
}

bool Param::animates() const
{
	return props_.getInt(kOfxParamPropAnimates, 0) != 0;
}

std::unique_ptr<Param> Param::instantiate() const
{
	auto out = std::make_unique<Param>(name_, type_);
	for (const std::string& name : props_.names()) {
		const int n = props_.dimension(name);
		for (int i = 0; i < n; ++i) {
			if (void* p = props_.getPointer(name, i); p != nullptr) {
				out->props_.setPointer(name, p, i);
				continue;
			}
			const std::string s = props_.getString(name, "\x01", i);
			if (s != "\x01") {
				out->props_.setString(name, s, i);
				continue;
			}
			const double d = props_.getDouble(name, 1.5e300, i);
			if (d != 1.5e300) {
				if (double(static_cast<int>(d)) == d && props_.getInt(name, -1, i) == static_cast<int>(d)) {
					out->props_.setInt(name, static_cast<int>(d), i);
				} else {
					out->props_.setDouble(name, d, i);
				}
			}
		}
	}
	out->props_.setString(kOfxPropType, kOfxTypeParameterInstance);
	if (isString()) {
		out->text_ = props_.getString(kOfxParamPropDefault, "");
	} else {
		std::vector<double> def = props_.getDoubles(kOfxParamPropDefault);
		def.resize(static_cast<std::size_t>(dimensions()), 0.0);
		out->numeric_ = def;
	}
	return out;
}

std::vector<double> Param::numericValueAt(Time time) const
{
	std::lock_guard<std::mutex> lock(mutex_);
	if (numericKeys_.empty()) {
		return numeric_;
	}
	auto upper = numericKeys_.upper_bound(time);
	if (upper == numericKeys_.begin()) {
		return upper->second;
	}
	auto lower = std::prev(upper);
	if (upper == numericKeys_.end() || isInteger()) {
		return lower->second;
	}
	const double t = (time - lower->first) / (upper->first - lower->first);
	std::vector<double> out = lower->second;
	for (std::size_t i = 0; i < out.size() && i < upper->second.size(); ++i) {
		out[i] = lower->second[i] + (upper->second[i] - lower->second[i]) * t;
	}
	return out;
}

std::string Param::stringValueAt(Time time) const
{
	std::lock_guard<std::mutex> lock(mutex_);
	if (textKeys_.empty()) {
		return text_;
	}
	auto upper = textKeys_.upper_bound(time);
	if (upper == textKeys_.begin()) {
		return upper->second;
	}
	return std::prev(upper)->second;
}

void Param::setNumericValue(const std::vector<double>& value)
{
	std::lock_guard<std::mutex> lock(mutex_);
	numeric_ = value;
	numeric_.resize(static_cast<std::size_t>(dimensions()), 0.0);
	numericKeys_.clear();
}

void Param::setNumericValueAt(Time time, const std::vector<double>& value)
{
	if (!animates()) {
		setNumericValue(value);
		return;
	}
	std::lock_guard<std::mutex> lock(mutex_);
	std::vector<double> v = value;
	v.resize(static_cast<std::size_t>(dimensions()), 0.0);
	numericKeys_[time] = v;
	numeric_ = v;
}

void Param::setStringValue(const std::string& value)
{
	std::lock_guard<std::mutex> lock(mutex_);
	text_ = value;
	textKeys_.clear();
}

void Param::setStringValueAt(Time time, const std::string& value)
{
	if (!animates()) {
		setStringValue(value);
		return;
	}
	std::lock_guard<std::mutex> lock(mutex_);
	textKeys_[time] = value;
	text_ = value;
}

int Param::keyCount() const
{
	std::lock_guard<std::mutex> lock(mutex_);
	return static_cast<int>(isString() ? textKeys_.size() : numericKeys_.size());
}

std::vector<Time> Param::keyTimes() const
{
	std::lock_guard<std::mutex> lock(mutex_);
	std::vector<Time> out;
	if (isString()) {
		for (const auto& [t, v] : textKeys_) {
			out.push_back(t);
		}
	} else {
		for (const auto& [t, v] : numericKeys_) {
			out.push_back(t);
		}
	}
	return out;
}

void Param::deleteKey(Time time)
{
	std::lock_guard<std::mutex> lock(mutex_);
	numericKeys_.erase(time);
	textKeys_.erase(time);
}

void Param::deleteAllKeys()
{
	std::lock_guard<std::mutex> lock(mutex_);
	numericKeys_.clear();
	textKeys_.clear();
}

Param* ParamSet::define(const std::string& type, const std::string& name)
{
	if (find(name) != nullptr) {
		return nullptr;
	}
	params_.push_back(std::make_unique<Param>(name, type));
	return params_.back().get();
}

Param* ParamSet::find(const std::string& name) const noexcept
{
	for (const auto& p : params_) {
		if (p->name() == name) {
			return p.get();
		}
	}
	return nullptr;
}

std::unique_ptr<ParamSet> ParamSet::instantiate() const
{
	auto out = std::make_unique<ParamSet>();
	for (const auto& p : params_) {
		out->params_.push_back(p->instantiate());
	}
	return out;
}

namespace {

struct ParamSuite {
	static OfxStatus paramDefine(OfxParamSetHandle set, const char* type, const char* name, OfxPropertySetHandle* props)
	{
		if (set == nullptr || type == nullptr || name == nullptr) {
			return kOfxStatErrBadHandle;
		}
		Param* p = ParamSet::from(set)->define(type, name);
		if (p == nullptr) {
			return kOfxStatErrExists;
		}
		if (props != nullptr) {
			*props = p->props().handle();
		}
		return kOfxStatOK;
	}
	static OfxStatus paramGetHandle(OfxParamSetHandle set, const char* name, OfxParamHandle* param, OfxPropertySetHandle* props)
	{
		if (set == nullptr || name == nullptr) {
			return kOfxStatErrBadHandle;
		}
		Param* p = ParamSet::from(set)->find(name);
		if (p == nullptr) {
			return kOfxStatErrUnknown;
		}
		*param = p->handle();
		if (props != nullptr) {
			*props = p->props().handle();
		}
		return kOfxStatOK;
	}
	static OfxStatus paramSetGetPropertySet(OfxParamSetHandle set, OfxPropertySetHandle* props)
	{
		if (set == nullptr) {
			return kOfxStatErrBadHandle;
		}
		*props = ParamSet::from(set)->props().handle();
		return kOfxStatOK;
	}
	static OfxStatus paramGetPropertySet(OfxParamHandle param, OfxPropertySetHandle* props)
	{
		if (param == nullptr) {
			return kOfxStatErrBadHandle;
		}
		*props = Param::from(param)->props().handle();
		return kOfxStatOK;
	}
	static OfxStatus readInto(Param* p, Time time, va_list& args)
	{
		if (p->isString()) {
			char** out = va_arg(args, char**);
			// The pointer must stay valid until the next write; the Param keeps the string.
			static thread_local std::string scratch;
			scratch = p->stringValueAt(time);
			*out = const_cast<char*>(scratch.c_str());
			return kOfxStatOK;
		}
		const std::vector<double> v = p->numericValueAt(time);
		const int dims = p->dimensions();
		if (dims == 0) {
			return kOfxStatErrUnsupported;
		}
		for (int i = 0; i < dims; ++i) {
			if (p->isInteger()) {
				int* out = va_arg(args, int*);
				*out = static_cast<int>(v[static_cast<std::size_t>(i)]);
			} else {
				double* out = va_arg(args, double*);
				*out = v[static_cast<std::size_t>(i)];
			}
		}
		return kOfxStatOK;
	}
	static OfxStatus paramGetValue(OfxParamHandle param, ...)
	{
		if (param == nullptr) {
			return kOfxStatErrBadHandle;
		}
		va_list args;
		va_start(args, param);
		const OfxStatus st = readInto(Param::from(param), 0.0, args);
		va_end(args);
		return st;
	}
	static OfxStatus paramGetValueAtTime(OfxParamHandle param, OfxTime time, ...)
	{
		if (param == nullptr) {
			return kOfxStatErrBadHandle;
		}
		va_list args;
		va_start(args, time);
		const OfxStatus st = readInto(Param::from(param), time, args);
		va_end(args);
		return st;
	}
	static OfxStatus paramGetDerivative(OfxParamHandle, OfxTime, ...) { return kOfxStatErrUnsupported; }
	static OfxStatus paramGetIntegral(OfxParamHandle, OfxTime, OfxTime, ...) { return kOfxStatErrUnsupported; }
	static OfxStatus writeFrom(Param* p, bool timed, Time time, va_list& args)
	{
		if (p->isString()) {
			const char* s = va_arg(args, const char*);
			if (timed) {
				p->setStringValueAt(time, s ? s : "");
			} else {
				p->setStringValue(s ? s : "");
			}
			return kOfxStatOK;
		}
		const int dims = p->dimensions();
		if (dims == 0) {
			return kOfxStatErrUnsupported;
		}
		std::vector<double> v(static_cast<std::size_t>(dims), 0.0);
		for (int i = 0; i < dims; ++i) {
			if (p->isInteger()) {
				v[static_cast<std::size_t>(i)] = va_arg(args, int);
			} else {
				v[static_cast<std::size_t>(i)] = va_arg(args, double);
			}
		}
		if (timed) {
			p->setNumericValueAt(time, v);
		} else {
			p->setNumericValue(v);
		}
		return kOfxStatOK;
	}
	static OfxStatus paramSetValue(OfxParamHandle param, ...)
	{
		if (param == nullptr) {
			return kOfxStatErrBadHandle;
		}
		va_list args;
		va_start(args, param);
		const OfxStatus st = writeFrom(Param::from(param), false, 0.0, args);
		va_end(args);
		return st;
	}
	static OfxStatus paramSetValueAtTime(OfxParamHandle param, OfxTime time, ...)
	{
		if (param == nullptr) {
			return kOfxStatErrBadHandle;
		}
		va_list args;
		va_start(args, time);
		const OfxStatus st = writeFrom(Param::from(param), true, time, args);
		va_end(args);
		return st;
	}
	static OfxStatus paramGetNumKeys(OfxParamHandle param, unsigned int* n)
	{
		if (param == nullptr) {
			return kOfxStatErrBadHandle;
		}
		*n = static_cast<unsigned int>(Param::from(param)->keyCount());
		return kOfxStatOK;
	}
	static OfxStatus paramGetKeyTime(OfxParamHandle param, unsigned int index, OfxTime* time)
	{
		if (param == nullptr) {
			return kOfxStatErrBadHandle;
		}
		const std::vector<Time> times = Param::from(param)->keyTimes();
		if (index >= times.size()) {
			return kOfxStatErrBadIndex;
		}
		*time = times[index];
		return kOfxStatOK;
	}
	static OfxStatus paramGetKeyIndex(OfxParamHandle param, OfxTime time, int direction, int* index)
	{
		if (param == nullptr) {
			return kOfxStatErrBadHandle;
		}
		const std::vector<Time> times = Param::from(param)->keyTimes();
		for (std::size_t i = 0; i < times.size(); ++i) {
			if ((direction == 0 && times[i] == time) || (direction > 0 && times[i] > time)) {
				*index = static_cast<int>(i);
				return kOfxStatOK;
			}
			if (direction < 0 && times[i] >= time) {
				if (i == 0) {
					return kOfxStatFailed;
				}
				*index = static_cast<int>(i - 1);
				return kOfxStatOK;
			}
		}
		if (direction < 0 && !times.empty()) {
			*index = static_cast<int>(times.size() - 1);
			return kOfxStatOK;
		}
		return kOfxStatFailed;
	}
	static OfxStatus paramDeleteKey(OfxParamHandle param, OfxTime time)
	{
		if (param == nullptr) {
			return kOfxStatErrBadHandle;
		}
		Param::from(param)->deleteKey(time);
		return kOfxStatOK;
	}
	static OfxStatus paramDeleteAllKeys(OfxParamHandle param)
	{
		if (param == nullptr) {
			return kOfxStatErrBadHandle;
		}
		Param::from(param)->deleteAllKeys();
		return kOfxStatOK;
	}
	static OfxStatus paramCopy(OfxParamHandle to, OfxParamHandle from, OfxTime, const OfxRangeD*)
	{
		if (to == nullptr || from == nullptr) {
			return kOfxStatErrBadHandle;
		}
		if (ParamSet::copyIsNoOp) {
			return kOfxStatOK;  // Q-RESOLVE-006
		}
		Param* dst = Param::from(to);
		Param* src = Param::from(from);
		if (src->isString()) {
			dst->setStringValue(src->stringValueAt(0.0));
		} else {
			dst->setNumericValue(src->numericValueAt(0.0));
			for (Time t : src->keyTimes()) {
				dst->setNumericValueAt(t, src->numericValueAt(t));
			}
		}
		return kOfxStatOK;
	}
	static OfxStatus paramEditBegin(OfxParamSetHandle set, const char* name)
	{
		if (set == nullptr) {
			return kOfxStatErrBadHandle;
		}
		ParamSet::from(set)->editDepth++;
		ParamSet::from(set)->editNames.emplace_back(name ? name : "");
		return kOfxStatOK;
	}
	static OfxStatus paramEditEnd(OfxParamSetHandle set)
	{
		if (set == nullptr) {
			return kOfxStatErrBadHandle;
		}
		if (ParamSet::from(set)->editDepth > 0) {
			ParamSet::from(set)->editDepth--;
		}
		return kOfxStatOK;
	}
};

}  // namespace

const OfxParameterSuiteV1* ParamSet::suite() noexcept
{
	static const OfxParameterSuiteV1 s = {
	    &ParamSuite::paramDefine, &ParamSuite::paramGetHandle, &ParamSuite::paramSetGetPropertySet, &ParamSuite::paramGetPropertySet,
	    &ParamSuite::paramGetValue, &ParamSuite::paramGetValueAtTime, &ParamSuite::paramGetDerivative, &ParamSuite::paramGetIntegral,
	    &ParamSuite::paramSetValue, &ParamSuite::paramSetValueAtTime, &ParamSuite::paramGetNumKeys, &ParamSuite::paramGetKeyTime,
	    &ParamSuite::paramGetKeyIndex, &ParamSuite::paramDeleteKey, &ParamSuite::paramDeleteAllKeys, &ParamSuite::paramCopy,
	    &ParamSuite::paramEditBegin, &ParamSuite::paramEditEnd,
	};
	return &s;
}

}  // namespace minihost
OFXGRIP_NAMESPACE_END
