// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
#include <ofxgrip/minihost/MiniHost.hpp>

#if defined(OFXGRIP_HAS_METAL)
#include <ofxgrip/gpu/MetalBackend.hpp>
#endif

#include "ofxGPURender.h"
#include "ofxMemory.h"
#include "ofxMessage.h"
#include "ofxMultiThread.h"
#include "ofxProgress.h"
#include "ofxTimeLine.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <thread>

OFXGRIP_NAMESPACE_BEGIN
namespace minihost {

// ---- objects ------------------------------------------------------------------------------------

/*! An image handed to the plug-in. The property set references pixels someone else owns. */
struct Image {
	PropSet props;
	std::shared_ptr<const ImageBuffer> keepAlive;
};

/*! A clip, descriptor or instance. */
class Clip {
public:
	explicit Clip(std::string name) : name_(std::move(name))
	{
		props_.setString(kOfxPropType, kOfxTypeClip);
		props_.setString(kOfxPropName, name_);
	}
	const std::string& name() const noexcept { return name_; }
	PropSet& props() noexcept { return props_; }
	std::shared_ptr<const ImageBuffer> image() const
	{
		std::lock_guard<std::mutex> lock(mutex_);
		return image_;
	}
	void setImage(std::shared_ptr<const ImageBuffer> image)
	{
		std::lock_guard<std::mutex> lock(mutex_);
		image_ = std::move(image);
	}
	bool isOutput() const noexcept { return name_ == kOfxImageEffectOutputClipName; }

	OfxImageClipHandle handle() noexcept { return reinterpret_cast<OfxImageClipHandle>(this); }
	static Clip* from(OfxImageClipHandle h) noexcept { return reinterpret_cast<Clip*>(h); }

private:
	std::string name_;
	PropSet props_;
	mutable std::mutex mutex_;
	std::shared_ptr<const ImageBuffer> image_;
};

/*! An effect descriptor or instance. */
class EffectObject {
public:
	EffectObject(MiniHost& host, bool instance) : host_(host), instance_(instance), params_(std::make_unique<ParamSet>()) {}
	PropSet& props() noexcept { return props_; }
	ParamSet& params() noexcept { return *params_; }
	void setParams(std::unique_ptr<ParamSet> p) { params_ = std::move(p); }
	Clip* defineClip(const std::string& name)
	{
		if (Clip* existing = clip(name)) {
			return existing;
		}
		clips_.push_back(std::make_unique<Clip>(name));
		return clips_.back().get();
	}
	Clip* clip(const std::string& name) const noexcept
	{
		for (const auto& c : clips_) {
			if (c->name() == name) {
				return c.get();
			}
		}
		return nullptr;
	}
	const std::vector<std::unique_ptr<Clip>>& clips() const noexcept { return clips_; }
	bool isInstance() const noexcept { return instance_; }
	MiniHost& host() noexcept { return host_; }
	Instance* instance{nullptr};

