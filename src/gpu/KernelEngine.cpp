// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
#include <ofxgrip/gpu/KernelEngine.hpp>

#include <ofxgrip/gpu/CpuKernelBackend.hpp>
#include <ofxgrip/gpu/KernelColor.hpp>

OFXGRIP_NAMESPACE_BEGIN

KernelEngine::KernelEngine()
{
	registerBackend(std::make_unique<CpuKernelBackend>());
}

KernelEngine& KernelEngine::shared()
{
	static KernelEngine* engine = new KernelEngine;
	return *engine;
}

void KernelEngine::releaseDeviceState()
{
	for (const auto& [api, backend] : backends_) {
		backend->releaseDeviceState();
	}
}

void KernelEngine::registerBackend(std::unique_ptr<KernelBackend> backend)
{
	if (!backend) {
		return;
	}
	const GpuApi api = backend->api();
	backends_[api] = std::move(backend);
}

KernelBackend* KernelEngine::backend(GpuApi api) const noexcept
{
	auto it = backends_.find(api);
	return it == backends_.end() ? nullptr : it->second.get();
}

std::vector<GpuApi> KernelEngine::availableApis() const
{
	std::vector<GpuApi> out;
	for (const auto& [api, backend] : backends_) {
		if (backend->available()) {
			out.push_back(api);
		}
	}
	return out;
}

Error KernelEngine::run(const KernelDispatch& dispatch)
{
	if (dispatch.layout == nullptr || dispatch.implementation == nullptr || dispatch.plan == nullptr) {
		return Error::make(ErrorCode::InvalidParameter, "The kernel dispatch lacks a layout, an implementation, or a plan");
	}
	const GpuApi api = dispatch.destination.isGpu() ? dispatch.destination.gpu.api : dispatch.accelerator;
	KernelBackend* target = backend(api);
	if (target == nullptr || !target->available()) {
		return Error::make(ErrorCode::MissingHostFeature, "No kernel backend serves this API").with("api", std::string(toString(api)));
	}
	if (dispatch.implementation->api != api) {
		return Error::make(ErrorCode::InvalidParameter, "The kernel implementation is for another API").with("api", std::string(toString(dispatch.implementation->api)));
	}
	if (dispatch.color.active()) {
		return runConverted(*target, dispatch);
	}
	return target->dispatch(dispatch);
}

Error KernelBackend::makeScratch(const KernelImage&, const RectI&, KernelImage&)
{
	return Error::make(ErrorCode::Unsupported, "This kernel backend makes no scratch image").with("api", std::string(toString(api())));
}

bool KernelEngine::convertsColorOn(GpuApi api) const noexcept
{
	const KernelBackend* frames = backend(api);
	const KernelColorBuild* build = kernelColorBuild(api);
	return frames != nullptr && frames->available() && build != nullptr && frames->accepts(*build->implementation);
}

namespace {

bool isColorImage(const KernelImage& image) noexcept
{
	const Components components = image.desc().components;
	return image.connected && (components == Components::RGBA || components == Components::RGB);
}

/*! One conversion pass on the frames' own backend: `source` into `destination` over `window`. */
Error convertColor(KernelBackend& frames, const KernelColorBuild& build, const KernelDispatch& around, const ColorConversion& conversion, const KernelImage& source, const KernelImage& destination, const RectI& window)
{
	KernelDispatch pass;
	pass.layout = &build.layout;
	pass.implementation = build.implementation;
	pass.plan = &build.plan;
	pass.cacheKey = kColorConvertKernelName;
	const KernelValueMap values = colorConvertValues(conversion, source.desc().premultiplication == Premultiplication::Premultiplied);
	pass.uniforms = packKernelUniforms(build.layout, build.plan, values, 1.0);
	pass.constants = packKernelConstants(build.plan, values, 1.0);
	pass.window = window;
	pass.pixelScale = around.pixelScale;
	pass.destination = destination;
	pass.sources.push_back(source);
	pass.threading = around.threading;
	pass.abort = around.abort;
	return frames.dispatch(pass);
}

}  // namespace

Error KernelEngine::runConverted(KernelBackend& target, const KernelDispatch& dispatch)
{
	// The intermediates live where the frames live. A CPU frame converts on the CPU backend
	// whatever accelerator runs the kernel.
	const GpuApi frameApi = dispatch.destination.isGpu() ? dispatch.destination.gpu.api : GpuApi::None;
	KernelBackend* frames = backend(frameApi);
	const KernelColorBuild* build = kernelColorBuild(frameApi);
	if (frames == nullptr || build == nullptr || !convertsColorOn(frameApi)) {
		return Error::make(ErrorCode::Unsupported, "No colourspace conversion runs on this API").with("api", std::string(toString(frameApi)));
	}
	const RectI window = dispatch.window.empty() ? dispatch.destination.bounds() : dispatch.window.intersect(dispatch.destination.bounds());
	if (window.empty()) {
		return Error::none();
	}
	KernelDispatch converted = dispatch;
	converted.color = KernelColorPass{};
	converted.window = window;
	if (!dispatch.color.toKernel.isIdentity()) {
		const auto toKernel = [&](KernelImage& image) -> Error {
			if (!isColorImage(image)) {
				return Error::none();
			}
			KernelImage scratch;
			if (Error e = frames->makeScratch(image, image.bounds(), scratch)) {
				return e;
			}
			if (Error e = convertColor(*frames, *build, dispatch, dispatch.color.toKernel, image, scratch, scratch.bounds())) {
				return e;
			}
			image = std::move(scratch);
			return Error::none();
		};
		for (KernelImage& source : converted.sources) {
			if (Error e = toKernel(source)) {
				return e;
			}
		}
		for (auto& [id, image] : converted.keyedImages) {
			if (Error e = toKernel(image)) {
				return e;
			}
		}
	}
	if (dispatch.color.toHost.isIdentity() || !isColorImage(dispatch.destination)) {
		return target.dispatch(converted);
	}
	KernelImage result;
	if (Error e = frames->makeScratch(dispatch.destination, window, result)) {
		return e;
	}
	converted.destination = result;
	if (Error e = target.dispatch(converted)) {
		return e;
	}
	return convertColor(*frames, *build, dispatch, dispatch.color.toHost, result, dispatch.destination, window);
}

const KernelImage* kernelImageFor(const KernelDispatch& dispatch, const KernelBindingPlan::Texture& texture) noexcept
{
	if (texture.id != kParameterIdNone) {
		auto it = dispatch.keyedImages.find(texture.id);
		return it == dispatch.keyedImages.end() ? nullptr : &it->second;
	}
	if (texture.positional >= 0 && static_cast<std::size_t>(texture.positional) < dispatch.sources.size()) {
		return &dispatch.sources[static_cast<std::size_t>(texture.positional)];
	}
	return nullptr;
}

OFXGRIP_NAMESPACE_END
