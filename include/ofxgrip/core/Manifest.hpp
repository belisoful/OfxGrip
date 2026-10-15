// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
/*!
	@file  Manifest.hpp
	@brief The effect manifest: the data record that declares an effect's identity, version,
	       capabilities, clips, and parameters.

	Introduced in OfxGrip 0.1.0. The manifest is OfxGrip's form of FxGrip's registration record and
	parameter configuration dictionaries (`FxGripTypes.h`). It is authored in C++ with the static
	factories on `ParameterSpec`, or loaded from JSON (`ManifestJson.hpp`). The OFX adapter lowers it
	to descriptors at describe time, and the extension bus hands it to observers before lowering so
	they can add or edit records, as FxGrip's add-parameters notification does.

	Parameter identity is declared, never derived: every parameter has a numeric `id` and a string
	`key`. `EffectManifest::validate` checks both for uniqueness, keeps the reserved range for the
	framework, and checks option keys and host limits.
*/
#pragma once

#include <ofxgrip/core/Error.hpp>
#include <ofxgrip/core/Flags.hpp>
#include <ofxgrip/core/Types.hpp>
#include <ofxgrip/core/Value.hpp>

#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

OFXGRIP_NAMESPACE_BEGIN

/*! The kind of a parameter. The numbers 0 to 19 mirror FxGrip's `FxParameterType` for the FxPlug
    native kinds, and 120 and up mirror FxGrip's custom controls, so a record converts between the
    two frameworks without a table. */
enum class ParameterType : std::int16_t {
	None = 0,
	Angle = 1,
	RGBA = 2,
	RGB = 3,
	Custom = 4,
	Float = 5,
	FontMenu = 6,
	Gradient = 7,
	Help = 8,
	Histogram = 9,
	ImageRef = 10,
	Int = 11,
	PathID = 12,
	Percent = 13,
	Point = 14,
	Menu = 15,
	PushButton = 16,
	String = 17,
	Toggle = 18,
	Group = 19,
	Point3D = 20,
	Blob = 21,       /*!< A byte-string value. Named `Blob`, as `ValueKind` is, because GCC's -Wshadow objects to `Bytes`. */

	Section = 120,
	Random = 121,
	Capsule = 122,
	Banner = 123,
	Presets = 124,
	Status = 125,
	Progress = 126,
	Switch = 127,
	Divider = 128,
	WebView = 129,
	VideoView = 130,
	LiveImage = 131,
	TrackingOpacity = 132,
	Analyzer = 133,
	ObjectTracker = 134,
};

/*! The type-key string of a parameter type (`"float"`, `"menu"`, `"section"`, …), as FxGrip
    writes it in a configuration record. */
std::string_view toString(ParameterType type) noexcept;
/*! Parses a type-key string. `"integer"` and `"int"` both parse as `Int`; `"button"` as
    `PushButton`; `"toggle"` and `"bool"` as `Toggle`; `"menu"` and `"choice"` as `Menu`. */
bool parseParameterType(std::string_view text, ParameterType& out) noexcept;
/*! Whether a parameter of this type carries a value that belongs in the render snapshot. Groups,
    buttons, help, and the display-only controls carry none. */
bool parameterTypeHasValue(ParameterType type) noexcept;
/*! Whether the type is a container whose children are declared under it. */
constexpr bool parameterTypeIsGroup(ParameterType type) noexcept
{
	return type == ParameterType::Group || type == ParameterType::Section;
}

/*! One option of a choice parameter. The key is stable across versions; the host index is
    generated from the option order. */
struct ChoiceOption {
	std::string key;
	std::string label;
	bool operator==(const ChoiceOption&) const = default;
};

/*! A per-host override of a parameter record. The host name is a profile family name such as
    `"resolve"`, `"nuke"`, `"natron"`, or `"ofx"` for every OpenFX host. */
struct HostOverride {
	bool exclude{false};             /*!< The parameter is not declared on this host. */
	std::optional<std::string> label; /*!< A different label on this host. */
	bool operator==(const HostOverride&) const = default;
};

/*! The edits a choice or toggle applies to other parameters when it changes: FxGrip's target
    preset. Every map is keyed by the target parameter's key. The sections apply in the order
    values, flags, tags, meta, labels; labels go last because some hosts misreport a string
    parameter when its label changes earlier in the same pass. Flag entries use the `parseFlags`
    syntax (`"+hidden"`, `"-disabled"`), applied over the target's current host flags. */
