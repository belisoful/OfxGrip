// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
#include <ofxgrip/core/Manifest.hpp>

#include <ofxgrip/core/Rules.hpp>

#include <array>
#include <functional>
#include <utility>

OFXGRIP_NAMESPACE_BEGIN

namespace {

constexpr std::array<std::pair<ParameterType, std::string_view>, 37> kTypeNames{{
	{ParameterType::None, "none"},
	{ParameterType::Angle, "angle"},
	{ParameterType::RGBA, "rgba"},
	{ParameterType::RGB, "rgb"},
	{ParameterType::Custom, "custom"},
	{ParameterType::Float, "float"},
	{ParameterType::FontMenu, "font"},
	{ParameterType::Gradient, "gradient"},
	{ParameterType::Help, "help"},
	{ParameterType::Histogram, "histogram"},
	{ParameterType::ImageRef, "imageref"},
	{ParameterType::Int, "integer"},
	{ParameterType::PathID, "path"},
	{ParameterType::Percent, "percent"},
	{ParameterType::Point, "point"},
	{ParameterType::Menu, "menu"},
	{ParameterType::PushButton, "button"},
	{ParameterType::String, "string"},
	{ParameterType::Toggle, "toggle"},
	{ParameterType::Group, "group"},
	{ParameterType::Point3D, "point3d"},
	{ParameterType::Blob, "bytes"},
	{ParameterType::Section, "section"},
	{ParameterType::Random, "random"},
	{ParameterType::Capsule, "capsule"},
	{ParameterType::Banner, "banner"},
	{ParameterType::Presets, "presets"},
	{ParameterType::Status, "status"},
	{ParameterType::Progress, "progress"},
	{ParameterType::Switch, "switch"},
	{ParameterType::Divider, "divider"},
	{ParameterType::WebView, "webview"},
	{ParameterType::VideoView, "videoview"},
	{ParameterType::LiveImage, "liveimage"},
	{ParameterType::TrackingOpacity, "trackingopacity"},
	{ParameterType::Analyzer, "analyzer"},
	{ParameterType::ObjectTracker, "objecttracker"},
}};

constexpr std::array<std::pair<std::string_view, ParameterType>, 6> kTypeAliases{{
	{"int", ParameterType::Int},
	{"bool", ParameterType::Toggle},
	{"boolean", ParameterType::Toggle},
	{"choice", ParameterType::Menu},
	{"double", ParameterType::Float},
	{"pushbutton", ParameterType::PushButton},
}};

bool validKey(std::string_view key)
{
	if (key.empty()) {
		return false;
	}
	for (char c : key) {
		const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '.';
		if (!ok) {
			return false;
		}
	}
	return true;
}

void flattenInto(const std::vector<ParameterSpec>& list, ParameterId parent, std::vector<ParameterSpec>& out)
{
	for (const ParameterSpec& spec : list) {
		ParameterSpec copy = spec;
		if (copy.parent == kParameterIdRoot && parent != kParameterIdRoot) {
			copy.parent = parent;
		}
		std::vector<ParameterSpec> children = std::move(copy.children);
		copy.children.clear();
		out.push_back(std::move(copy));
		flattenInto(children, spec.id, out);
	}
}

const ParameterSpec* findIn(const std::vector<ParameterSpec>& list, const std::function<bool(const ParameterSpec&)>& match) noexcept
{
	for (const ParameterSpec& spec : list) {
		if (match(spec)) {
			return &spec;
		}
		if (const ParameterSpec* inner = findIn(spec.children, match)) {
			return inner;
		}
	}
	return nullptr;
}

ParameterSpec base(ParameterId id, std::string key, ParameterType type, std::string label)
{
	ParameterSpec spec;
	spec.id = id;
	spec.key = std::move(key);
	spec.type = type;
	spec.label = std::move(label);
	return spec;
}

}  // namespace

std::string_view toString(ParameterType type) noexcept
{
	for (const auto& [t, name] : kTypeNames) {
		if (t == type) {
			return name;
		}
	}
	return "none";
}

bool parseParameterType(std::string_view text, ParameterType& out) noexcept
{
	for (const auto& [t, name] : kTypeNames) {
		if (name == text) {
			out = t;
			return true;
		}
	}
	for (const auto& [alias, t] : kTypeAliases) {
		if (alias == text) {
			out = t;
			return true;
		}
	}
	return false;
}

