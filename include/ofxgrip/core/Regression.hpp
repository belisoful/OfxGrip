// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
/*!
	@file  Regression.hpp
	@brief Development-time checks of a manifest that report without blocking the load.

	Introduced in OfxGrip 0.3.0. This is FxGrip's `FxGripRegression`. `validate()` rejects a
	manifest that cannot work; these checks report what still looks wrong: a UUID that is not a
	UUID, a version of 0, an identifier without a reverse-DNS dot, a parameter without a label or
	hint, a choice with fewer than two options, a range with no width, an About link that is
	not a URL, a presets control whose tag lists nothing. The extension runs them at load and
	logs each report when the manifest sets the `regression` property.
*/
#pragma once

#include <ofxgrip/core/Extension.hpp>
#include <ofxgrip/core/HostServices.hpp>
#include <ofxgrip/core/Manifest.hpp>

#include <string>
#include <vector>

OFXGRIP_NAMESPACE_BEGIN

/*! One report. */
struct RegressionReport {
	std::string check;     /*!< `uuid`, `version`, `identifier`, `label`, `hint`, `options`, `range`, `about`, `presets` */
	LogLevel level{LogLevel::Warning};
	std::string message;
	bool operator==(const RegressionReport&) const = default;
};

/*! Runs every check. */
std::vector<RegressionReport> checkManifest(const EffectManifest& manifest);
/*! Whether text is an 8-4-4-4-12 hexadecimal UUID. */
bool looksLikeUuid(std::string_view text) noexcept;

/*! The extension. */
class RegressionExtension final : public Extension {
public:
	std::string name() const override { return "Regression"; }
	std::vector<EventId> interests() const override { return {}; }
	void handle(Event&) override {}
	bool load(EffectHost& host, int index) override;
	const std::vector<RegressionReport>& reports() const noexcept { return reports_; }

private:
	std::vector<RegressionReport> reports_;
};

OFXGRIP_NAMESPACE_END
