// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
/*!
	@file  Flags.hpp
	@brief The two flag sets a parameter carries: host flags the host presents, and app flags
	       OfxGrip keeps for itself.

	Introduced in OfxGrip 0.1.0. FxGrip packs its app flags into the unused high bits of FxPlug's
	flags word. OfxGrip keeps the two sets apart: `HostFlags` lowers to OpenFX properties
	(`Secret`, `Enabled`, `Animates`, `Persistant`, `EvaluateOnChange`, `CanUndo`, `GroupOpen`),
	and `AppFlags` never reaches the host. The configuration strings keep FxGrip's names, so a
	FxGrip parameter record converts without edits.
*/
#pragma once

#include <ofxgrip/core/Types.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

OFXGRIP_NAMESPACE_BEGIN

/*! A flag the host acts on. */
enum class HostFlag : std::uint32_t {
	NotAnimatable = 1u << 0,          /*!< The parameter holds one value and no keyframes. */
	Hidden = 1u << 1,                 /*!< The host hides the parameter (OFX `Secret`). */
	Disabled = 1u << 2,               /*!< The host shows the parameter greyed out (OFX `Enabled` = 0). */
	Collapsed = 1u << 3,              /*!< A group starts closed (OFX `GroupOpen` = 0). */
	DontSave = 1u << 4,               /*!< The value is not persisted (OFX `Persistant` = 0). */
	NoEvaluateOnChange = 1u << 5,     /*!< A change does not trigger a render (OFX `EvaluateOnChange` = 0). */
	NoUndo = 1u << 6,                 /*!< Changes are not undoable (OFX `CanUndo` = 0). */
	CustomUI = 1u << 7,               /*!< The parameter vends a custom interact where the host allows one. */
	IgnoreMinMax = 1u << 8,           /*!< Values may pass outside the declared range. */
	UseFullViewWidth = 1u << 11,      /*!< The control spans the inspector width. FxPlug only; kept for the record. */
};

/*! A flag OfxGrip acts on. None of these reaches the host. */
enum class AppFlag : std::uint32_t {
	NoState = 1u << 0,        /*!< The value stays out of the render snapshot. */
	PresetNoValue = 1u << 1,  /*!< A preset omits the parameter's value. */
	PresetNoTags = 1u << 2,   /*!< A preset omits the parameter's tags. */
	PresetNoMeta = 1u << 3,   /*!< A preset omits the parameter's meta. */
	HiddenProxy = 1u << 4,    /*!< The parameter is hidden in proxy mode. */
	InDebugMode = 1u << 5,    /*!< The parameter is shown only in debug mode. */
	NoDebug = 1u << 6,        /*!< The parameter is not shown in debug mode. */
};

/*! A set of flags over an enum whose cases are single bits. */
template <typename Flag>
class FlagSet {
public:
	using Underlying = std::uint32_t;

	constexpr FlagSet() = default;
	constexpr explicit FlagSet(Underlying bits) : bits_(bits) {}
	constexpr FlagSet(Flag flag) : bits_(static_cast<Underlying>(flag)) {}  // NOLINT(google-explicit-constructor)

	constexpr bool has(Flag flag) const noexcept { return (bits_ & static_cast<Underlying>(flag)) != 0; }
	constexpr FlagSet& set(Flag flag, bool on = true) noexcept
	{
		if (on) {
			bits_ |= static_cast<Underlying>(flag);
		} else {
			bits_ &= ~static_cast<Underlying>(flag);
		}
		return *this;
	}
	constexpr FlagSet& clear(Flag flag) noexcept { return set(flag, false); }
	constexpr Underlying bits() const noexcept { return bits_; }
	constexpr bool none() const noexcept { return bits_ == 0; }
	constexpr bool operator==(const FlagSet&) const noexcept = default;
	constexpr FlagSet operator|(FlagSet other) const noexcept { return FlagSet(bits_ | other.bits_); }
	constexpr FlagSet& operator|=(FlagSet other) noexcept
	{
		bits_ |= other.bits_;
		return *this;
	}

private:
	Underlying bits_{0};
};

using HostFlags = FlagSet<HostFlag>;
using AppFlags = FlagSet<AppFlag>;

constexpr HostFlags operator|(HostFlag a, HostFlag b) noexcept { return HostFlags(a) | HostFlags(b); }
constexpr AppFlags operator|(AppFlag a, AppFlag b) noexcept { return AppFlags(a) | AppFlags(b); }

/*! The configuration string of a host flag (`"hidden"`, `"notanimatable"`, …). */
std::string_view toString(HostFlag flag) noexcept;
/*! The configuration string of an app flag (`"nostate"`, `"presetnovalue"`, …). */
std::string_view toString(AppFlag flag) noexcept;
/*! Parses one host-flag name. */
bool parseHostFlag(std::string_view name, HostFlag& out) noexcept;
/*! Parses one app-flag name. */
bool parseAppFlag(std::string_view name, AppFlag& out) noexcept;

/*! The result of parsing a flag list. */
struct ParsedFlags {
	HostFlags host;
	AppFlags app;
	std::vector<std::string> unknown;  /*!< Names that matched neither set. */
};

/*! Parses a list of flag names. A bare or `+`-prefixed name sets its flag; a `-`-prefixed name
    clears it. The parse starts from `initial`, so a list can adjust defaults. */
ParsedFlags parseFlags(const std::vector<std::string>& names, HostFlags initialHost = {}, AppFlags initialApp = {});
/*! Splits a string on spaces, commas, semicolons, and pipes, then parses the pieces. */
ParsedFlags parseFlags(std::string_view list, HostFlags initialHost = {}, AppFlags initialApp = {});
/*! The names of every set flag, for logs and JSON. */
std::vector<std::string> flagNames(HostFlags host, AppFlags app);

OFXGRIP_NAMESPACE_END
