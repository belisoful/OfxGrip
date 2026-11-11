// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
#include <ofxgrip/gpu/CudaKernelBackend.hpp>

#include <ofxgrip/core/KernelTile.hpp>

#include <cstring>
#include <map>
#include <mutex>
#include <optional>

OFXGRIP_NAMESPACE_BEGIN

namespace {

using cuda::CudaDevicePointer;
using cuda::CudaDriver;
using cuda::CudaObject;
using cuda::CudaResult;
using cuda::kCudaSuccess;

Error failed(const CudaDriver& driver, const char* what, CudaResult result)
{
	const char* name = nullptr;
	driver.getErrorName(result, &name);
	return Error::make(ErrorCode::Render, what).with("cudaResult", std::to_string(result)).with("cudaError", name != nullptr ? name : "unknown");
}

CudaDevicePointer devicePointer(const void* handle) noexcept
{
	return static_cast<CudaDevicePointer>(reinterpret_cast<std::uintptr_t>(handle));
}

int formatFor(BitDepth depth) noexcept
{
	switch (depth) {
	case BitDepth::UInt8: return cuda::kCudaFormatUInt8;
	case BitDepth::UInt16: return cuda::kCudaFormatUInt16;
	case BitDepth::Half: return cuda::kCudaFormatHalf;
	case BitDepth::Float: return cuda::kCudaFormatFloat;
	default: return 0;
	}
}

unsigned channelsFor(Components components) noexcept
{
	// A CUDA array holds 1, 2, or 4 channels.
	switch (components) {
	case Components::RGBA: return 4;
	case Components::Alpha: return 1;
	default: return 0;
	}
}

std::uint64_t fnv1a(std::string_view bytes) noexcept
{
	std::uint64_t hash = 1469598103934665603ull;
	for (const char c : bytes) {
		hash ^= static_cast<std::uint8_t>(c);
		hash *= 1099511628211ull;
	}
	return hash;
}

std::size_t alignUp(std::size_t value, std::size_t alignment) noexcept
{
	return (value + alignment - 1) / alignment * alignment;
}

/*! One loaded module. The launch mutex serializes the launches that share its constant memory. */
struct Module {
	void* context{nullptr};
	void* module{nullptr};
	void* function{nullptr};
	CudaDevicePointer globals{0};
	std::size_t globalsBytes{0};
	int textureAlignment{0};
	int pitchAlignment{0};
	std::mutex launch;
};

/*! Everything one dispatch creates on the device, released on every path out. */
struct Frame {
	const CudaDriver& driver;
	std::vector<CudaDevicePointer> memory;
	std::vector<void*> arrays;
	std::vector<CudaObject> textures;
	std::vector<CudaObject> surfaces;

	explicit Frame(const CudaDriver& d) : driver(d) {}
	Frame(const Frame&) = delete;
	Frame& operator=(const Frame&) = delete;
	~Frame()
	{
		for (CudaObject object : textures) {
			driver.texObjectDestroy(object);
		}
		for (CudaObject object : surfaces) {
			driver.surfObjectDestroy(object);
		}
		for (void* array : arrays) {
			driver.arrayDestroy(array);
		}
		for (CudaDevicePointer pointer : memory) {
			driver.memFree(pointer);
		}
	}

	Error array(std::size_t width, std::size_t height, int format, unsigned channels, void*& out)
	{
		cuda::CudaArrayDescriptor descriptor;
		descriptor.width = width;
		descriptor.height = height;
		descriptor.format = format;
		descriptor.channels = channels;
		if (CudaResult r = driver.arrayCreate(&out, &descriptor); r != kCudaSuccess) {
			out = nullptr;
			return failed(driver, "CUDA created no array", r);
		}
		arrays.push_back(out);
		return Error::none();
	}