bool parameterTypeHasValue(ParameterType type) noexcept
{
	switch (type) {
	case ParameterType::None:
	case ParameterType::Help:
	case ParameterType::PushButton:
	case ParameterType::Group:
	case ParameterType::Section:
	case ParameterType::Capsule:
	case ParameterType::Banner:
	case ParameterType::Divider:
	case ParameterType::WebView:
	case ParameterType::VideoView:
	case ParameterType::LiveImage:
	case ParameterType::Analyzer:
		return false;
	default:
		return true;
	}
}

bool ParameterSpec::hasState() const noexcept
{
	return parameterTypeHasValue(type) && !appFlags.has(AppFlag::NoState);
}

int ParameterSpec::optionIndex(std::string_view optionKey) const noexcept
{
	for (std::size_t i = 0; i < options.size(); ++i) {
		if (options[i].key == optionKey) {
			return static_cast<int>(i);
		}
	}
	return -1;
}

std::string_view ParameterSpec::optionKey(int index) const noexcept
{
	if (index < 0 || static_cast<std::size_t>(index) >= options.size()) {
		return {};
	}
	return options[static_cast<std::size_t>(index)].key;
}

int ParameterSpec::dimensions() const noexcept
{
	switch (type) {
	case ParameterType::Float:
	case ParameterType::Int:
	case ParameterType::Angle:
	case ParameterType::Percent:
	case ParameterType::Toggle:
	case ParameterType::Switch:
	case ParameterType::Menu:
	case ParameterType::Presets:
	case ParameterType::Random:
	case ParameterType::Progress:
	case ParameterType::TrackingOpacity:
		return 1;
	case ParameterType::Point:
		return 2;
	case ParameterType::Point3D:
	case ParameterType::RGB:
		return 3;
	case ParameterType::RGBA:
		return 4;
	default:
		return 0;
	}
}

ParameterSpec ParameterSpec::floatParam(ParameterId id, std::string key, std::string label, double defaultValue, double minimum, double maximum)
{
	ParameterSpec spec = base(id, std::move(key), ParameterType::Float, std::move(label));
	spec.defaultValue = defaultValue;
	spec.minimum = {minimum};
	spec.maximum = {maximum};
	return spec;
}

ParameterSpec ParameterSpec::intParam(ParameterId id, std::string key, std::string label, int defaultValue, int minimum, int maximum)
{
	ParameterSpec spec = base(id, std::move(key), ParameterType::Int, std::move(label));
	spec.defaultValue = defaultValue;
	spec.minimum = {static_cast<double>(minimum)};
	spec.maximum = {static_cast<double>(maximum)};
	return spec;
}

ParameterSpec ParameterSpec::boolParam(ParameterId id, std::string key, std::string label, bool defaultValue)
{
	ParameterSpec spec = base(id, std::move(key), ParameterType::Toggle, std::move(label));
	spec.defaultValue = defaultValue;
	return spec;
}

ParameterSpec ParameterSpec::angleParam(ParameterId id, std::string key, std::string label, double defaultValue)
{
	ParameterSpec spec = base(id, std::move(key), ParameterType::Angle, std::move(label));
	spec.defaultValue = defaultValue;
	spec.minimum = {-360.0};
	spec.maximum = {360.0};
	return spec;
}

ParameterSpec ParameterSpec::percentParam(ParameterId id, std::string key, std::string label, double defaultValue)
{
	ParameterSpec spec = base(id, std::move(key), ParameterType::Percent, std::move(label));
	spec.defaultValue = defaultValue;
	spec.minimum = {0.0};
	spec.maximum = {1.0};
	return spec;
}

ParameterSpec ParameterSpec::pointParam(ParameterId id, std::string key, std::string label, Point2D defaultValue)
{
	ParameterSpec spec = base(id, std::move(key), ParameterType::Point, std::move(label));
	spec.defaultValue = defaultValue;
	return spec;
}

ParameterSpec ParameterSpec::rgbParam(ParameterId id, std::string key, std::string label, RGB defaultValue)
{
	ParameterSpec spec = base(id, std::move(key), ParameterType::RGB, std::move(label));
	spec.defaultValue = defaultValue;
	spec.minimum = {0.0, 0.0, 0.0};
	spec.maximum = {1.0, 1.0, 1.0};
	return spec;
}