	OfxImageEffectHandle handle() noexcept { return reinterpret_cast<OfxImageEffectHandle>(this); }
	static EffectObject* from(OfxImageEffectHandle h) noexcept { return reinterpret_cast<EffectObject*>(h); }
	static EffectObject* from(const void* h) noexcept { return reinterpret_cast<EffectObject*>(const_cast<void*>(h)); }

private:
	MiniHost& host_;
	bool instance_;
	PropSet props_;
	std::unique_ptr<ParamSet> params_;
	std::vector<std::unique_ptr<Clip>> clips_;
};

namespace {

/*! The render task the current thread is inside. The output clip's image comes from here. */
struct RenderTask {
	Instance* instance{nullptr};
	ImageBuffer* output{nullptr};
	RectI tile;
	Point2D renderScale{1, 1};
	Field field{Field::None};
	GpuApi gpu{GpuApi::None};
	void* gpuOutput{nullptr};                             /*!< The output frame's `MTLBuffer`. */
	const std::map<std::string, void*>* gpuInputs{nullptr}; /*!< Clip name → input `MTLBuffer`. */
};
thread_local RenderTask* currentTask = nullptr;
thread_local unsigned currentThreadIndex = 0;
thread_local bool spawnedThread = false;

std::mutex& hostTableMutex()
{
	static std::mutex m;
	return m;
}

std::map<const PropSet*, MiniHost*>& hostTable()
{
	static std::map<const PropSet*, MiniHost*> table;
	return table;
}

MiniHost* hostFor(OfxPropertySetHandle hostProps)
{
	std::lock_guard<std::mutex> lock(hostTableMutex());
	auto it = hostTable().find(PropSet::from(hostProps));
	return it == hostTable().end() ? nullptr : it->second;
}

const char* depthName(BitDepth depth)
{
	switch (depth) {
	case BitDepth::UInt8: return kOfxBitDepthByte;
	case BitDepth::UInt16: return kOfxBitDepthShort;
	case BitDepth::Half: return kOfxBitDepthHalf;
	case BitDepth::Float: return kOfxBitDepthFloat;
	case BitDepth::None: break;
	}
	return kOfxBitDepthNone;
}

const char* componentsName(Components c)
{
	switch (c) {
	case Components::RGBA: return kOfxImageComponentRGBA;
	case Components::RGB: return kOfxImageComponentRGB;
	case Components::Alpha: return kOfxImageComponentAlpha;
	case Components::None: break;
	}
	return kOfxImageComponentNone;
}

const char* premultName(Premultiplication p)
{
	switch (p) {
	case Premultiplication::Opaque: return kOfxImageOpaque;
	case Premultiplication::Premultiplied: return kOfxImagePreMultiplied;
	case Premultiplication::Unpremultiplied: return kOfxImageUnPreMultiplied;
	}
	return kOfxImageOpaque;
}

const char* fieldName(Field f)
{
	switch (f) {
	case Field::None: return kOfxImageFieldNone;
	case Field::Both: return kOfxImageFieldBoth;
	case Field::Lower: return kOfxImageFieldLower;
	case Field::Upper: return kOfxImageFieldUpper;
	case Field::Single: return kOfxImageFieldSingle;
	case Field::Doubled: return kOfxImageFieldDoubled;
	}
	return kOfxImageFieldNone;
}

Image* makeImage(const ImageView& view, std::shared_ptr<const ImageBuffer> keepAlive, Point2D renderScale, bool scaleAlwaysOne, Field field)
{
	auto* image = new Image();
	image->keepAlive = std::move(keepAlive);
	PropSet& p = image->props;
	p.setString(kOfxPropType, kOfxTypeImage);
	p.setString(kOfxImageEffectPropPixelDepth, depthName(view.desc.depth));
	p.setString(kOfxImageEffectPropComponents, componentsName(view.desc.components));
	p.setString(kOfxImageEffectPropPreMultiplication, premultName(view.desc.premultiplication));
	p.setDoubles(kOfxImageEffectPropRenderScale, scaleAlwaysOne ? std::vector<double>{1.0, 1.0} : std::vector<double>{renderScale.x, renderScale.y});
	p.setDouble(kOfxImagePropPixelAspectRatio, view.desc.pixelAspect);
	p.setPointer(kOfxImagePropData, view.data);
	p.setInts(kOfxImagePropBounds, {view.desc.bounds.x1, view.desc.bounds.y1, view.desc.bounds.x2, view.desc.bounds.y2});
	p.setInts(kOfxImagePropRegionOfDefinition, {view.desc.regionOfDefinition.x1, view.desc.regionOfDefinition.y1, view.desc.regionOfDefinition.x2, view.desc.regionOfDefinition.y2});
	p.setInt(kOfxImagePropRowBytes, static_cast<int>(view.rowBytes));
	p.setString(kOfxImagePropField, fieldName(field));
	p.setString(kOfxImagePropUniqueIdentifier, "minihost-image");
	return image;
}

struct MemoryBlock {
	void* data{nullptr};
	int locks{0};
};

struct Mutex {
	std::recursive_mutex mutex;
};

}  // namespace

// ---- suites -------------------------------------------------------------------------------------

struct HostSuites {
	// image effect suite
	static OfxStatus getPropertySet(OfxImageEffectHandle effect, OfxPropertySetHandle* props)
	{
		if (effect == nullptr) {
			return kOfxStatErrBadHandle;
		}
		*props = EffectObject::from(effect)->props().handle();
		return kOfxStatOK;
	}
	static OfxStatus getParamSet(OfxImageEffectHandle effect, OfxParamSetHandle* set)
	{
		if (effect == nullptr) {
			return kOfxStatErrBadHandle;
		}
		*set = EffectObject::from(effect)->params().handle();
		return kOfxStatOK;
	}
	static OfxStatus clipDefine(OfxImageEffectHandle effect, const char* name, OfxPropertySetHandle* props)
	{
		if (effect == nullptr || name == nullptr) {
			return kOfxStatErrBadHandle;
		}
		Clip* clip = EffectObject::from(effect)->defineClip(name);
		if (props != nullptr) {
			*props = clip->props().handle();
		}
		return kOfxStatOK;
	}
	static OfxStatus clipGetHandle(OfxImageEffectHandle effect, const char* name, OfxImageClipHandle* clip, OfxPropertySetHandle* props)
	{
		if (effect == nullptr || name == nullptr) {
			return kOfxStatErrBadHandle;
		}
		Clip* c = EffectObject::from(effect)->clip(name);
		if (c == nullptr) {
			return kOfxStatErrUnknown;
		}
		*clip = c->handle();
		if (props != nullptr) {
			*props = c->props().handle();
		}
		return kOfxStatOK;
	}
	static OfxStatus clipGetPropertySet(OfxImageClipHandle clip, OfxPropertySetHandle* props)
	{
		if (clip == nullptr) {
			return kOfxStatErrBadHandle;
		}
		*props = Clip::from(clip)->props().handle();
		return kOfxStatOK;
	}
	static OfxStatus clipGetImage(OfxImageClipHandle clipHandle, OfxTime, const OfxRectD*, OfxPropertySetHandle* imageHandle)
	{
		if (clipHandle == nullptr || imageHandle == nullptr) {
			return kOfxStatErrBadHandle;
		}
		Clip* clip = Clip::from(clipHandle);
		RenderTask* task = currentTask;
		const Point2D scale = task != nullptr ? task->renderScale : Point2D{1, 1};
		const Field field = task != nullptr ? task->field : Field::None;
		if (clip->isOutput()) {
			if (task == nullptr || task->output == nullptr) {
				*imageHandle = nullptr;
				return kOfxStatFailed;
			}
			ImageView full = task->output->view();
			const bool scaleOne = task->instance->host().options().imageScaleAlwaysOne;
			if (task->gpu != GpuApi::None) {
				// A GPU frame covers the whole window; the render window selects the tile.
				full.data = static_cast<std::uint8_t*>(task->gpuOutput);
				*imageHandle = makeImage(full, nullptr, scale, scaleOne, field)->props.handle();
				return kOfxStatOK;
			}
			ImageView tile = full.subView(task->tile);
			tile.desc.regionOfDefinition = full.desc.bounds;
			*imageHandle = makeImage(tile, nullptr, scale, scaleOne, field)->props.handle();
			return kOfxStatOK;
		}
		std::shared_ptr<const ImageBuffer> image = clip->image();
		if (!image) {
			*imageHandle = nullptr;
			return kOfxStatFailed;
		}
		bool scaleOne = false;
		if (task != nullptr) {
			scaleOne = task->instance->host().options().imageScaleAlwaysOne;
		}
		ImageView view = image->view();
		if (task != nullptr && task->gpu != GpuApi::None && task->gpuInputs != nullptr) {
			auto gpu = task->gpuInputs->find(clip->name());
			if (gpu != task->gpuInputs->end()) {
				view.data = static_cast<std::uint8_t*>(gpu->second);
			}
		}
		*imageHandle = makeImage(view, image, scale, scaleOne, field)->props.handle();
		return kOfxStatOK;
	}
	static OfxStatus clipReleaseImage(OfxPropertySetHandle imageHandle)
	{
		if (imageHandle == nullptr) {
			return kOfxStatErrBadHandle;
		}
		// The property set is the first member of Image, so the handle is the Image.
		delete reinterpret_cast<Image*>(PropSet::from(imageHandle));
		return kOfxStatOK;
	}
	static OfxStatus clipGetRegionOfDefinition(OfxImageClipHandle clipHandle, OfxTime, OfxRectD* bounds)
	{
		if (clipHandle == nullptr || bounds == nullptr) {
			return kOfxStatErrBadHandle;
		}
		Clip* clip = Clip::from(clipHandle);
		std::shared_ptr<const ImageBuffer> image = clip->image();
		if (!image) {
			return kOfxStatFailed;
		}
		const RectI b = image->desc().bounds;
		bounds->x1 = b.x1;
		bounds->y1 = b.y1;
		bounds->x2 = b.x2;
		bounds->y2 = b.y2;
		return kOfxStatOK;
	}
	static int abort(OfxImageEffectHandle effect)
	{
		if (effect == nullptr) {
			return 0;
		}
		Instance* instance = EffectObject::from(effect)->instance;
		return instance != nullptr && instance->abortFlag.load() ? 1 : 0;
	}
	static OfxStatus imageMemoryAlloc(OfxImageEffectHandle, size_t nBytes, OfxImageMemoryHandle* memoryHandle)
	{
		auto* block = new MemoryBlock();
		block->data = std::malloc(nBytes == 0 ? 1 : nBytes);
		*memoryHandle = reinterpret_cast<OfxImageMemoryHandle>(block);
		return kOfxStatOK;
	}
	static OfxStatus imageMemoryFree(OfxImageMemoryHandle memoryHandle)
	{
		auto* block = reinterpret_cast<MemoryBlock*>(memoryHandle);
		if (block == nullptr) {
			return kOfxStatErrBadHandle;
		}
		std::free(block->data);
		delete block;
		return kOfxStatOK;
	}
	static OfxStatus imageMemoryLock(OfxImageMemoryHandle memoryHandle, void** returnedPtr)
	{
		auto* block = reinterpret_cast<MemoryBlock*>(memoryHandle);
		if (block == nullptr) {
			return kOfxStatErrBadHandle;
		}
		block->locks++;
		*returnedPtr = block->data;
		return kOfxStatOK;
	}
	static OfxStatus imageMemoryUnlock(OfxImageMemoryHandle memoryHandle)
	{
		auto* block = reinterpret_cast<MemoryBlock*>(memoryHandle);
		if (block == nullptr) {
			return kOfxStatErrBadHandle;
		}
		if (block->locks > 0) {
			block->locks--;
		}
		return kOfxStatOK;
	}

