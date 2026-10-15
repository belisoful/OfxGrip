// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
/*!
	@file  MetalBackend.hpp
	@brief The Metal backend: a context per host command queue with a pipeline cache and a
	       scratch-buffer pool, command buffers that dispatch compute kernels over `MTLBuffer`
	       frames, and owned buffers for hosts and tests.

	Introduced in OfxGrip 0.2.0. macOS only. The backend reaches Metal through metal-cpp, so no
	Objective-C class exists in the plug-in. Every type here is opaque: the handles are the
	`id<MTLBuffer>`, `id<MTLCommandQueue>`, and `id<MTLDevice>` pointers OpenFX hands over, and
	nothing in this header includes a Metal header.

	The OpenFX contract (`ofxGPURender.h`): the host passes `id<MTLBuffer>` frames and its own
	command queue; the plug-in encodes onto that queue, commits, and returns without waiting.
	`MetalContext::forQueue` keeps one context per queue for the life of the process, so compiled
	pipelines and pooled scratch buffers are shared by every instance that renders on it. The
	cache holds only immutable pipelines and reusable buffers under a mutex; nothing in it changes
	a render's result.
*/
#pragma once

#include <ofxgrip/core/Error.hpp>
#include <ofxgrip/core/Types.hpp>

#include <array>
#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

OFXGRIP_NAMESPACE_BEGIN
namespace metal {

class MetalContext;

/*! The pixel formats the backend wraps host frames in. Introduced in OfxGrip 0.4.0. */
enum class MetalPixelFormat : std::uint8_t {
	Invalid = 0,
	RGBA32Float,
	RGBA16Float,
	RGBA16Unorm,
	RGBA8Unorm,
	R32Float,
	R16Float,
	R16Unorm,
	R8Unorm,
};

/*! The pixel format of a frame layout, or `Invalid` for a layout Metal has no format for
    (three-component RGB). */
MetalPixelFormat metalPixelFormatFor(BitDepth depth, Components components) noexcept;
/*! The bytes per pixel of a format. */
std::size_t metalPixelBytes(MetalPixelFormat format) noexcept;

/*! The scalar types a function constant takes. The values are `MTLDataType`. */
enum class MetalDataType : std::uint32_t {
	Float = 3,
	Half = 16,
	Int = 29,
	UInt = 33,
	Short = 37,
	UShort = 41,
	Char = 45,
	UChar = 49,
	Bool = 53,
};

/*! One function constant value for a specialized pipeline. */
struct MetalFunctionConstant {
	unsigned index{0};
	MetalDataType type{MetalDataType::Float};
	std::array<std::uint8_t, 16> bytes{};
};

/*! A texture the backend owns: over a host buffer, or a private allocation. Copies share it. */
class MetalTexture {
public:
	MetalTexture() = default;
	bool valid() const noexcept { return impl_ != nullptr; }
	/*! The `id<MTLTexture>`. */
	void* handle() const noexcept;
	unsigned width() const noexcept;
	unsigned height() const noexcept;
	MetalPixelFormat format() const noexcept;

	struct Impl;  /*!< Implementation detail; defined in the backend source. */

private:
	friend class MetalContext;
	friend class MetalCommandBuffer;
	std::shared_ptr<Impl> impl_;
};

/*! A compiled compute pipeline. Copies share the state. */
class MetalPipeline {
public:
	MetalPipeline() = default;
	bool valid() const noexcept { return impl_ != nullptr; }
	/*! The `MTLComputePipelineState`. */
	void* state() const noexcept;
	unsigned maxThreadsPerThreadgroup() const noexcept;
	unsigned threadExecutionWidth() const noexcept;
	const std::string& function() const noexcept;

	struct Impl;  /*!< Implementation detail; defined in the backend source. */

private:
	friend class MetalContext;
	std::shared_ptr<const Impl> impl_;
};

/*! A buffer the backend owns: a pooled scratch buffer or a fresh allocation. Shared storage,
    so `contents()` is a CPU address. A pooled buffer returns to its pool when the last copy is
    destroyed; a fresh one is released. */
class MetalBuffer {
public:
	MetalBuffer() = default;
	bool valid() const noexcept { return impl_ != nullptr; }
	/*! The `id<MTLBuffer>`. */
	void* handle() const noexcept;
	std::size_t length() const noexcept;
	void* contents() const noexcept;

	struct Impl;  /*!< Implementation detail; defined in the backend source. */

private:
	friend class MetalContext;
	friend class MetalCommandBuffer;
	std::shared_ptr<Impl> impl_;
};

/*! One compute dispatch: a pipeline, the thread grid, and the buffer and byte arguments. Byte
    arguments are copied at encode time. */
struct MetalDispatch {
	struct BufferArg {
		void* handle{nullptr};
		std::size_t offset{0};
		unsigned index{0};
	};
	struct BytesArg {
		const void* data{nullptr};
		std::size_t length{0};
		unsigned index{0};
	};
	struct TextureArg {
		void* handle{nullptr};
		unsigned index{0};
	};
	MetalPipeline pipeline;
	unsigned width{1};
	unsigned height{1};
	unsigned depth{1};
	std::vector<BufferArg> buffers;
	std::vector<BytesArg> bytes;
	std::vector<TextureArg> textures;      /*!< `id<MTLTexture>` arguments. Introduced in OfxGrip 0.4.0. */
	std::vector<unsigned> bilinearSamplers; /*!< Sampler indexes that take the context's bilinear clamp sampler. */
	/*! A fixed threadgroup size; zeros mean the pipeline's own width. With a fixed size the grid
	    counts whole threadgroups, so the kernel bounds-checks. */
	unsigned threadsPerThreadgroup[3]{0, 0, 0};
};

/*! A command buffer on the context's queue. Encode, then `commit` (the OpenFX contract: no wait)
    or `commitAndWait` (hosts and tests). An uncommitted buffer is dropped on destruction. */
class MetalCommandBuffer {
public:
	MetalCommandBuffer();
	~MetalCommandBuffer();
	MetalCommandBuffer(MetalCommandBuffer&&) noexcept;
	MetalCommandBuffer& operator=(MetalCommandBuffer&&) noexcept;
	MetalCommandBuffer(const MetalCommandBuffer&) = delete;
	MetalCommandBuffer& operator=(const MetalCommandBuffer&) = delete;