ParameterSpec ParameterSpec::rgbaParam(ParameterId id, std::string key, std::string label, RGBA defaultValue)
{
	ParameterSpec spec = base(id, std::move(key), ParameterType::RGBA, std::move(label));
	spec.defaultValue = defaultValue;
	spec.minimum = {0.0, 0.0, 0.0, 0.0};
	spec.maximum = {1.0, 1.0, 1.0, 1.0};
	return spec;
}

ParameterSpec ParameterSpec::stringParam(ParameterId id, std::string key, std::string label, std::string defaultValue)
{
	ParameterSpec spec = base(id, std::move(key), ParameterType::String, std::move(label));
	spec.defaultValue = std::move(defaultValue);
	return spec;
}

ParameterSpec ParameterSpec::menuParam(ParameterId id, std::string key, std::string label, std::vector<ChoiceOption> options, int defaultIndex)
{
	ParameterSpec spec = base(id, std::move(key), ParameterType::Menu, std::move(label));
	spec.options = std::move(options);
	spec.defaultValue = defaultIndex;
	return spec;
}

ParameterSpec ParameterSpec::buttonParam(ParameterId id, std::string key, std::string label, std::string selector)
{
	ParameterSpec spec = base(id, std::move(key), ParameterType::PushButton, std::move(label));
	spec.selector = std::move(selector);
	return spec;
}

ParameterSpec ParameterSpec::groupParam(ParameterId id, std::string key, std::string label, std::vector<ParameterSpec> children)
{
	ParameterSpec spec = base(id, std::move(key), ParameterType::Group, std::move(label));
	spec.children = std::move(children);
	return spec;
}

ParameterSpec& ParameterSpec::withHint(std::string text)
{
	hint = std::move(text);
	return *this;
}

ParameterSpec& ParameterSpec::withDisplayRange(double minimumValue, double maximumValue)
{
	displayMinimum = {minimumValue};
	displayMaximum = {maximumValue};
	return *this;
}

ParameterSpec& ParameterSpec::withFlags(HostFlags host, AppFlags app)
{
	hostFlags = host;
	appFlags = app;
	return *this;
}

ParameterSpec& ParameterSpec::withTags(std::vector<std::string> list)
{
	tags = std::move(list);
	return *this;
}

ParameterSpec& ParameterSpec::withMeta(std::string name, Value value)
{
	meta[std::move(name)] = std::move(value);
	return *this;
}

ParameterSpec& ParameterSpec::withParent(ParameterId groupId)
{
	parent = groupId;
	return *this;
}

ParameterSpec& ParameterSpec::withVisibleWhen(std::string rule)
{
	visibleWhen = std::move(rule);
	return *this;
}

ParameterSpec& ParameterSpec::withEnabledWhen(std::string rule)
{
	enabledWhen = std::move(rule);
	return *this;
}

ParameterSpec& ParameterSpec::withHost(std::string host, HostOverride override)
{
	hosts[std::move(host)] = std::move(override);
	return *this;
}

ParameterSpec& ParameterSpec::withTargetPreset(std::string option, TargetPreset preset)
{
	targetPresets[std::move(option)] = std::move(preset);
	return *this;
}

ParameterSpec& ParameterSpec::withResetValue(Value value)
{
	resetValue = std::move(value);
	return *this;
}

