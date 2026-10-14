// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
/*!
	@file  Suites.hpp
	@brief The host suites OfxGrip fetches, with validation, and the status mapping between
	       `OfxStatus` and `Error`.

	Introduced in OfxGrip 0.1.0. Only the property, image-effect, parameter, and memory suites are
	required (Q-OFX-ANY-002). Every other suite is optional, and the message suite V2 is used only
	when all three of its function pointers are non-null (Q-RESOLVE-007).
*/
#pragma once

#include <ofxgrip/core/Error.hpp>
#include <ofxgrip/core/HostProfile.hpp>

#include "ofxCore.h"
#include "ofxDialog.h"
#include "ofxDrawSuite.h"
#include "ofxGPURender.h"
#include "ofxImageEffect.h"
#include "ofxInteract.h"
#include "ofxMemory.h"
#include "ofxMessage.h"
#include "ofxMultiThread.h"
#include "ofxParam.h"
#include "ofxParametricParam.h"
#include "ofxProgress.h"
#include "ofxProperty.h"
#include "ofxTimeLine.h"

OFXGRIP_NAMESPACE_BEGIN

/*! Maps an OpenFX status to an error. `kOfxStatOK`, `kOfxStatReplyYes`, `kOfxStatReplyNo`, and
    `kOfxStatReplyDefault` map to success. */
Error statusToError(OfxStatus status, const char* what);
/*! Maps an error to the status the host receives. Cancellation maps to `kOfxStatOK`. */
OfxStatus errorToStatus(const Error& error) noexcept;
/*! The name of a status, for logs. */
const char* statusName(OfxStatus status) noexcept;

/*! The fetched suites. Pointers are null for suites the host does not vend. */
struct Suites {
	const OfxHost* host{nullptr};
	const OfxPropertySuiteV1* property{nullptr};
	const OfxImageEffectSuiteV1* imageEffect{nullptr};
	const OfxParameterSuiteV1* parameter{nullptr};
	const OfxMemorySuiteV1* memory{nullptr};
	const OfxMultiThreadSuiteV1* multiThread{nullptr};
	const OfxMessageSuiteV1* messageV1{nullptr};
	const OfxMessageSuiteV2* messageV2{nullptr};
	const OfxProgressSuiteV1* progressV1{nullptr};
	const OfxProgressSuiteV2* progressV2{nullptr};
	const OfxTimeLineSuiteV1* timeLine{nullptr};
	const OfxParametricParameterSuiteV1* parametric{nullptr};
	const OfxImageEffectOpenGLRenderSuiteV1* openGLRender{nullptr};
	const OfxDrawSuiteV1* draw{nullptr};
	const OfxInteractSuiteV1* interact{nullptr};
	const OfxDialogSuiteV1* dialog{nullptr};

	/*! Fetches every suite. Fails with `MissingHostFeature` when a required suite is absent. */
	Error fetch(const OfxHost* fromHost);
	/*! Whether the required suites are present. */
	bool ready() const noexcept { return property != nullptr && imageEffect != nullptr && parameter != nullptr && memory != nullptr; }
	/*! Whether message suite V2 has all three function pointers. */
	bool messageV2Usable() const noexcept;
	/*! The availability record for the host profile. */
	SuiteAvailability availability() const noexcept;
};

OFXGRIP_NAMESPACE_END
