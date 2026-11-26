// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
#include "CudaDeviceContext.hpp"

#include <ofxgrip/gpu/CudaDriver.hpp>

#if defined(_WIN32)
#include <windows.h>
#elif !defined(__APPLE__)
#include <dlfcn.h>
#endif

namespace ofxgrip::testing {

namespace {

using cuda::CudaResult;
using cuda::kCudaSuccess;

void* openDriverLibrary() noexcept
{
#if defined(_WIN32)
	return static_cast<void*>(::LoadLibraryA("nvcuda.dll"));
#elif defined(__APPLE__)
	return nullptr;
#else
	void* library = ::dlopen("libcuda.so.1", RTLD_NOW | RTLD_LOCAL);
	return library != nullptr ? library : ::dlopen("libcuda.so", RTLD_NOW | RTLD_LOCAL);
#endif
}

template <typename F>
F driverFunction(void* library, const char* name) noexcept
{
#if defined(_WIN32)
	return reinterpret_cast<F>(reinterpret_cast<void*>(::GetProcAddress(static_cast<HMODULE>(library), name)));
#elif defined(__APPLE__)
	static_cast<void>(library);
	static_cast<void>(name);
	return nullptr;
#else
	return reinterpret_cast<F>(::dlsym(library, name));
#endif
}

using DeviceGet = CudaResult (*)(int* device, int ordinal);
using PrimaryRetain = CudaResult (*)(void** context, int device);
using PrimaryRelease = CudaResult (*)(int device);
using SetCurrent = CudaResult (*)(void* context);

void releasePrimaryContext(void* library, int device) noexcept
{
	PrimaryRelease release = driverFunction<PrimaryRelease>(library, "cuDevicePrimaryCtxRelease_v2");
	if (release == nullptr) {
		release = driverFunction<PrimaryRelease>(library, "cuDevicePrimaryCtxRelease");
	}
	if (release != nullptr) {
		release(device);
	}
}

}  // namespace

CudaDeviceContext::CudaDeviceContext()
{
	if (cuda::CudaDriver::system() == nullptr) {
		reason_ = "No CUDA driver is installed";
		return;
	}
	library_ = openDriverLibrary();
	if (library_ == nullptr) {
		reason_ = "The CUDA driver library did not open a second time";
		return;
	}
	const DeviceGet deviceGet = driverFunction<DeviceGet>(library_, "cuDeviceGet");
	const PrimaryRetain retain = driverFunction<PrimaryRetain>(library_, "cuDevicePrimaryCtxRetain");
	const SetCurrent setCurrent = driverFunction<SetCurrent>(library_, "cuCtxSetCurrent");
	if (deviceGet == nullptr || retain == nullptr || setCurrent == nullptr) {
		reason_ = "The CUDA driver lacks the primary-context functions";
		return;
	}
	if (const CudaResult r = deviceGet(&device_, 0); r != kCudaSuccess) {
		reason_ = "cuDeviceGet(0) returned " + std::to_string(r);
		return;
	}
	void* context = nullptr;
	if (const CudaResult r = retain(&context, device_); r != kCudaSuccess || context == nullptr) {
		reason_ = "cuDevicePrimaryCtxRetain returned " + std::to_string(r);
		return;
	}
	if (const CudaResult r = setCurrent(context); r != kCudaSuccess) {
		reason_ = "cuCtxSetCurrent returned " + std::to_string(r);
		releasePrimaryContext(library_, device_);
		return;
	}
	context_ = context;
}

CudaDeviceContext::~CudaDeviceContext()
{
	if (context_ == nullptr) {
		return;
	}
	if (const SetCurrent setCurrent = driverFunction<SetCurrent>(library_, "cuCtxSetCurrent")) {
		setCurrent(nullptr);
	}
	releasePrimaryContext(library_, device_);
}

}  // namespace ofxgrip::testing
