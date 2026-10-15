// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
/*!
	@file  About.hpp
	@brief The About menu: a choice parameter built from the manifest's `about` table whose rows
	       open links or show dialogs, and the extension that acts on a selection.

	Introduced in OfxGrip 0.3.0. This is FxGrip's `FxGripAboutMenu` lowered to a standard choice
	parameter, because OpenFX hosts have no custom inspector views (Q-RESOLVE-019). Row 0 is the
	main text or the menu's name; the entries follow in order. Selecting a row runs its action
	and the menu snaps back to row 0, so the control is momentary.
*/
#pragma once

#include <ofxgrip/core/Extension.hpp>
#include <ofxgrip/core/Manifest.hpp>

#include <string>
#include <vector>

OFXGRIP_NAMESPACE_BEGIN

/*! The choice parameter that presents the menu. Carries no state and never animates. */
ParameterSpec aboutMenuParameter(const AboutMenu& menu, ParameterId id, std::string key);

/*! The extension. */
class AboutExtension final : public Extension {
public:
	AboutExtension(AboutMenu menu, ParameterId parameter) : menu_(std::move(menu)), parameter_(parameter) {}
	std::string name() const override { return "About"; }
	std::vector<EventId> interests() const override { return {EventId::InstanceChanged}; }
	int priority(EventId) const override { return -12; }
	void handle(Event& event) override;

	const AboutMenu& menu() const noexcept { return menu_; }
	ParameterId parameter() const noexcept { return parameter_; }
	/*! The URLs opened so far, for tests and logs. */
	const std::vector<std::string>& opened() const noexcept { return opened_; }

	/*! Runs the action of one entry: opens its link (trying the fallbacks when the first
	    fails) or shows its dialog. Text and separator rows do nothing. */
	void activate(const AboutEntry& entry);

private:
	AboutMenu menu_;
	ParameterId parameter_;
	std::vector<std::string> opened_;
};

OFXGRIP_NAMESPACE_END