struct TargetPreset {
	std::map<std::string, Value> values;
	std::map<std::string, std::vector<std::string>> flags;
	std::map<std::string, std::vector<std::string>> tags;
	std::map<std::string, std::map<std::string, Value>> meta;
	std::map<std::string, std::string> labels;
	bool empty() const noexcept { return values.empty() && flags.empty() && tags.empty() && meta.empty() && labels.empty(); }
	bool operator==(const TargetPreset&) const = default;
};

/*! The declaration of one parameter. */
struct ParameterSpec {
	ParameterId id{kParameterIdNone};
	std::string key;                       /*!< The OFX parameter name and the preset key. */
	ParameterType type{ParameterType::None};
	std::string label;
	std::string hint;
	ParameterId parent{kParameterIdRoot};  /*!< The containing group, or the root. */

	Value defaultValue;
	Value resetValue;                      /*!< The value a reset restores; empty means the default. */
	std::vector<double> minimum;           /*!< Per-dimension hard minimum; empty means unbounded. */
	std::vector<double> maximum;
	std::vector<double> displayMinimum;    /*!< Per-dimension slider minimum; empty means the hard range. */
	std::vector<double> displayMaximum;
	double increment{0};                   /*!< The drag increment; 0 means the host default. */
	int digits{2};                         /*!< Decimal digits for a double. */

	HostFlags hostFlags;
	AppFlags appFlags;
	std::vector<ChoiceOption> options;
	std::vector<std::string> tags;
	std::map<std::string, Value> meta;
	std::string visibleWhen;               /*!< A visibility rule (phase 1). Empty means always. */
	std::string enabledWhen;               /*!< An enablement rule (phase 1). Empty means always. */
	std::map<std::string, HostOverride> hosts;
	/*! Target presets keyed by the option key of a choice, or `"true"` and `"false"` for a
	    toggle. Applied by the meta extension on a user change (phase 1). */
	std::map<std::string, TargetPreset> targetPresets;
	std::string selector;                  /*!< The click action name of a push button. */
	std::vector<ParameterSpec> children;   /*!< Declared under a group; flattened at validation. */

	/*! Whether the parameter belongs in the render snapshot: it has a value and lacks `NoState`. */
	bool hasState() const noexcept;
	/*! The option index of a stable key, or -1. */
	int optionIndex(std::string_view optionKey) const noexcept;
	/*! The stable key of an option index, or empty. */
	std::string_view optionKey(int index) const noexcept;
	/*! The number of value dimensions: 1 for scalars, 2 for points, 3 for 3D points and RGB, 4 for
	    RGBA, 0 for everything without a numeric value. */
	int dimensions() const noexcept;

	/*! Factories for the common kinds. Each sets `id`, `key`, `type`, `label`, and the default. */
	static ParameterSpec floatParam(ParameterId id, std::string key, std::string label, double defaultValue, double minimum, double maximum);
	static ParameterSpec intParam(ParameterId id, std::string key, std::string label, int defaultValue, int minimum, int maximum);
	static ParameterSpec boolParam(ParameterId id, std::string key, std::string label, bool defaultValue);
	static ParameterSpec angleParam(ParameterId id, std::string key, std::string label, double defaultValue);
	static ParameterSpec percentParam(ParameterId id, std::string key, std::string label, double defaultValue);
	static ParameterSpec pointParam(ParameterId id, std::string key, std::string label, Point2D defaultValue);
	static ParameterSpec rgbParam(ParameterId id, std::string key, std::string label, RGB defaultValue);
	static ParameterSpec rgbaParam(ParameterId id, std::string key, std::string label, RGBA defaultValue);
	static ParameterSpec stringParam(ParameterId id, std::string key, std::string label, std::string defaultValue);
	static ParameterSpec menuParam(ParameterId id, std::string key, std::string label, std::vector<ChoiceOption> options, int defaultIndex = 0);
	static ParameterSpec buttonParam(ParameterId id, std::string key, std::string label, std::string selector = {});
	static ParameterSpec groupParam(ParameterId id, std::string key, std::string label, std::vector<ParameterSpec> children = {});

