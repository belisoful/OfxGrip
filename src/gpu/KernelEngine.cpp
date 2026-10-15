// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
#include <ofxgrip/gpu/KernelEngine.hpp>

#include <ofxgrip/gpu/CpuKernelBackend.hpp>

OFXGRIP_NAMESPACE_BEGIN

KernelEngine::KernelEngine()
{
	registerBackend(std::make_unique<CpuKernelBackend>());
}

KernelEngine& KernelEngine::shared()
{
	static KernelEngine engine;
	return engine;
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
	const GpuApi api = dispatch.destination.gpu.api;
	KernelBackend* target = backend(api);
	if (target == nullptr || !target->available()) {
		return Error::make(ErrorCode::MissingHostFeature, "No kernel backend serves this API").with("api", std::string(toString(api)));
	}
	if (dispatch.implementation->api != api) {
		return Error::make(ErrorCode::InvalidParameter, "The kernel implementation is for another API").with("api", std::string(toString(dispatch.implementation->api)));
	}
	return target->dispatch(dispatch);
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
