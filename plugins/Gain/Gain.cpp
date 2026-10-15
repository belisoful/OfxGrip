// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
/*!
	@file  Gain.cpp
	@brief The OfxGrip sample effect: a per-channel gain filter built on the framework.

	The manifest declares a master gain, a toggle that reveals per-channel gains inside a group
	through a visibility rule, and a choice of clamping. Render reads only the snapshot and the
	images it is handed. On macOS the effect also renders Metal frames through the backend, with
	the same arithmetic as the CPU path.
*/
#include <ofxgrip/ofx/Plugin.hpp>

#if defined(OFXGRIP_HAS_METAL)
#include <ofxgrip/gpu/MetalBackend.hpp>
#endif

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
	m.version = Version{1, 1, 0};
	m.capabilities.supportsTiles = true;
	m.capabilities.supportsMultiResolution = true;
	m.capabilities.noSpatialAwareness = true;
	m.capabilities.depths = {BitDepth::UInt8, BitDepth::UInt16, BitDepth::Half, BitDepth::Float};
#if defined(OFXGRIP_HAS_METAL)
	m.capabilities.metalRender = true;
#endif

	m.parameters.push_back(ParameterSpec::floatParam(kGain, "gain", "Gain", 1.0, 0.0, 10.0).withDisplayRange(0.0, 4.0).withHint("Multiplies every channel.").withTags({"gain"}));
	m.parameters.push_back(ParameterSpec::boolParam(kPerChannel, "perChannel", "Per-channel gains", false).withHint("Reveals one gain per channel."));
	m.parameters.push_back(ParameterSpec::groupParam(kChannels, "channels", "Channels",
	                                                 {
	                                                     ParameterSpec::floatParam(kGainR, "gainR", "Red", 1.0, 0.0, 10.0).withDisplayRange(0.0, 4.0).withTags({"gain"}),
	                                                     ParameterSpec::floatParam(kGainG, "gainG", "Green", 1.0, 0.0, 10.0).withDisplayRange(0.0, 4.0).withTags({"gain"}),
	                                                     ParameterSpec::floatParam(kGainB, "gainB", "Blue", 1.0, 0.0, 10.0).withDisplayRange(0.0, 4.0).withTags({"gain"}),
	                                                     ParameterSpec::floatParam(kGainA, "gainA", "Alpha", 1.0, 0.0, 10.0).withDisplayRange(0.0, 4.0).withTags({"gain"}),
	                                                 })
	                           .withFlags(HostFlags(HostFlag::Hidden))
	                           .withVisibleWhen("perChannel"));
	m.parameters.push_back(ParameterSpec::menuParam(kClamp, "clamp", "Clamp", {{"none", "None"}, {"unit", "0 to 1"}}, 0).withHint("Limits the result."));
	m.parameters.push_back(ParameterSpec::buttonParam(kAbout, "about", "About OfxGrip Gain", "about"));
	return m;
}

#if defined(OFXGRIP_HAS_METAL)

/*! The kernel arguments. The layout matches `GainParams` in the shader source. */
struct GainParams {
	float gain[4];
	int clampResult;
	int components;
	int hasSource;
	int srcComponents;
	int srcX1, srcY1, srcX2, srcY2;
	int dstX1, dstY1, dstX2, dstY2;
	int winX1, winY1, winX2, winY2;
	int srcRowBytes;
	int dstRowBytes;
};

constexpr const char* kGainShader = R"MSL(
#include <metal_stdlib>
using namespace metal;

struct GainParams {
    float gain[4];
    int clampResult;
    int components;
    int hasSource;
    int srcComponents;
    int srcX1, srcY1, srcX2, srcY2;
    int dstX1, dstY1, dstX2, dstY2;
    int winX1, winY1, winX2, winY2;
    int srcRowBytes;
    int dstRowBytes;
};

