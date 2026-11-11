// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
#include <ofxgrip/gpu/VulkanKernelBackend.hpp>

#include <ofxgrip/core/KernelTile.hpp>
#include <ofxgrip/gpu/VulkanBackend.hpp>

#include "KernelPixels.hpp"

#include <cstring>
#include <mutex>
#include <optional>

OFXGRIP_NAMESPACE_BEGIN

namespace {

std::vector<float> floatTexels(const ImageView& view)
{
	const RectI& b = view.desc.bounds;
	std::vector<float> texels(static_cast<std::size_t>(b.width()) * static_cast<std::size_t>(b.height()) * 4);
	float* at = texels.data();
	for (int y = b.y1; y < b.y2; ++y) {
		for (int x = b.x1; x < b.x2; ++x) {
			const std::array<float, 4> texel = kernelTexel(view, x, y);
			std::memcpy(at, texel.data(), sizeof(float) * 4);
			at += 4;
		}
	}
	return texels;
}

}  // namespace

bool VulkanKernelBackend::available() const noexcept
{
	return vulkan::VulkanContext::available();
}

void VulkanKernelBackend::registerWithEngine()
{
	static std::once_flag once;
	std::call_once(once, []() { KernelEngine::shared().registerBackend(std::make_unique<VulkanKernelBackend>()); });
}

Error VulkanKernelBackend::dispatch(const KernelDispatch& d)
{
	const KernelLayout& layout = *d.layout;
	if (layout.form() != KernelBindingForm::Descriptors) {
		return Error::make(ErrorCode::InvalidParameter, "The Vulkan backend needs the SPIR-V target's descriptor layout");
	}
	if (d.destination.isGpu() || !d.destination.cpu.valid()) {
		return Error::make(ErrorCode::InvalidParameter, "The Vulkan backend writes a CPU destination");
	}
	std::shared_ptr<vulkan::VulkanContext> context = vulkan::VulkanContext::shared();
	if (!context) {
		return Error::make(ErrorCode::MissingHostFeature, "No Vulkan device is available").with("reason", vulkan::VulkanContext::unavailableReason());
	}
	const ImageView& out = d.destination.cpu;
	const RectI window = d.window.empty() ? out.desc.bounds : d.window.intersect(out.desc.bounds);
	if (window.empty()) {
		return Error::none();
	}
	if (d.abort && d.abort()) {
		return Error::cancelled();
	}

	vulkan::VulkanJob job;
	job.cacheKey = d.cacheKey.empty() ? "ofxgrip.kernel" : d.cacheKey;
	job.spirv = d.implementation->source;
	std::vector<std::vector<float>> uploads;
	uploads.reserve(d.plan->textures.size());
	std::vector<std::optional<RectI>> bounds(layout.textureCount());
	for (const KernelBindingPlan::Texture& bound : d.plan->textures) {
		const KernelTexture* texture = layout.textureNamed(bound.name);
		const KernelImage* image = kernelImageFor(d, bound);
		if (image != nullptr && image->connected && image->isGpu()) {
			return Error::make(ErrorCode::InvalidParameter, "The Vulkan backend reads CPU sources").with("texture", bound.name);
		}
		vulkan::VulkanSourceBinding source;
		source.binding = static_cast<std::uint32_t>(texture->slotIndex);
		if (image != nullptr && image->connected && image->cpu.valid()) {
			const RectI& b = image->cpu.desc.bounds;
			uploads.push_back(floatTexels(image->cpu));
			source.rgba = uploads.back().data();
			source.width = static_cast<unsigned>(b.width());
			source.height = static_cast<unsigned>(b.height());
			if (bound.mapIndex < bounds.size()) {
				bounds[bound.mapIndex] = b;
			}
		}
		job.sources.push_back(source);
	}
	const KernelTexture& destination = layout.destination();
	if (destination.mapIndex < bounds.size()) {
		bounds[destination.mapIndex] = window;
	}
	// The frames upload bottom row first, so texel row 0 is the bottom row, as on Metal.
	const GripTile tile = makeGripTile(window, d.pixelScale, bounds, true);
	if (layout.hasTile()) {
		job.buffers.push_back({static_cast<std::uint32_t>(layout.tileIndex()), &tile, sizeof tile});
	}
	Bytes uniforms = d.uniforms;
	if (layout.hasUniforms()) {
		// The shader's block is the layout's length whatever the dispatch packed.
		uniforms.resize(std::max(uniforms.size(), layout.uniformsLength()), 0);
		job.buffers.push_back({static_cast<std::uint32_t>(layout.uniformsIndex()), uniforms.data(), uniforms.size()});
	}
	for (const KernelConstantValue& value : d.constants) {
		if (value.slotIndex == kKernelNoIndex) {
			continue;
		}
		vulkan::VulkanConstant constant;
		constant.id = static_cast<std::uint32_t>(value.slotIndex);
		if (value.scalarType == KernelScalarType::Bool) {
			// A Vulkan specialization bool is a four-byte VkBool32.
			const std::uint32_t flag = value.bytes[0] != 0 ? 1u : 0u;
			std::memcpy(constant.bytes.data(), &flag, sizeof flag);
			constant.length = sizeof flag;
		} else {
			constant.length = std::min(value.length, constant.bytes.size());
			std::memcpy(constant.bytes.data(), value.bytes.data(), constant.length);
		}
		job.constants.push_back(constant);
	}
	for (const KernelSampler& sampler : layout.samplers()) {
		job.samplers.push_back(static_cast<std::uint32_t>(sampler.slotIndex));
	}
	job.destinationBinding = static_cast<std::uint32_t>(destination.slotIndex);
	job.width = static_cast<unsigned>(window.width());
	job.height = static_cast<unsigned>(window.height());
	std::vector<float> result(static_cast<std::size_t>(job.width) * job.height * 4);
	job.destination = result.data();
	job.groupSize = layout.threadGroupSize();
	if (Error e = context->run(job)) {
		return e;
	}
	const float* texel = result.data();
	for (int y = window.y1; y < window.y2; ++y) {
		for (int x = window.x1; x < window.x2; ++x) {
			writeKernelTexel(out, x, y, texel);
			texel += 4;
		}
	}
	return Error::none();
}

OFXGRIP_NAMESPACE_END