	Error texture(const cuda::CudaResourceDescriptor& resource, CudaObject& out)
	{
		cuda::CudaTextureDescriptor descriptor;
		descriptor.addressMode[0] = cuda::kCudaAddressClamp;
		descriptor.addressMode[1] = cuda::kCudaAddressClamp;
		descriptor.addressMode[2] = cuda::kCudaAddressClamp;
		descriptor.filterMode = cuda::kCudaFilterLinear;
		descriptor.flags = cuda::kCudaNormalizedCoordinates;
		if (CudaResult r = driver.texObjectCreate(&out, &resource, &descriptor, nullptr); r != kCudaSuccess) {
			return failed(driver, "CUDA created no texture object", r);
		}
		textures.push_back(out);
		return Error::none();
	}
};

}  // namespace

struct CudaKernelBackend::Impl {
	const CudaDriver* driver{nullptr};
	mutable std::mutex mutex;
	std::map<std::string, std::unique_ptr<Module>> modules;

	Error moduleFor(void* context, const KernelDispatch& d, Module*& out);
	Error sourceTexture(Frame& frame, const Module& module, const GpuImage& image, void* stream, CudaObject& out);
};

CudaKernelBackend::CudaKernelBackend() : CudaKernelBackend(CudaDriver::system()) {}

CudaKernelBackend::CudaKernelBackend(const CudaDriver* driver) : impl_(std::make_unique<Impl>())
{
	impl_->driver = driver != nullptr && driver->complete() ? driver : nullptr;
}

CudaKernelBackend::~CudaKernelBackend()
{
	if (impl_->driver == nullptr) {
		return;
	}
	// A module unloads only inside its own context; a context that is gone took its modules.
	void* current = nullptr;
	impl_->driver->ctxGetCurrent(&current);
	for (auto& [key, module] : impl_->modules) {
		if (module->module != nullptr && module->context == current) {
			impl_->driver->moduleUnload(module->module);
		}
	}
}

bool CudaKernelBackend::available() const noexcept
{
	return impl_->driver != nullptr;
}

std::size_t CudaKernelBackend::cachedModuleCount() const
{
	std::lock_guard<std::mutex> lock(impl_->mutex);
	return impl_->modules.size();
}

void CudaKernelBackend::registerWithEngine()
{
	static std::once_flag once;
	std::call_once(once, []() { KernelEngine::shared().registerBackend(std::make_unique<CudaKernelBackend>()); });
}

Error CudaKernelBackend::Impl::moduleFor(void* context, const KernelDispatch& d, Module*& out)
{
	const std::string& ptx = d.implementation->source;
	const std::string key = std::to_string(reinterpret_cast<std::uintptr_t>(context)) + "|" + d.cacheKey + "|" + std::to_string(fnv1a(ptx)) + "|" + std::to_string(ptx.size());
	std::lock_guard<std::mutex> lock(mutex);
	if (auto it = modules.find(key); it != modules.end()) {
		out = it->second.get();
		return Error::none();
	}
	auto loaded = std::make_unique<Module>();
	loaded->context = context;
	// The image is PTX text; std::string keeps the terminator the driver reads to.
	if (CudaResult r = driver->moduleLoadData(&loaded->module, ptx.c_str()); r != kCudaSuccess) {
		return failed(*driver, "CUDA loaded no module from the kernel's PTX", r).with("kernel", d.cacheKey);
	}
	const std::string& entry = d.layout->entryPoint();
	if (CudaResult r = driver->moduleGetFunction(&loaded->function, loaded->module, entry.c_str()); r != kCudaSuccess) {
		driver->moduleUnload(loaded->module);
		return failed(*driver, "The CUDA module has no such kernel function", r).with("entryPoint", entry);
	}
	if (CudaResult r = driver->moduleGetGlobal(&loaded->globals, &loaded->globalsBytes, loaded->module, kCudaGlobalsName); r != kCudaSuccess) {
		driver->moduleUnload(loaded->module);
		return failed(*driver, "The CUDA module has no globals block", r).with("global", kCudaGlobalsName);
	}
	if (loaded->globalsBytes < d.layout->globalsLength()) {
		driver->moduleUnload(loaded->module);
		return Error::make(ErrorCode::InvalidParameter, "The CUDA module's globals block is smaller than its reflection layout; rebuild the PTX with the layout")
		    .with("module", std::to_string(loaded->globalsBytes))
		    .with("layout", std::to_string(d.layout->globalsLength()));
	}
	int device = 0;
	if (driver->ctxGetDevice(&device) == kCudaSuccess) {
		driver->deviceGetAttribute(&loaded->textureAlignment, cuda::kCudaAttributeTextureAlignment, device);
		driver->deviceGetAttribute(&loaded->pitchAlignment, cuda::kCudaAttributeTexturePitchAlignment, device);
	}
	out = loaded.get();
	modules.emplace(key, std::move(loaded));
	return Error::none();
}

