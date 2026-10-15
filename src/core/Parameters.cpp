// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
#include <ofxgrip/core/Parameters.hpp>

#include <algorithm>

OFXGRIP_NAMESPACE_BEGIN

// ---- Parameter ----------------------------------------------------------------------------------

Parameter::Parameter(ParameterSpec spec, ParameterService& service, MetaManager* meta) : spec_(std::move(spec)), service_(service), meta_(meta) {}

std::vector<Parameter*> Parameter::descendants() const
{
	std::vector<Parameter*> out;
	for (Parameter* child : children_) {
		out.push_back(child);
		const std::vector<Parameter*> below = child->descendants();
		out.insert(out.end(), below.begin(), below.end());
	}
	return out;
}

Error Parameter::get(Time time, Value& out) const
{
	return service_.get(spec_.id, time, out);
}

Error Parameter::getCurrent(Value& out) const
{
	return service_.getCurrent(spec_.id, out);
}

Value Parameter::value(Time time) const
{
	Value v;
	return service_.get(spec_.id, time, v).ok() ? v : Value();
}

Value Parameter::current() const
{
	Value v;
	return service_.getCurrent(spec_.id, v).ok() ? v : Value();
}

double Parameter::number(Time time, double fallback) const
{
	const Value v = value(time);
	return v.empty() ? fallback : v.toDouble(fallback);
}

int Parameter::integer(Time time, int fallback) const
{
	const Value v = value(time);
	return v.empty() ? fallback : v.toInt(fallback);
}

bool Parameter::boolean(Time time, bool fallback) const
{
	const Value v = value(time);
	return v.empty() ? fallback : v.toBool(fallback);
}

Point2D Parameter::point(Time time, Point2D fallback) const
{
	const Value v = value(time);
	return v.is<Point2D>() ? v.as<Point2D>() : fallback;
}

Point3D Parameter::point3D(Time time, Point3D fallback) const
{
	const Value v = value(time);
	return v.is<Point3D>() ? v.as<Point3D>() : fallback;
}

RGB Parameter::rgb(Time time, RGB fallback) const
{
	const Value v = value(time);
	if (v.is<RGB>()) {
		return v.as<RGB>();
	}
	if (v.is<RGBA>()) {
		const RGBA c = v.as<RGBA>();
		return RGB{c.r, c.g, c.b};
	}
	return fallback;
}

RGBA Parameter::rgba(Time time, RGBA fallback) const
{
	const Value v = value(time);
	if (v.is<RGBA>()) {
		return v.as<RGBA>();
	}
	if (v.is<RGB>()) {
		const RGB c = v.as<RGB>();
		return RGBA{c.r, c.g, c.b, 1.0};
	}
	return fallback;
}

std::string Parameter::string(Time time, std::string fallback) const
{
	const Value v = value(time);
	return v.is<std::string>() ? v.as<std::string>() : fallback;
}

std::string Parameter::optionKey(Time time) const
{
	const Value v = value(time);
	return v.empty() ? std::string() : std::string(spec_.optionKey(v.toInt(-1)));
}

std::string Parameter::currentOptionKey() const
{
	const Value v = current();
	return v.empty() ? std::string() : std::string(spec_.optionKey(v.toInt(-1)));
}

Error Parameter::set(Time time, const Value& value)
{
	return service_.set(spec_.id, time, value);
}

Error Parameter::setCurrent(const Value& value)
{
	return service_.setCurrent(spec_.id, value);
}

Error Parameter::setOption(Time time, std::string_view optionKey)
{
	const int index = spec_.optionIndex(optionKey);
	if (index < 0) {
		return Error::make(ErrorCode::NotFound, "No such option").with("parameter", spec_.key).with("option", std::string(optionKey));
	}
	return service_.set(spec_.id, time, Value(index));
}

Error Parameter::setCurrentOption(std::string_view optionKey)
{
	const int index = spec_.optionIndex(optionKey);
	if (index < 0) {
		return Error::make(ErrorCode::NotFound, "No such option").with("parameter", spec_.key).with("option", std::string(optionKey));
	}
	return service_.setCurrent(spec_.id, Value(index));
}

Error Parameter::reset(Time time)
{
	return service_.set(spec_.id, time, spec_.resetValue.empty() ? spec_.defaultValue : spec_.resetValue);
}

Error Parameter::resetCurrent()
{
	return service_.setCurrent(spec_.id, spec_.resetValue.empty() ? spec_.defaultValue : spec_.resetValue);
}

HostFlags Parameter::flags() const
{
	HostFlags out;
	return service_.getFlags(spec_.id, out).ok() ? out : spec_.hostFlags;
}

Error Parameter::setFlags(HostFlags flags)
{
	return service_.setFlags(spec_.id, flags);
}

bool Parameter::hidden() const
{
	return flags().has(HostFlag::Hidden);
}

Error Parameter::setHidden(bool hidden)
{
	HostFlags current = flags();
	if (current.has(HostFlag::Hidden) == hidden) {
		return Error::none();
	}
	return service_.setFlags(spec_.id, current.set(HostFlag::Hidden, hidden));
}

bool Parameter::enabled() const
{
	return !flags().has(HostFlag::Disabled);
}

Error Parameter::setEnabled(bool enabled)
{
	HostFlags current = flags();
	if (current.has(HostFlag::Disabled) == !enabled) {
		return Error::none();
	}
	return service_.setFlags(spec_.id, current.set(HostFlag::Disabled, !enabled));
}