	bool valid() const noexcept;
	bool committed() const noexcept;
	Error dispatch(const MetalDispatch& dispatch);
	/*! Copies bytes between buffers with a blit. */
	Error copy(void* sourceBuffer, std::size_t sourceOffset, void* destinationBuffer, std::size_t destinationOffset, std::size_t length);
	/*! Fills a byte range of a buffer with a value. */
	Error fill(void* buffer, std::size_t offset, std::size_t length, std::uint8_t value);
	/*! Copies a buffer's rows into a texture, or a texture into a buffer's rows, through a blit.
	    `bytesPerRow` is the buffer's row stride; the texture's size is the copied region.
	    Introduced in OfxGrip 0.4.0. */
	Error copyBufferToTexture(void* sourceBuffer, std::size_t sourceOffset, std::size_t bytesPerRow, const MetalTexture& destination);
	Error copyTextureToBuffer(const MetalTexture& source, void* destinationBuffer, std::size_t destinationOffset, std::size_t bytesPerRow);
	/*! Keeps a buffer alive until the command buffer completes, so a scratch buffer returns to
	    the pool only after the GPU is done with it. */
	void retain(MetalBuffer buffer);
	/*! Keeps a texture alive until the command buffer completes. */
	void retain(MetalTexture texture);
	Error commit();
	Error commitAndWait();

private:
	friend class MetalContext;
	struct Impl;
	std::unique_ptr<Impl> impl_;
	std::shared_ptr<MetalContext> context_;
};

/*! The context over one command queue. */
class MetalContext : public std::enable_shared_from_this<MetalContext> {
public:
	~MetalContext();
	MetalContext(const MetalContext&) = delete;
	MetalContext& operator=(const MetalContext&) = delete;

	/*! Whether the process has a Metal device. */
	static bool available() noexcept;
	/*! The context for a host's `id<MTLCommandQueue>`. One per queue for the process lifetime;
	    null for a null queue. */
	static std::shared_ptr<MetalContext> forQueue(void* commandQueue);
	/*! A context over the system default device and a queue the backend owns, for hosts and
	    tests. One per process. Null without a device. */
	static std::shared_ptr<MetalContext> systemDefault();

	/*! The `id<MTLCommandQueue>`. */
	void* queue() const noexcept;
	/*! The `id<MTLDevice>`. */
	void* device() const noexcept;
	std::string deviceName() const;

	/*! The pipeline for `function` in the Metal Shading Language `source`. The library compiles
	    once per device under `libraryName`; the pipeline builds once per device and function. */
	Error pipeline(std::string_view libraryName, std::string_view source, std::string_view function, MetalPipeline& out);
	/*! The pipeline for `function` specialized with function constants. The pipeline builds
	    once per device, function, and constant set. Fails with `ErrorCode::InvalidParameter`
	    when a constant's type differs from the compiled function's, or a required constant has
	    no value. Introduced in OfxGrip 0.4.0. */
	Error pipeline(std::string_view libraryName, std::string_view source, std::string_view function, const std::vector<MetalFunctionConstant>& constants, MetalPipeline& out);
	MetalCommandBuffer commandBuffer();
	/*! A texture over a buffer's rows, with no copy. `bytesPerRow` must be a multiple of the
	    format's linear alignment; `linearTextureAlignment` tells it. Introduced in OfxGrip 0.4.0. */
	Error textureOverBuffer(void* buffer, std::size_t offset, std::size_t bytesPerRow, unsigned width, unsigned height, MetalPixelFormat format, bool shaderWrite, MetalTexture& out);
	/*! A private texture, for intermediates. */
	Error newTexture(unsigned width, unsigned height, MetalPixelFormat format, bool shaderWrite, MetalTexture& out);
	/*! The row alignment a buffer-backed texture of a format needs, in bytes. */
	std::size_t linearTextureAlignment(MetalPixelFormat format) const;
	/*! The `id<MTLSamplerState>` of a bilinear, clamp-to-edge, normalized sampler, built once. */
	void* bilinearSampler();
	/*! A shared-storage buffer of at least `length` bytes from the pool. */
	MetalBuffer scratch(std::size_t length);
	/*! A fresh shared-storage buffer, filled from `initial` when given and zeroed otherwise. */
	MetalBuffer newBuffer(std::size_t length, const void* initial = nullptr);
	/*! Waits until every command buffer committed to the queue so far has completed. */
	Error waitIdle();
	/*! The bytes held by free pooled buffers. */
	std::size_t pooledBytes() const;
	/*! Releases the free pooled buffers. */
	void drainPool();
	/*! The number of pipelines built on this context's device. */
	std::size_t pipelineCount() const;

private:
	friend struct MetalBuffer::Impl;
	friend class MetalCommandBuffer;
	MetalContext();
	struct Impl;
	std::unique_ptr<Impl> impl_;
};

}  // namespace metal
OFXGRIP_NAMESPACE_END