Error CudaKernelBackend::Impl::sourceTexture(Frame& frame, const Module& module, const GpuImage& image, void* stream, CudaObject& out)
{
	const int format = formatFor(image.desc.depth);
	const unsigned channels = channelsFor(image.desc.components);
	if (format == 0 || channels == 0 || image.rowBytes <= 0) {
		return Error::make(ErrorCode::Format, "CUDA has no texture format for this frame layout").with("components", std::string(toString(image.desc.components)));
	}
	const std::size_t width = static_cast<std::size_t>(image.desc.bounds.width());
	const std::size_t height = static_cast<std::size_t>(image.desc.bounds.height());
	const std::size_t pitch = static_cast<std::size_t>(image.rowBytes);
	const CudaDevicePointer pointer = devicePointer(image.handle);
	cuda::CudaResourceDescriptor resource;
	// In place when the host's memory meets the device's texture alignment (and the host allows
	// reading it in place: Q-RESOLVE-031); through an array otherwise.
	const bool aligned = module.textureAlignment > 0 && module.pitchAlignment > 0 && pointer % static_cast<CudaDevicePointer>(module.textureAlignment) == 0
	                     && pitch % static_cast<std::size_t>(module.pitchAlignment) == 0;
	if (aligned && !image.needsIntermediateCopy) {
		resource.type = cuda::kCudaResourcePitch2D;
		resource.resource.pitch2D.pointer = pointer;
		resource.resource.pitch2D.format = format;
		resource.resource.pitch2D.channels = channels;
		resource.resource.pitch2D.width = width;
		resource.resource.pitch2D.height = height;
		resource.resource.pitch2D.pitchBytes = pitch;
		return frame.texture(resource, out);
	}
	void* array = nullptr;
	if (Error e = frame.array(width, height, format, channels, array)) {
		return e;
	}
	cuda::CudaMemcpy2D copy;
	copy.sourceType = cuda::kCudaMemoryDevice;
	copy.sourceDevice = pointer;
	copy.sourcePitch = pitch;
	copy.destinationType = cuda::kCudaMemoryArray;
	copy.destinationArray = array;
	copy.widthBytes = width * image.desc.pixelBytes();
	copy.height = height;
	if (CudaResult r = driver->memcpy2DAsync(&copy, stream); r != kCudaSuccess) {
		return failed(*driver, "CUDA copied no source frame into an array", r);
	}
	resource.type = cuda::kCudaResourceArray;
	resource.resource.array.array = array;
	return frame.texture(resource, out);
}

