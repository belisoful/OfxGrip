// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
#include <ofxgrip/gpu/MetalKernelBackend.hpp>

#include <ofxgrip/core/KernelTile.hpp>
#include <ofxgrip/gpu/MetalBackend.hpp>

#include <mutex>

OFXGRIP_NAMESPACE_BEGIN

namespace {

metal::MetalDataType dataTypeFor(KernelScalarType type) noexcept
{
	switch (type) {
	case KernelScalarType::Float32: return metal::MetalDataType::Float;
	case KernelScalarType::Float16: return metal::MetalDataType::Half;
	case KernelScalarType::Int32: return metal::MetalDataType::Int;
	case KernelScalarType::UInt32: return metal::MetalDataType::UInt;
	case KernelScalarType::Int16: return metal::MetalDataType::Short;
	case KernelScalarType::UInt16: return metal::MetalDataType::UShort;
	case KernelScalarType::Int8: return metal::MetalDataType::Char;
	case KernelScalarType::UInt8: return metal::MetalDataType::UChar;
	case KernelScalarType::Bool: return metal::MetalDataType::Bool;
	case KernelScalarType::None: break;
	}
	return metal::MetalDataType::Float;
}

/*! A frame as a texture: over its buffer when aligned, else an intermediate filled by a blit. */
Error textureForFrame(metal::MetalContext& context, metal::MetalCommandBuffer& command, const GpuImage& frame, bool shaderWrite, bool upload, metal::MetalTexture& out, bool& direct)
{
	const metal::MetalPixelFormat format = metal::metalPixelFormatFor(frame.desc.depth, frame.desc.components);
	if (format == metal::MetalPixelFormat::Invalid) {
		return Error::make(ErrorCode::Format, "Metal has no texture format for this frame layout").with("components", std::string(toString(frame.desc.components)));
	}
	const unsigned width = static_cast<unsigned>(frame.desc.bounds.width());
	const unsigned height = static_cast<unsigned>(frame.desc.bounds.height());
	const std::size_t rowBytes = static_cast<std::size_t>(frame.rowBytes);
	const std::size_t alignment = context.linearTextureAlignment(format);
	direct = frame.rowBytes > 0 && rowBytes >= width * metal::metalPixelBytes(format) && (alignment == 0 || rowBytes % alignment == 0);
	if (direct) {
		return context.textureOverBuffer(frame.handle, 0, rowBytes, width, height, format, shaderWrite, out);
	}
	if (Error e = context.newTexture(width, height, format, shaderWrite, out)) {
		return e;
	}
	if (upload) {
		return command.copyBufferToTexture(frame.handle, 0, rowBytes, out);
	}
	return Error::none();
}

}  // namespace

bool MetalKernelBackend::available() const noexcept
{
	return metal::MetalContext::available();
}

void MetalKernelBackend::registerWithEngine()
{
	static std::once_flag once;
	std::call_once(once, []() { KernelEngine::shared().registerBackend(std::make_unique<MetalKernelBackend>()); });
}

