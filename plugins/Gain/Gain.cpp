// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
/*!
	@file  Gain.cpp
	@brief The OfxGrip sample effect: a per-channel gain filter built on the framework.

	The manifest declares a master gain, a toggle that reveals per-channel gains inside a group,
	and a choice of clamping. Render reads only the snapshot and the images it is handed.
*/
#include <ofxgrip/ofx/Plugin.hpp>

#include <algorithm>
#include <cmath>

namespace {

using namespace ofxgrip;

enum : ParameterId {
	kGain = 100,
	kPerChannel = 101,
	kChannels = 110,
	kGainR = 111,
	kGainG = 112,
	kGainB = 113,
	kGainA = 114,
	kClamp = 120,
	kAbout = 900,
};

EffectManifest makeManifest()
{
	EffectManifest m;
	m.identity.ofxId = "com.belisoful.ofxgrip.gain";
	m.identity.uuid = "4A6E3D2C-7B1F-4E8A-9C5D-0F2B3A4C5D6E";
	m.identity.name = "OfxGrip Gain";
	m.identity.shortName = "Gain";
	m.identity.grouping = "OfxGrip";
	m.identity.description = "Multiplies each channel by a gain. The OfxGrip sample effect.";
	m.identity.kind = EffectKind::Filter;
	m.identity.extraContexts = {EffectKind::General};
	m.version = Version{1, 0, 0};
	m.capabilities.supportsTiles = true;
	m.capabilities.supportsMultiResolution = true;
	m.capabilities.noSpatialAwareness = true;
	m.capabilities.depths = {BitDepth::UInt8, BitDepth::UInt16, BitDepth::Half, BitDepth::Float};

	m.parameters.push_back(ParameterSpec::floatParam(kGain, "gain", "Gain", 1.0, 0.0, 10.0).withDisplayRange(0.0, 4.0).withHint("Multiplies every channel.").withTags({"gain"}));
	m.parameters.push_back(ParameterSpec::boolParam(kPerChannel, "perChannel", "Per-channel gains", false).withHint("Reveals one gain per channel."));
	m.parameters.push_back(ParameterSpec::groupParam(kChannels, "channels", "Channels",
	                                                 {
	                                                     ParameterSpec::floatParam(kGainR, "gainR", "Red", 1.0, 0.0, 10.0).withDisplayRange(0.0, 4.0).withTags({"gain"}),
	                                                     ParameterSpec::floatParam(kGainG, "gainG", "Green", 1.0, 0.0, 10.0).withDisplayRange(0.0, 4.0).withTags({"gain"}),
	                                                     ParameterSpec::floatParam(kGainB, "gainB", "Blue", 1.0, 0.0, 10.0).withDisplayRange(0.0, 4.0).withTags({"gain"}),
	                                                     ParameterSpec::floatParam(kGainA, "gainA", "Alpha", 1.0, 0.0, 10.0).withDisplayRange(0.0, 4.0).withTags({"gain"}),
	                                                 })
	                           .withFlags(HostFlags(HostFlag::Hidden)));
	m.parameters.push_back(ParameterSpec::menuParam(kClamp, "clamp", "Clamp", {{"none", "None"}, {"unit", "0 to 1"}}, 0).withHint("Limits the result."));
	m.parameters.push_back(ParameterSpec::buttonParam(kAbout, "about", "About OfxGrip Gain", "about"));
	return m;
}

class GainEffect final : public Effect {
public:
	Error onCreate() override { return syncVisibility(); }

	Error onInstanceChanged(const ChangeEvent& change) override
	{
		if (change.parameter == kPerChannel) {
			return syncVisibility();
		}
		return Error::none();
	}

	Error onParameterClicked(ParameterId id) override
	{
		if (id == kAbout) {
			services().feedback().message(MessageKind::Message, "about", "OfxGrip Gain, the OfxGrip sample effect. MIT License.");
		}
		return Error::none();
	}

	Error onIsIdentity(const IsIdentityArgs&, const RenderSnapshot& snapshot, std::string& clip, Time&, bool& identity) override
	{
		float gains[4];
		readGains(snapshot, gains);
		identity = snapshot.integer(kClamp) == 0 && gains[0] == 1.0f && gains[1] == 1.0f && gains[2] == 1.0f && gains[3] == 1.0f;
		clip = "Source";
		return Error::none();
	}

	Error render(RenderContext& context) override
	{
		if (context.outputIsGpu()) {
			return Error::make(ErrorCode::Unsupported, "GPU render is not available in this phase");
		}
		const InputImage* source = context.source();
		float gains[4];
		readGains(context.snapshot, gains);
		const bool clamp = context.snapshot.integer(kClamp) == 1;
		const ImageView& out = context.output;
		const std::size_t components = componentCount(out.desc.components);
		return context.forEachBand([&](const RectI& band) {
			for (int y = band.y1; y < band.y2; ++y) {
				for (int x = band.x1; x < band.x2; ++x) {
					std::uint8_t* dst = out.pixel(x, y);
					const std::uint8_t* src = (source != nullptr && source->connected) ? source->view.pixel(x, y) : nullptr;
					if (dst == nullptr) {
						continue;
					}
					for (std::size_t c = 0; c < components; ++c) {
						float value = 0.0f;
						if (src != nullptr) {
							const std::size_t gainIndex = out.desc.components == Components::Alpha ? 3 : c;
							value = readComponent(src, source->view.desc.depth, c) * gains[gainIndex];
							if (clamp) {
								value = std::clamp(value, 0.0f, 1.0f);
							}
						}
						writeComponent(dst, out.desc.depth, c, value);
					}
				}
			}
		});
	}

private:
	static void readGains(const RenderSnapshot& snapshot, float gains[4])
	{
		const float master = static_cast<float>(snapshot.number(kGain, 1.0));
		if (snapshot.boolean(kPerChannel)) {
			gains[0] = master * static_cast<float>(snapshot.number(kGainR, 1.0));
			gains[1] = master * static_cast<float>(snapshot.number(kGainG, 1.0));
			gains[2] = master * static_cast<float>(snapshot.number(kGainB, 1.0));
			gains[3] = master * static_cast<float>(snapshot.number(kGainA, 1.0));
		} else {
			gains[0] = gains[1] = gains[2] = master;
			gains[3] = 1.0f;
		}
	}

	Error syncVisibility()
	{
		Value perChannel;
		if (Error e = services().parameters().getCurrent(kPerChannel, perChannel)) {
			return e;
		}
		HostFlags flags;
		if (Error e = services().parameters().getFlags(kChannels, flags)) {
			return e;
		}
		flags.set(HostFlag::Hidden, !perChannel.toBool(false));
		return services().parameters().setFlags(kChannels, flags);
	}
};

void registerPlugins()
{
	PluginRegistry::instance().registerFactory(std::make_unique<EffectFactoryFor<GainEffect>>(makeManifest()));
}

}  // namespace

OFXGRIP_BUNDLE(registerPlugins)
