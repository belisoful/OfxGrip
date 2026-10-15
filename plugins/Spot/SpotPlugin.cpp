// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
/*!
	@file  Spot.cpp
	@brief The OfxGrip kernel sample: one Slang compute kernel renders a soft spotlight on Metal
	       and on the CPU, with its parameters declared in the kernel's metadata block.

	The base manifest carries the identity and the About button; `Spot.slang` declares the
	center, radius, softness, outside gain, tint, and the Invert specialization constant. The
	factory scans the kernel at registration, declares the Metal path and the CPU fallback where
	the kernel builds resolve, and the effect renders through `KernelEffect`.
*/
#include <ofxgrip/ofx/KernelEffect.hpp>
#include <ofxgrip/ofx/Plugin.hpp>

OFXGRIP_DECLARE_KERNEL(Spot);

namespace {

using namespace ofxgrip;

enum : ParameterId {
	kAbout = 900,
};

EffectManifest baseManifest()
{
	EffectManifest m;
	m.identity.ofxId = "com.belisoful.ofxgrip.spot";
	m.identity.uuid = "5B7C8D9E-0F1A-4B2C-8D3E-4F5A6B7C8D9E";
	m.identity.name = "OfxGrip Spot";
	m.identity.shortName = "Spot";
	m.identity.grouping = "OfxGrip";
	m.identity.description = "A soft spotlight rendered by one Slang kernel on Metal and on the CPU. The OfxGrip kernel sample.";
	m.identity.author = "Belisoful";
	m.identity.kind = EffectKind::Filter;
	m.version = Version{1, 0, 0};
	m.capabilities.supportsTiles = true;
	m.capabilities.supportsMultiResolution = true;
	m.capabilities.depths = {BitDepth::UInt8, BitDepth::UInt16, BitDepth::Half, BitDepth::Float};
	m.properties["overlay"] = Value(true);
	// The sample ships as a 30-day trial through the built-in provider: Trial renders clean, an
	// expired trial renders the unlicensed mark. A store's provider replaces `trial` by name.
	m.licensing.provider = "trial";
	m.licensing.settings["days"] = Value(30);
	m.licensing.watermarkTrial = false;
	m.parameters.push_back(ParameterSpec::buttonParam(kAbout, "about", "About OfxGrip Spot", "about"));
	return m;
}

/*! The kernel's parameter IDs, as its metadata block declares them. */
enum : ParameterId {
	kCenter = 1,
	kRadius = 2,
};

class SpotEffect final : public KernelEffect {
public:
	/*! A movable circle with a radius handle over the spotlight. */
	std::unique_ptr<OverlayControl> createOverlay(OverlayParameters& parameters) override
	{
		auto control = std::make_unique<OverlayControl>(parameters);
		control->addParts(circleParts(OverlayShapeOption::Body | OverlayShapeOption::RadiusHandle, 1, kCenter, kRadius));
		return control;
	}

	Error onParameterClicked(ParameterId id) override
	{
		if (id == kAbout) {
			services().feedback().message(MessageKind::Message, "about", "OfxGrip Spot, the OfxGrip kernel sample. One Slang kernel, every backend. MIT License.");
		}
		return Error::none();
	}
};

void registerPlugins()
{
	PluginRegistry::instance().registerFactory(std::make_unique<KernelEffectFactoryFor<SpotEffect>>(baseManifest(), std::vector<KernelRecord>{kernelRecordFor(OFXGRIP_KERNEL(Spot))}));
}

}  // namespace

OFXGRIP_BUNDLE(registerPlugins)
