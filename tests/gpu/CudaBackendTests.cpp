// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
// The CUDA kernel backend and watermark against a fake driver. The fake keeps device memory,
// arrays, and texture and surface objects on the host, so the tests read what the backend
// bound, launched, and stored. A launch runs a hook in place of the kernel. No test here needs
// an NVIDIA GPU, and none proves the pixels a real kernel writes.
#include <ofxgrip/gpu/CudaDriver.hpp>
#include <ofxgrip/gpu/CudaKernelBackend.hpp>
#include <ofxgrip/gpu/CudaWatermark.hpp>
#include <ofxgrip/gpu/KernelCpu.hpp>
#include <ofxgrip/core/KernelMetadata.hpp>
#include <ofxgrip/core/KernelTile.hpp>

#include <gtest/gtest.h>

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <sstream>

OFXGRIP_DECLARE_KERNEL(GripInspect);

using namespace ofxgrip;
using namespace ofxgrip::cuda;

namespace {

struct FakeArray {
	std::size_t width{0};
	std::size_t height{0};
	int format{0};
	unsigned channels{0};
	std::vector<std::uint8_t> bytes;
	std::size_t texelBytes() const { return (format == kCudaFormatFloat ? 4u : format == kCudaFormatUInt8 ? 1u : 2u) * channels; }
	std::size_t rowBytes() const { return width * texelBytes(); }
};

struct FakeObject {
	bool surface{false};
	CudaResourceDescriptor resource;
	CudaTextureDescriptor texture;
};

/*! The fake driver's state. One at a time; the driver functions are plain function pointers. */
struct Fake {
	void* current{reinterpret_cast<void*>(std::uintptr_t{0x1000})};
	std::set<void*> memory;
	std::map<void*, std::unique_ptr<FakeArray>> arrays;
	std::map<CudaObject, FakeObject> objects;
	CudaObject nextObject{100};
	std::vector<std::uint8_t> globals = std::vector<std::uint8_t>(128, 0);
	std::string function{"fxGripKernel"};
	std::vector<std::string> images;
	int unloads{0};
	int launches{0};
	int synchronizations{0};
	unsigned grid[3]{0, 0, 0};
	unsigned block[3]{0, 0, 0};
	void* stream{nullptr};
	int textureAlignment{32};
	int pitchAlignment{32};
	std::string failOn;
	int failAfter{0};  // the call of that name that fails, counted from 0
	std::map<std::string, int> calls;
	std::function<void(Fake&)> onLaunch;