Error EffectManifest::validate() const
{
	auto fail = [](std::string message, const ParameterSpec* spec) {
		Error e = Error::make(ErrorCode::InvalidManifest, std::move(message));
		if (spec != nullptr) {
			e.with("parameter", std::to_string(spec->id)).with("key", spec->key);
		}
		return e;
	};
	if (identity.ofxId.empty()) {
		return fail("The effect has no OFX identifier", nullptr);
	}
	for (char c : identity.ofxId) {
		if (c <= ' ' || c > '~') {
			return fail("The OFX identifier must be printable ASCII without whitespace", nullptr);
		}
	}
	if (identity.name.empty()) {
		return fail("The effect has no name", nullptr);
	}
	if (version.major == 0 && version.minor == 0 && version.patch == 0) {
		return fail("The effect version is 0.0.0", nullptr);
	}
	for (unsigned major : deprecatedMajors) {
		if (major >= version.major) {
			return fail("A deprecated major must be lower than the current major", nullptr);
		}
	}
	if (capabilities.depths.empty()) {
		return fail("The effect supports no bit depth", nullptr);
	}
	if (!licensing.empty()) {
		Version v;
		if (!licensing.productVersion.empty() && !Version::parse(licensing.productVersion, v)) {
			return fail("The licensing product version does not parse", nullptr);
		}
		if (!licensing.licensedVersion.empty() && licensing.licensedVersion != "*" && !Version::parse(licensing.licensedVersion, v)) {
			return fail("The licensed version does not parse", nullptr);
		}
		if (licensing.unlicensedWatermark && licensing.unlicensedWatermark->opacity < 0) {
			return fail("The unlicensed watermark opacity is negative", nullptr);
		}
		if (licensing.trialWatermark && licensing.trialWatermark->opacity < 0) {
			return fail("The trial watermark opacity is negative", nullptr);
		}
	}
	std::set<std::string> clipNames;
	for (const ClipSpec& clip : clips) {
		if (clip.name.empty()) {
			return fail("A clip has no name", nullptr);
		}
		if (clip.name == "Output") {
			return fail("The output clip is implicit and must not be declared", nullptr);
		}
		if (!clipNames.insert(clip.name).second) {
			return fail("Two clips share the name " + clip.name, nullptr);
		}
	}

	const std::vector<ParameterSpec> flat = flattenedParameters();
	std::set<ParameterId> ids;
	std::set<std::string> keys;
	for (const ParameterSpec& spec : flat) {
		if (spec.id == kParameterIdNone || spec.id == kParameterIdRoot) {
			return fail("A parameter has no identifier", &spec);
		}
		if (isReservedParameterId(spec.id)) {
			return fail("Identifiers 9991 to 9999 are reserved for the framework", &spec);
		}
		if (!ids.insert(spec.id).second) {
			return fail("Two parameters share an identifier", &spec);
		}
		if (!validKey(spec.key)) {
			return fail("A parameter key must be non-empty and use letters, digits, '_' or '.'", &spec);
		}
		if (!keys.insert(spec.key).second) {
			return fail("Two parameters share a key", &spec);
		}
		if (spec.type == ParameterType::None) {
			return fail("A parameter has no type", &spec);
		}
		if (spec.parent != kParameterIdRoot && ids.count(spec.parent) == 0) {
			bool found = false;
			for (const ParameterSpec& other : flat) {
				if (other.id == spec.parent && parameterTypeIsGroup(other.type)) {
					found = true;
					break;
				}
			}
			if (!found) {
				return fail("A parameter names a parent that is not a declared group", &spec);
			}
		}
		if (spec.type == ParameterType::Menu || spec.type == ParameterType::Presets) {
			std::set<std::string> optionKeys;
			for (const ChoiceOption& option : spec.options) {
				if (option.key.empty()) {
					return fail("A choice option has no key", &spec);
				}
				if (!optionKeys.insert(option.key).second) {
					return fail("Two options share the key " + option.key, &spec);
				}
			}
		}
		const int dims = spec.dimensions();
		if (dims > 0) {
			if (!spec.minimum.empty() && static_cast<int>(spec.minimum.size()) != dims && spec.minimum.size() != 1) {
				return fail("The minimum has the wrong number of dimensions", &spec);
			}
			if (!spec.maximum.empty() && static_cast<int>(spec.maximum.size()) != dims && spec.maximum.size() != 1) {
				return fail("The maximum has the wrong number of dimensions", &spec);
			}
			for (std::size_t i = 0; i < spec.minimum.size() && i < spec.maximum.size(); ++i) {
				if (spec.minimum[i] > spec.maximum[i]) {
					return fail("The minimum exceeds the maximum", &spec);
				}
			}
		}
	}
	for (const ParameterSpec& spec : flat) {
		Rule rule;
		if (Error e = Rule::parse(spec.visibleWhen, rule)) {
			return fail("The visibleWhen rule does not parse: " + e.developerMessage(), &spec);
		}
		for (const std::string& ref : rule.references()) {
			if (keys.count(ref) == 0) {
				return fail("The visibleWhen rule names an undeclared parameter " + ref, &spec);
			}
		}
		if (Error e = Rule::parse(spec.enabledWhen, rule)) {
			return fail("The enabledWhen rule does not parse: " + e.developerMessage(), &spec);
		}
		for (const std::string& ref : rule.references()) {
			if (keys.count(ref) == 0) {
				return fail("The enabledWhen rule names an undeclared parameter " + ref, &spec);
			}
		}
		if (spec.targetPresets.empty()) {
			continue;
		}
		const bool choice = spec.type == ParameterType::Menu || spec.type == ParameterType::Presets;
		const bool toggle = spec.type == ParameterType::Toggle || spec.type == ParameterType::Switch;
		if (!choice && !toggle) {
			return fail("Only a choice or a toggle carries target presets", &spec);
		}
		for (const auto& [option, preset] : spec.targetPresets) {
			if (choice && spec.optionIndex(option) < 0) {
				return fail("A target preset names an unknown option " + option, &spec);
			}
			if (toggle && option != "true" && option != "false") {
				return fail("A toggle's target presets are keyed by \"true\" and \"false\"", &spec);
			}
			auto checkKeys = [&](const auto& section) -> const std::string* {
				for (const auto& [key, unused] : section) {
					(void)unused;
					if (keys.count(key) == 0) {
						return &key;
					}
				}
				return nullptr;
			};
			const std::string* bad = checkKeys(preset.values);
			bad = bad != nullptr ? bad : checkKeys(preset.flags);
			bad = bad != nullptr ? bad : checkKeys(preset.tags);
			bad = bad != nullptr ? bad : checkKeys(preset.meta);
			bad = bad != nullptr ? bad : checkKeys(preset.labels);
			if (bad != nullptr) {
				return fail("A target preset names an undeclared parameter " + *bad, &spec);
			}
			for (const auto& [key, edits] : preset.flags) {
				const ParsedFlags parsed = parseFlags(edits);
				if (!parsed.unknown.empty()) {
					return fail("A target preset uses an unknown flag " + parsed.unknown.front(), &spec);
				}
			}
		}
	}
	for (const Migration& migration : migrations) {
		if (migration.fromSchemaVersion >= schemaVersion) {
			return fail("A migration must come from an older schema version", nullptr);
		}
	}
	for (const ManifestPreset& preset : presets) {
		if (preset.name.empty()) {
			return fail("A manifest preset has no name", nullptr);
		}
		for (const auto& [key, value] : preset.values) {
			if (keys.count(key) == 0) {
				return fail("The preset " + preset.name + " names an undeclared parameter " + key, nullptr);
			}
		}
		for (const auto& [key, list] : preset.tags) {
			if (keys.count(key) == 0) {
				return fail("The preset " + preset.name + " names an undeclared parameter " + key, nullptr);
			}
		}
		for (const auto& [key, entries] : preset.meta) {
			if (keys.count(key) == 0) {
				return fail("The preset " + preset.name + " names an undeclared parameter " + key, nullptr);
			}
		}
	}
	for (const AboutEntry& entry : about.entries) {
		if (entry.kind == AboutEntry::Kind::Link && entry.url.empty()) {
			return fail("An About link has no URL", nullptr);
		}
		if (entry.kind != AboutEntry::Kind::Separator && entry.label.empty()) {
			return fail("An About row has no label", nullptr);
		}
	}
	return Error::none();
}