	// memory suite
	static OfxStatus memoryAlloc(void*, size_t nBytes, void** allocatedData)
	{
		*allocatedData = std::malloc(nBytes == 0 ? 1 : nBytes);
		return *allocatedData != nullptr ? kOfxStatOK : kOfxStatErrMemory;
	}
	static OfxStatus memoryFree(void* allocatedData)
	{
		std::free(allocatedData);
		return kOfxStatOK;
	}

	// multithread suite
	static OfxStatus multiThread(OfxThreadFunctionV1 func, unsigned int nThreads, void* customArg)
	{
		if (func == nullptr) {
			return kOfxStatErrValue;
		}
		if (nThreads <= 1) {
			func(0, 1, customArg);
			return kOfxStatOK;
		}
		std::vector<std::thread> threads;
		threads.reserve(nThreads);
		for (unsigned i = 0; i < nThreads; ++i) {
			threads.emplace_back([=]() {
				currentThreadIndex = i;
				spawnedThread = true;
				func(i, nThreads, customArg);
				spawnedThread = false;
			});
		}
		for (std::thread& t : threads) {
			t.join();
		}
		return kOfxStatOK;
	}
	static OfxStatus multiThreadDummy(OfxThreadFunctionV1 func, unsigned int, void* customArg)
	{
		// Q-RESOLVE-003: the stub runs the function once, as if there were one thread.
		if (func == nullptr) {
			return kOfxStatErrValue;
		}
		func(0, 1, customArg);
		return kOfxStatOK;
	}
	static OfxStatus multiThreadNumCPUs(unsigned int* nCPUs)
	{
		const unsigned hw = std::thread::hardware_concurrency();
		*nCPUs = hw == 0 ? 1 : hw;
		return kOfxStatOK;
	}
	static OfxStatus multiThreadIndex(unsigned int* threadIndex)
	{
		*threadIndex = currentThreadIndex;
		return kOfxStatOK;
	}
	static int multiThreadIsSpawnedThread() { return spawnedThread ? 1 : 0; }
	static OfxStatus mutexCreate(OfxMutexHandle* mutex, int lockCount)
	{
		auto* m = new Mutex();
		for (int i = 0; i < lockCount; ++i) {
			m->mutex.lock();
		}
		*mutex = reinterpret_cast<OfxMutexHandle>(m);
		return kOfxStatOK;
	}
	static OfxStatus mutexDestroy(const OfxMutexHandle mutex)
	{
		delete reinterpret_cast<Mutex*>(mutex);
		return kOfxStatOK;
	}
	static OfxStatus mutexLock(const OfxMutexHandle mutex)
	{
		reinterpret_cast<Mutex*>(mutex)->mutex.lock();
		return kOfxStatOK;
	}
	static OfxStatus mutexUnLock(const OfxMutexHandle mutex)
	{
		reinterpret_cast<Mutex*>(mutex)->mutex.unlock();
		return kOfxStatOK;
	}
	static OfxStatus mutexTryLock(const OfxMutexHandle mutex)
	{
		return reinterpret_cast<Mutex*>(mutex)->mutex.try_lock() ? kOfxStatOK : kOfxStatFailed;
	}

	// message suites
	static Instance* instanceOf(void* handle)
	{
		if (handle == nullptr) {
			return nullptr;
		}
		EffectObject* object = EffectObject::from(handle);
		return object->isInstance() ? object->instance : nullptr;
	}
	static OfxStatus message(void* handle, const char* messageType, const char* messageId, const char* format, ...)
	{
		char buffer[4096];
		va_list args;
		va_start(args, format);
		std::vsnprintf(buffer, sizeof buffer, format ? format : "", args);
		va_end(args);
		std::string line = std::string(messageType ? messageType : "?") + "|" + (messageId ? messageId : "") + "|" + buffer;
		if (Instance* instance = instanceOf(handle)) {
			std::lock_guard<std::mutex> lock(instance->mutex_);
			instance->messages_.push_back(std::move(line));
		}
		if (messageType != nullptr && std::strcmp(messageType, kOfxMessageQuestion) == 0) {
			return kOfxStatReplyYes;
		}
		return kOfxStatOK;
	}
	static OfxStatus setPersistentMessage(void* handle, const char* messageType, const char* messageId, const char* format, ...)
	{
		char buffer[4096];
		va_list args;
		va_start(args, format);
		std::vsnprintf(buffer, sizeof buffer, format ? format : "", args);
		va_end(args);
		if (Instance* instance = instanceOf(handle)) {
			std::lock_guard<std::mutex> lock(instance->mutex_);
			instance->persistentMessage_ = std::string(messageType ? messageType : "?") + "|" + (messageId ? messageId : "") + "|" + buffer;
			return kOfxStatOK;
		}
		return kOfxStatErrBadHandle;
	}
	static OfxStatus clearPersistentMessage(void* handle)
	{
		if (Instance* instance = instanceOf(handle)) {
			std::lock_guard<std::mutex> lock(instance->mutex_);
			instance->persistentMessage_.clear();
			return kOfxStatOK;
		}
		return kOfxStatErrBadHandle;
	}

	// progress suites
	static OfxStatus progressStartV1(void* handle, const char*)
	{
		if (Instance* instance = instanceOf(handle)) {
			std::lock_guard<std::mutex> lock(instance->mutex_);
			instance->progress_.push_back(0.0);
		}
		return kOfxStatOK;
	}
	static OfxStatus progressStartV2(void* handle, const char*, const char*) { return progressStartV1(handle, nullptr); }
	static OfxStatus progressUpdate(void* handle, double progress)
	{
		if (Instance* instance = instanceOf(handle)) {
			std::lock_guard<std::mutex> lock(instance->mutex_);
			instance->progress_.push_back(progress);
		}
		return kOfxStatOK;
	}
	static OfxStatus progressEnd(void* handle)
	{
		if (Instance* instance = instanceOf(handle)) {
			std::lock_guard<std::mutex> lock(instance->mutex_);
			instance->progress_.push_back(1.0);
		}
		return kOfxStatOK;
	}

	// timeline suite
	static OfxStatus getTime(void* handle, double* time)
	{
		if (Instance* instance = instanceOf(handle)) {
			*time = instance->currentTime();
			return kOfxStatOK;
		}
		return kOfxStatErrBadHandle;
	}
	static OfxStatus gotoTime(void* handle, double time)
	{
		if (Instance* instance = instanceOf(handle)) {
			instance->setCurrentTime(time);
			return kOfxStatOK;
		}
		return kOfxStatErrBadHandle;
	}
	static OfxStatus getTimeBounds(void* handle, double* first, double* last)
	{
		if (Instance* instance = instanceOf(handle)) {
			*first = instance->project().firstFrame;
			*last = instance->project().lastFrame;
			return kOfxStatOK;
		}
		return kOfxStatErrBadHandle;
	}

