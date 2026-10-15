// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
/*!
	@file  Analysis.hpp
	@brief The analysis pass: fetches a clip's frames over a range inside a host callback, hands
	       each to the effect's per-frame hook, and stores the records in the frame data.

	Introduced in OfxGrip 0.3.0. OpenFX has no host-driven analysis, so OfxGrip runs its own pass
	from a parameter action (an analyzer button). The pass runs synchronously inside the
	instance-changed action, reports through the progress suite, and stops when the host asks
	for an abort. Images are fetched with `clipGetImage`, so the pass needs a host that answers
	image fetches outside render; a host that refuses stores nothing and reports the failure.
*/
#pragma once

#include <ofxgrip/core/Error.hpp>
#include <ofxgrip/core/Types.hpp>

#include <string>

OFXGRIP_NAMESPACE_BEGIN

class Effect;

/*! What to analyze. Empty bounds mean the instance's whole time range. */
struct AnalysisRequest {
	std::string clip{"Source"};
	Time first{0};
	Time last{-1};       /*!< Below `first` means the end of the time range. */
	double step{1.0};
	bool backward{false};
	bool clearFirst{true};  /*!< Drop the stored records before the pass. */
};

/*! What a pass did. */
struct AnalysisResult {
	long framesAnalyzed{0};
	long framesStored{0};
	bool aborted{false};
	Error error;
};

/*! Runs a pass over the effect's clip. */
AnalysisResult runAnalysis(Effect& effect, const AnalysisRequest& request);

OFXGRIP_NAMESPACE_END