Error CudaKernelBackend::dispatch(const KernelDispatch& d)
{
	if (impl_->driver == nullptr) {
		return Error::make(ErrorCode::MissingHostFeature, "No CUDA driver is loaded");
	}
	const CudaDriver& driver = *impl_->driver;
	const KernelLayout& layout = *d.layout;
	if (layout.form() != KernelBindingForm::Block) {
		return Error::make(ErrorCode::InvalidParameter, "The CUDA backend needs the CUDA target's block layout");
	}
	const GpuImage& out = d.destination.gpu;
	if (out.api != GpuApi::Cuda || out.handle == nullptr || out.rowBytes <= 0) {
		return Error::make(ErrorCode::InvalidParameter, "The CUDA backend writes a CUDA frame");
	}
	if (out.desc.depth != BitDepth::Float || out.desc.components != Components::RGBA) {
		return Error::make(ErrorCode::Format, "The CUDA backend writes 32-bit float RGBA frames").with("depth", std::string(toString(out.desc.depth))).with("components", std::string(toString(out.desc.components)));
	}
	const RectI window = d.window.empty() ? out.desc.bounds : d.window.intersect(out.desc.bounds);
	if (window.empty()) {
		return Error::none();
	}
	void* context = nullptr;
	if (CudaResult r = driver.ctxGetCurrent(&context); r != kCudaSuccess || context == nullptr) {
		return Error::make(ErrorCode::MissingHostFeature, "The host made no CUDA context current on the render thread");
	}
	if (d.abort && d.abort()) {
		return Error::cancelled();
	}
	Module* module = nullptr;
	if (Error e = impl_->moduleFor(context, d, module)) {
		return e;
	}
	void* stream = out.queue;
	Frame frame(driver);

	// The uniforms and the tile, in one device allocation.
	const std::size_t uniformsBytes = std::max<std::size_t>(layout.uniformsLength(), 16);
	const std::size_t tileOffset = alignUp(uniformsBytes, 16);
	CudaDevicePointer block = 0;
	if (CudaResult r = driver.memAlloc(&block, tileOffset + sizeof(GripTile)); r != kCudaSuccess) {
		return Error::make(ErrorCode::Memory, "CUDA allocated no memory for the kernel's uniforms").with("cudaResult", std::to_string(r));
	}
	frame.memory.push_back(block);

	Bytes globals(std::max<std::size_t>(layout.globalsLength(), 1), 0);
	auto put = [&globals](std::size_t offset, const void* data, std::size_t size) {
		if (offset + size <= globals.size()) {
			std::memcpy(globals.data() + offset, data, size);
		}
	};
	std::vector<std::optional<RectI>> bounds(layout.textureCount());
	for (const KernelBindingPlan::Texture& bound : d.plan->textures) {
		const KernelTexture* texture = layout.textureNamed(bound.name);
		const KernelImage* image = kernelImageFor(d, bound);
		CudaObject object = 0;
		if (image != nullptr && image->connected && image->isGpu() && image->gpu.handle != nullptr) {
			if (image->gpu.api != GpuApi::Cuda) {
				return Error::make(ErrorCode::InvalidParameter, "The CUDA backend reads CUDA sources").with("texture", bound.name);
			}
			if (Error e = impl_->sourceTexture(frame, *module, image->gpu, stream, object)) {
				return e.with("texture", bound.name);
			}
			if (bound.mapIndex < bounds.size()) {
				bounds[bound.mapIndex] = image->gpu.desc.bounds;
			}
		} else {
			if (image != nullptr && image->connected && !image->isGpu()) {
				return Error::make(ErrorCode::InvalidParameter, "The CUDA backend reads CUDA sources").with("texture", bound.name);
			}
			// A disconnected image reads transparent black: one zero texel.
			void* array = nullptr;
			if (Error e = frame.array(1, 1, cuda::kCudaFormatFloat, 4, array)) {
				return e;
			}
			const float zero[4] = {0, 0, 0, 0};
			cuda::CudaMemcpy2D clear;
			clear.sourceType = cuda::kCudaMemoryHost;
			clear.sourceHost = zero;
			clear.sourcePitch = sizeof zero;
			clear.destinationType = cuda::kCudaMemoryArray;
			clear.destinationArray = array;
			clear.widthBytes = sizeof zero;
			clear.height = 1;
			if (CudaResult r = driver.memcpy2D(&clear); r != kCudaSuccess) {
				return failed(driver, "CUDA cleared no empty texture", r);
			}
			cuda::CudaResourceDescriptor resource;
			resource.type = cuda::kCudaResourceArray;
			resource.resource.array.array = array;
			if (Error e = frame.texture(resource, object)) {
				return e;
			}
		}
		put(texture->blockOffset, &object, sizeof object);
	}

	// The destination: a float RGBA array the kernel writes as a surface.
	const KernelTexture& destination = layout.destination();
	if (destination.mapIndex < bounds.size()) {
		bounds[destination.mapIndex] = window;
	}
	const std::size_t width = static_cast<std::size_t>(window.width());
	const std::size_t height = static_cast<std::size_t>(window.height());
	void* staging = nullptr;
	if (Error e = frame.array(width, height, cuda::kCudaFormatFloat, 4, staging)) {
		return e;
	}
	cuda::CudaResourceDescriptor surfaceResource;
	surfaceResource.type = cuda::kCudaResourceArray;
	surfaceResource.resource.array.array = staging;
	CudaObject surface = 0;
	if (CudaResult r = driver.surfObjectCreate(&surface, &surfaceResource); r != kCudaSuccess) {
		return failed(driver, "CUDA created no surface object", r);
	}
	frame.surfaces.push_back(surface);
	put(destination.blockOffset, &surface, sizeof surface);

	// OpenFX frames are bottom-up, so row 0 of every texture is the bottom row.
	const GripTile tile = makeGripTile(window, d.pixelScale, bounds, true);
	Bytes uniforms = d.uniforms;
	uniforms.resize(uniformsBytes, 0);
	if (CudaResult r = driver.memcpyHtoD(block, uniforms.data(), uniformsBytes); r != kCudaSuccess) {
		return failed(driver, "CUDA copied no uniforms to the device", r);
	}
	if (CudaResult r = driver.memcpyHtoD(block + tileOffset, &tile, sizeof tile); r != kCudaSuccess) {
		return failed(driver, "CUDA copied no tile to the device", r);
	}
	const CudaDevicePointer uniformsPointer = block;
	const CudaDevicePointer tilePointer = block + tileOffset;
	if (layout.hasUniforms()) {
		put(layout.uniformsOffset(), &uniformsPointer, sizeof uniformsPointer);
	}
	if (layout.hasTile()) {
		put(layout.tileOffset(), &tilePointer, sizeof tilePointer);
	}
	for (const KernelConstantValue& constant : d.constants) {
		put(constant.blockOffset, constant.bytes.data(), constant.length);
	}

	const std::array<unsigned, 3>& group = layout.threadGroupSize();
	const unsigned gridX = (static_cast<unsigned>(width) + group[0] - 1) / group[0];
	const unsigned gridY = (static_cast<unsigned>(height) + group[1] - 1) / group[1];
	{
		// The module's constant memory is shared by every launch, so the globals stay in place
		// until this launch has finished.
		std::lock_guard<std::mutex> lock(module->launch);
		if (CudaResult r = driver.memcpyHtoD(module->globals, globals.data(), layout.globalsLength()); r != kCudaSuccess) {
			return failed(driver, "CUDA copied no globals into the module", r);
		}
		if (CudaResult r = driver.launchKernel(module->function, gridX, gridY, 1, group[0], group[1], group[2], 0, stream, nullptr, nullptr); r != kCudaSuccess) {
			return failed(driver, "CUDA launched no kernel", r).with("kernel", d.cacheKey);
		}
		if (CudaResult r = driver.streamSynchronize(stream); r != kCudaSuccess) {
			return failed(driver, "The CUDA kernel did not finish", r).with("kernel", d.cacheKey);
		}
	}
	cuda::CudaMemcpy2D store;
	store.sourceType = cuda::kCudaMemoryArray;
	store.sourceArray = staging;
	store.destinationXBytes = static_cast<std::size_t>(window.x1 - out.desc.bounds.x1) * out.desc.pixelBytes();
	store.destinationY = static_cast<std::size_t>(window.y1 - out.desc.bounds.y1);
	store.destinationType = cuda::kCudaMemoryDevice;
	store.destinationDevice = devicePointer(out.handle);
	store.destinationPitch = static_cast<std::size_t>(out.rowBytes);
	store.widthBytes = width * out.desc.pixelBytes();
	store.height = height;
	if (CudaResult r = driver.memcpy2D(&store); r != kCudaSuccess) {
		return failed(driver, "CUDA copied no result into the host's frame", r);
	}
	return Error::none();
}

OFXGRIP_NAMESPACE_END
