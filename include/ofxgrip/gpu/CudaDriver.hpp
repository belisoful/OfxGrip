// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
/*!
	@file  CudaDriver.hpp
	@brief The part of the CUDA driver API the CUDA backend calls, as a table of function pointers
	       over the driver library opened at run time.

	Introduced in OfxGrip 0.5.0. The backend needs no CUDA toolkit to build: the types below
	declare the driver's binary interface, and `CudaDriver::system()` resolves the functions
	from `nvcuda.dll` or `libcuda.so.1` when the machine has an NVIDIA driver. A plug-in that
	carries the backend therefore loads on a machine without one and reports the backend
	unavailable. A test passes its own table to exercise the backend without a GPU.

	The driver API is 64-bit here: a device pointer and a texture or surface object are
	`unsigned long long`, and a context, module, function, stream, or array is an opaque pointer.
*/
#pragma once

#include <ofxgrip/core/Types.hpp>

#include <cstddef>

OFXGRIP_NAMESPACE_BEGIN

namespace cuda {

using CudaResult = int;
using CudaDevicePointer = unsigned long long;
using CudaObject = unsigned long long;

inline constexpr CudaResult kCudaSuccess = 0;

/*! `CUarray_format`. */
inline constexpr int kCudaFormatUInt8 = 0x01;
inline constexpr int kCudaFormatUInt16 = 0x02;
inline constexpr int kCudaFormatHalf = 0x10;
inline constexpr int kCudaFormatFloat = 0x20;
/*! `CUresourcetype`. */
inline constexpr int kCudaResourceArray = 0x00;
inline constexpr int kCudaResourcePitch2D = 0x03;
/*! `CUaddress_mode`, `CUfilter_mode`, and the texture flags. */
inline constexpr int kCudaAddressClamp = 1;
inline constexpr int kCudaFilterLinear = 1;
inline constexpr unsigned kCudaNormalizedCoordinates = 0x02;
/*! `CUmemorytype`. */
inline constexpr int kCudaMemoryHost = 0x01;
inline constexpr int kCudaMemoryDevice = 0x02;
inline constexpr int kCudaMemoryArray = 0x03;
/*! `CUdevice_attribute`. */
inline constexpr int kCudaAttributeTextureAlignment = 14;
inline constexpr int kCudaAttributeTexturePitchAlignment = 51;

/*! `CUDA_ARRAY_DESCRIPTOR`. */
struct CudaArrayDescriptor {
	std::size_t width{0};
	std::size_t height{0};
	int format{0};
	unsigned channels{0};
};

/*! `CUDA_RESOURCE_DESC`. */
struct CudaResourceDescriptor {
	int type{0};
	union {
		struct {
			void* array;
		} array;
		struct {
			CudaDevicePointer pointer;
			int format;
			unsigned channels;
			std::size_t width;
			std::size_t height;
			std::size_t pitchBytes;
		} pitch2D;
		struct {
			int reserved[32];
		} reserved;
	} resource{};
	unsigned flags{0};
};

/*! `CUDA_TEXTURE_DESC`. */
struct CudaTextureDescriptor {
	int addressMode[3]{0, 0, 0};
	int filterMode{0};
	unsigned flags{0};
	unsigned maxAnisotropy{0};
	int mipmapFilterMode{0};
	float mipmapLevelBias{0};
	float minMipmapLevelClamp{0};
	float maxMipmapLevelClamp{0};
	float borderColor[4]{0, 0, 0, 0};
	int reserved[12]{};
};

/*! `CUDA_MEMCPY2D`. */
struct CudaMemcpy2D {
	std::size_t sourceXBytes{0};
	std::size_t sourceY{0};
	int sourceType{0};
	const void* sourceHost{nullptr};
	CudaDevicePointer sourceDevice{0};
	void* sourceArray{nullptr};
	std::size_t sourcePitch{0};
	std::size_t destinationXBytes{0};
	std::size_t destinationY{0};
	int destinationType{0};
	void* destinationHost{nullptr};
	CudaDevicePointer destinationDevice{0};
	void* destinationArray{nullptr};
	std::size_t destinationPitch{0};
	std::size_t widthBytes{0};
	std::size_t height{0};
};

/*! The driver functions, named after the `cu…` function each one is. */
struct CudaDriver {
	CudaResult (*init)(unsigned flags){nullptr};
	CudaResult (*ctxGetCurrent)(void** context){nullptr};
	CudaResult (*ctxGetDevice)(int* device){nullptr};
	CudaResult (*deviceGetAttribute)(int* value, int attribute, int device){nullptr};
	CudaResult (*getErrorName)(CudaResult error, const char** name){nullptr};
	CudaResult (*moduleLoadData)(void** module, const void* image){nullptr};
	CudaResult (*moduleUnload)(void* module){nullptr};
	CudaResult (*moduleGetFunction)(void** function, void* module, const char* name){nullptr};
	CudaResult (*moduleGetGlobal)(CudaDevicePointer* pointer, std::size_t* bytes, void* module, const char* name){nullptr};
	CudaResult (*memAlloc)(CudaDevicePointer* pointer, std::size_t bytes){nullptr};
	CudaResult (*memFree)(CudaDevicePointer pointer){nullptr};
	CudaResult (*memcpyHtoD)(CudaDevicePointer destination, const void* source, std::size_t bytes){nullptr};
	CudaResult (*memcpy2D)(const CudaMemcpy2D* copy){nullptr};
	CudaResult (*memcpy2DAsync)(const CudaMemcpy2D* copy, void* stream){nullptr};
	CudaResult (*arrayCreate)(void** array, const CudaArrayDescriptor* descriptor){nullptr};
	CudaResult (*arrayDestroy)(void* array){nullptr};
	CudaResult (*texObjectCreate)(CudaObject* object, const CudaResourceDescriptor* resource, const CudaTextureDescriptor* texture, const void* view){nullptr};
	CudaResult (*texObjectDestroy)(CudaObject object){nullptr};
	CudaResult (*surfObjectCreate)(CudaObject* object, const CudaResourceDescriptor* resource){nullptr};
	CudaResult (*surfObjectDestroy)(CudaObject object){nullptr};
	CudaResult (*launchKernel)(void* function, unsigned gridX, unsigned gridY, unsigned gridZ, unsigned blockX, unsigned blockY, unsigned blockZ, unsigned sharedBytes, void* stream, void** parameters, void** extra){nullptr};
	CudaResult (*streamSynchronize)(void* stream){nullptr};

	/*! Whether every function is set. */
	bool complete() const noexcept;
	/*! The machine's driver, initialized, or null when no NVIDIA driver is installed. The
	    outcome of the first call is kept. */
	static const CudaDriver* system();
};

}  // namespace cuda

OFXGRIP_NAMESPACE_END