	bool fails(const char* name)
	{
		const int call = calls[name]++;
		return failOn == name && call >= failAfter;
	}
	std::size_t liveResources() const { return memory.size() + arrays.size() + objects.size(); }
	FakeArray* arrayOf(CudaObject object)
	{
		auto it = objects.find(object);
		if (it == objects.end() || it->second.resource.type != kCudaResourceArray) {
			return nullptr;
		}
		auto found = arrays.find(it->second.resource.resource.array.array);
		return found == arrays.end() ? nullptr : found->second.get();
	}
};

Fake* gFake = nullptr;
constexpr CudaResult kFailure = 2;

std::uint8_t* hostOf(CudaDevicePointer pointer)
{
	return reinterpret_cast<std::uint8_t*>(static_cast<std::uintptr_t>(pointer));
}

const std::uint8_t* rowOf(int type, const void* host, CudaDevicePointer device, void* array, std::size_t pitch, std::size_t xBytes, std::size_t y)
{
	if (type == kCudaMemoryHost) {
		return static_cast<const std::uint8_t*>(host) + y * pitch + xBytes;
	}
	if (type == kCudaMemoryDevice) {
		return hostOf(device) + y * pitch + xBytes;
	}
	FakeArray* a = gFake->arrays.at(array).get();
	return a->bytes.data() + y * a->rowBytes() + xBytes;
}

CudaResult fakeMemcpy2D(const CudaMemcpy2D* c)
{
	if (gFake->fails("memcpy2D")) {
		return kFailure;
	}
	for (std::size_t row = 0; row < c->height; ++row) {
		const std::uint8_t* from = rowOf(c->sourceType, c->sourceHost, c->sourceDevice, c->sourceArray, c->sourcePitch, c->sourceXBytes, c->sourceY + row);
		auto* to = const_cast<std::uint8_t*>(rowOf(c->destinationType, c->destinationHost, c->destinationDevice, c->destinationArray, c->destinationPitch, c->destinationXBytes, c->destinationY + row));
		std::memcpy(to, from, c->widthBytes);
	}
	return kCudaSuccess;
}

CudaDriver fakeDriver()
{
	CudaDriver d;
	d.init = [](unsigned) -> CudaResult { return kCudaSuccess; };
	d.ctxGetCurrent = [](void** context) -> CudaResult {
		*context = gFake->current;
		return gFake->fails("ctxGetCurrent") ? kFailure : kCudaSuccess;
	};
	d.ctxGetDevice = [](int* device) -> CudaResult {
		*device = 0;
		return kCudaSuccess;
	};
	d.deviceGetAttribute = [](int* value, int attribute, int) -> CudaResult {
		*value = attribute == kCudaAttributeTexturePitchAlignment ? gFake->pitchAlignment : gFake->textureAlignment;
		return kCudaSuccess;
	};
	d.getErrorName = [](CudaResult error, const char** name) -> CudaResult {
		*name = error == kFailure ? "CUDA_ERROR_OUT_OF_MEMORY" : nullptr;
		return kCudaSuccess;
	};
	d.moduleLoadData = [](void** module, const void* image) -> CudaResult {
		if (gFake->fails("moduleLoadData")) {
			return kFailure;
		}
		gFake->images.emplace_back(static_cast<const char*>(image));
		*module = reinterpret_cast<void*>(std::uintptr_t{0x2000} + gFake->images.size());
		return kCudaSuccess;
	};
	d.moduleUnload = [](void*) -> CudaResult {
		++gFake->unloads;
		return kCudaSuccess;
	};
	d.moduleGetFunction = [](void** function, void*, const char* name) -> CudaResult {
		if (gFake->fails("moduleGetFunction") || gFake->function != name) {
			return kFailure;
		}
		*function = reinterpret_cast<void*>(std::uintptr_t{0x3000});
		return kCudaSuccess;
	};
	d.moduleGetGlobal = [](CudaDevicePointer* pointer, std::size_t* bytes, void*, const char* name) -> CudaResult {
		if (gFake->fails("moduleGetGlobal") || std::string(name) != kCudaGlobalsName) {
			return kFailure;
		}
		*pointer = static_cast<CudaDevicePointer>(reinterpret_cast<std::uintptr_t>(gFake->globals.data()));
		*bytes = gFake->globals.size();
		return kCudaSuccess;
	};
	d.memAlloc = [](CudaDevicePointer* pointer, std::size_t bytes) -> CudaResult {
		if (gFake->fails("memAlloc")) {
			return kFailure;
		}
		void* block = std::calloc(1, bytes);
		gFake->memory.insert(block);
		*pointer = static_cast<CudaDevicePointer>(reinterpret_cast<std::uintptr_t>(block));
		return kCudaSuccess;
	};
	d.memFree = [](CudaDevicePointer pointer) -> CudaResult {
		void* block = hostOf(pointer);
		gFake->memory.erase(block);
		std::free(block);
		return kCudaSuccess;
	};
	d.memcpyHtoD = [](CudaDevicePointer destination, const void* source, std::size_t bytes) -> CudaResult {
		if (gFake->fails("memcpyHtoD")) {
			return kFailure;
		}
		std::memcpy(hostOf(destination), source, bytes);
		return kCudaSuccess;
	};
	d.memcpy2D = fakeMemcpy2D;
	d.memcpy2DAsync = [](const CudaMemcpy2D* copy, void*) -> CudaResult {
		if (gFake->fails("memcpy2DAsync")) {
			return kFailure;
		}
		--gFake->calls["memcpy2D"];  // the shared body below counts itself
		return fakeMemcpy2D(copy);
	};
	d.arrayCreate = [](void** array, const CudaArrayDescriptor* descriptor) -> CudaResult {
		if (gFake->fails("arrayCreate")) {
			return kFailure;
		}
		auto created = std::make_unique<FakeArray>();
		created->width = descriptor->width;
		created->height = descriptor->height;
		created->format = descriptor->format;
		created->channels = descriptor->channels;
		created->bytes.assign(created->rowBytes() * created->height, 0xcd);
		*array = created.get();
		gFake->arrays[created.get()] = std::move(created);
		return kCudaSuccess;
	};
	d.arrayDestroy = [](void* array) -> CudaResult {
		gFake->arrays.erase(array);
		return kCudaSuccess;
	};
	d.texObjectCreate = [](CudaObject* object, const CudaResourceDescriptor* resource, const CudaTextureDescriptor* texture, const void*) -> CudaResult {
		if (gFake->fails("texObjectCreate")) {
			return kFailure;
		}
		*object = gFake->nextObject++;
		gFake->objects[*object] = FakeObject{false, *resource, *texture};
		return kCudaSuccess;
	};
	d.texObjectDestroy = [](CudaObject object) -> CudaResult {
		gFake->objects.erase(object);
		return kCudaSuccess;
	};
	d.surfObjectCreate = [](CudaObject* object, const CudaResourceDescriptor* resource) -> CudaResult {
		if (gFake->fails("surfObjectCreate")) {
			return kFailure;
		}
		*object = gFake->nextObject++;
		gFake->objects[*object] = FakeObject{true, *resource, CudaTextureDescriptor{}};
		return kCudaSuccess;
	};
	d.surfObjectDestroy = [](CudaObject object) -> CudaResult {
		gFake->objects.erase(object);
		return kCudaSuccess;
	};
	d.launchKernel = [](void*, unsigned gx, unsigned gy, unsigned gz, unsigned bx, unsigned by, unsigned bz, unsigned, void* stream, void**, void**) -> CudaResult {
		if (gFake->fails("launchKernel")) {
			return kFailure;
		}
		++gFake->launches;
		gFake->grid[0] = gx;
		gFake->grid[1] = gy;
		gFake->grid[2] = gz;
		gFake->block[0] = bx;
		gFake->block[1] = by;
		gFake->block[2] = bz;
		gFake->stream = stream;
		if (gFake->onLaunch) {
			gFake->onLaunch(*gFake);
		}
		return kCudaSuccess;
	};
	d.streamSynchronize = [](void*) -> CudaResult {
		if (gFake->fails("streamSynchronize")) {
			return kFailure;
		}
		++gFake->synchronizations;
		return kCudaSuccess;
	};
	return d;
}

/*! A CUDA frame whose device memory is host memory, with a chosen pitch and alignment. */
struct DeviceFrame {
	ImageDesc desc;
	std::size_t pitch{0};
	void* block{nullptr};
	std::uint8_t* data{nullptr};