bool EffectManifest::property(std::string_view name, bool fallback) const
{
	auto it = properties.find(std::string(name));
	return it == properties.end() ? fallback : it->second.toBool(fallback);
}

std::vector<ParameterSpec> EffectManifest::flattenedParameters() const
{
	std::vector<ParameterSpec> out;
	flattenInto(parameters, kParameterIdRoot, out);
	return out;
}

const ParameterSpec* EffectManifest::find(ParameterId id) const noexcept
{
	return findIn(parameters, [id](const ParameterSpec& s) { return s.id == id; });
}

const ParameterSpec* EffectManifest::find(std::string_view key) const noexcept
{
	return findIn(parameters, [key](const ParameterSpec& s) { return s.key == key; });
}

const ClipSpec* EffectManifest::findClip(std::string_view name) const noexcept
{
	for (const ClipSpec& clip : clips) {
		if (clip.name == name) {
			return &clip;
		}
	}
	return nullptr;
}

std::vector<ClipSpec> EffectManifest::effectiveClips() const
{
	if (!clips.empty()) {
		return clips;
	}
	switch (identity.kind) {
	case EffectKind::Filter:
	case EffectKind::General:
	case EffectKind::Retimer:
		return {ClipSpec{"Source", "Source", false, false, false, {Components::RGBA}}};
	case EffectKind::Transition:
		return {ClipSpec{"SourceFrom", "From", false, false, false, {Components::RGBA}},
		        ClipSpec{"SourceTo", "To", false, false, false, {Components::RGBA}}};
	case EffectKind::Generator:
		return {};
	}
	return {};
}

OFXGRIP_NAMESPACE_END
