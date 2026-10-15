// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
#include <ofxgrip/gpu/MetalBackend.hpp>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include <algorithm>
#include <cstring>
#include <map>
#include <mutex>

OFXGRIP_NAMESPACE_BEGIN
namespace metal {

// ---- device cache -------------------------------------------------------------------------------

namespace {

std::string describe(NS::Error* error)
{
	if (error == nullptr || error->localizedDescription() == nullptr) {
		return "unknown Metal error";
	}
	const char* text = error->localizedDescription()->utf8String();
	return text == nullptr ? "unknown Metal error" : std::string(text);
}

/*! An owned NSString. */
class OwnedString {
public:
	explicit OwnedString(std::string_view text) : string_(NS::String::alloc()->init(std::string(text).c_str(), NS::UTF8StringEncoding)) {}
	~OwnedString()
	{
		if (string_ != nullptr) {
			string_->release();
		}
	}
	OwnedString(const OwnedString&) = delete;
	OwnedString& operator=(const OwnedString&) = delete;
	NS::String* get() const noexcept { return string_; }

private:
	NS::String* string_;
};

/*! Drains autoreleased objects on scope exit. Created per encode and per compile, on the thread
    that does the work, because render threads carry no pool of their own. */
class PoolScope {
public:
	PoolScope() : pool_(NS::AutoreleasePool::alloc()->init()) {}
	~PoolScope()
	{
		if (pool_ != nullptr) {
			pool_->release();
		}
	}
	PoolScope(const PoolScope&) = delete;
	PoolScope& operator=(const PoolScope&) = delete;

private:
	NS::AutoreleasePool* pool_;
};

}  // namespace

struct MetalPipeline::Impl {
	MTL::ComputePipelineState* state{nullptr};
	std::string function;
	unsigned maxThreads{1};
	unsigned width{1};
	~Impl()
	{
		if (state != nullptr) {
			state->release();
		}
	}
};

void* MetalPipeline::state() const noexcept
{
	return impl_ ? impl_->state : nullptr;
}

unsigned MetalPipeline::maxThreadsPerThreadgroup() const noexcept
{
	return impl_ ? impl_->maxThreads : 0;
}

unsigned MetalPipeline::threadExecutionWidth() const noexcept
{
	return impl_ ? impl_->width : 0;
}

const std::string& MetalPipeline::function() const noexcept
{
	static const std::string none;
	return impl_ ? impl_->function : none;
}

namespace {

/*! Compiled libraries and pipelines for one device, shared by every context on that device. */
struct DeviceCache {
	std::mutex mutex;
	std::map<std::string, MTL::Library*> libraries;
	std::map<std::string, std::shared_ptr<const MetalPipeline::Impl>> pipelines;
	MTL::SamplerState* bilinear{nullptr};
	~DeviceCache()
	{
		for (auto& [name, library] : libraries) {
			library->release();
		}
		if (bilinear != nullptr) {
			bilinear->release();
		}
	}
};

std::mutex& registryMutex()
{
	static std::mutex m;
	return m;
}

std::map<void*, std::shared_ptr<DeviceCache>>& deviceCaches()
{
	static std::map<void*, std::shared_ptr<DeviceCache>> caches;
	return caches;
}

std::map<void*, std::shared_ptr<MetalContext>>& contexts()
{
	static std::map<void*, std::shared_ptr<MetalContext>> table;
	return table;
}

std::shared_ptr<DeviceCache> cacheFor(MTL::Device* device)
{
	std::lock_guard<std::mutex> lock(registryMutex());
	std::shared_ptr<DeviceCache>& cache = deviceCaches()[device];
	if (!cache) {
		cache = std::make_shared<DeviceCache>();
	}
	return cache;
}

}  // namespace

// ---- buffers ------------------------------------------------------------------------------------

struct MetalBuffer::Impl {
	MTL::Buffer* buffer{nullptr};
	std::weak_ptr<MetalContext> owner;
	bool pooled{false};
	~Impl();
};

void* MetalBuffer::handle() const noexcept
{
	return impl_ ? impl_->buffer : nullptr;
}

std::size_t MetalBuffer::length() const noexcept
{
	return impl_ ? static_cast<std::size_t>(impl_->buffer->length()) : 0;
}

void* MetalBuffer::contents() const noexcept
{
	return impl_ ? impl_->buffer->contents() : nullptr;
}

// ---- pixel formats and textures -----------------------------------------------------------------

MetalPixelFormat metalPixelFormatFor(BitDepth depth, Components components) noexcept
{
	const bool rgba = components == Components::RGBA;
	const bool alpha = components == Components::Alpha;
	if (!rgba && !alpha) {
		return MetalPixelFormat::Invalid;
	}
	switch (depth) {
	case BitDepth::Float: return rgba ? MetalPixelFormat::RGBA32Float : MetalPixelFormat::R32Float;
	case BitDepth::Half: return rgba ? MetalPixelFormat::RGBA16Float : MetalPixelFormat::R16Float;
	case BitDepth::UInt16: return rgba ? MetalPixelFormat::RGBA16Unorm : MetalPixelFormat::R16Unorm;
	case BitDepth::UInt8: return rgba ? MetalPixelFormat::RGBA8Unorm : MetalPixelFormat::R8Unorm;
	case BitDepth::None: break;
	}
	return MetalPixelFormat::Invalid;
}

std::size_t metalPixelBytes(MetalPixelFormat format) noexcept
{
	switch (format) {
	case MetalPixelFormat::RGBA32Float: return 16;
	case MetalPixelFormat::RGBA16Float:
	case MetalPixelFormat::RGBA16Unorm: return 8;
	case MetalPixelFormat::RGBA8Unorm:
	case MetalPixelFormat::R32Float: return 4;
	case MetalPixelFormat::R16Float:
	case MetalPixelFormat::R16Unorm: return 2;
	case MetalPixelFormat::R8Unorm: return 1;
	case MetalPixelFormat::Invalid: break;
	}
	return 0;
}

namespace {

MTL::PixelFormat nativeFormat(MetalPixelFormat format) noexcept
{
	switch (format) {
	case MetalPixelFormat::RGBA32Float: return MTL::PixelFormatRGBA32Float;
	case MetalPixelFormat::RGBA16Float: return MTL::PixelFormatRGBA16Float;
	case MetalPixelFormat::RGBA16Unorm: return MTL::PixelFormatRGBA16Unorm;
	case MetalPixelFormat::RGBA8Unorm: return MTL::PixelFormatRGBA8Unorm;
	case MetalPixelFormat::R32Float: return MTL::PixelFormatR32Float;
	case MetalPixelFormat::R16Float: return MTL::PixelFormatR16Float;
	case MetalPixelFormat::R16Unorm: return MTL::PixelFormatR16Unorm;
	case MetalPixelFormat::R8Unorm: return MTL::PixelFormatR8Unorm;
	case MetalPixelFormat::Invalid: break;
	}
	return MTL::PixelFormatInvalid;
}

}  // namespace

struct MetalTexture::Impl {
	MTL::Texture* texture{nullptr};
	unsigned width{0};
	unsigned height{0};
	MetalPixelFormat format{MetalPixelFormat::Invalid};
	~Impl()
	{
		if (texture != nullptr) {
			texture->release();
		}
	}
};

void* MetalTexture::handle() const noexcept
{
	return impl_ ? impl_->texture : nullptr;
}

unsigned MetalTexture::width() const noexcept
{
	return impl_ ? impl_->width : 0;
}

unsigned MetalTexture::height() const noexcept
{
	return impl_ ? impl_->height : 0;
}

MetalPixelFormat MetalTexture::format() const noexcept
{
	return impl_ ? impl_->format : MetalPixelFormat::Invalid;
}

// ---- context ------------------------------------------------------------------------------------

struct MetalContext::Impl {
	/*! Resources a committed command buffer still reads. The Metal command buffer retains the
	    Metal objects itself; this list keeps the wrappers, so a pooled scratch buffer stays out
	    of the pool until the GPU is done with it. Swept on the next allocation. A completion
	    handler would do the same, but it runs on a Metal thread the sanitizers cannot follow. */
	struct InFlight {
		MTL::CommandBuffer* buffer{nullptr};
		std::vector<MetalBuffer> buffers;
		std::vector<MetalTexture> textures;
	};
	MTL::Device* device{nullptr};
	MTL::CommandQueue* queue{nullptr};
	bool ownsQueue{false};
	std::shared_ptr<DeviceCache> cache;
	std::mutex poolMutex;
	std::vector<MTL::Buffer*> pool;
	std::mutex inFlightMutex;
	std::vector<InFlight> inFlight;