	DeviceFrame(const RectI& bounds, BitDepth depth, Components components, std::size_t padding = 0, std::size_t misalign = 0)
	{
		desc.bounds = bounds;
		desc.regionOfDefinition = bounds;
		desc.depth = depth;
		desc.components = components;
		pitch = desc.minimumRowBytes() + padding;
		const std::size_t bytes = pitch * static_cast<std::size_t>(bounds.height());
		block = std::calloc(1, bytes + 128);
		const std::uintptr_t aligned = (reinterpret_cast<std::uintptr_t>(block) + 63) / 64 * 64;
		data = reinterpret_cast<std::uint8_t*>(aligned + misalign);
	}
	DeviceFrame(const DeviceFrame&) = delete;
	DeviceFrame& operator=(const DeviceFrame&) = delete;
	~DeviceFrame() { std::free(block); }

	ImageView view() const
	{
		ImageView v;
		v.desc = desc;
		v.data = data;
		v.rowBytes = static_cast<std::ptrdiff_t>(pitch);
		return v;
	}
	KernelImage image(void* stream = nullptr) const
	{
		KernelImage k;
		k.connected = true;
		k.gpu.api = GpuApi::Cuda;
		k.gpu.queue = stream;
		k.gpu.handle = data;
		k.gpu.desc = desc;
		k.gpu.rowBytes = static_cast<std::ptrdiff_t>(pitch);
		return k;
	}
	void fill()
	{
		const ImageView v = view();
		for (int y = desc.bounds.y1; y < desc.bounds.y2; ++y) {
			for (int x = desc.bounds.x1; x < desc.bounds.x2; ++x) {
				for (std::size_t c = 0; c < componentCount(desc.components); ++c) {
					writeComponent(v.pixel(x, y), desc.depth, c, static_cast<float>(x + y * 3 + static_cast<int>(c)) / 64.0f);
				}
			}
		}
	}
};

std::string fixture(const char* name)
{
	std::ifstream in(std::filesystem::path(OFXGRIP_KERNEL_FIXTURES) / name, std::ios::binary);
	std::stringstream text;
	text << in.rdbuf();
	return text.str();
}

// GripInspect's parameter IDs (its metadata block).
enum : ParameterId { kRadius = 1, kGain = 2, kMode = 9, kTaps = 10, kFill = 11, kRamp = 12, kMask = 13 };

/*! The inspect kernel as a CUDA build: the checked-in CUDA layout with a stand-in for the PTX,
    which only an NVIDIA toolchain produces. */
struct CudaInspect {
	KernelMetadata metadata;
	KernelLayout layout;
	KernelBindingPlan plan;
	KernelImplementation implementation;

	CudaInspect()
	{
		const KernelRecord record = kernelRecordFor(OFXGRIP_KERNEL(GripInspect));
		Error e = KernelMetadata::scan(record.slangSource, metadata);
		EXPECT_TRUE(e.ok()) << e.describe();
		implementation.api = GpuApi::Cuda;
		implementation.universal = true;
		implementation.source = "// stand-in PTX";
		implementation.layoutJson = fixture("GripInspect.cuda.json");
		implementation.entryPoint = "fxGripKernel";
		e = KernelLayout::parse(implementation.layoutJson, layout);
		EXPECT_TRUE(e.ok()) << e.describe();
		e = KernelBindingPlan::resolve(layout, metadata, plan);
		EXPECT_TRUE(e.ok()) << e.describe();
	}

