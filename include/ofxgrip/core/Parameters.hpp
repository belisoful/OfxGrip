// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
/*!
	@file  Parameters.hpp
	@brief The runtime parameter objects: one `Parameter` per host parameter over the parameter
	       service, grouped into a tree, looked up by ID or key.

	Introduced in OfxGrip 0.2.0. This is FxGrip's parameter model (`FxGripParameterBase`, the
	typed classes, groups, and the effect's subscripts) without the per-kind class hierarchy: OFX
	answers a parameter's type, flags, and options itself, so one class with typed accessors
	covers every kind. A parameter reads and writes through `ParameterService`, so every access
	posts the API-traffic events; tags and meta read through the instance's `MetaManager` when
	one is loaded and fall back to the manifest record otherwise.
*/
#pragma once

#include <ofxgrip/core/Error.hpp>
#include <ofxgrip/core/Flags.hpp>
#include <ofxgrip/core/HostServices.hpp>
#include <ofxgrip/core/Manifest.hpp>
#include <ofxgrip/core/MetaManager.hpp>
#include <ofxgrip/core/Types.hpp>
#include <ofxgrip/core/Value.hpp>

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

OFXGRIP_NAMESPACE_BEGIN

class ParameterModel;

/*! One parameter. */
class Parameter {
public:
	Parameter(ParameterSpec spec, ParameterService& service, MetaManager* meta);

	/*! Identity. */
	ParameterId id() const noexcept { return spec_.id; }
	const std::string& key() const noexcept { return spec_.key; }
	ParameterType type() const noexcept { return spec_.type; }
	const ParameterSpec& spec() const noexcept { return spec_; }
	bool isGroup() const noexcept { return parameterTypeIsGroup(spec_.type); }
	bool hasState() const noexcept { return spec_.hasState(); }
	int dimensions() const noexcept { return spec_.dimensions(); }
	const std::string& selector() const noexcept { return spec_.selector; }
	AppFlags appFlags() const noexcept { return spec_.appFlags; }

	/*! The tree. */
	Parameter* parent() const noexcept { return parent_; }
	const std::vector<Parameter*>& children() const noexcept { return children_; }
	/*! Every descendant in declaration order. */
	std::vector<Parameter*> descendants() const;

	/*! Values. The `Error` forms report the host's answer; the typed forms return the fallback on
	    any failure. */
	Error get(Time time, Value& out) const;
	Error getCurrent(Value& out) const;
	Value value(Time time) const;
	Value current() const;
	double number(Time time, double fallback = 0.0) const;
	int integer(Time time, int fallback = 0) const;
	bool boolean(Time time, bool fallback = false) const;
	Point2D point(Time time, Point2D fallback = {}) const;
	Point3D point3D(Time time, Point3D fallback = {}) const;
	RGB rgb(Time time, RGB fallback = {}) const;
	RGBA rgba(Time time, RGBA fallback = {}) const;
	std::string string(Time time, std::string fallback = {}) const;
	/*! The stable option key of a choice at a time, or empty. */
	std::string optionKey(Time time) const;
	/*! The stable option key of a choice at the current time, or empty. */
	std::string currentOptionKey() const;

	Error set(Time time, const Value& value);
	Error setCurrent(const Value& value);
	/*! Selects a choice option by its stable key. */
	Error setOption(Time time, std::string_view optionKey);
	Error setCurrentOption(std::string_view optionKey);
	/*! Writes the reset value, or the default when the record declares none. */
	Error reset(Time time);
	Error resetCurrent();

	/*! Host flags. */
	HostFlags flags() const;
	Error setFlags(HostFlags flags);
	bool hidden() const;
	Error setHidden(bool hidden);
	bool enabled() const;
	Error setEnabled(bool enabled);

	/*! Presentation. */
	std::string label() const;
	Error setLabel(std::string_view label);
	Error setHint(std::string_view hint);
	Error setOptions(const std::vector<ChoiceOption>& options);
	Error setRange(const std::vector<double>& minimum, const std::vector<double>& maximum);
	Error setDisplayRange(const std::vector<double>& minimum, const std::vector<double>& maximum);

	/*! Keyframes. */
	int keyframeCount() const;
	Error deleteAllKeyframes();

	/*! Tags and meta. Reads come from the meta manager when the effect loaded one, else from
	    the manifest record. Writes need the meta manager. */
	std::vector<std::string> tags() const;
	bool hasTag(std::string_view tag) const;
	Error setTags(std::vector<std::string> tags);
	Error addTag(std::string_view tag);
	Error removeTag(std::string_view tag);
	std::optional<Value> meta(std::string_view name) const;
	std::map<std::string, Value> allMeta() const;
	Error setMeta(std::string_view name, Value value);
	Error removeMeta(std::string_view name);

	ParameterService& service() const noexcept { return service_; }
	MetaManager* metaManager() const noexcept { return meta_; }

private:
	friend class ParameterModel;
	Error noMeta() const;

	ParameterSpec spec_;
	ParameterService& service_;
	MetaManager* meta_;
	Parameter* parent_{nullptr};
	std::vector<Parameter*> children_;
};

/*! The parameter objects of one instance. Built from the service's bound parameters. */
class ParameterModel {
public:
	explicit ParameterModel(ParameterService& service, MetaManager* meta = nullptr);
	ParameterModel(const ParameterModel&) = delete;
	ParameterModel& operator=(const ParameterModel&) = delete;

	/*! Rebuilds the objects from `service.ids()`. Existing pointers become invalid. */
	void rebuild();
	/*! Rebinds every parameter to a meta manager. */
	void setMetaManager(MetaManager* meta);

	Parameter* find(ParameterId id) const noexcept;
	Parameter* find(std::string_view key) const noexcept;
	Parameter* operator[](ParameterId id) const noexcept { return find(id); }
	Parameter* operator[](std::string_view key) const noexcept { return find(key); }

	/*! Every parameter in declaration order. */
	const std::vector<Parameter*>& all() const noexcept { return order_; }
	/*! The parameters without a parent. */
	std::vector<Parameter*> roots() const;
	/*! The parameters carrying a tag, in declaration order. */
	std::vector<Parameter*> withTag(std::string_view tag) const;
	std::size_t size() const noexcept { return order_.size(); }
	ParameterService& service() const noexcept { return service_; }
	MetaManager* metaManager() const noexcept { return meta_; }

private:
	ParameterService& service_;
	MetaManager* meta_;
	std::vector<std::unique_ptr<Parameter>> owned_;
	std::vector<Parameter*> order_;
	std::map<ParameterId, Parameter*> byId_;
	std::map<std::string, Parameter*> byKey_;
};

OFXGRIP_NAMESPACE_END