	static const void* fetchSuite(OfxPropertySetHandle hostProps, const char* suiteName, int suiteVersion)
	{
		MiniHost* host = hostFor(hostProps);
		if (host == nullptr || suiteName == nullptr) {
			return nullptr;
		}
		const std::string name(suiteName);
		const std::string versioned = name + ":" + std::to_string(suiteVersion);
		const std::set<std::string>& withheld = host->options().withheldSuites;
		if (withheld.count(name) != 0 || withheld.count(versioned) != 0) {
			return nullptr;
		}
		host->logAction("fetchSuite " + versioned);
		if (name == kOfxPropertySuite && suiteVersion == 1) {
			return PropSet::suite();
		}
		if (name == kOfxParameterSuite && suiteVersion == 1) {
			return ParamSet::suite();
		}
		if (name == kOfxImageEffectSuite && suiteVersion == 1) {
			static const OfxImageEffectSuiteV1 s = {&getPropertySet, &getParamSet, &clipDefine, &clipGetHandle, &clipGetPropertySet, &clipGetImage, &clipReleaseImage, &clipGetRegionOfDefinition, &abort, &imageMemoryAlloc, &imageMemoryFree, &imageMemoryLock, &imageMemoryUnlock};
			return &s;
		}
		if (name == kOfxMemorySuite && suiteVersion == 1) {
			static const OfxMemorySuiteV1 s = {&memoryAlloc, &memoryFree};
			return &s;
		}
		if (name == kOfxMultiThreadSuite && suiteVersion == 1) {
			static const OfxMultiThreadSuiteV1 real = {&multiThread, &multiThreadNumCPUs, &multiThreadIndex, &multiThreadIsSpawnedThread, &mutexCreate, &mutexDestroy, &mutexLock, &mutexUnLock, &mutexTryLock};
			static const OfxMultiThreadSuiteV1 dummy = {&multiThreadDummy, &multiThreadNumCPUs, &multiThreadIndex, &multiThreadIsSpawnedThread, &mutexCreate, &mutexDestroy, &mutexLock, &mutexUnLock, &mutexTryLock};
			return host->options().threadSuiteDummy ? &dummy : &real;
		}
		if (name == kOfxMessageSuite && suiteVersion == 1) {
			static const OfxMessageSuiteV1 s = {&message};
			return &s;
		}
		if (name == kOfxMessageSuite && suiteVersion == 2) {
			static const OfxMessageSuiteV2 good = {&message, &setPersistentMessage, &clearPersistentMessage};
			static const OfxMessageSuiteV2 broken = {&message, nullptr, nullptr};
			return host->options().messageV2Broken ? &broken : &good;
		}
		if (name == kOfxProgressSuite && suiteVersion == 1) {
			static const OfxProgressSuiteV1 s = {&progressStartV1, &progressUpdate, &progressEnd};
			return &s;
		}
		if (name == kOfxProgressSuite && suiteVersion == 2) {
			static const OfxProgressSuiteV2 s = {&progressStartV2, &progressUpdate, &progressEnd};
			return &s;
		}
		if (name == kOfxTimeLineSuite && suiteVersion == 1) {
			static const OfxTimeLineSuiteV1 s = {&getTime, &gotoTime, &getTimeBounds};
			return &s;
		}
		return nullptr;
	}
};

// ---- HostOptions presets ------------------------------------------------------------------------

HostOptions HostOptions::spec()
{
	return HostOptions{};
}

HostOptions HostOptions::resolve21()
{
	HostOptions o;
	o.name = "DaVinciResolveLite";
	o.label = "DaVinci Resolve Lite";
	o.version = {21, 1, 1};
	o.versionLabel = "21.1.1";
	o.apiMajor = 1;
	o.apiMinor = 4;
	o.supportsTiles = false;
	o.supportsMultiResolution = false;
	o.supportsCustomInteract = false;
	o.supportsChoiceAnimation = true;
	o.supportsBooleanAnimation = true;
	o.supportsStringAnimation = false;
	o.supportsCustomAnimation = false;
	o.maxPages = 0;
	o.contexts = {"OfxImageEffectContextFilter", "OfxImageEffectContextGeneral", "OfxImageEffectContextGenerator", "OfxImageEffectContextTransition"};
	o.components = {"OfxImageComponentRGBA", "OfxImageComponentAlpha"};
	o.depths = {"OfxBitDepthByte", "OfxBitDepthShort", "OfxBitDepthFloat"};
	// Live scan of 21.1.1 free on 2026-10-14: parametric, OpenGL render, progress V2, and dialog
	// are absent; interact and the OFX 1.5 draw suite are present; Metal render is "true" on
	// macOS; the native-origin property is missing.
	o.withheldSuites = {"OfxParametricParameterSuite", "OfxImageEffectOpenGLRenderSuite", "OfxProgressSuite:2", "OfxDialogSuite"};
	o.withheldHostProps = {kOfxPropHostOSHandle};
	o.nativeOrigin.clear();
	o.metalRenderSupported = true;
	o.clipsAlwaysConnected = true;
	o.threadSuiteDummy = true;
	o.messageV2Broken = true;
	o.imageScaleAlwaysOne = true;
	o.nonConformantInstanceType = true;
	o.paramCopyNoOp = true;
	o.renderDespiteIdentity = true;
	o.noPluginHandleOnInstance = true;
	return o;
}

HostOptions HostOptions::nuke()
{
	HostOptions o;
	o.name = "uk.co.thefoundry.nuke";
	o.label = "Nuke";
	o.version = {15, 0, 0};
	o.versionLabel = "15.0";
	o.supportsTiles = true;
	o.supportsMultiResolution = true;
	o.supportsMultipleClipDepths = false;
	o.depths = {"OfxBitDepthFloat"};
	o.components = {"OfxImageComponentRGBA", "OfxImageComponentRGB", "OfxImageComponentAlpha"};
	o.supportsChoiceAnimation = true;
	o.supportsBooleanAnimation = true;
	o.supportsStringAnimation = true;
	o.supportsCustomAnimation = true;
	o.maxPages = 8;
	o.withheldSuites = {"OfxDialogSuite"};
	return o;
}

HostOptions HostOptions::natron()
{
	HostOptions o;
	o.name = "fr.inria.Natron";
	o.label = "Natron";
	o.version = {2, 5, 0};
	o.versionLabel = "2.5.0";
	o.supportsTiles = true;
	o.supportsMultiResolution = true;
	o.supportsMultipleClipDepths = true;
	o.components = {"OfxImageComponentRGBA", "OfxImageComponentRGB", "OfxImageComponentAlpha"};
	o.supportsChoiceAnimation = true;
	o.supportsBooleanAnimation = true;
	o.supportsStringAnimation = true;
	o.supportsCustomAnimation = true;
	o.supportsCustomInteract = true;
	o.supportsParametric = true;
	o.maxPages = 16;
	return o;
}

// ---- MiniHost -----------------------------------------------------------------------------------

MiniHost::MiniHost(HostOptions options) : options_(std::move(options))
{
	fillHostProps();
	ofxHost_.host = hostProps_.handle();
	ofxHost_.fetchSuite = &HostSuites::fetchSuite;
	std::lock_guard<std::mutex> lock(hostTableMutex());
	hostTable()[&hostProps_] = this;
}

MiniHost::~MiniHost()
{
	if (loaded_) {
		unload();
	}
	std::lock_guard<std::mutex> lock(hostTableMutex());
	hostTable().erase(&hostProps_);
}

void MiniHost::fillHostProps()
{
	PropSet& p = hostProps_;
	p.setString(kOfxPropType, kOfxTypeImageEffectHost);
	p.setString(kOfxPropName, options_.name);
	p.setString(kOfxPropLabel, options_.label);
	p.setInts(kOfxPropVersion, options_.version);
	p.setString(kOfxPropVersionLabel, options_.versionLabel);
	p.setInts(kOfxPropAPIVersion, {options_.apiMajor, options_.apiMinor});
	p.setInt(kOfxImageEffectHostPropIsBackground, 0);
	p.setInt(kOfxImageEffectPropSupportsOverlays, options_.supportsOverlays ? 1 : 0);
	p.setInt(kOfxImageEffectPropSupportsMultiResolution, options_.supportsMultiResolution ? 1 : 0);
	p.setInt(kOfxImageEffectPropSupportsTiles, options_.supportsTiles ? 1 : 0);
	p.setInt(kOfxImageEffectPropTemporalClipAccess, 1);
	p.setStrings(kOfxImageEffectPropSupportedComponents, options_.components);
	p.setStrings(kOfxImageEffectPropSupportedContexts, options_.contexts);
	p.setStrings(kOfxImageEffectPropSupportedPixelDepths, options_.depths);
	p.setInt(kOfxImageEffectPropSupportsMultipleClipDepths, options_.supportsMultipleClipDepths ? 1 : 0);
	p.setInt(kOfxImageEffectPropSupportsMultipleClipPARs, options_.supportsMultipleClipPARs ? 1 : 0);
	p.setInt(kOfxImageEffectPropSetableFrameRate, 0);
	p.setInt(kOfxImageEffectPropSetableFielding, 0);
	p.setInt(kOfxParamHostPropSupportsCustomInteract, options_.supportsCustomInteract ? 1 : 0);
	p.setInt(kOfxParamHostPropSupportsStringAnimation, options_.supportsStringAnimation ? 1 : 0);
	p.setInt(kOfxParamHostPropSupportsChoiceAnimation, options_.supportsChoiceAnimation ? 1 : 0);
	p.setInt(kOfxParamHostPropSupportsBooleanAnimation, options_.supportsBooleanAnimation ? 1 : 0);
	p.setInt(kOfxParamHostPropSupportsCustomAnimation, options_.supportsCustomAnimation ? 1 : 0);
	p.setInt(kOfxParamHostPropSupportsStrChoice, options_.supportsStrChoice ? 1 : 0);
	p.setInt(kOfxParamHostPropMaxParameters, options_.maxParameters);
	p.setInt(kOfxParamHostPropMaxPages, options_.maxPages);
	p.setInts(kOfxParamHostPropPageRowColumnCount, {0, 0});
	if (!options_.nativeOrigin.empty()) {
		p.setString(kOfxImageEffectHostPropNativeOrigin, options_.nativeOrigin);
	}
	p.setPointer(kOfxPropHostOSHandle, nullptr);
	p.setInt(kOfxImageEffectPropRenderQualityDraft, 1);
	p.setString(kOfxImageEffectPropMetalRenderSupported, options_.metalRenderSupported ? "true" : "false");
	p.setString(kOfxImageEffectPropCudaRenderSupported, "false");
	p.setString(kOfxImageEffectPropOpenCLRenderSupported, "false");
	p.setString(kOfxImageEffectPropOpenGLRenderSupported, "false");
	for (const std::string& name : options_.withheldHostProps) {
		p.withhold(name);
	}
}

void MiniHost::logAction(const std::string& line)
{
	std::lock_guard<std::mutex> lock(logMutex_);
	actionLog_.push_back(line);
}

std::vector<std::string> MiniHost::actionLog() const
{
	std::lock_guard<std::mutex> lock(logMutex_);
	return actionLog_;
}

void MiniHost::clearActionLog()
{
	std::lock_guard<std::mutex> lock(logMutex_);
	actionLog_.clear();
}

OfxStatus MiniHost::dispatch(const char* action, const void* handle, PropSet* inArgs, PropSet* outArgs)
{
	if (plugin_ == nullptr || plugin_->mainEntry == nullptr) {
		return kOfxStatErrBadHandle;
	}
	logAction(action);
	return plugin_->mainEntry(action, handle, inArgs ? inArgs->handle() : nullptr, outArgs ? outArgs->handle() : nullptr);
}

OfxStatus MiniHost::load(OfxPlugin* plugin, const std::string& bundlePath)
{
	if (plugin == nullptr || plugin->setHost == nullptr || plugin->mainEntry == nullptr) {
		return kOfxStatErrBadHandle;
	}
	plugin_ = plugin;
	plugin_->setHost(&ofxHost_);
	logAction("setHost");
	descriptor_ = std::make_unique<EffectObject>(*this, false);
	descriptor_->props().setString(kOfxPropType, kOfxTypeImageEffect);
	descriptor_->props().setString(kOfxPluginPropFilePath, bundlePath);
	const OfxStatus status = dispatch(kOfxActionLoad, nullptr, nullptr, nullptr);
	loaded_ = status == kOfxStatOK || status == kOfxStatReplyDefault;
	return status;
}

OfxStatus MiniHost::describe()
{
	if (!loaded_) {
		return kOfxStatErrBadHandle;
	}
	OfxStatus status = dispatch(kOfxActionDescribe, descriptor_->handle(), nullptr, nullptr);
	if (status != kOfxStatOK && status != kOfxStatReplyDefault) {
		return status;
	}
	const std::vector<std::string> wanted = descriptor_->props().getStrings(kOfxImageEffectPropSupportedContexts);
	describedContexts_.clear();
	contextDescriptors_.clear();
	for (const std::string& context : wanted) {
		if (std::find(options_.contexts.begin(), options_.contexts.end(), context) == options_.contexts.end()) {
			continue;
		}
		auto contextDescriptor = std::make_unique<EffectObject>(*this, false);
		contextDescriptor->props().setString(kOfxPropType, kOfxTypeImageEffect);
		contextDescriptor->props().setString(kOfxImageEffectPropContext, context);
		contextDescriptor->props().setString(kOfxPluginPropFilePath, descriptor_->props().getString(kOfxPluginPropFilePath));
		PropSet in;
		in.setString(kOfxImageEffectPropContext, context);
		status = dispatch(kOfxImageEffectActionDescribeInContext, contextDescriptor->handle(), &in, nullptr);
		if (status != kOfxStatOK && status != kOfxStatReplyDefault) {
			return status;
		}
		describedContexts_.push_back(context);
		contextDescriptors_[context] = std::move(contextDescriptor);
	}
	return kOfxStatOK;
}

OfxStatus MiniHost::unload()
{
	if (!loaded_) {
		return kOfxStatOK;
	}
	const OfxStatus status = dispatch(kOfxActionUnload, nullptr, nullptr, nullptr);
	loaded_ = false;
	return status;
}

PropSet* MiniHost::descriptorProps() const
{
	return descriptor_ ? &descriptor_->props() : nullptr;
}

std::vector<std::string> MiniHost::describedContexts() const
{
	return describedContexts_;
}

const ParamSet* MiniHost::contextParams(const std::string& context) const
{
	auto it = contextDescriptors_.find(context);
	return it == contextDescriptors_.end() ? nullptr : &it->second->params();
}

std::vector<std::string> MiniHost::contextClips(const std::string& context) const
{
	std::vector<std::string> out;
	auto it = contextDescriptors_.find(context);
	if (it == contextDescriptors_.end()) {
		return out;
	}
	for (const auto& clip : it->second->clips()) {
		out.push_back(clip->name());
	}
	return out;
}

std::unique_ptr<Instance> MiniHost::createInstance(const std::string& context, const Project& project, const std::string& resolvePage, const std::function<void(ParamSet&)>& restore)
{
	auto it = contextDescriptors_.find(context);
	if (it == contextDescriptors_.end()) {
		return nullptr;
	}
	EffectObject& contextDescriptor = *it->second;
	auto object = std::make_unique<EffectObject>(*this, true);
	PropSet& p = object->props();
	p.setString(kOfxPropType, options_.nonConformantInstanceType ? kOfxTypeImageEffect : kOfxTypeImageEffectInstance);
	p.setString(kOfxImageEffectPropContext, context);
	if (!options_.noPluginHandleOnInstance) {
		p.setPointer(kOfxImageEffectPropPluginHandle, plugin_);
	}
	const double width = project.width;
	const double height = project.height;
	p.setDoubles(kOfxImageEffectPropProjectSize, {width, height});
	p.setDoubles(kOfxImageEffectPropProjectOffset, {0.0, 0.0});
	p.setDoubles(kOfxImageEffectPropProjectExtent, {width, height});
	p.setDouble(kOfxImageEffectPropProjectPixelAspectRatio, project.pixelAspect);
	p.setDouble(kOfxImageEffectInstancePropEffectDuration, project.lastFrame - project.firstFrame + 1);
	p.setDouble(kOfxImageEffectPropFrameRate, project.frameRate);
	p.setDoubles(kOfxImageEffectPropFrameRange, {project.firstFrame, project.lastFrame});
	p.setInt(kOfxPropIsInteractive, 0);
	p.setInt(kOfxImageEffectInstancePropSequentialRender, 0);
	p.setPointer(kOfxPropInstanceData, nullptr);
	object->setParams(contextDescriptor.params().instantiate());
	if (restore) {
		restore(object->params());
	}
	for (const auto& clip : contextDescriptor.clips()) {
		Clip* instanceClip = object->defineClip(clip->name());
		PropSet& cp = instanceClip->props();
		cp.setInt(kOfxImageClipPropOptional, clip->props().getInt(kOfxImageClipPropOptional, 0));
		cp.setInt(kOfxImageClipPropIsMask, clip->props().getInt(kOfxImageClipPropIsMask, 0));
		cp.setStrings(kOfxImageEffectPropSupportedComponents, clip->props().getStrings(kOfxImageEffectPropSupportedComponents));
		cp.setString(kOfxImageEffectPropPixelDepth, kOfxBitDepthFloat);
		cp.setString(kOfxImageEffectPropComponents, kOfxImageComponentRGBA);
		cp.setString(kOfxImageClipPropUnmappedPixelDepth, kOfxBitDepthFloat);
		cp.setString(kOfxImageClipPropUnmappedComponents, kOfxImageComponentRGBA);
		cp.setString(kOfxImageEffectPropPreMultiplication, kOfxImagePreMultiplied);
		cp.setDouble(kOfxImagePropPixelAspectRatio, project.pixelAspect);
		cp.setDouble(kOfxImageEffectPropFrameRate, project.frameRate);
		cp.setDoubles(kOfxImageEffectPropFrameRange, {project.firstFrame, project.lastFrame});
		cp.setString(kOfxImageClipPropFieldOrder, kOfxImageFieldNone);
		cp.setInt(kOfxImageClipPropConnected, instanceClip->isOutput() || options_.clipsAlwaysConnected ? 1 : 0);
		cp.setDoubles(kOfxImageEffectPropUnmappedFrameRange, {project.firstFrame, project.lastFrame});
		cp.setDouble(kOfxImageEffectPropUnmappedFrameRate, project.frameRate);
		cp.setInt(kOfxImageClipPropContinuousSamples, 0);
	}
	std::unique_ptr<Instance> instance(new Instance(*this, std::move(object), context, project));
	PropSet in;
	if (!resolvePage.empty()) {
		in.setString("OfxImageEffectPropResolvePage", resolvePage);
	}
	const OfxStatus status = instance->dispatch(kOfxActionCreateInstance, &in, nullptr);
	if (status != kOfxStatOK && status != kOfxStatReplyDefault) {
		instance->object_->instance = nullptr;
		return nullptr;
	}
	return instance;
}

// ---- Instance -----------------------------------------------------------------------------------

Instance::Instance(MiniHost& host, std::unique_ptr<EffectObject> object, std::string context, Project project)
: host_(host), object_(std::move(object)), context_(std::move(context)), project_(project)
{
	object_->instance = this;
}

Instance::~Instance()
{
	if (object_ && object_->instance != nullptr) {
		dispatch(kOfxActionDestroyInstance, nullptr, nullptr);
	}
}

OfxStatus Instance::dispatch(const char* action, PropSet* inArgs, PropSet* outArgs)
{
	return host_.dispatch(action, object_->handle(), inArgs, outArgs);
}

PropSet& Instance::props()
{
	return object_->props();
}

OfxImageEffectHandle Instance::effectHandle() const noexcept
{
	return object_->handle();
}

OfxStatus Instance::instanceChanged(const std::string& paramName, Time time, const char* reason)
{
	PropSet in;
	in.setString(kOfxPropType, kOfxTypeParameter);
	in.setString(kOfxPropName, paramName);
	in.setString(kOfxPropChangeReason, reason);
	in.setDouble(kOfxPropTime, time);
	in.setDoubles(kOfxImageEffectPropRenderScale, {1.0, 1.0});
	OfxStatus status = dispatch(kOfxActionBeginInstanceChanged, &in, nullptr);
	if (status != kOfxStatOK && status != kOfxStatReplyDefault) {
		return status;
	}
	status = dispatch(kOfxActionInstanceChanged, &in, nullptr);
	const OfxStatus end = dispatch(kOfxActionEndInstanceChanged, &in, nullptr);
	if (status == kOfxStatReplyDefault) {
		status = kOfxStatOK;
	}
	return status != kOfxStatOK ? status : (end == kOfxStatReplyDefault ? kOfxStatOK : end);
}

OfxStatus Instance::clipChanged(const std::string& clipName, Time time)
{
	PropSet in;
	in.setString(kOfxPropType, kOfxTypeClip);
	in.setString(kOfxPropName, clipName);
	in.setString(kOfxPropChangeReason, kOfxChangeUserEdited);
	in.setDouble(kOfxPropTime, time);
	in.setDoubles(kOfxImageEffectPropRenderScale, {1.0, 1.0});
	dispatch(kOfxActionBeginInstanceChanged, &in, nullptr);
	const OfxStatus status = dispatch(kOfxActionInstanceChanged, &in, nullptr);
	dispatch(kOfxActionEndInstanceChanged, &in, nullptr);
	return status == kOfxStatReplyDefault ? kOfxStatOK : status;
}

OfxStatus Instance::syncPrivateData()
{
	const OfxStatus s = dispatch(kOfxActionSyncPrivateData, nullptr, nullptr);
	return s == kOfxStatReplyDefault ? kOfxStatOK : s;
}

OfxStatus Instance::purgeCaches()
{
	const OfxStatus s = dispatch(kOfxActionPurgeCaches, nullptr, nullptr);
	return s == kOfxStatReplyDefault ? kOfxStatOK : s;
}

OfxStatus Instance::beginInstanceEdit()
{
	const OfxStatus s = dispatch(kOfxActionBeginInstanceEdit, nullptr, nullptr);
	return s == kOfxStatReplyDefault ? kOfxStatOK : s;
}

OfxStatus Instance::endInstanceEdit()
{
	const OfxStatus s = dispatch(kOfxActionEndInstanceEdit, nullptr, nullptr);
	return s == kOfxStatReplyDefault ? kOfxStatOK : s;
}

Param* Instance::param(const std::string& name) const
{
	return object_->params().find(name);
}

std::vector<std::string> Instance::paramNames() const
{
	std::vector<std::string> out;
	for (const auto& p : object_->params().params()) {
		out.push_back(p->name());
	}
	return out;
}

void Instance::setParam(const std::string& name, const std::vector<double>& value, Time time, bool keyframe)
{
	if (Param* p = param(name)) {
		if (keyframe) {
			p->setNumericValueAt(time, value);
		} else {
			p->setNumericValue(value);
		}
	}
}

void Instance::setStringParam(const std::string& name, const std::string& value)
{
	if (Param* p = param(name)) {
		p->setStringValue(value);
	}
}

void Instance::setInput(const std::string& clipName, std::shared_ptr<const ImageBuffer> image)
{
	Clip* clip = object_->clip(clipName);
	if (clip == nullptr) {
		return;
	}
	clip->setImage(std::move(image));
	clip->props().setInt(kOfxImageClipPropConnected, 1);
	if (std::shared_ptr<const ImageBuffer> img = clip->image()) {
		clip->props().setString(kOfxImageEffectPropPixelDepth, depthName(img->desc().depth));
		clip->props().setString(kOfxImageEffectPropComponents, componentsName(img->desc().components));
		clip->props().setString(kOfxImageClipPropUnmappedPixelDepth, depthName(img->desc().depth));
		clip->props().setString(kOfxImageClipPropUnmappedComponents, componentsName(img->desc().components));
		clip->props().setString(kOfxImageEffectPropPreMultiplication, premultName(img->desc().premultiplication));
	}
}

void Instance::disconnect(const std::string& clipName)
{
	Clip* clip = object_->clip(clipName);
	if (clip == nullptr) {
		return;
	}
	clip->setImage(nullptr);
	clip->props().setInt(kOfxImageClipPropConnected, host_.options().clipsAlwaysConnected ? 1 : 0);
}

bool Instance::isConnected(const std::string& clipName) const
{
	Clip* clip = object_->clip(clipName);
	return clip != nullptr && clip->image() != nullptr;
}

OfxStatus Instance::clipPreferences()
{
	PropSet out;
	const OfxStatus status = dispatch(kOfxImageEffectActionGetClipPreferences, nullptr, &out);
	if (status == kOfxStatOK) {
		for (const auto& clip : object_->clips()) {
			const std::string components = out.getString("OfxImageClipPropComponents_" + clip->name(), "");
			if (!components.empty()) {
				clip->props().setString(kOfxImageEffectPropComponents, components);
			}
			const std::string depth = out.getString("OfxImageClipPropDepth_" + clip->name(), "");
			if (!depth.empty()) {
				clip->props().setString(kOfxImageEffectPropPixelDepth, depth);
			}
		}
	}
	return status == kOfxStatReplyDefault ? kOfxStatOK : status;
}

OfxStatus Instance::regionOfDefinition(Time time, Point2D renderScale, RectD& out)
{
	PropSet in;
	in.setDouble(kOfxPropTime, time);
	in.setDoubles(kOfxImageEffectPropRenderScale, {renderScale.x, renderScale.y});
	PropSet outArgs;
	const OfxStatus status = dispatch(kOfxImageEffectActionGetRegionOfDefinition, &in, &outArgs);
	if (status == kOfxStatOK) {
		const std::vector<double> rod = outArgs.getDoubles(kOfxImageEffectPropRegionOfDefinition);
		if (rod.size() == 4) {
			out = RectD{rod[0], rod[1], rod[2], rod[3]};
			return kOfxStatOK;
		}
	}
	if (status != kOfxStatOK && status != kOfxStatReplyDefault) {
		return status;
	}
	RectD rod;
	bool any = false;
	for (const auto& clip : object_->clips()) {
		if (clip->isOutput()) {
			continue;
		}
		if (std::shared_ptr<const ImageBuffer> image = clip->image()) {
			const RectI b = image->desc().bounds;
			const RectD canonical = toCanonical(b, image->desc().renderScale, image->desc().pixelAspect);
			rod = any ? rod.unite(canonical) : canonical;
			any = true;
		}
	}
	if (!any) {
		rod = RectD{0, 0, static_cast<double>(project_.width), static_cast<double>(project_.height)};
	}
	out = rod;
	return kOfxStatOK;
}

bool Instance::isIdentity(Time time, Point2D renderScale, const RectI& window, std::string& clip, Time& identityTime)
{
	PropSet in;
	in.setDouble(kOfxPropTime, time);
	in.setDoubles(kOfxImageEffectPropRenderScale, {renderScale.x, renderScale.y});
	in.setInts(kOfxImageEffectPropRenderWindow, {window.x1, window.y1, window.x2, window.y2});
	in.setString(kOfxImageEffectPropFieldToRender, kOfxImageFieldNone);
	PropSet out;
	const OfxStatus status = dispatch(kOfxImageEffectActionIsIdentity, &in, &out);
	if (status != kOfxStatOK) {
		return false;
	}
	clip = out.getString(kOfxPropName, "");
	identityTime = out.getDouble(kOfxPropTime, time);
	return true;
}

RenderResult Instance::render(const RenderRequest& request)
{
	RenderResult result;
	const double sx = request.renderScale.x > 0 ? request.renderScale.x : 1.0;
	const double sy = request.renderScale.y > 0 ? request.renderScale.y : 1.0;
	RectI window = request.window;
	if (window.empty()) {
		window = RectI{0, 0, static_cast<int>(std::lround(project_.width * sx / project_.pixelAspect)), static_cast<int>(std::lround(project_.height * sy))};
	}
	ImageDesc desc;
	desc.bounds = window;
	desc.regionOfDefinition = window;
	desc.depth = request.depth;
	desc.components = request.components;
	desc.premultiplication = request.premultiplication;
	desc.pixelAspect = project_.pixelAspect;
	desc.field = request.field;
	desc.renderScale = {sx, sy};
	result.image = ImageBuffer(desc);
	object_->clip(kOfxImageEffectOutputClipName)->props().setString(kOfxImageEffectPropPixelDepth, depthName(request.depth));
	object_->clip(kOfxImageEffectOutputClipName)->props().setString(kOfxImageEffectPropComponents, componentsName(request.components));

	result.status = clipPreferences();
	if (result.status != kOfxStatOK) {
		return result;
	}
	RectD rod;
	result.status = regionOfDefinition(request.time, {sx, sy}, rod);
	if (result.status != kOfxStatOK) {
		return result;
	}
	std::string identityClip;
	Time identityTime = request.time;
	if (isIdentity(request.time, {sx, sy}, window, identityClip, identityTime) && !host_.options().renderDespiteIdentity) {
		result.wasIdentity = true;
		result.identityClip = identityClip;
		result.identityTime = identityTime;
		Clip* clip = object_->clip(identityClip);
		if (std::shared_ptr<const ImageBuffer> image = clip ? clip->image() : nullptr) {
			result.image.view().copyFrom(image->view());
		}
		return result;
	}

	// GPU frames: upload the inputs, allocate the output, and hand the plug-in the host's queue.
	std::map<std::string, void*> gpuInputHandles;
	void* gpuOutputHandle = nullptr;
	void* gpuQueue = nullptr;
#if defined(OFXGRIP_HAS_METAL)
	std::shared_ptr<metal::MetalContext> metalContext;
	std::vector<metal::MetalBuffer> metalBuffers;
	metal::MetalBuffer metalOutput;
	if (request.gpu == GpuApi::Metal && host_.options().metalRenderSupported) {
		PropSet* descriptor = host_.descriptorProps();
		const bool pluginSupportsMetal = descriptor != nullptr && descriptor->getString(kOfxImageEffectPropMetalRenderSupported, "false") == "true";
		if (pluginSupportsMetal) {
			metalContext = metal::MetalContext::systemDefault();
		}
	}
	if (metalContext) {
		for (const auto& clip : object_->clips()) {
			std::shared_ptr<const ImageBuffer> image = clip->isOutput() ? nullptr : clip->image();
			if (!image) {
				continue;
			}
			metalBuffers.push_back(metalContext->newBuffer(image->bytes().size(), image->bytes().data()));
			gpuInputHandles[clip->name()] = metalBuffers.back().handle();
		}
		metalOutput = metalContext->newBuffer(result.image.bytes().size());
		gpuOutputHandle = metalOutput.handle();
		gpuQueue = metalContext->queue();
		result.gpu = GpuApi::Metal;
	}
#endif

	PropSet seq;
	seq.setDoubles(kOfxImageEffectPropFrameRange, {request.time, request.time});
	seq.setDouble(kOfxImageEffectPropFrameStep, 1.0);
	seq.setInt(kOfxPropIsInteractive, request.interactive ? 1 : 0);
	seq.setDoubles(kOfxImageEffectPropRenderScale, {sx, sy});
	seq.setInt(kOfxImageEffectPropSequentialRenderStatus, 0);
	seq.setInt(kOfxImageEffectPropInteractiveRenderStatus, request.interactive ? 1 : 0);
	seq.setInt(kOfxImageEffectPropRenderQualityDraft, request.draft ? 1 : 0);
	seq.setInt(kOfxImageEffectPropMetalEnabled, result.gpu == GpuApi::Metal ? 1 : 0);
	if (result.gpu == GpuApi::Metal) {
		seq.setPointer(kOfxImageEffectPropMetalCommandQueue, gpuQueue);
	}
	OfxStatus status = dispatch(kOfxImageEffectActionBeginSequenceRender, &seq, nullptr);
	if (status != kOfxStatOK && status != kOfxStatReplyDefault) {
		result.status = status;
		return result;
	}

	std::vector<RectI> tiles;
	if (request.tileSize <= 0) {
		tiles.push_back(window);
	} else {
		for (int y = window.y1; y < window.y2; y += request.tileSize) {
			for (int x = window.x1; x < window.x2; x += request.tileSize) {
				tiles.push_back(RectI{x, y, std::min(x + request.tileSize, window.x2), std::min(y + request.tileSize, window.y2)});
			}
		}
	}
	result.tileCount = static_cast<int>(tiles.size());
	result.tileStatuses.assign(tiles.size(), kOfxStatOK);

	std::atomic<std::size_t> next{0};
	auto worker = [&]() {
		for (std::size_t i = next.fetch_add(1); i < tiles.size(); i = next.fetch_add(1)) {
			RenderTask task;
			task.instance = this;
			task.output = &result.image;
			task.tile = tiles[i];
			task.renderScale = {sx, sy};
			task.field = request.field;
			task.gpu = result.gpu;
			task.gpuOutput = gpuOutputHandle;
			task.gpuInputs = &gpuInputHandles;
			PropSet in;
			in.setDouble(kOfxPropTime, request.time);
			in.setDoubles(kOfxImageEffectPropRenderScale, {sx, sy});
			in.setInts(kOfxImageEffectPropRenderWindow, {task.tile.x1, task.tile.y1, task.tile.x2, task.tile.y2});
			in.setString(kOfxImageEffectPropFieldToRender, fieldName(request.field));
			in.setInt(kOfxImageEffectPropRenderQualityDraft, request.draft ? 1 : 0);
			in.setInt(kOfxPropIsInteractive, request.interactive ? 1 : 0);
			in.setInt(kOfxImageEffectPropSequentialRenderStatus, 0);
			in.setInt(kOfxImageEffectPropInteractiveRenderStatus, request.interactive ? 1 : 0);
			in.setInt(kOfxImageEffectPropOpenGLEnabled, 0);
			in.setInt(kOfxImageEffectPropMetalEnabled, result.gpu == GpuApi::Metal ? 1 : 0);
			if (result.gpu == GpuApi::Metal) {
				in.setPointer(kOfxImageEffectPropMetalCommandQueue, gpuQueue);
			}
			in.setInt(kOfxImageEffectPropCudaEnabled, 0);
			in.setInt(kOfxImageEffectPropOpenCLEnabled, 0);
			currentTask = &task;
			const OfxStatus tileStatus = dispatch(kOfxImageEffectActionRender, &in, nullptr);
			currentTask = nullptr;
			result.tileStatuses[i] = tileStatus == kOfxStatReplyDefault ? kOfxStatOK : tileStatus;
		}
	};
	const unsigned threads = std::max(1u, std::min(request.threads, static_cast<unsigned>(tiles.size())));
	std::vector<std::thread> pool;
	for (unsigned t = 1; t < threads; ++t) {
		pool.emplace_back(worker);
	}
	worker();
	for (std::thread& t : pool) {
		t.join();
	}

	status = dispatch(kOfxImageEffectActionEndSequenceRender, &seq, nullptr);
	if (status != kOfxStatOK && status != kOfxStatReplyDefault) {
		result.status = status;
	}
#if defined(OFXGRIP_HAS_METAL)
	if (metalContext) {
		// The plug-in commits without waiting (ofxGPURender.h); the host waits before it reads.
		if (Error e = metalContext->waitIdle()) {
			result.status = kOfxStatFailed;
		} else {
			std::memcpy(result.image.bytes().data(), metalOutput.contents(), result.image.bytes().size());
		}
	}
#endif
	return result;
}

std::vector<std::string> Instance::messages() const
{
	std::lock_guard<std::mutex> lock(mutex_);
	return messages_;
}

std::string Instance::persistentMessage() const
{
	std::lock_guard<std::mutex> lock(mutex_);
	return persistentMessage_;
}

std::vector<double> Instance::progressUpdates() const
{
	std::lock_guard<std::mutex> lock(mutex_);
	return progress_;
}

// ---- helpers ------------------------------------------------------------------------------------

std::vector<double> numericValue(const Instance& instance, const std::string& name, Time time)
{
	Param* p = instance.param(name);
	return p == nullptr ? std::vector<double>{} : p->numericValueAt(time);
}

std::string stringValue(const Instance& instance, const std::string& name, Time time)
{
	Param* p = instance.param(name);
	return p == nullptr ? std::string() : p->stringValueAt(time);
}

std::shared_ptr<ImageBuffer> makeRampImage(int width, int height, BitDepth depth, Components components)
{
	ImageDesc desc;
	desc.bounds = RectI{0, 0, width, height};
	desc.regionOfDefinition = desc.bounds;
	desc.depth = depth;
	desc.components = components;
	desc.premultiplication = Premultiplication::Premultiplied;
	auto image = std::make_shared<ImageBuffer>(desc);
	ImageView view = image->view();
	const std::size_t n = componentCount(components);
	for (int y = 0; y < height; ++y) {
		for (int x = 0; x < width; ++x) {
			std::uint8_t* px = view.pixel(x, y);
			const float r = width > 1 ? static_cast<float>(x) / static_cast<float>(width - 1) : 0.0f;
			const float g = height > 1 ? static_cast<float>(y) / static_cast<float>(height - 1) : 0.0f;
			const float values[4] = {r, g, 0.5f, 1.0f};
			for (std::size_t c = 0; c < n; ++c) {
				writeComponent(px, depth, c, components == Components::Alpha ? 1.0f : values[c]);
			}
		}
	}
	return image;
}

}  // namespace minihost
OFXGRIP_NAMESPACE_END