	KernelDispatch dispatch(const DeviceFrame& destination, void* stream = nullptr) const
	{
		KernelValueMap values;
		values[kRadius] = Value(3.0);
		values[kGain] = Value(0.5);
		values[kMode] = Value(7);
		values[kTaps] = Value(2);
		values[kFill] = Value(0.125);
		KernelDispatch d;
		d.layout = &layout;
		d.implementation = &implementation;
		d.plan = &plan;
		d.cacheKey = "ofxgrip.tests.cuda";
		d.uniforms = packKernelUniforms(layout, plan, values, 1.0);
		d.constants = packKernelConstants(plan, values, 1.0);
		d.destination = destination.image(stream);
		return d;
	}
};

template <typename T>
T globalAt(const Fake& fake, std::size_t offset)
{
	T value{};
	std::memcpy(&value, fake.globals.data() + offset, sizeof value);
	return value;
}

class CudaBackend : public ::testing::Test {
protected:
	void SetUp() override
	{
		gFake = &fake_;
		driver_ = fakeDriver();
	}
	void TearDown() override { gFake = nullptr; }
	Fake fake_;
	CudaDriver driver_;
	CudaInspect inspect_;
};

}  // namespace

TEST(CudaDriver, TheSystemDriverIsAbsentOrComplete)
{
	const CudaDriver* system = CudaDriver::system();
	EXPECT_EQ(system, CudaDriver::system());
	if (system != nullptr) {
		EXPECT_TRUE(system->complete());
	}
	EXPECT_FALSE(CudaDriver{}.complete());
	CudaDriver partial;
	partial.init = [](unsigned) -> CudaResult { return kCudaSuccess; };
	EXPECT_FALSE(partial.complete());
	// A backend over no driver, or over a partial one, is unavailable and says so.
	CudaKernelBackend none(nullptr);
	EXPECT_FALSE(none.available());
	EXPECT_EQ(none.api(), GpuApi::Cuda);
	CudaKernelBackend incomplete(&partial);
	EXPECT_FALSE(incomplete.available());
	CudaInspect inspect;
	DeviceFrame out({0, 0, 8, 8}, BitDepth::Float, Components::RGBA);
	EXPECT_EQ(none.dispatch(inspect.dispatch(out)).code(), ErrorCode::MissingHostFeature);
	CudaKernelBackend systemBackend;
	EXPECT_EQ(systemBackend.available(), system != nullptr);
}