template <typename T> inline float toUnit(T v);
template <> inline float toUnit<uchar>(uchar v) { return float(v) / 255.0f; }
template <> inline float toUnit<ushort>(ushort v) { return float(v) / 65535.0f; }
template <> inline float toUnit<half>(half v) { return float(v); }
template <> inline float toUnit<float>(float v) { return v; }

template <typename T> inline T fromUnit(float v);
template <> inline uchar fromUnit<uchar>(float v) { return uchar(clamp(v * 255.0f + 0.5f, 0.0f, 255.0f)); }
template <> inline ushort fromUnit<ushort>(float v) { return ushort(clamp(v * 65535.0f + 0.5f, 0.0f, 65535.0f)); }
template <> inline half fromUnit<half>(float v) { return half(v); }
template <> inline float fromUnit<float>(float v) { return v; }

template <typename T>
inline void gainPixel(device const uchar* src, device uchar* dst, constant GainParams& p, uint2 gid)
{
    const int x = p.winX1 + int(gid.x);
    const int y = p.winY1 + int(gid.y);
    if (x >= p.winX2 || y >= p.winY2) {
        return;
    }
    if (x < p.dstX1 || x >= p.dstX2 || y < p.dstY1 || y >= p.dstY2) {
        return;
    }
    device T* d = (device T*)(dst + (y - p.dstY1) * p.dstRowBytes) + (x - p.dstX1) * p.components;
    const bool inSource = p.hasSource != 0 && x >= p.srcX1 && x < p.srcX2 && y >= p.srcY1 && y < p.srcY2;
    if (!inSource) {
        for (int c = 0; c < p.components; ++c) {
            d[c] = fromUnit<T>(0.0f);
        }
        return;
    }
    device const T* s = (device const T*)(src + (y - p.srcY1) * p.srcRowBytes) + (x - p.srcX1) * p.srcComponents;
    for (int c = 0; c < p.components; ++c) {
        const int gainIndex = p.components == 1 ? 3 : c;
        float v = toUnit<T>(s[c]) * p.gain[gainIndex];
        if (p.clampResult != 0) {
            v = clamp(v, 0.0f, 1.0f);
        }
        d[c] = fromUnit<T>(v);
    }
}

kernel void gain_byte(device const uchar* src [[buffer(0)]], device uchar* dst [[buffer(1)]], constant GainParams& p [[buffer(2)]], uint2 gid [[thread_position_in_grid]])
{
    gainPixel<uchar>(src, dst, p, gid);
}
kernel void gain_short(device const uchar* src [[buffer(0)]], device uchar* dst [[buffer(1)]], constant GainParams& p [[buffer(2)]], uint2 gid [[thread_position_in_grid]])
{
    gainPixel<ushort>(src, dst, p, gid);
}
kernel void gain_half(device const uchar* src [[buffer(0)]], device uchar* dst [[buffer(1)]], constant GainParams& p [[buffer(2)]], uint2 gid [[thread_position_in_grid]])
{
    gainPixel<half>(src, dst, p, gid);
}
kernel void gain_float(device const uchar* src [[buffer(0)]], device uchar* dst [[buffer(1)]], constant GainParams& p [[buffer(2)]], uint2 gid [[thread_position_in_grid]])
{
    gainPixel<float>(src, dst, p, gid);
}
)MSL";

const char* kernelNameFor(BitDepth depth) noexcept
{
	switch (depth) {
	case BitDepth::UInt8: return "gain_byte";
	case BitDepth::UInt16: return "gain_short";
	case BitDepth::Half: return "gain_half";
	case BitDepth::Float: return "gain_float";
	case BitDepth::None: break;
	}
	return nullptr;
}

#endif

