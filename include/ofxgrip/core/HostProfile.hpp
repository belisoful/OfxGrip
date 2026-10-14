// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
/*!
	@file  HostProfile.hpp
	@brief What the host is and what it can do: probed capabilities, identity, and the active
	       quirk set.

	Introduced in OfxGrip 0.1.0. The profile implements D-005. The adapter fills it at load from
	the host description properties, the fetched suites, and the host name and version. Code asks
	`has(Quirk)`, never for a host name. Resolution order: an explicit override (tests and the
	mini host inject quirks this way), then a capability the profile probed directly, then the
	compiled registry table matched by host-name prefix and version range.
*/
#pragma once

#include <ofxgrip/core/Quirks.hpp>
#include <ofxgrip/core/Types.hpp>

#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

OFXGRIP_NAMESPACE_BEGIN

/*! A host family, derived from the host name prefix. Code never branches on it; it feeds the
    registry lookup and the logs. */
enum class HostFamily : std::uint8_t {
	Unknown = 0,
	Resolve,
	Fusion,
	Nuke,
	Natron,
	Vegas,
	Catalyst,
	Flame,
	Baselight,
	Scratch,
	Tuttle,
	OfxGripMiniHost,
};

/*! The name of a host family, lower-case, as the manifest's `hosts` overrides use it. */
std::string_view toString(HostFamily family) noexcept;

/*! The Resolve edition, when known (Q-RESOLVE-001). */
enum class HostEdition : std::uint8_t {
	Unknown = 0,
	Free,
	Studio,
};

/*! Which suites the host vends, with the function pointers validated where a host is known to
    advertise a suite it does not implement (Q-RESOLVE-007). */
struct SuiteAvailability {
	bool property{false};
	bool imageEffect{false};
	bool parameter{false};
	bool memory{false};
	bool multiThread{false};
	bool messageV1{false};
	bool messageV2{false};          /*!< Vended with every function pointer non-null. */
	bool messageV2Vended{false};    /*!< Vended at all; false here with `messageV2Vended` true is Q-RESOLVE-007. */
	bool progressV1{false};
	bool progressV2{false};
	bool progressV2Vended{false};
	bool timeLine{false};
	bool parametric{false};
	bool openGLRender{false};
	bool draw{false};
	bool interact{false};
	bool dialog{false};
	bool operator==(const SuiteAvailability&) const = default;
};

/*! The host's probed capabilities, read from the host description property set. Every field
    has the spec default so a missing property (Q-OFX-ANY-015) reads as the spec says. */
struct HostCapabilities {
	bool supportsTiles{true};
	bool supportsMultiResolution{true};
	bool supportsMultipleClipDepths{false};
	bool supportsMultipleClipPARs{false};
	bool supportsOverlays{true};
	bool supportsCustomInteract{false};
	bool supportsChoiceAnimation{false};
	bool supportsBooleanAnimation{false};
	bool supportsStringAnimation{false};
	bool supportsCustomAnimation{false};
	bool supportsStrChoice{false};
	bool supportsStrChoiceAnimation{false};
	bool supportsParametricAnimation{false};
	bool supportsSetableFrameRate{false};
	bool supportsSetableFielding{false};
	bool supportsRenderQualityDraft{false};
	bool temporalClipAccess{false};
	bool isBackground{false};
	int maxParameters{-1};
	int maxPages{0};
	bool metalRender{false};
	bool cudaRender{false};
	bool cudaStream{false};
	bool openCLRender{false};
	bool openGLRender{false};
	std::string nativeOrigin;               /*!< The host's UI origin property, as a string. */
	std::vector<std::string> supportedContexts;
	std::vector<std::string> supportedComponents;
	std::vector<std::string> supportedPixelDepths;
	bool operator==(const HostCapabilities&) const = default;
};

/*! The profile. */
class HostProfile {
public:
	std::string hostName;                   /*!< `kOfxPropName`, for example `DaVinciResolveLite`. */
	std::string hostLabel;                  /*!< `kOfxPropLabel`. */
	Version hostVersion;                    /*!< `kOfxPropVersion`. */
	std::string hostVersionLabel;           /*!< `kOfxPropVersionLabel`. */
	int apiVersionMajor{1};                 /*!< `kOfxPropAPIVersion`; 1.0 when missing. */
	int apiVersionMinor{0};
	HostFamily family{HostFamily::Unknown};
	HostEdition edition{HostEdition::Unknown};
	SuiteAvailability suites;
	HostCapabilities capabilities;
	std::map<std::string, bool> extensionPropertyAccepted; /*!< Tolerant-write results (Q-OFX-ANY-014). */

	/*! Derives `family` and `edition` from the host name and label. */
	void deriveIdentity();
	/*! Whether a quirk applies. */
	bool has(Quirk quirk) const noexcept;
	/*! Forces a quirk on or off, for tests and for the mini host's imitation profiles. */
	void override(Quirk quirk, bool on);
	/*! Removes an override. */
	void clearOverride(Quirk quirk);
	/*! Every quirk that applies, in registry order. */
	std::vector<Quirk> activeQuirks() const;
	/*! One line per active quirk, for the load log. */
	std::string describe() const;

private:
	bool probed(Quirk quirk, bool& result) const noexcept;
	std::map<Quirk, bool> overrides_;
};

OFXGRIP_NAMESPACE_END