TEST_F(CudaBackend, BindsTheGlobalsLaunchesAndStoresTheWindow)
{
	const RectI bounds{0, 0, 20, 12};
	const RectI window{4, 2, 17, 11};
	void* stream = reinterpret_cast<void*>(std::uintptr_t{0x5000});
	DeviceFrame out(bounds, BitDepth::Float, Components::RGBA, 32);
	DeviceFrame aligned({0, 0, 16, 8}, BitDepth::Float, Components::RGBA);  // 256-byte rows on a 64-byte boundary
	DeviceFrame fusion({0, 0, 16, 8}, BitDepth::UInt8, Components::RGBA);
	DeviceFrame mask({2, 1, 9, 6}, BitDepth::Half, Components::Alpha, 3);   // an unaligned pitch
	aligned.fill();
	fusion.fill();
	mask.fill();
	KernelDispatch d = inspect_.dispatch(out, stream);
	d.window = window;
	d.pixelScale = {0.5, 0.5};
	KernelImage fusionImage = fusion.image(stream);
	fusionImage.gpu.needsIntermediateCopy = true;  // Q-RESOLVE-031
	d.sources = {aligned.image(stream), fusionImage};
	d.keyedImages[kMask] = mask.image(stream);
	const KernelLayout& layout = inspect_.layout;

	GripTile seenTile;
	Bytes seenUniforms;
	std::map<std::string, FakeObject> seenObjects;
	std::map<std::string, std::vector<std::uint8_t>> seenArrayBytes;
	fake_.onLaunch = [&](Fake& fake) {
		std::memcpy(&seenTile, hostOf(globalAt<CudaDevicePointer>(fake, layout.tileOffset())), sizeof seenTile);
		seenUniforms.resize(layout.uniformsLength());
		std::memcpy(seenUniforms.data(), hostOf(globalAt<CudaDevicePointer>(fake, layout.uniformsOffset())), seenUniforms.size());
		for (const KernelTexture& texture : layout.textures()) {
			const CudaObject object = globalAt<CudaObject>(fake, texture.blockOffset);
			seenObjects[texture.name] = fake.objects.at(object);
			if (FakeArray* array = fake.arrayOf(object)) {
				seenArrayBytes[texture.name] = array->bytes;
			}
		}
		// The stand-in kernel: every texel of the surface becomes (x, y, 0.25, 1).
		FakeArray* surface = fake.arrayOf(globalAt<CudaObject>(fake, layout.destination().blockOffset));
		ASSERT_NE(surface, nullptr);
		for (std::size_t y = 0; y < surface->height; ++y) {
			for (std::size_t x = 0; x < surface->width; ++x) {
				const float texel[4] = {static_cast<float>(x), static_cast<float>(y), 0.25f, 1.0f};
				std::memcpy(surface->bytes.data() + y * surface->rowBytes() + x * 16, texel, sizeof texel);
			}
		}
	};

	{
		CudaKernelBackend backend(&driver_);
		ASSERT_TRUE(backend.available());
		const Error e = backend.dispatch(d);
		ASSERT_TRUE(e.ok()) << e.describe();

		// The module, the function, and the launch.
		ASSERT_EQ(fake_.images.size(), 1u);
		EXPECT_EQ(fake_.images[0], "// stand-in PTX");
		EXPECT_EQ(fake_.launches, 1);
		EXPECT_EQ(fake_.synchronizations, 1);
		EXPECT_EQ(fake_.stream, stream);
		EXPECT_EQ(fake_.grid[0], 2u);  // 13 texels in 8-wide groups
		EXPECT_EQ(fake_.grid[1], 2u);  // 9 texels in 8-high groups
		EXPECT_EQ(fake_.grid[2], 1u);
		EXPECT_EQ(fake_.block[0], 8u);
		EXPECT_EQ(fake_.block[1], 8u);
		EXPECT_EQ(fake_.block[2], 1u);

		// The globals: the packed uniforms, the tile over the window with ascending rows, the
		// constants as plain values.
		EXPECT_EQ(seenUniforms, d.uniforms);
		EXPECT_FLOAT_EQ(seenTile.destinationBounds[0], 4.0f);
		EXPECT_FLOAT_EQ(seenTile.destinationBounds[1], 2.0f);
		EXPECT_FLOAT_EQ(seenTile.destinationBounds[2], 17.0f);
		EXPECT_FLOAT_EQ(seenTile.destinationBounds[3], 11.0f);
		EXPECT_FLOAT_EQ(seenTile.pixelScale[0], 0.5f);
		EXPECT_EQ(seenTile.reserved & kGripTileRowsAscend, kGripTileRowsAscend);
		EXPECT_EQ(seenTile.textureCount, layout.textureCount());
		EXPECT_EQ(globalAt<std::int32_t>(fake_, layout.constantNamed("MODE")->blockOffset), 7);
		EXPECT_EQ(globalAt<std::int32_t>(fake_, layout.constantNamed("TAPS")->blockOffset), 2);
		EXPECT_FLOAT_EQ(globalAt<float>(fake_, layout.constantNamed("FILL")->blockOffset), 0.125f);

		// An aligned frame binds in place.
		const FakeObject& first = seenObjects.at("first");
		EXPECT_FALSE(first.surface);
		EXPECT_EQ(first.resource.type, kCudaResourcePitch2D);
		EXPECT_EQ(hostOf(first.resource.resource.pitch2D.pointer), aligned.data);
		EXPECT_EQ(first.resource.resource.pitch2D.format, kCudaFormatFloat);
		EXPECT_EQ(first.resource.resource.pitch2D.channels, 4u);
		EXPECT_EQ(first.resource.resource.pitch2D.width, 16u);
		EXPECT_EQ(first.resource.resource.pitch2D.height, 8u);
		EXPECT_EQ(first.resource.resource.pitch2D.pitchBytes, aligned.pitch);
		EXPECT_EQ(first.texture.filterMode, kCudaFilterLinear);
		EXPECT_EQ(first.texture.addressMode[0], kCudaAddressClamp);
		EXPECT_EQ(first.texture.addressMode[1], kCudaAddressClamp);
		EXPECT_EQ(first.texture.flags, kCudaNormalizedCoordinates);
		// A frame the host forbids reading in place, and a frame with an unaligned pitch, are
		// copied into arrays of their own depth and layout.
		const FakeObject& second = seenObjects.at("second");
		EXPECT_EQ(second.resource.type, kCudaResourceArray);
		ASSERT_EQ(seenArrayBytes.at("second").size(), 16u * 8u * 4u);
		EXPECT_EQ(std::memcmp(seenArrayBytes.at("second").data(), fusion.data, 16 * 8 * 4), 0);
		EXPECT_EQ(seenObjects.at("mask").resource.type, kCudaResourceArray);
		const std::vector<std::uint8_t>& maskBytes = seenArrayBytes.at("mask");
		ASSERT_EQ(maskBytes.size(), 7u * 5u * 2u);
		for (std::size_t row = 0; row < 5; ++row) {
			EXPECT_EQ(std::memcmp(maskBytes.data() + row * 14, mask.data + row * mask.pitch, 14), 0) << "row " << row;
		}
		// An unbound texture reads one transparent black texel.
		const std::vector<std::uint8_t>& empty = seenArrayBytes.at("ramp");
		EXPECT_EQ(empty, std::vector<std::uint8_t>(16, 0));
		EXPECT_TRUE(seenObjects.at("destination").surface);
		// The tile maps the masked texture over its own bounds.
		const GripTileMap expected = gripTileMapFor(mask.desc.bounds, true);
		const unsigned maskIndex = layout.textureNamed("mask")->mapIndex;
		EXPECT_FLOAT_EQ(seenTile.textures[maskIndex].u[0], expected.u[0]);
		EXPECT_FLOAT_EQ(seenTile.textures[maskIndex].v[2], expected.v[2]);

		// The window holds the kernel's pixels; the rest of the frame is untouched.
		const ImageView v = out.view();
		for (int y = bounds.y1; y < bounds.y2; ++y) {
			for (int x = bounds.x1; x < bounds.x2; ++x) {
				const float r = readComponent(v.pixel(x, y), BitDepth::Float, 0);
				const float g = readComponent(v.pixel(x, y), BitDepth::Float, 1);
				if (window.contains(x, y)) {
					ASSERT_FLOAT_EQ(r, static_cast<float>(x - window.x1)) << x << "," << y;
					ASSERT_FLOAT_EQ(g, static_cast<float>(y - window.y1)) << x << "," << y;
				} else {
					ASSERT_FLOAT_EQ(r, 0.0f) << x << "," << y;
				}
			}
		}
		// Every device resource of the dispatch is released.
		EXPECT_EQ(fake_.liveResources(), 0u);

		// The module is loaded once per context, key, and PTX.
		ASSERT_TRUE(backend.dispatch(d).ok());
		EXPECT_EQ(fake_.images.size(), 1u);
		EXPECT_EQ(backend.cachedModuleCount(), 1u);
		KernelDispatch other = d;
		other.cacheKey = "ofxgrip.tests.cuda.other";
		ASSERT_TRUE(backend.dispatch(other).ok());
		EXPECT_EQ(fake_.images.size(), 2u);
		void* first1000 = fake_.current;
		fake_.current = reinterpret_cast<void*>(std::uintptr_t{0x1100});
		ASSERT_TRUE(backend.dispatch(d).ok());
		EXPECT_EQ(fake_.images.size(), 3u);
		EXPECT_EQ(backend.cachedModuleCount(), 3u);
		fake_.current = first1000;
		EXPECT_EQ(fake_.unloads, 0);
	}
	// The backend unloads the modules of the context that is current, and leaves the others to
	// their context.
	EXPECT_EQ(fake_.unloads, 2);
}

