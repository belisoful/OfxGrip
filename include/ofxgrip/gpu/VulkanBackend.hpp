// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
/*!
	@file  VulkanBackend.hpp
	@brief The Vulkan compute device OfxGrip opens for itself, and the job that runs one SPIR-V
	       compute kernel over CPU pixels on it.

	Introduced in OfxGrip 0.5.0. No OpenFX host delivers Vulkan frames, so this backend owns its
	device. A job uploads float RGBA sources, binds the kernel's descriptors, dispatches, and
	reads the float RGBA destination back before it returns. The Vulkan loader is opened at run
	time (`vulkan-1.dll`, `libvulkan.so.1`, `libvulkan.1.dylib`), so a plug-in that links this
	backend loads on a machine without Vulkan and reports the backend unavailable there. On macOS
	the loader reaches Metal through MoltenVK. A software device (lavapipe) serves machines and
	CI runners with no GPU.

	The handles are opaque: no Vulkan header is included from this header.
*/
#pragma once

#include <ofxgrip/core/Error.hpp>
#include <ofxgrip/core/Types.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

OFXGRIP_NAMESPACE_BEGIN

namespace vulkan {

/*! What the opened device reports. */
struct VulkanDeviceInfo {
	std::string name;
	std::uint32_t apiVersion{0};
	bool software{false};       /*!< A CPU device such as lavapipe. */
	bool float32Linear{false};  /*!< RGBA32F samples with linear filtering; sources upload as RGBA16F otherwise. */
};

/*! One specialization constant, as Vulkan takes it: a `bool` is four bytes. */
struct VulkanConstant {
	std::uint32_t id{0};
	std::array<std::uint8_t, 8> bytes{};
	std::size_t length{0};
};

/*! One uniform buffer of a job. The bytes are copied before the dispatch. */
struct VulkanBufferBinding {
	std::uint32_t binding{0};
	const void* data{nullptr};
	std::size_t length{0};
};

/*! One sampled source: `width × height` float RGBA texels, the first row at v = 0. A null
    `rgba` binds one transparent black texel. */
struct VulkanSourceBinding {
	std::uint32_t binding{0};
	const float* rgba{nullptr};
	unsigned width{0};
	unsigned height{0};
};

/*! One compute job. Every binding is in descriptor set 0. */
struct VulkanJob {
	std::string cacheKey;                       /*!< Names the pipeline in the device's cache. */
	std::string_view spirv;                     /*!< The SPIR-V module, as `slangc -target spirv` emits it. */
	std::vector<VulkanConstant> constants;
	std::vector<VulkanBufferBinding> buffers;
	std::vector<VulkanSourceBinding> sources;
	std::vector<std::uint32_t> samplers;        /*!< Bindings that receive the bilinear, clamp-to-edge sampler. */
	std::uint32_t destinationBinding{0};
	unsigned width{0};                          /*!< The destination's size in texels. */
	unsigned height{0};
	float* destination{nullptr};                /*!< Receives `width × height` float RGBA texels. */
	std::array<unsigned, 3> groupSize{8, 8, 1}; /*!< The kernel's local size. */
};

/*! The device. One per process, opened on first use. */
class VulkanContext {
public:
	~VulkanContext();
	VulkanContext(const VulkanContext&) = delete;
	VulkanContext& operator=(const VulkanContext&) = delete;

	/*! The process's device, or null when no loader, no driver, or no compute queue exists.
	    The outcome of the first call is kept. */
	static std::shared_ptr<VulkanContext> shared();
	/*! Whether `shared()` returns a device. */
	static bool available() noexcept;
	/*! Why `shared()` returned null; empty when a device is open or was never asked for. */
	static std::string unavailableReason();
	/*! Opens a separate device through a named loader library. An empty path searches the
	    platform's default names. Tests use it to cover the failure paths. */
	static Error open(const std::string& libraryPath, std::shared_ptr<VulkanContext>& out);

	const VulkanDeviceInfo& info() const noexcept;
	/*! Runs a job and waits for it. Jobs from several threads may run at once; submissions to
	    the device's queue are serialized. */
	Error run(const VulkanJob& job);
	/*! The number of pipelines the cache holds. */
	std::size_t cachedPipelineCount() const;

private:
	struct Impl;
	explicit VulkanContext(std::unique_ptr<Impl> impl);
	std::unique_ptr<Impl> impl_;
};

/*! The name of the compute entry point a SPIR-V module declares; empty when the bytes are not
    a SPIR-V module with one. `slangc` names it `main` whatever the Slang function is called. */
std::string spirvComputeEntryPoint(std::string_view spirv);

}  // namespace vulkan

OFXGRIP_NAMESPACE_END