std::string Parameter::label() const
{
	std::string out;
	return service_.getLabel(spec_.id, out).ok() ? out : spec_.label;
}

Error Parameter::setLabel(std::string_view label)
{
	return service_.setLabel(spec_.id, label);
}

Error Parameter::setHint(std::string_view hint)
{
	return service_.setHint(spec_.id, hint);
}

Error Parameter::setOptions(const std::vector<ChoiceOption>& options)
{
	return service_.setOptions(spec_.id, options);
}

Error Parameter::setRange(const std::vector<double>& minimum, const std::vector<double>& maximum)
{
	return service_.setRange(spec_.id, minimum, maximum);
}

Error Parameter::setDisplayRange(const std::vector<double>& minimum, const std::vector<double>& maximum)
{
	return service_.setDisplayRange(spec_.id, minimum, maximum);
}

int Parameter::keyframeCount() const
{
	int out = 0;
	return service_.keyframeCount(spec_.id, out).ok() ? out : 0;
}

Error Parameter::deleteAllKeyframes()
{
	return service_.deleteAllKeyframes(spec_.id);
}

Error Parameter::noMeta() const
{
	return Error::make(ErrorCode::Unsupported, "The effect has no meta manager").with("parameter", spec_.key);
}

std::vector<std::string> Parameter::tags() const
{
	if (meta_ != nullptr && meta_->has(spec_.id)) {
		return meta_->tags(spec_.id);
	}
	return spec_.tags;
}

bool Parameter::hasTag(std::string_view tag) const
{
	const std::vector<std::string> list = tags();
	return std::find(list.begin(), list.end(), tag) != list.end();
}

Error Parameter::setTags(std::vector<std::string> tags)
{
	if (meta_ == nullptr) {
		return noMeta();
	}
	meta_->setTags(spec_.id, std::move(tags));
	return Error::none();
}

Error Parameter::addTag(std::string_view tag)
{
	if (meta_ == nullptr) {
		return noMeta();
	}
	meta_->addTag(spec_.id, tag);
	return Error::none();
}

Error Parameter::removeTag(std::string_view tag)
{
	if (meta_ == nullptr) {
		return noMeta();
	}
	meta_->removeTag(spec_.id, tag);
	return Error::none();
}

std::optional<Value> Parameter::meta(std::string_view name) const
{
	if (meta_ != nullptr && meta_->has(spec_.id)) {
		return meta_->meta(spec_.id, name);
	}
	auto it = spec_.meta.find(std::string(name));
	if (it == spec_.meta.end()) {
		return std::nullopt;
	}
	return it->second;
}

std::map<std::string, Value> Parameter::allMeta() const
{
	if (meta_ != nullptr && meta_->has(spec_.id)) {
		return meta_->metaOf(spec_.id);
	}
	return spec_.meta;
}

Error Parameter::setMeta(std::string_view name, Value value)
{
	if (meta_ == nullptr) {
		return noMeta();
	}
	meta_->setMeta(spec_.id, name, std::move(value));
	return Error::none();
}

Error Parameter::removeMeta(std::string_view name)
{
	if (meta_ == nullptr) {
		return noMeta();
	}
	meta_->removeMeta(spec_.id, name);
	return Error::none();
}

// ---- ParameterModel -----------------------------------------------------------------------------

ParameterModel::ParameterModel(ParameterService& service, MetaManager* meta) : service_(service), meta_(meta)
{
	rebuild();
}

void ParameterModel::rebuild()
{
	owned_.clear();
	order_.clear();
	byId_.clear();
	byKey_.clear();
	for (ParameterId id : service_.ids()) {
		const ParameterSpec* spec = service_.spec(id);
		if (spec == nullptr) {
			continue;
		}
		auto parameter = std::make_unique<Parameter>(*spec, service_, meta_);
		Parameter* raw = parameter.get();
		owned_.push_back(std::move(parameter));
		order_.push_back(raw);
		byId_[raw->id()] = raw;
		byKey_[raw->key()] = raw;
	}
	for (Parameter* parameter : order_) {
		const ParameterId parent = parameter->spec().parent;
		if (parent == kParameterIdRoot) {
			continue;
		}
		auto it = byId_.find(parent);
		if (it == byId_.end()) {
			continue;
		}
		parameter->parent_ = it->second;
		it->second->children_.push_back(parameter);
	}
}

void ParameterModel::setMetaManager(MetaManager* meta)
{
	meta_ = meta;
	for (Parameter* parameter : order_) {
		parameter->meta_ = meta;
	}
}

Parameter* ParameterModel::find(ParameterId id) const noexcept
{
	auto it = byId_.find(id);
	return it == byId_.end() ? nullptr : it->second;
}

Parameter* ParameterModel::find(std::string_view key) const noexcept
{
	auto it = byKey_.find(std::string(key));
	return it == byKey_.end() ? nullptr : it->second;
}

std::vector<Parameter*> ParameterModel::roots() const
{
	std::vector<Parameter*> out;
	for (Parameter* parameter : order_) {
		if (parameter->parent_ == nullptr) {
			out.push_back(parameter);
		}
	}
	return out;
}

std::vector<Parameter*> ParameterModel::withTag(std::string_view tag) const
{
	std::vector<Parameter*> out;
	for (Parameter* parameter : order_) {
		if (parameter->hasTag(tag)) {
			out.push_back(parameter);
		}
	}
	return out;
}

OFXGRIP_NAMESPACE_END
