// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
#include <ofxgrip/gpu/CudaDriver.hpp>

#if defined(_WIN32)
#include <windows.h>
#elif !defined(__APPLE__)
#include <dlfcn.h>
#endif

#include <mutex>

OFXGRIP_NAMESPACE_BEGIN

namespace cuda {

static_assert(sizeof(void*) == 8, "The CUDA driver interface declared here is the 64-bit one");
static_assert(sizeof(CudaArrayDescriptor) == 24, "CudaArrayDescriptor must match CUDA_ARRAY_DESCRIPTOR");
static_assert(sizeof(CudaResourceDescriptor) == 144, "CudaResourceDescriptor must match CUDA_RESOURCE_DESC");
static_assert(sizeof(CudaTextureDescriptor) == 104, "CudaTextureDescriptor must match CUDA_TEXTURE_DESC");
static_assert(sizeof(CudaMemcpy2D) == 128, "CudaMemcpy2D must match CUDA_MEMCPY2D");

bool CudaDriver::complete() const noexcept
{
	return init != nullptr && ctxGetCurrent != nullptr && ctxGetDevice != nullptr && deviceGetAttribute != nullptr && getErrorName != nullptr && moduleLoadData != nullptr && moduleUnload != nullptr
	       && moduleGetFunction != nullptr && moduleGetGlobal != nullptr && memAlloc != nullptr && memFree != nullptr && memcpyHtoD != nullptr && memcpy2D != nullptr && memcpy2DAsync != nullptr
	       && arrayCreate != nullptr && arrayDestroy != nullptr && texObjectCreate != nullptr && texObjectDestroy != nullptr && surfObjectCreate != nullptr && surfObjectDestroy != nullptr
	       && launchKernel != nullptr && streamSynchronize != nullptr;
}

namespace {

#if !defined(__APPLE__)

void* openDriverLibrary() noexcept
{
#if defined(_WIN32)
	return static_cast<void*>(::LoadLibraryA("nvcuda.dll"));
#else
	void* library = ::dlopen("libcuda.so.1", RTLD_NOW | RTLD_LOCAL);
	return library != nullptr ? library : ::dlopen("libcuda.so", RTLD_NOW | RTLD_LOCAL);
#endif
}

void* driverSymbol(void* library, const char* name) noexcept
{
#if defined(_WIN32)
	return reinterpret_cast<void*>(::GetProcAddress(static_cast<HMODULE>(library), name));
#else
	return ::dlsym(library, name);
#endif
}

template <typename F>
void resolve(void* library, const char* name, F& out) noexcept
{
	out = reinterpret_cast<F>(driverSymbol(library, name));
}

bool loadSystemDriver(CudaDriver& driver) noexcept
{
	void* library = openDriverLibrary();
	if (library == nullptr) {
		return false;
	}
	// The suffixed names are the ones the toolkit's header maps the plain names to.
	resolve(library, "cuInit", driver.init);
	resolve(library, "cuCtxGetCurrent", driver.ctxGetCurrent);
	resolve(library, "cuCtxGetDevice", driver.ctxGetDevice);
	resolve(library, "cuDeviceGetAttribute", driver.deviceGetAttribute);
	resolve(library, "cuGetErrorName", driver.getErrorName);
	resolve(library, "cuModuleLoadData", driver.moduleLoadData);
	resolve(library, "cuModuleUnload", driver.moduleUnload);
	resolve(library, "cuModuleGetFunction", driver.moduleGetFunction);
	resolve(library, "cuModuleGetGlobal_v2", driver.moduleGetGlobal);
	resolve(library, "cuMemAlloc_v2", driver.memAlloc);
	resolve(library, "cuMemFree_v2", driver.memFree);
	resolve(library, "cuMemcpyHtoD_v2", driver.memcpyHtoD);
	resolve(library, "cuMemcpy2D_v2", driver.memcpy2D);
	resolve(library, "cuMemcpy2DAsync_v2", driver.memcpy2DAsync);
	resolve(library, "cuArrayCreate_v2", driver.arrayCreate);
	resolve(library, "cuArrayDestroy", driver.arrayDestroy);
	resolve(library, "cuTexObjectCreate", driver.texObjectCreate);
	resolve(library, "cuTexObjectDestroy", driver.texObjectDestroy);
	resolve(library, "cuSurfObjectCreate", driver.surfObjectCreate);
	resolve(library, "cuSurfObjectDestroy", driver.surfObjectDestroy);
	resolve(library, "cuLaunchKernel", driver.launchKernel);
	resolve(library, "cuStreamSynchronize", driver.streamSynchronize);
	return driver.complete() && driver.init(0) == kCudaSuccess;
}

#endif

}  // namespace

const CudaDriver* CudaDriver::system()
{
#if defined(__APPLE__)
	// NVIDIA ships no driver for macOS.
	return nullptr;
#else
	static CudaDriver driver;
	static bool loaded = false;
	static std::once_flag once;
	std::call_once(once, []() { loaded = loadSystemDriver(driver); });
	return loaded ? &driver : nullptr;
#endif
}

}  // namespace cuda

OFXGRIP_NAMESPACE_END
