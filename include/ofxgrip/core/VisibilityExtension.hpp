// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
/*!
	@file  VisibilityExtension.hpp
	@brief The framework extension that applies `visibleWhen` and `enabledWhen` rules by setting
	       the hidden and disabled host flags.

	Introduced in OfxGrip 0.2.0. The rules evaluate on creation, on every change of a parameter a
	rule references, and on `SyncPrivateData`, as `Research/Comparison.md` §5.4 lays out for OFX.
	On a host where `Secret` is permanent after describe (Q-NUKE-010), a visibility rule lowers to
	the enabled flag instead.
*/
#pragma once

#include <ofxgrip/core/Extension.hpp>
#include <ofxgrip/core/Rules.hpp>

#include <map>
#include <set>
#include <string>
#include <vector>

OFXGRIP_NAMESPACE_BEGIN

/*! The extension. */
class VisibilityExtension final : public Extension {
public:
	/*! The priority rules apply at on a change: after target presets, before the author's
	    observers. */
	static constexpr int kPriorityRules = 0;

	std::string name() const override { return "Visibility"; }
	std::vector<EventId> interests() const override;
	int priority(EventId id) const override;
	void handle(Event& event) override;
	bool load(EffectHost& host, int index) override;

	/*! Re-evaluates every rule. */
	Error applyAll();
	/*! Re-evaluates the rules that reference a parameter key. */
	Error applyDependents(std::string_view key);
	/*! The parameters with rules, in declaration order. */
	const std::vector<ParameterId>& ruled() const noexcept { return ruled_; }
	/*! Rules that failed to parse at load, by parameter key. */
	const std::map<std::string, std::string>& parseFailures() const noexcept { return parseFailures_; }

private:
	struct Entry {
		ParameterId id{kParameterIdNone};
		Rule visible;
		Rule enabled;
	};
	Error apply(const Entry& entry);

	std::vector<ParameterId> ruled_;
	std::map<ParameterId, Entry> entries_;
	std::map<std::string, std::set<ParameterId>> dependents_;
	std::map<std::string, std::string> parseFailures_;
	bool secretIsPermanent_{false};
};

OFXGRIP_NAMESPACE_END
