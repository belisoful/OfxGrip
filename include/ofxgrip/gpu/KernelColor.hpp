// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
/*!
	@file  KernelColor.hpp
	@brief The embedded colourspace conversion kernel and its parsed build per API.

	Introduced in OfxGrip 0.5.0. This is FxGrip's `FxGripShaderConverter` as a kernel.
	`kernels/GripColorConvert.slang` decodes a texel to linear light, applies a gamut matrix,
	and encodes. The kernels library embeds its Metal, C++, SPIR-V, and CUDA builds, so the
	conversion runs on every backend a kernel runs on. `KernelEngine::run` dispatches it around
	a kernel whose `KernelColorPass` is active.
*/
#pragma once

#include <ofxgrip/core/ColorGamut.hpp>
#include <ofxgrip/core/KernelCatalog.hpp>
#include <ofxgrip/core/KernelLayout.hpp>
#include <ofxgrip/core/KernelValues.hpp>

OFXGRIP_NAMESPACE_BEGIN

/*! The name of the conversion kernel in a catalog and in a backend's program cache. */
inline constexpr const char* kColorConvertKernelName = "GripColorConvert";

/*! The conversion kernel's build for one API. */
struct KernelColorBuild {
	const KernelImplementation* implementation{nullptr};
	KernelLayout layout;
	KernelBindingPlan plan;
};

/*! The conversion kernel's record: one implementation per embedded build. */
const KernelRecord& colorConvertKernelRecord();

/*! The conversion kernel's build for an API, or null when the kernel embeds none for it or its
    layout does not parse. `None` is the CPU build. The table is built once and never destroyed. */
const KernelColorBuild* kernelColorBuild(GpuApi api);

/*! The conversion kernel's parameter values for one conversion. `premultiplied` says the
    image's color components carry the alpha product. */
KernelValueMap colorConvertValues(const ColorConversion& conversion, bool premultiplied);

OFXGRIP_NAMESPACE_END