class GainEffect final : public Effect {
public:
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
#if defined(OFXGRIP_HAS_METAL)
			if (context.outputGpu.api == GpuApi::Metal) {
				return renderMetal(context);
			}
#endif
			return Error::make(ErrorCode::Unsupported, "This GPU API is not supported").with("api", std::string(toString(context.outputGpu.api)));
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

#if defined(OFXGRIP_HAS_METAL)
	Error renderMetal(RenderContext& context)
	{
		const GpuImage& out = context.outputGpu;
		const char* kernel = kernelNameFor(out.desc.depth);
		if (kernel == nullptr || out.handle == nullptr) {
			return Error::make(ErrorCode::Format, "The Metal output frame is unusable");
		}
		std::shared_ptr<metal::MetalContext> backend = metal::MetalContext::forQueue(out.queue);
		if (!backend) {
			return Error::make(ErrorCode::MissingHostFeature, "The host passed no Metal command queue");
		}
		metal::MetalPipeline pipeline;
		if (Error e = backend->pipeline("ofxgrip.gain", kGainShader, kernel, pipeline)) {
			return e;
		}
		const InputImage* source = context.source();
		const bool hasSource = source != nullptr && source->connected && source->gpu.handle != nullptr;
		if (hasSource && source->gpu.desc.depth != out.desc.depth) {
			return Error::make(ErrorCode::Format, "The Metal source and output frames differ in depth");
		}
		GainParams params{};
		readGains(context.snapshot, params.gain);
		params.clampResult = context.snapshot.integer(kClamp) == 1 ? 1 : 0;
		params.components = static_cast<int>(componentCount(out.desc.components));
		params.hasSource = hasSource ? 1 : 0;
		params.dstX1 = out.desc.bounds.x1;
		params.dstY1 = out.desc.bounds.y1;
		params.dstX2 = out.desc.bounds.x2;
		params.dstY2 = out.desc.bounds.y2;
		params.dstRowBytes = static_cast<int>(out.rowBytes);
		const RectI window = context.args.renderWindow.empty() ? out.desc.bounds : context.args.renderWindow.intersect(out.desc.bounds);
		params.winX1 = window.x1;
		params.winY1 = window.y1;
		params.winX2 = window.x2;
		params.winY2 = window.y2;

		metal::MetalCommandBuffer command = backend->commandBuffer();
		if (!command.valid()) {
			return Error::make(ErrorCode::Render, "No Metal command buffer is available");
		}
		void* sourceHandle = out.handle;
		if (hasSource) {
			const GpuImage& in = source->gpu;
			params.srcComponents = static_cast<int>(componentCount(in.desc.components));
			params.srcX1 = in.desc.bounds.x1;
			params.srcY1 = in.desc.bounds.y1;
			params.srcX2 = in.desc.bounds.x2;
			params.srcY2 = in.desc.bounds.y2;
			params.srcRowBytes = static_cast<int>(in.rowBytes);
			sourceHandle = in.handle;
			if (in.needsIntermediateCopy) {
				// Q-RESOLVE-031: on the Fusion page the host's buffers are not read in place.
				const std::size_t bytes = static_cast<std::size_t>(in.rowBytes) * static_cast<std::size_t>(in.desc.bounds.height());
				metal::MetalBuffer scratch = backend->scratch(bytes);
				if (!scratch.valid()) {
					return Error::make(ErrorCode::Memory, "No Metal scratch buffer is available");
				}
				if (Error e = command.copy(in.handle, 0, scratch.handle(), 0, bytes)) {
					return e;
				}
				sourceHandle = scratch.handle();
				command.retain(std::move(scratch));
			}
		}
		metal::MetalDispatch dispatch;
		dispatch.pipeline = pipeline;
		dispatch.width = static_cast<unsigned>(window.width());
		dispatch.height = static_cast<unsigned>(window.height());
		dispatch.buffers = {{sourceHandle, 0, 0}, {out.handle, 0, 1}};
		dispatch.bytes = {{&params, sizeof params, 2}};
		if (Error e = command.dispatch(dispatch)) {
			return e;
		}
		// Commit without waiting, as ofxGPURender.h asks; the host synchronizes the queue.
		return command.commit();
	}
#endif
};

void registerPlugins()
{
	PluginRegistry::instance().registerFactory(std::make_unique<EffectFactoryFor<GainEffect>>(makeManifest()));
}

}  // namespace

OFXGRIP_BUNDLE(registerPlugins)