TEST_F(CudaBackend, BindsAMisalignedFrameThroughAnArray)
{
	DeviceFrame out({0, 0, 8, 8}, BitDepth::Float, Components::RGBA);
	DeviceFrame odd({0, 0, 8, 8}, BitDepth::Float, Components::RGBA, 0, 4);  // off the texture alignment
	odd.fill();
	KernelDispatch d = inspect_.dispatch(out);
	d.sources = {odd.image()};
	int firstType = -1;
	fake_.onLaunch = [&](Fake& fake) { firstType = fake.objects.at(globalAt<CudaObject>(fake, inspect_.layout.textureNamed("first")->blockOffset)).resource.type; };
	CudaKernelBackend backend(&driver_);
	ASSERT_TRUE(backend.dispatch(d).ok());
	EXPECT_EQ(firstType, kCudaResourceArray);
	// A device that reports no alignment never binds in place.
	fake_.textureAlignment = 0;
	KernelDispatch again = inspect_.dispatch(out);
	again.cacheKey = "ofxgrip.tests.cuda.noalignment";
	DeviceFrame aligned({0, 0, 8, 8}, BitDepth::Float, Components::RGBA);
	again.sources = {aligned.image()};
	firstType = -1;
	ASSERT_TRUE(backend.dispatch(again).ok());
	EXPECT_EQ(firstType, kCudaResourceArray);
}