Error MetalKernelBackend::dispatch(const KernelDispatch& d)
{
	const KernelLayout& layout = *d.layout;
	const KernelImplementation& impl = *d.implementation;
	if (layout.form() != KernelBindingForm::Slots) {
		return Error::make(ErrorCode::InvalidParameter, "The Metal backend needs the Metal target's slot layout");
	}
	const GpuImage& out = d.destination.gpu;
	if (out.api != GpuApi::Metal || out.handle == nullptr) {
		return Error::make(ErrorCode::InvalidParameter, "The Metal backend writes a Metal frame");
	}
	std::shared_ptr<metal::MetalContext> context = metal::MetalContext::forQueue(out.queue);
	if (!context) {
		return Error::make(ErrorCode::MissingHostFeature, "The host passed no Metal command queue");
	}
	const RectI window = d.window.empty() ? out.desc.bounds : d.window.intersect(out.desc.bounds);
	if (window.empty()) {
		return Error::none();
	}

	std::vector<metal::MetalFunctionConstant> constants;
	for (const KernelConstantValue& c : d.constants) {
		if (c.slotIndex == kKernelNoIndex) {
			continue;
		}
		metal::MetalFunctionConstant constant;
		constant.index = static_cast<unsigned>(c.slotIndex);
		constant.type = dataTypeFor(c.scalarType);
		constant.bytes = c.bytes;
		constants.push_back(constant);
	}
	metal::MetalPipeline pipeline;
	if (Error e = context->pipeline(d.cacheKey.empty() ? "ofxgrip.kernel" : d.cacheKey, impl.source, layout.entryPoint(), constants, pipeline)) {
		return e;
	}
	metal::MetalCommandBuffer command = context->commandBuffer();
	if (!command.valid()) {
		return Error::make(ErrorCode::Render, "No Metal command buffer is available");
	}

	// The read textures, in map order, with the tile maps over their bounds.
	std::vector<std::optional<RectI>> bounds(layout.textureCount());
	metal::MetalDispatch dispatch;
	dispatch.pipeline = pipeline;
	metal::MetalTexture empty;
	for (const KernelBindingPlan::Texture& bound : d.plan->textures) {
		const KernelTexture* texture = layout.textureNamed(bound.name);
		const KernelImage* image = kernelImageFor(d, bound);
		metal::MetalTexture bindable;
		if (image != nullptr && image->connected && image->isGpu() && image->gpu.handle != nullptr) {
			GpuImage frame = image->gpu;
			if (frame.needsIntermediateCopy) {
				// Q-RESOLVE-031: on the Fusion page the host's buffers are not read in place.
				const std::size_t bytes = static_cast<std::size_t>(frame.rowBytes) * static_cast<std::size_t>(frame.desc.bounds.height());
				metal::MetalBuffer scratch = context->scratch(bytes);
				if (!scratch.valid()) {
					return Error::make(ErrorCode::Memory, "No Metal scratch buffer is available");
				}
				if (Error e = command.copy(frame.handle, 0, scratch.handle(), 0, bytes)) {
					return e;
				}
				frame.handle = scratch.handle();
				command.retain(std::move(scratch));
			}
			bool direct = false;
			if (Error e = textureForFrame(*context, command, frame, false, true, bindable, direct)) {
				return e;
			}
			if (bound.mapIndex < bounds.size()) {
				bounds[bound.mapIndex] = frame.desc.bounds;
			}
		} else {
			if (!empty.valid()) {
				if (Error e = context->newTexture(1, 1, metal::MetalPixelFormat::RGBA32Float, false, empty)) {
					return e;
				}
			}
			bindable = empty;
		}
		dispatch.textures.push_back({bindable.handle(), static_cast<unsigned>(texture->slotIndex)});
		command.retain(bindable);
	}

	// The destination: in place when the window covers the frame and the rows allow it.
	const KernelTexture& destination = layout.destination();
	metal::MetalTexture target;
	bool direct = false;
	const bool wholeFrame = window == out.desc.bounds;
	if (wholeFrame) {
		if (Error e = textureForFrame(*context, command, out, true, false, target, direct)) {
			return e;
		}
	} else {
		const metal::MetalPixelFormat format = metal::metalPixelFormatFor(out.desc.depth, out.desc.components);
		if (format == metal::MetalPixelFormat::Invalid) {
			return Error::make(ErrorCode::Format, "Metal has no texture format for this frame layout");
		}
		if (Error e = context->newTexture(static_cast<unsigned>(window.width()), static_cast<unsigned>(window.height()), format, true, target)) {
			return e;
		}
	}
	if (destination.mapIndex < bounds.size()) {
		bounds[destination.mapIndex] = window;
	}
	dispatch.textures.push_back({target.handle(), static_cast<unsigned>(destination.slotIndex)});
	command.retain(target);
	for (const KernelSampler& sampler : layout.samplers()) {
		dispatch.bilinearSamplers.push_back(static_cast<unsigned>(sampler.slotIndex));
	}
	const GripTile tile = makeGripTile(window, d.pixelScale, bounds, true);
	if (layout.hasTile()) {
		dispatch.bytes.push_back({&tile, sizeof tile, static_cast<unsigned>(layout.tileIndex())});
	}
	if (layout.hasUniforms() && !d.uniforms.empty()) {
		dispatch.bytes.push_back({d.uniforms.data(), d.uniforms.size(), static_cast<unsigned>(layout.uniformsIndex())});
	}
	dispatch.width = static_cast<unsigned>(window.width());
	dispatch.height = static_cast<unsigned>(window.height());
	const std::array<unsigned, 3>& group = layout.threadGroupSize();
	dispatch.threadsPerThreadgroup[0] = group[0];
	dispatch.threadsPerThreadgroup[1] = group[1];
	dispatch.threadsPerThreadgroup[2] = group[2];
	if (Error e = command.dispatch(dispatch)) {
		return e;
	}
	if (!direct) {
		const std::size_t pixelBytes = metal::metalPixelBytes(target.format());
		const std::size_t offset = static_cast<std::size_t>(window.y1 - out.desc.bounds.y1) * static_cast<std::size_t>(out.rowBytes) + static_cast<std::size_t>(window.x1 - out.desc.bounds.x1) * pixelBytes;
		if (Error e = command.copyTextureToBuffer(target, out.handle, offset, static_cast<std::size_t>(out.rowBytes))) {
			return e;
		}
	}
	// Commit without waiting, as ofxGPURender.h asks; the host synchronizes the queue.
	return command.commit();
}

OFXGRIP_NAMESPACE_END