	~Impl()
	{
		std::vector<InFlight> pending;
		{
			std::lock_guard<std::mutex> lock(inFlightMutex);
			pending.swap(inFlight);
		}
		for (InFlight& entry : pending) {
			entry.buffers.clear();
			entry.textures.clear();
			entry.buffer->release();
		}
		for (MTL::Buffer* buffer : pool) {
			buffer->release();
		}
		if (queue != nullptr) {
			queue->release();
		}
		if (device != nullptr) {
			device->release();
		}
	}

	void returnToPool(MTL::Buffer* buffer)
	{
		std::lock_guard<std::mutex> lock(poolMutex);
		pool.push_back(buffer);
	}

	void hold(MTL::CommandBuffer* buffer, std::vector<MetalBuffer> buffers, std::vector<MetalTexture> textures)
	{
		std::lock_guard<std::mutex> lock(inFlightMutex);
		inFlight.push_back({buffer->retain(), std::move(buffers), std::move(textures)});
	}

	/*! Releases the wrappers of every command buffer the GPU has finished. */
	void sweep()
	{
		std::vector<InFlight> done;
		{
			std::lock_guard<std::mutex> lock(inFlightMutex);
			for (auto it = inFlight.begin(); it != inFlight.end();) {
				if (it->buffer->status() >= MTL::CommandBufferStatusCompleted) {
					done.push_back(std::move(*it));
					it = inFlight.erase(it);
				} else {
					++it;
				}
			}
		}
		for (InFlight& entry : done) {
			entry.buffers.clear();
			entry.textures.clear();
			entry.buffer->release();
		}
	}
};

MetalBuffer::Impl::~Impl()
{
	if (buffer == nullptr) {
		return;
	}
	if (pooled) {
		if (std::shared_ptr<MetalContext> context = owner.lock()) {
			context->impl_->returnToPool(buffer);
			return;
		}
	}
	buffer->release();
}

MetalContext::MetalContext() : impl_(std::make_unique<Impl>()) {}

MetalContext::~MetalContext() = default;

bool MetalContext::available() noexcept
{
	return systemDefault() != nullptr;
}

std::shared_ptr<MetalContext> MetalContext::forQueue(void* commandQueue)
{
	if (commandQueue == nullptr) {
		return nullptr;
	}
	std::lock_guard<std::mutex> lock(registryMutex());
	std::shared_ptr<MetalContext>& entry = contexts()[commandQueue];
	if (!entry) {
		auto* queue = static_cast<MTL::CommandQueue*>(commandQueue);
		std::shared_ptr<MetalContext> context(new MetalContext());
		context->impl_->queue = queue->retain();
		context->impl_->device = queue->device()->retain();
		entry = context;
	}
	if (!entry->impl_->cache) {
		// The device cache lookup takes the registry mutex itself; fill it after unlocking.
		std::shared_ptr<MetalContext> context = entry;
		MTL::Device* device = context->impl_->device;
		std::shared_ptr<DeviceCache>& cache = deviceCaches()[device];
		if (!cache) {
			cache = std::make_shared<DeviceCache>();
		}
		context->impl_->cache = cache;
	}
	return entry;
}

std::shared_ptr<MetalContext> MetalContext::systemDefault()
{
	static std::shared_ptr<MetalContext> context = []() -> std::shared_ptr<MetalContext> {
		MTL::Device* device = MTL::CreateSystemDefaultDevice();
		if (device == nullptr) {
			return nullptr;
		}
		MTL::CommandQueue* queue = device->newCommandQueue();
		if (queue == nullptr) {
			device->release();
			return nullptr;
		}
		std::shared_ptr<MetalContext> made(new MetalContext());
		made->impl_->device = device;
		made->impl_->queue = queue;
		made->impl_->ownsQueue = true;
		made->impl_->cache = cacheFor(device);
		std::lock_guard<std::mutex> lock(registryMutex());
		contexts()[queue] = made;
		return made;
	}();
	return context;
}

void* MetalContext::queue() const noexcept
{
	return impl_->queue;
}

void* MetalContext::device() const noexcept
{
	return impl_->device;
}

std::string MetalContext::deviceName() const
{
	PoolScope pool;
	NS::String* name = impl_->device->name();
	const char* text = name == nullptr ? nullptr : name->utf8String();
	return text == nullptr ? std::string() : std::string(text);
}

Error MetalContext::pipeline(std::string_view libraryName, std::string_view source, std::string_view function, MetalPipeline& out)
{
	return pipeline(libraryName, source, function, {}, out);
}

Error MetalContext::pipeline(std::string_view libraryName, std::string_view source, std::string_view function, const std::vector<MetalFunctionConstant>& constants, MetalPipeline& out)
{
	DeviceCache& cache = *impl_->cache;
	std::lock_guard<std::mutex> lock(cache.mutex);
	std::string key = std::string(libraryName) + "/" + std::string(function);
	for (const MetalFunctionConstant& c : constants) {
		key += "/" + std::to_string(c.index) + ":" + std::to_string(static_cast<unsigned>(c.type)) + "=";
		for (std::uint8_t b : c.bytes) {
			key += std::to_string(b) + ".";
		}
	}
	if (auto it = cache.pipelines.find(key); it != cache.pipelines.end()) {
		out.impl_ = it->second;
		return Error::none();
	}
	PoolScope pool;
	MTL::Library* library = nullptr;
	if (auto it = cache.libraries.find(std::string(libraryName)); it != cache.libraries.end()) {
		library = it->second;
	} else {
		OwnedString text(source);
		MTL::CompileOptions* options = MTL::CompileOptions::alloc()->init();
		NS::Error* error = nullptr;
		library = impl_->device->newLibrary(text.get(), options, &error);
		options->release();
		if (library == nullptr) {
			return Error::make(ErrorCode::Render, "The Metal library did not compile", describe(error)).with("library", std::string(libraryName));
		}
		cache.libraries[std::string(libraryName)] = library;
	}
	OwnedString name(function);
	MTL::Function* fn = library->newFunction(name.get());
	if (fn == nullptr) {
		return Error::make(ErrorCode::NotFound, "The Metal library has no such function").with("library", std::string(libraryName)).with("function", std::string(function));
	}
	NS::Dictionary* compiled = fn->functionConstantsDictionary();
	if (compiled != nullptr && compiled->count() > 0) {
		// Metal specializes a function with an unset required constant without an error, so the
		// types and the required set are checked here against the reflection the kernel carries.
		MTL::FunctionConstantValues* values = MTL::FunctionConstantValues::alloc()->init();
		std::map<unsigned, MTL::FunctionConstant*> byIndex;
		NS::Enumerator<NS::Object>* keys = compiled->keyEnumerator();
		while (NS::Object* k = keys->nextObject()) {
			auto* constant = compiled->object<MTL::FunctionConstant>(k);
			byIndex[static_cast<unsigned>(constant->index())] = constant;
		}
		for (const MetalFunctionConstant& c : constants) {
			auto it = byIndex.find(c.index);
			if (it == byIndex.end()) {
				continue;
			}
			if (static_cast<std::uint32_t>(it->second->type()) != static_cast<std::uint32_t>(c.type)) {
				values->release();
				fn->release();
				return Error::make(ErrorCode::InvalidParameter, "A function constant's type differs between the reflection and the compiled library").with("index", std::to_string(c.index));
			}
			values->setConstantValue(c.bytes.data(), static_cast<MTL::DataType>(c.type), c.index);
			byIndex.erase(it);
		}
		for (const auto& [index, constant] : byIndex) {
			if (constant->required()) {
				values->release();
				fn->release();
				return Error::make(ErrorCode::InvalidParameter, "A required function constant has no value").with("index", std::to_string(index));
			}
		}
		NS::Error* specializeError = nullptr;
		MTL::Function* specialized = library->newFunction(name.get(), values, &specializeError);
		values->release();
		fn->release();
		if (specialized == nullptr) {
			return Error::make(ErrorCode::Render, "The Metal function did not specialize", describe(specializeError)).with("function", std::string(function));
		}
		fn = specialized;
	}
	NS::Error* error = nullptr;
	MTL::ComputePipelineState* state = impl_->device->newComputePipelineState(fn, &error);
	fn->release();
	if (state == nullptr) {
		return Error::make(ErrorCode::Render, "The Metal pipeline did not build", describe(error)).with("function", std::string(function));
	}
	auto impl = std::make_shared<MetalPipeline::Impl>();
	impl->state = state;
	impl->function = std::string(function);
	impl->maxThreads = static_cast<unsigned>(state->maxTotalThreadsPerThreadgroup());
	impl->width = static_cast<unsigned>(state->threadExecutionWidth());
	cache.pipelines[key] = impl;
	out.impl_ = impl;
	return Error::none();
}

MetalBuffer MetalContext::scratch(std::size_t length)
{
	impl_->sweep();
	MTL::Buffer* found = nullptr;
	{
		std::lock_guard<std::mutex> lock(impl_->poolMutex);
		auto best = impl_->pool.end();
		for (auto it = impl_->pool.begin(); it != impl_->pool.end(); ++it) {
			if ((*it)->length() >= length && (best == impl_->pool.end() || (*it)->length() < (*best)->length())) {
				best = it;
			}
		}
		if (best != impl_->pool.end()) {
			found = *best;
			impl_->pool.erase(best);
		}
	}
	if (found == nullptr) {
		found = impl_->device->newBuffer(std::max<std::size_t>(length, 16), MTL::ResourceStorageModeShared);
		if (found == nullptr) {
			return {};
		}
	}
	MetalBuffer buffer;
	buffer.impl_ = std::make_shared<MetalBuffer::Impl>();
	buffer.impl_->buffer = found;
	buffer.impl_->owner = weak_from_this();
	buffer.impl_->pooled = true;
	return buffer;
}

MetalBuffer MetalContext::newBuffer(std::size_t length, const void* initial)
{
	impl_->sweep();
	const std::size_t size = std::max<std::size_t>(length, 16);
	MTL::Buffer* made = initial != nullptr && length > 0 ? impl_->device->newBuffer(initial, size, MTL::ResourceStorageModeShared) : impl_->device->newBuffer(size, MTL::ResourceStorageModeShared);
	if (made == nullptr) {
		return {};
	}
	if (initial == nullptr) {
		std::memset(made->contents(), 0, size);
	} else if (length < size) {
		std::memset(static_cast<std::uint8_t*>(made->contents()) + length, 0, size - length);
	}
	MetalBuffer buffer;
	buffer.impl_ = std::make_shared<MetalBuffer::Impl>();
	buffer.impl_->buffer = made;
	buffer.impl_->pooled = false;
	return buffer;
}

Error MetalContext::waitIdle()
{
	MetalCommandBuffer fence = commandBuffer();
	if (!fence.valid()) {
		return Error::make(ErrorCode::Render, "No Metal command buffer is available");
	}
	Error e = fence.commitAndWait();
	impl_->sweep();
	return e;
}

std::size_t MetalContext::pooledBytes() const
{
	std::lock_guard<std::mutex> lock(impl_->poolMutex);
	std::size_t total = 0;
	for (MTL::Buffer* buffer : impl_->pool) {
		total += static_cast<std::size_t>(buffer->length());
	}
	return total;
}

void MetalContext::drainPool()
{
	impl_->sweep();
	std::vector<MTL::Buffer*> drained;
	{
		std::lock_guard<std::mutex> lock(impl_->poolMutex);
		drained.swap(impl_->pool);
	}
	for (MTL::Buffer* buffer : drained) {
		buffer->release();
	}
}

std::size_t MetalContext::pipelineCount() const
{
	std::lock_guard<std::mutex> lock(impl_->cache->mutex);
	return impl_->cache->pipelines.size();
}

namespace {

MTL::TextureDescriptor* textureDescriptor(MetalPixelFormat format, unsigned width, unsigned height, bool shaderWrite, bool shared)
{
	MTL::TextureDescriptor* descriptor = MTL::TextureDescriptor::texture2DDescriptor(nativeFormat(format), width, height, false);
	descriptor->setUsage(shaderWrite ? (MTL::TextureUsageShaderRead | MTL::TextureUsageShaderWrite) : MTL::TextureUsageShaderRead);
	descriptor->setStorageMode(shared ? MTL::StorageModeShared : MTL::StorageModePrivate);
	return descriptor;
}

}  // namespace

Error MetalContext::textureOverBuffer(void* buffer, std::size_t offset, std::size_t bytesPerRow, unsigned width, unsigned height, MetalPixelFormat format, bool shaderWrite, MetalTexture& out)
{
	if (buffer == nullptr) {
		return Error::make(ErrorCode::BadHandle, "The texture's buffer is null");
	}
	if (format == MetalPixelFormat::Invalid || width == 0 || height == 0) {
		return Error::make(ErrorCode::Format, "The texture has no usable format or size");
	}
	const std::size_t alignment = linearTextureAlignment(format);
	if (bytesPerRow < width * metalPixelBytes(format) || (alignment != 0 && bytesPerRow % alignment != 0) || (alignment != 0 && offset % alignment != 0)) {
		return Error::make(ErrorCode::Format, "The buffer's rows are not aligned for a texture").with("bytesPerRow", std::to_string(bytesPerRow)).with("alignment", std::to_string(alignment));
	}
	auto* native = static_cast<MTL::Buffer*>(buffer);
	if (offset + bytesPerRow * (height - 1) + width * metalPixelBytes(format) > native->length()) {
		return Error::make(ErrorCode::BadIndex, "The texture exceeds the buffer's length");
	}
	PoolScope pool;
	// The buffer's own storage mode decides the texture's; the descriptor must agree with it.
	MTL::TextureDescriptor* descriptor = textureDescriptor(format, width, height, shaderWrite, native->storageMode() == MTL::StorageModeShared);
	descriptor->setStorageMode(native->storageMode());
	MTL::Texture* texture = native->newTexture(descriptor, offset, bytesPerRow);
	if (texture == nullptr) {
		return Error::make(ErrorCode::Render, "The buffer-backed texture did not allocate");
	}
	out.impl_ = std::make_shared<MetalTexture::Impl>();
	out.impl_->texture = texture;
	out.impl_->width = width;
	out.impl_->height = height;
	out.impl_->format = format;
	return Error::none();
}

Error MetalContext::newTexture(unsigned width, unsigned height, MetalPixelFormat format, bool shaderWrite, MetalTexture& out)
{
	if (format == MetalPixelFormat::Invalid || width == 0 || height == 0) {
		return Error::make(ErrorCode::Format, "The texture has no usable format or size");
	}
	PoolScope pool;
	MTL::Texture* texture = impl_->device->newTexture(textureDescriptor(format, width, height, shaderWrite, false));
	if (texture == nullptr) {
		return Error::make(ErrorCode::Memory, "The texture did not allocate");
	}
	out.impl_ = std::make_shared<MetalTexture::Impl>();
	out.impl_->texture = texture;
	out.impl_->width = width;
	out.impl_->height = height;
	out.impl_->format = format;
	return Error::none();
}

std::size_t MetalContext::linearTextureAlignment(MetalPixelFormat format) const
{
	if (format == MetalPixelFormat::Invalid) {
		return 0;
	}
	return static_cast<std::size_t>(impl_->device->minimumLinearTextureAlignmentForPixelFormat(nativeFormat(format)));
}

void* MetalContext::bilinearSampler()
{
	DeviceCache& cache = *impl_->cache;
	std::lock_guard<std::mutex> lock(cache.mutex);
	if (cache.bilinear == nullptr) {
		PoolScope pool;
		MTL::SamplerDescriptor* descriptor = MTL::SamplerDescriptor::alloc()->init();
		descriptor->setMinFilter(MTL::SamplerMinMagFilterLinear);
		descriptor->setMagFilter(MTL::SamplerMinMagFilterLinear);
		descriptor->setMipFilter(MTL::SamplerMipFilterNotMipmapped);
		descriptor->setSAddressMode(MTL::SamplerAddressModeClampToEdge);
		descriptor->setTAddressMode(MTL::SamplerAddressModeClampToEdge);
		descriptor->setNormalizedCoordinates(true);
		cache.bilinear = impl_->device->newSamplerState(descriptor);
		descriptor->release();
	}
	return cache.bilinear;
}

// ---- command buffers ----------------------------------------------------------------------------

struct MetalCommandBuffer::Impl {
	MTL::CommandBuffer* buffer{nullptr};
	std::vector<MetalBuffer> retained;
	std::vector<MetalTexture> retainedTextures;
	bool committed{false};
	~Impl()
	{
		if (buffer != nullptr) {
			buffer->release();
		}
	}
};

MetalCommandBuffer::MetalCommandBuffer() = default;
MetalCommandBuffer::~MetalCommandBuffer() = default;
MetalCommandBuffer::MetalCommandBuffer(MetalCommandBuffer&&) noexcept = default;
MetalCommandBuffer& MetalCommandBuffer::operator=(MetalCommandBuffer&&) noexcept = default;

MetalCommandBuffer MetalContext::commandBuffer()
{
	impl_->sweep();
	MetalCommandBuffer out;
	PoolScope pool;
	MTL::CommandBuffer* buffer = impl_->queue->commandBuffer();
	if (buffer == nullptr) {
		return out;
	}
	out.impl_ = std::make_unique<MetalCommandBuffer::Impl>();
	out.impl_->buffer = buffer->retain();
	out.context_ = shared_from_this();
	return out;
}

bool MetalCommandBuffer::valid() const noexcept
{
	return impl_ != nullptr && impl_->buffer != nullptr;
}

bool MetalCommandBuffer::committed() const noexcept
{
	return impl_ != nullptr && impl_->committed;
}

Error MetalCommandBuffer::dispatch(const MetalDispatch& dispatch)
{
	if (!valid()) {
		return Error::make(ErrorCode::BadHandle, "The Metal command buffer is not valid");
	}
	if (impl_->committed) {
		return Error::make(ErrorCode::WrongPhase, "The Metal command buffer was already committed");
	}
	if (!dispatch.pipeline.valid()) {
		return Error::make(ErrorCode::BadHandle, "The dispatch has no pipeline");
	}
	if (dispatch.width == 0 || dispatch.height == 0 || dispatch.depth == 0) {
		return Error::none();
	}
	PoolScope pool;
	MTL::ComputeCommandEncoder* encoder = impl_->buffer->computeCommandEncoder();
	if (encoder == nullptr) {
		return Error::make(ErrorCode::Render, "No compute encoder is available");
	}
	encoder->setComputePipelineState(static_cast<MTL::ComputePipelineState*>(dispatch.pipeline.state()));
	for (const MetalDispatch::BufferArg& arg : dispatch.buffers) {
		if (arg.handle == nullptr) {
			encoder->endEncoding();
			return Error::make(ErrorCode::BadHandle, "A dispatch buffer argument is null").with("index", std::to_string(arg.index));
		}
		encoder->setBuffer(static_cast<MTL::Buffer*>(arg.handle), arg.offset, arg.index);
	}
	for (const MetalDispatch::BytesArg& arg : dispatch.bytes) {
		encoder->setBytes(arg.data, arg.length, arg.index);
	}
	for (const MetalDispatch::TextureArg& arg : dispatch.textures) {
		if (arg.handle == nullptr) {
			encoder->endEncoding();
			return Error::make(ErrorCode::BadHandle, "A dispatch texture argument is null").with("index", std::to_string(arg.index));
		}
		encoder->setTexture(static_cast<MTL::Texture*>(arg.handle), arg.index);
	}
	if (!dispatch.bilinearSamplers.empty()) {
		auto* sampler = static_cast<MTL::SamplerState*>(context_->bilinearSampler());
		for (unsigned index : dispatch.bilinearSamplers) {
			encoder->setSamplerState(sampler, index);
		}
	}
	// Threadgroup counts are rounded up and the kernel bounds-checks, which every Metal device
	// supports; non-uniform threadgroups are not required.
	unsigned width = dispatch.threadsPerThreadgroup[0];
	unsigned height = dispatch.threadsPerThreadgroup[1];
	unsigned depth = dispatch.threadsPerThreadgroup[2];
	if (width == 0 || height == 0 || depth == 0) {
		width = std::max(1u, dispatch.pipeline.threadExecutionWidth());
		height = std::max(1u, dispatch.pipeline.maxThreadsPerThreadgroup() / width);
		depth = 1;
	}
	if (width * height * depth > std::max(1u, dispatch.pipeline.maxThreadsPerThreadgroup())) {
		encoder->endEncoding();
		return Error::make(ErrorCode::Render, "The threadgroup exceeds the pipeline's maximum").with("threads", std::to_string(width * height * depth));
	}
	const MTL::Size group(width, height, depth);
	const MTL::Size groups((dispatch.width + width - 1) / width, (dispatch.height + height - 1) / height, (dispatch.depth + depth - 1) / depth);
	encoder->dispatchThreadgroups(groups, group);
	encoder->endEncoding();
	return Error::none();
}

Error MetalCommandBuffer::copyBufferToTexture(void* sourceBuffer, std::size_t sourceOffset, std::size_t bytesPerRow, const MetalTexture& destination)
{
	if (!valid()) {
		return Error::make(ErrorCode::BadHandle, "The Metal command buffer is not valid");
	}
	if (impl_->committed) {
		return Error::make(ErrorCode::WrongPhase, "The Metal command buffer was already committed");
	}
	if (sourceBuffer == nullptr || !destination.valid()) {
		return Error::make(ErrorCode::BadHandle, "A copy operand is null");
	}
	auto* source = static_cast<MTL::Buffer*>(sourceBuffer);
	const std::size_t pixelBytes = metalPixelBytes(destination.format());
	if (bytesPerRow < destination.width() * pixelBytes || sourceOffset + bytesPerRow * (destination.height() - 1) + destination.width() * pixelBytes > source->length()) {
		return Error::make(ErrorCode::BadIndex, "The copy exceeds the buffer's length");
	}
	PoolScope pool;
	MTL::BlitCommandEncoder* encoder = impl_->buffer->blitCommandEncoder();
	if (encoder == nullptr) {
		return Error::make(ErrorCode::Render, "No blit encoder is available");
	}
	encoder->copyFromBuffer(source, sourceOffset, bytesPerRow, 0, MTL::Size(destination.width(), destination.height(), 1), static_cast<MTL::Texture*>(destination.handle()), 0, 0, MTL::Origin(0, 0, 0));
	encoder->endEncoding();
	return Error::none();
}

Error MetalCommandBuffer::copyTextureToBuffer(const MetalTexture& source, void* destinationBuffer, std::size_t destinationOffset, std::size_t bytesPerRow)
{
	if (!valid()) {
		return Error::make(ErrorCode::BadHandle, "The Metal command buffer is not valid");
	}
	if (impl_->committed) {
		return Error::make(ErrorCode::WrongPhase, "The Metal command buffer was already committed");
	}
	if (destinationBuffer == nullptr || !source.valid()) {
		return Error::make(ErrorCode::BadHandle, "A copy operand is null");
	}
	auto* destination = static_cast<MTL::Buffer*>(destinationBuffer);
	const std::size_t pixelBytes = metalPixelBytes(source.format());
	if (bytesPerRow < source.width() * pixelBytes || destinationOffset + bytesPerRow * (source.height() - 1) + source.width() * pixelBytes > destination->length()) {
		return Error::make(ErrorCode::BadIndex, "The copy exceeds the buffer's length");
	}
	PoolScope pool;
	MTL::BlitCommandEncoder* encoder = impl_->buffer->blitCommandEncoder();
	if (encoder == nullptr) {
		return Error::make(ErrorCode::Render, "No blit encoder is available");
	}
	encoder->copyFromTexture(static_cast<MTL::Texture*>(source.handle()), 0, 0, MTL::Origin(0, 0, 0), MTL::Size(source.width(), source.height(), 1), destination, destinationOffset, bytesPerRow, 0);
	encoder->endEncoding();
	return Error::none();
}

void MetalCommandBuffer::retain(MetalTexture texture)
{
	if (impl_ && texture.valid()) {
		impl_->retainedTextures.push_back(std::move(texture));
	}
}

Error MetalCommandBuffer::copy(void* sourceBuffer, std::size_t sourceOffset, void* destinationBuffer, std::size_t destinationOffset, std::size_t length)
{
	if (!valid()) {
		return Error::make(ErrorCode::BadHandle, "The Metal command buffer is not valid");
	}
	if (impl_->committed) {
		return Error::make(ErrorCode::WrongPhase, "The Metal command buffer was already committed");
	}
	if (sourceBuffer == nullptr || destinationBuffer == nullptr) {
		return Error::make(ErrorCode::BadHandle, "A copy buffer is null");
	}
	auto* source = static_cast<MTL::Buffer*>(sourceBuffer);
	auto* destination = static_cast<MTL::Buffer*>(destinationBuffer);
	if (sourceOffset + length > source->length() || destinationOffset + length > destination->length()) {
		return Error::make(ErrorCode::BadIndex, "The copy exceeds a buffer's length");
	}
	if (length == 0) {
		return Error::none();
	}
	PoolScope pool;
	MTL::BlitCommandEncoder* encoder = impl_->buffer->blitCommandEncoder();
	if (encoder == nullptr) {
		return Error::make(ErrorCode::Render, "No blit encoder is available");
	}
	encoder->copyFromBuffer(source, sourceOffset, destination, destinationOffset, length);
	encoder->endEncoding();
	return Error::none();
}

Error MetalCommandBuffer::fill(void* buffer, std::size_t offset, std::size_t length, std::uint8_t value)
{
	if (!valid()) {
		return Error::make(ErrorCode::BadHandle, "The Metal command buffer is not valid");
	}
	if (impl_->committed) {
		return Error::make(ErrorCode::WrongPhase, "The Metal command buffer was already committed");
	}
	if (buffer == nullptr) {
		return Error::make(ErrorCode::BadHandle, "The fill buffer is null");
	}
	auto* target = static_cast<MTL::Buffer*>(buffer);
	if (offset + length > target->length()) {
		return Error::make(ErrorCode::BadIndex, "The fill exceeds the buffer's length");
	}
	if (length == 0) {
		return Error::none();
	}
	PoolScope pool;
	MTL::BlitCommandEncoder* encoder = impl_->buffer->blitCommandEncoder();
	if (encoder == nullptr) {
		return Error::make(ErrorCode::Render, "No blit encoder is available");
	}
	encoder->fillBuffer(target, NS::Range(offset, length), value);
	encoder->endEncoding();
	return Error::none();
}

void MetalCommandBuffer::retain(MetalBuffer buffer)
{
	if (impl_ && buffer.valid()) {
		impl_->retained.push_back(std::move(buffer));
	}
}

Error MetalCommandBuffer::commit()
{
	if (!valid()) {
		return Error::make(ErrorCode::BadHandle, "The Metal command buffer is not valid");
	}
	if (impl_->committed) {
		return Error::make(ErrorCode::WrongPhase, "The Metal command buffer was already committed");
	}
	if (!impl_->retained.empty() || !impl_->retainedTextures.empty()) {
		// The context keeps the retained wrappers until a later sweep sees the GPU done with them.
		context_->impl_->hold(impl_->buffer, std::move(impl_->retained), std::move(impl_->retainedTextures));
		impl_->retained.clear();
		impl_->retainedTextures.clear();
	}
	impl_->buffer->commit();
	impl_->committed = true;
	return Error::none();
}

Error MetalCommandBuffer::commitAndWait()
{
	if (Error e = commit()) {
		return e;
	}
	impl_->buffer->waitUntilCompleted();
	PoolScope pool;
	if (NS::Error* error = impl_->buffer->error()) {
		return Error::make(ErrorCode::Render, "The Metal command buffer failed", describe(error));
	}
	return Error::none();
}

}  // namespace metal
OFXGRIP_NAMESPACE_END
