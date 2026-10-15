// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
/*!
	@file  Debug.hpp
	@brief The debug menu: a choice parameter whose commands reveal hidden parameters during
	       development and hide them again.

	Introduced in OfxGrip 0.3.0. This is FxGrip's `FxGripDebugMenu` lowered to a standard choice
	parameter. Two gates must both allow it: the manifest property `debugMenu` and the factory's
	`allowsDebugFeatures()`, so a compiled override blocks a manifest edit from re-enabling the
	menu in a release. A parameter carrying `AppFlag::NoDebug` stays hidden; one carrying
	`AppFlag::InDebugMode` starts hidden and shows only in debug mode.
*/
#pragma once

#include <ofxgrip/core/Extension.hpp>
#include <ofxgrip/core/Manifest.hpp>

#include <set>
#include <string>
#include <vector>

OFXGRIP_NAMESPACE_BEGIN

/*! The choice parameter that presents the menu: `Debug`, `Reveal hidden parameters`,
    `Hide parameters again`. */
ParameterSpec debugMenuParameter(ParameterId id, std::string key);

/*! The extension. */
class DebugExtension final : public Extension {
public:
	explicit DebugExtension(ParameterId parameter) : parameter_(parameter) {}
	std::string name() const override { return "Debug"; }
	std::vector<EventId> interests() const override { return {EventId::InstanceChanged}; }
	int priority(EventId) const override { return -12; }
	void handle(Event& event) override;

	ParameterId parameter() const noexcept { return parameter_; }
	bool debugMode() const noexcept { return debugMode_; }
	/*! The parameters the last reveal un-hid. */
	const std::set<ParameterId>& revealed() const noexcept { return revealed_; }
	/*! Reveals every hidden parameter without `NoDebug`, except the framework's own. */
	Error reveal();
	/*! Hides the parameters the reveal un-hid. */
	Error hide();

private:
	ParameterId parameter_;
	bool debugMode_{false};
	std::set<ParameterId> revealed_;
};

OFXGRIP_NAMESPACE_END