	/*! Chaining setters. */
	ParameterSpec& withHint(std::string text);
	ParameterSpec& withDisplayRange(double minimum, double maximum);
	ParameterSpec& withFlags(HostFlags host, AppFlags app = {});
	ParameterSpec& withTags(std::vector<std::string> list);
	ParameterSpec& withMeta(std::string name, Value value);
	ParameterSpec& withParent(ParameterId groupId);
	ParameterSpec& withVisibleWhen(std::string rule);
	ParameterSpec& withEnabledWhen(std::string rule);
	ParameterSpec& withHost(std::string host, HostOverride override);
	ParameterSpec& withTargetPreset(std::string option, TargetPreset preset);
	ParameterSpec& withResetValue(Value value);
};

/*! The declaration of an input clip. The output clip is implicit. */
struct ClipSpec {
	std::string name;                      /*!< `"Source"` for the filter input, `"Mask"`, … */
	std::string label;
	bool optional{false};
	bool isMask{false};
	bool temporalAccess{false};            /*!< The effect fetches frames at other times. */
	std::vector<Components> components{Components::RGBA};
	bool operator==(const ClipSpec&) const = default;
};

/*! What the effect can do, which the adapter declares to the host. The GPU flags are the paths
    the effect's kernels resolve on; the engine computes them in phase 3. */
struct Capabilities {
	bool supportsTiles{true};
	bool supportsMultiResolution{true};
	bool temporalClipAccess{false};
	bool hostFrameThreading{true};
	bool noSpatialAwareness{false};        /*!< Resolve runs such effects during LUT generation. */
	bool fieldRenderTwiceAlways{false};
	ThreadSafety threadSafety{ThreadSafety::FullySafe};
	std::vector<BitDepth> depths{BitDepth::UInt8, BitDepth::UInt16, BitDepth::Float};
	bool cpuRender{true};
	bool metalRender{false};
	bool cudaRender{false};
	bool cudaStream{false};
	bool openCLRender{false};
	bool openGLRender{false};
	bool operator==(const Capabilities&) const = default;
};

/*! Who the effect is. */
struct EffectIdentity {
	std::string ofxId;                     /*!< Reverse-DNS identifier, for example `com.vendor.Glow`. */
	std::string uuid;                      /*!< The FxGrip-style UUID, shared with presets. */
	std::string name;                      /*!< The display label. */
	std::string shortName;
	std::string grouping;                  /*!< The menu group, for example `"OfxGrip"`. */
	std::string description;
	EffectKind kind{EffectKind::Filter};
	std::vector<EffectKind> extraContexts; /*!< Further OFX contexts the effect also supports. */
	bool operator==(const EffectIdentity&) const = default;
};

/*! One migration step, run when a saved instance's schema version is older. A rename copies the
    value of a still-declared old parameter (keep it declared, hidden, and `NoState` so the host
    persists it) into the new parameter once, then the stored schema version advances. Value
    transforms beyond renames run in the effect's `onMigrate` hook. */
struct Migration {
	unsigned fromSchemaVersion{0};
	std::map<std::string, std::string> renames;  /*!< old key → new key */
	bool operator==(const Migration&) const = default;
};

/*! An effect manifest. */
struct EffectManifest {
	unsigned schemaVersion{1};
	EffectIdentity identity;
	Version version{1, 0, 0};
	std::vector<unsigned> deprecatedMajors;
	Capabilities capabilities;
	std::vector<ClipSpec> clips;
	std::vector<ParameterSpec> parameters;
	std::vector<Migration> migrations;
	std::vector<std::string> priorUuids;   /*!< Earlier identities whose presets still apply. */
	/*! Free-form effect properties for extensions. The framework reads `manageMeta` and
	    `manageVisibility` (both default true) to decide whether to load its meta and visibility
	    extensions on an instance. */
	std::map<std::string, Value> properties;

	/*! Checks the manifest. The first failure is returned with context tags naming the parameter. */
	Error validate() const;
	/*! Every parameter in declaration order with groups flattened, each child's `parent` set. */
	std::vector<ParameterSpec> flattenedParameters() const;
	/*! The parameter with an ID, searching groups, or null. */
	const ParameterSpec* find(ParameterId id) const noexcept;
	/*! The parameter with a key, searching groups, or null. */
	const ParameterSpec* find(std::string_view key) const noexcept;
	/*! The clip with a name, or null. */
	const ClipSpec* findClip(std::string_view name) const noexcept;
	/*! The standard clips of the effect kind when `clips` is empty: `Source` for a filter,
	    `SourceFrom` and `SourceTo` for a transition, none for a generator. */
	std::vector<ClipSpec> effectiveClips() const;
};

OFXGRIP_NAMESPACE_END