TEST_F(CudaBackend, RefusesWhatItCannotRun)
{
	CudaKernelBackend backend(&driver_);
	DeviceFrame out({0, 0, 8, 8}, BitDepth::Float, Components::RGBA);
	// A slot layout.
	KernelLayout metal;
	ASSERT_TRUE(KernelLayout::parse(fixture("GripInspect.metal.json"), metal).ok());
	KernelDispatch slots = inspect_.dispatch(out);
	slots.layout = &metal;
	EXPECT_EQ(backend.dispatch(slots).code(), ErrorCode::InvalidParameter);
	// A CPU destination, a frame with no memory, a frame of another API.
	KernelDispatch cpu = inspect_.dispatch(out);
	cpu.destination = KernelImage{};
	EXPECT_EQ(backend.dispatch(cpu).code(), ErrorCode::InvalidParameter);
	KernelDispatch noHandle = inspect_.dispatch(out);
	noHandle.destination.gpu.handle = nullptr;
	EXPECT_EQ(backend.dispatch(noHandle).code(), ErrorCode::InvalidParameter);
	// The emitted kernel writes raw float RGBA.
	for (BitDepth depth : {BitDepth::UInt8, BitDepth::UInt16, BitDepth::Half}) {
		DeviceFrame other({0, 0, 8, 8}, depth, Components::RGBA);
		EXPECT_EQ(backend.dispatch(inspect_.dispatch(other)).code(), ErrorCode::Format) << toString(depth);
	}
	DeviceFrame alpha({0, 0, 8, 8}, BitDepth::Float, Components::Alpha);
	EXPECT_EQ(backend.dispatch(inspect_.dispatch(alpha)).code(), ErrorCode::Format);
	// An empty window is a successful no-op; an abort cancels before anything is loaded.
	KernelDispatch empty = inspect_.dispatch(out);
	empty.window = {40, 40, 50, 50};
	EXPECT_TRUE(backend.dispatch(empty).ok());
	KernelDispatch aborted = inspect_.dispatch(out);
	aborted.abort = []() { return true; };
	EXPECT_TRUE(backend.dispatch(aborted).isCancellation());
	EXPECT_TRUE(fake_.images.empty());
	// No current context.
	void* context = fake_.current;
	fake_.current = nullptr;
	EXPECT_EQ(backend.dispatch(inspect_.dispatch(out)).code(), ErrorCode::MissingHostFeature);
	fake_.current = context;
	// Sources the backend cannot bind: an RGB frame, a CPU view, a frame of another API.
	DeviceFrame rgb({0, 0, 8, 8}, BitDepth::Float, Components::RGB);
	KernelDispatch rgbSource = inspect_.dispatch(out);
	rgbSource.sources = {rgb.image()};
	EXPECT_EQ(backend.dispatch(rgbSource).code(), ErrorCode::Format);
	ImageBuffer cpuBuffer(out.desc);
	KernelDispatch cpuSource = inspect_.dispatch(out);
	KernelImage cpuImage;
	cpuImage.connected = true;
	cpuImage.cpu = cpuBuffer.view();
	cpuSource.sources = {cpuImage};
	EXPECT_EQ(backend.dispatch(cpuSource).code(), ErrorCode::InvalidParameter);
	KernelDispatch metalSource = inspect_.dispatch(out);
	KernelImage metalImage = out.image();
	metalImage.gpu.api = GpuApi::Metal;
	metalSource.sources = {metalImage};
	EXPECT_EQ(backend.dispatch(metalSource).code(), ErrorCode::InvalidParameter);
	EXPECT_EQ(fake_.liveResources(), 0u);
	// A module whose function or globals do not match the layout.
	fake_.function = "anotherKernel";
	KernelDispatch wrongEntry = inspect_.dispatch(out);
	wrongEntry.cacheKey = "ofxgrip.tests.cuda.entry";
	EXPECT_EQ(backend.dispatch(wrongEntry).code(), ErrorCode::Render);
	fake_.function = "fxGripKernel";
	fake_.globals.resize(8);
	KernelDispatch small = inspect_.dispatch(out);
	small.cacheKey = "ofxgrip.tests.cuda.small";
	const Error tooSmall = backend.dispatch(small);
	EXPECT_EQ(tooSmall.code(), ErrorCode::InvalidParameter);
	EXPECT_NE(tooSmall.describe().find("rebuild the PTX"), std::string::npos) << tooSmall.describe();
	// Neither mismatched module is kept; the one the refused sources loaded is.
	EXPECT_EQ(backend.cachedModuleCount(), 1u);
}

TEST_F(CudaBackend, ReleasesEverythingWhenTheDriverFails)
{
	DeviceFrame out({0, 0, 8, 8}, BitDepth::Float, Components::RGBA);
	DeviceFrame aligned({0, 0, 8, 8}, BitDepth::Float, Components::RGBA);
	DeviceFrame copied({0, 0, 8, 8}, BitDepth::Float, Components::RGBA, 4);
	struct Case {
		const char* function;
		int call;
		ErrorCode code;
	};
	const Case cases[] = {
	    {"ctxGetCurrent", 0, ErrorCode::MissingHostFeature},
	    {"moduleLoadData", 0, ErrorCode::Render},
	    {"moduleGetFunction", 0, ErrorCode::Render},
	    {"moduleGetGlobal", 0, ErrorCode::Render},
	    {"memAlloc", 0, ErrorCode::Memory},
	    {"texObjectCreate", 0, ErrorCode::Render},   // the in-place source
	    {"texObjectCreate", 1, ErrorCode::Render},   // the copied source
	    {"texObjectCreate", 2, ErrorCode::Render},   // an unbound texture
	    {"arrayCreate", 0, ErrorCode::Render},       // the copied source's array
	    {"arrayCreate", 1, ErrorCode::Render},       // an unbound texture's texel
	    {"arrayCreate", 3, ErrorCode::Render},       // the destination's staging array
	    {"memcpy2DAsync", 0, ErrorCode::Render},
	    {"memcpy2D", 0, ErrorCode::Render},          // the clear of an unbound texture
	    {"memcpy2D", 2, ErrorCode::Render},          // the store into the host's frame
	    {"surfObjectCreate", 0, ErrorCode::Render},
	    {"memcpyHtoD", 0, ErrorCode::Render},        // the uniforms
	    {"memcpyHtoD", 1, ErrorCode::Render},        // the tile
	    {"memcpyHtoD", 2, ErrorCode::Render},        // the globals
	    {"launchKernel", 0, ErrorCode::Render},
	    {"streamSynchronize", 0, ErrorCode::Render},
	};
	int index = 0;
	for (const Case& c : cases) {
		Fake fresh;
		gFake = &fresh;
		fresh.failOn = c.function;
		fresh.failAfter = c.call;
		CudaKernelBackend backend(&driver_);
		KernelDispatch d = inspect_.dispatch(out);
		d.cacheKey = "ofxgrip.tests.cuda.fail" + std::to_string(index++);
		d.sources = {aligned.image(), copied.image()};
		const Error e = backend.dispatch(d);
		EXPECT_EQ(e.code(), c.code) << c.function << " call " << c.call << ": " << e.describe();
		EXPECT_EQ(fresh.liveResources(), 0u) << c.function << " call " << c.call;
		if (c.code == ErrorCode::Render) {
			EXPECT_NE(e.describe().find("CUDA_ERROR_OUT_OF_MEMORY"), std::string::npos) << e.describe();
		}
	}
	gFake = &fake_;
}

TEST_F(CudaBackend, WatermarkCompositesBetweenTwoCopies)
{
	WatermarkConfiguration configuration = WatermarkConfiguration::unlicensed("I");
	configuration.angleDegrees = 0;
	configuration.fontSize = 216;
	configuration.color = {1, 0, 0, 1};
	configuration.opacity = 1.0;
	const Watermark mark(configuration);
	const RectI bounds{0, 0, 70, 70};
	const RectI window{10, 20, 60, 50};
	for (BitDepth depth : {BitDepth::Float, BitDepth::Half, BitDepth::UInt8, BitDepth::UInt16}) {
		DeviceFrame device(bounds, depth, Components::RGBA, 12);
		device.fill();
		ImageBuffer expected(device.desc);
		ASSERT_TRUE(expected.view().copyFrom(device.view()).ok());
		mark.composite(expected.view(), window, {70, 70});
		GpuImage frame = device.image(reinterpret_cast<void*>(std::uintptr_t{0x7000})).gpu;
		const Error e = cudaWatermark(frame, window, {70, 70}, {}, mark, &driver_);
		ASSERT_TRUE(e.ok()) << e.describe();
		const ImageView a = expected.view();
		const ImageView b = device.view();
		for (int y = bounds.y1; y < bounds.y2; ++y) {
			ASSERT_EQ(std::memcmp(a.pixel(0, y), b.pixel(0, y), device.desc.minimumRowBytes()), 0) << toString(depth) << " row " << y;
		}
	}
	EXPECT_EQ(fake_.synchronizations, 4);
	EXPECT_EQ(fake_.liveResources(), 0u);

	DeviceFrame device(bounds, BitDepth::Float, Components::RGBA);
	GpuImage frame = device.image().gpu;
	// Nothing to do: an empty mark, a window off the frame.
	EXPECT_TRUE(cudaWatermark(frame, window, {70, 70}, {}, Watermark(WatermarkConfiguration{}), &driver_).ok());
	EXPECT_TRUE(cudaWatermark(frame, {100, 100, 120, 120}, {70, 70}, {}, mark, &driver_).ok());
	// Frames the path refuses.
	GpuImage metal = frame;
	metal.api = GpuApi::Metal;
	EXPECT_EQ(cudaWatermark(metal, window, {70, 70}, {}, mark, &driver_).code(), ErrorCode::Format);
	GpuImage empty = frame;
	empty.handle = nullptr;
	EXPECT_EQ(cudaWatermark(empty, window, {70, 70}, {}, mark, &driver_).code(), ErrorCode::Format);
	CudaDriver partial;
	EXPECT_EQ(cudaWatermark(frame, window, {70, 70}, {}, mark, &partial).code(), ErrorCode::MissingHostFeature);
	if (CudaDriver::system() == nullptr) {
		EXPECT_EQ(cudaWatermark(frame, window, {70, 70}, {}, mark).code(), ErrorCode::MissingHostFeature);
	}
	// Each driver failure is reported.
	fake_.failOn = "streamSynchronize";
	EXPECT_EQ(cudaWatermark(frame, window, {70, 70}, {}, mark, &driver_).code(), ErrorCode::Render);
	fake_.calls.clear();
	fake_.failOn = "memcpy2D";
	fake_.failAfter = 0;
	EXPECT_EQ(cudaWatermark(frame, window, {70, 70}, {}, mark, &driver_).code(), ErrorCode::Render);
	fake_.calls.clear();
	fake_.failAfter = 1;
	EXPECT_EQ(cudaWatermark(frame, window, {70, 70}, {}, mark, &driver_).code(), ErrorCode::Render);
}
