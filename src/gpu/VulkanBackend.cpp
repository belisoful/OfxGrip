// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
#include <ofxgrip/gpu/VulkanBackend.hpp>

#include <ofxgrip/core/Image.hpp>

// The loader is opened at run time, so the header declares types only.
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>

OFXGRIP_NAMESPACE_BEGIN

namespace vulkan {

namespace {

#define OFXGRIP_VK_INSTANCE_FUNCTIONS(X) \
	X(DestroyInstance) \
	X(EnumeratePhysicalDevices) \
	X(GetPhysicalDeviceProperties) \
	X(GetPhysicalDeviceFeatures) \
	X(GetPhysicalDeviceQueueFamilyProperties) \
	X(GetPhysicalDeviceMemoryProperties) \
	X(GetPhysicalDeviceFormatProperties) \
	X(EnumerateDeviceExtensionProperties) \
	X(CreateDevice) \
	X(GetDeviceProcAddr)

#define OFXGRIP_VK_DEVICE_FUNCTIONS(X) \
	X(DestroyDevice) \
	X(GetDeviceQueue) \
	X(DeviceWaitIdle) \
	X(CreateCommandPool) \
	X(DestroyCommandPool) \
	X(AllocateCommandBuffers) \
	X(BeginCommandBuffer) \
	X(EndCommandBuffer) \
	X(QueueSubmit) \
	X(CreateFence) \
	X(DestroyFence) \
	X(WaitForFences) \
	X(CreateBuffer) \
	X(DestroyBuffer) \
	X(GetBufferMemoryRequirements) \
	X(AllocateMemory) \
	X(FreeMemory) \
	X(BindBufferMemory) \
	X(MapMemory) \
	X(UnmapMemory) \
	X(CreateImage) \
	X(DestroyImage) \
	X(GetImageMemoryRequirements) \
	X(BindImageMemory) \
	X(CreateImageView) \
	X(DestroyImageView) \
	X(CreateSampler) \
	X(DestroySampler) \
	X(CreateShaderModule) \
	X(DestroyShaderModule) \
	X(CreateDescriptorSetLayout) \
	X(DestroyDescriptorSetLayout) \
	X(CreatePipelineLayout) \
	X(DestroyPipelineLayout) \
	X(CreateComputePipelines) \
	X(DestroyPipeline) \
	X(CreateDescriptorPool) \
	X(DestroyDescriptorPool) \
	X(AllocateDescriptorSets) \
	X(UpdateDescriptorSets) \
	X(CmdBindPipeline) \
	X(CmdBindDescriptorSets) \
	X(CmdDispatch) \
	X(CmdPipelineBarrier) \
	X(CmdCopyBufferToImage) \
	X(CmdCopyImageToBuffer) \
	X(CmdClearColorImage)

struct Functions {
	PFN_vkGetInstanceProcAddr GetInstanceProcAddr{nullptr};
	PFN_vkCreateInstance CreateInstance{nullptr};
	PFN_vkEnumerateInstanceExtensionProperties EnumerateInstanceExtensionProperties{nullptr};
	PFN_vkGetPhysicalDeviceFeatures2 GetPhysicalDeviceFeatures2{nullptr};  // Vulkan 1.1; null on a 1.0 instance
#define X(name) PFN_vk##name name{nullptr};
	OFXGRIP_VK_INSTANCE_FUNCTIONS(X)
	OFXGRIP_VK_DEVICE_FUNCTIONS(X)
#undef X
};

constexpr std::uint32_t kSpirvMagic = 0x07230203u;
constexpr std::uint32_t kSpirvOpEntryPoint = 15;
constexpr std::uint32_t kSpirvModelGLCompute = 5;
constexpr std::uint32_t kSpirvOpCapability = 17;

/*! The shader features a device was created with. A kernel whose SPIR-V declares a capability
    outside them is refused before the driver sees it. */
struct ShaderFeatures {
	bool int8{false};
	bool int16{false};
	bool int64{false};
	bool float16{false};
	bool float64{false};
	bool uniform8{false};
	bool uniform16{false};
	bool imageReadWithoutFormat{false};
	bool imageWriteWithoutFormat{false};
};

/*! The name of the first SPIR-V capability a module declares that the features lack; null when
    the device serves every capability this backend knows. */
const char* missingCapability(const std::vector<std::uint32_t>& words, const ShaderFeatures& features) noexcept
{
	std::size_t at = 5;
	while (at < words.size()) {
		const std::uint32_t count = words[at] >> 16;
		const std::uint32_t op = words[at] & 0xffffu;
		if (count == 0 || at + count > words.size()) {
			break;
		}
		if (op == kSpirvOpCapability && count == 2) {
			switch (words[at + 1]) {
			case 9: if (!features.float16) { return "Float16"; } break;
			case 10: if (!features.float64) { return "Float64"; } break;
			case 11: if (!features.int64) { return "Int64"; } break;
			case 22: if (!features.int16) { return "Int16"; } break;
			case 39: if (!features.int8) { return "Int8"; } break;
			case 55: if (!features.imageReadWithoutFormat) { return "StorageImageReadWithoutFormat"; } break;
			case 56: if (!features.imageWriteWithoutFormat) { return "StorageImageWriteWithoutFormat"; } break;
			case 4433:
			case 4434: if (!features.uniform16) { return "UniformAndStorageBuffer16BitAccess"; } break;
			case 4448:
			case 4449: if (!features.uniform8) { return "UniformAndStorageBuffer8BitAccess"; } break;
			default: break;
			}
		}
		at += count;
	}
	return nullptr;
}
constexpr std::uint64_t kJobTimeoutNanoseconds = 120ull * 1000ull * 1000ull * 1000ull;

void* openLibrary(const std::string& path) noexcept
{
#if defined(_WIN32)
	return static_cast<void*>(::LoadLibraryA(path.c_str()));
#else
	return ::dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
#endif
}

void* librarySymbol(void* library, const char* name) noexcept
{
#if defined(_WIN32)
	return reinterpret_cast<void*>(::GetProcAddress(static_cast<HMODULE>(library), name));
#else
	return ::dlsym(library, name);
#endif
}

std::vector<std::string> defaultLibraryNames()
{
	std::vector<std::string> names;
	if (const char* named = std::getenv("OFXGRIP_VULKAN_LIBRARY"); named != nullptr && named[0] != '\0') {
		names.emplace_back(named);
	}
#if defined(_WIN32)
	names.emplace_back("vulkan-1.dll");
#elif defined(__APPLE__)
	// dyld's fallback search covers /usr/local/lib; Homebrew on Apple silicon installs elsewhere.
	names.emplace_back("libvulkan.1.dylib");
	names.emplace_back("/opt/homebrew/lib/libvulkan.1.dylib");
	names.emplace_back("/usr/local/lib/libvulkan.1.dylib");
	names.emplace_back("libMoltenVK.dylib");
#else
	names.emplace_back("libvulkan.so.1");
	names.emplace_back("libvulkan.so");
#endif
	return names;
}

Error vkError(const char* what, VkResult result)
{
	return Error::make(ErrorCode::Render, what).with("vkResult", std::to_string(static_cast<int>(result)));
}

std::vector<std::uint32_t> spirvWords(std::string_view spirv)
{
	std::vector<std::uint32_t> words;
	if (spirv.size() < 20 || spirv.size() % 4 != 0) {
		return words;
	}
	words.resize(spirv.size() / 4);
	std::memcpy(words.data(), spirv.data(), spirv.size());
	if (words[0] != kSpirvMagic) {
		words.clear();
	}
	return words;
}

/*! The Vulkan version a SPIR-V version needs: 1.0 → 1.0, to 1.3 → 1.1, to 1.5 → 1.2, 1.6 → 1.3. */
std::uint32_t vulkanVersionForSpirv(std::uint32_t spirvVersion) noexcept
{
	const std::uint32_t minor = (spirvVersion >> 8) & 0xffu;
	if (minor == 0) {
		return VK_API_VERSION_1_0;
	}
	if (minor <= 3) {
		return VK_API_VERSION_1_1;
	}
	if (minor <= 5) {
		return VK_API_VERSION_1_2;
	}
	return VK_API_VERSION_1_3;
}

std::string entryPointOf(const std::vector<std::uint32_t>& words)
{
	std::size_t at = 5;
	while (at < words.size()) {
		const std::uint32_t count = words[at] >> 16;
		const std::uint32_t op = words[at] & 0xffffu;
		if (count == 0 || at + count > words.size()) {
			break;
		}
		if (op == kSpirvOpEntryPoint && count > 3 && words[at + 1] == kSpirvModelGLCompute) {
			const char* text = reinterpret_cast<const char*>(&words[at + 3]);
			const std::size_t limit = (count - 3) * 4;
			return std::string(text, ::strnlen(text, limit));
		}
		at += count;
	}
	return {};
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

VkDeviceSize alignUp(VkDeviceSize value, VkDeviceSize alignment) noexcept
{
	return alignment <= 1 ? value : (value + alignment - 1) / alignment * alignment;
}

int deviceRank(VkPhysicalDeviceType type) noexcept
{
	switch (type) {
	case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU: return 4;
	case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: return 3;
	case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU: return 2;
	case VK_PHYSICAL_DEVICE_TYPE_CPU: return 1;
	default: return 0;
	}
}

struct Pipeline {
	VkShaderModule module{VK_NULL_HANDLE};
	VkDescriptorSetLayout setLayout{VK_NULL_HANDLE};
	VkPipelineLayout layout{VK_NULL_HANDLE};
	VkPipeline pipeline{VK_NULL_HANDLE};
};

/*! The opened loader, instance, and device, with the pipeline cache. */
struct Device {
	void* library{nullptr};
	Functions vk;
	VkInstance instance{VK_NULL_HANDLE};
	VkPhysicalDevice physical{VK_NULL_HANDLE};
	VkDevice device{VK_NULL_HANDLE};
	VkQueue queue{VK_NULL_HANDLE};
	std::uint32_t queueFamily{0};
	VkSampler sampler{VK_NULL_HANDLE};
	VkPhysicalDeviceMemoryProperties memory{};
	VkPhysicalDeviceLimits limits{};
	VulkanDeviceInfo info;
	ShaderFeatures shaderFeatures;
	std::mutex queueMutex;
	mutable std::mutex cacheMutex;
	std::map<std::string, Pipeline> pipelines;

	Device() = default;
	Device(const Device&) = delete;
	Device& operator=(const Device&) = delete;
	~Device();
	Error open(const std::string& libraryPath);
	Error createInstance();
	Error pickDevice();
	Error createDevice();
	bool memoryType(std::uint32_t typeBits, VkMemoryPropertyFlags flags, std::uint32_t& out) const noexcept;
	void destroy(Pipeline& pipeline) noexcept;
	Error pipelineFor(const VulkanJob& job, Pipeline& out);
};

/*! Everything one job creates. The destructor releases it on every path out of `run`. */
struct JobResources {
	Device& context;
	VkBuffer staging{VK_NULL_HANDLE};
	VkDeviceMemory stagingMemory{VK_NULL_HANDLE};
	void* mapped{nullptr};
	std::vector<VkImage> images;
	std::vector<VkDeviceMemory> imageMemory;
	std::vector<VkImageView> views;
	VkDescriptorPool descriptorPool{VK_NULL_HANDLE};
	VkCommandPool commandPool{VK_NULL_HANDLE};
	VkFence fence{VK_NULL_HANDLE};

	explicit JobResources(Device& owner) : context(owner) {}
	JobResources(const JobResources&) = delete;
	JobResources& operator=(const JobResources&) = delete;
	~JobResources()
	{
		const Functions& vk = context.vk;
		const VkDevice device = context.device;
		if (fence != VK_NULL_HANDLE) {
			vk.DestroyFence(device, fence, nullptr);
		}
		if (commandPool != VK_NULL_HANDLE) {
			vk.DestroyCommandPool(device, commandPool, nullptr);
		}
		if (descriptorPool != VK_NULL_HANDLE) {
			vk.DestroyDescriptorPool(device, descriptorPool, nullptr);
		}
		for (VkImageView view : views) {
			vk.DestroyImageView(device, view, nullptr);
		}
		for (VkImage image : images) {
			vk.DestroyImage(device, image, nullptr);
		}
		for (VkDeviceMemory block : imageMemory) {
			vk.FreeMemory(device, block, nullptr);
		}
		if (mapped != nullptr) {
			vk.UnmapMemory(device, stagingMemory);
		}
		if (staging != VK_NULL_HANDLE) {
			vk.DestroyBuffer(device, staging, nullptr);
		}
		if (stagingMemory != VK_NULL_HANDLE) {
			vk.FreeMemory(device, stagingMemory, nullptr);
		}
	}

	Error image(unsigned width, unsigned height, VkFormat format, VkImageUsageFlags usage, VkImage& outImage, VkImageView& outView)
	{
		const Functions& vk = context.vk;
		VkImageCreateInfo info{};
		info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		info.imageType = VK_IMAGE_TYPE_2D;
		info.format = format;
		info.extent = {width, height, 1};
		info.mipLevels = 1;
		info.arrayLayers = 1;
		info.samples = VK_SAMPLE_COUNT_1_BIT;
		info.tiling = VK_IMAGE_TILING_OPTIMAL;
		info.usage = usage;
		info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		if (VkResult r = vk.CreateImage(context.device, &info, nullptr, &outImage); r != VK_SUCCESS) {
			return vkError("Vulkan created no image", r);
		}
		images.push_back(outImage);
		VkMemoryRequirements needs{};
		vk.GetImageMemoryRequirements(context.device, outImage, &needs);
		VkMemoryAllocateInfo allocation{};
		allocation.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		allocation.allocationSize = needs.size;
		if (!context.memoryType(needs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, allocation.memoryTypeIndex) && !context.memoryType(needs.memoryTypeBits, 0, allocation.memoryTypeIndex)) {
			return Error::make(ErrorCode::Memory, "The Vulkan device has no memory type for an image");
		}
		VkDeviceMemory block = VK_NULL_HANDLE;
		if (VkResult r = vk.AllocateMemory(context.device, &allocation, nullptr, &block); r != VK_SUCCESS) {
			return Error::make(ErrorCode::Memory, "Vulkan allocated no image memory").with("vkResult", std::to_string(static_cast<int>(r)));
		}
		imageMemory.push_back(block);
		if (VkResult r = vk.BindImageMemory(context.device, outImage, block, 0); r != VK_SUCCESS) {
			return vkError("Vulkan bound no image memory", r);
		}
		VkImageViewCreateInfo view{};
		view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		view.image = outImage;
		view.viewType = VK_IMAGE_VIEW_TYPE_2D;
		view.format = format;
		view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
		if (VkResult r = vk.CreateImageView(context.device, &view, nullptr, &outView); r != VK_SUCCESS) {
			return vkError("Vulkan created no image view", r);
		}
		views.push_back(outView);
		return Error::none();
	}
};

void transition(const Functions& vk, VkCommandBuffer command, VkImage image, VkImageLayout from, VkImageLayout to, VkAccessFlags sourceAccess, VkAccessFlags destinationAccess, VkPipelineStageFlags sourceStage, VkPipelineStageFlags destinationStage)
{
	VkImageMemoryBarrier barrier{};
	barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	barrier.srcAccessMask = sourceAccess;
	barrier.dstAccessMask = destinationAccess;
	barrier.oldLayout = from;
	barrier.newLayout = to;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.image = image;
	barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
	vk.CmdPipelineBarrier(command, sourceStage, destinationStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
}

VkBufferImageCopy wholeImage(VkDeviceSize offset, unsigned width, unsigned height) noexcept
{
	VkBufferImageCopy region{};
	region.bufferOffset = offset;
	region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
	region.imageExtent = {width, height, 1};
	return region;
}

Device::~Device()
{
	if (device != VK_NULL_HANDLE) {
		vk.DeviceWaitIdle(device);
		for (auto& [key, pipeline] : pipelines) {
			destroy(pipeline);
		}
		if (sampler != VK_NULL_HANDLE) {
			vk.DestroySampler(device, sampler, nullptr);
		}
		vk.DestroyDevice(device, nullptr);
	}
	if (instance != VK_NULL_HANDLE) {
		vk.DestroyInstance(instance, nullptr);
	}
	// The loader stays open for the life of the process: a driver may keep threads that still
	// run its code after the device is gone.
}

void Device::destroy(Pipeline& pipeline) noexcept
{
	if (pipeline.pipeline != VK_NULL_HANDLE) {
		vk.DestroyPipeline(device, pipeline.pipeline, nullptr);
	}
	if (pipeline.layout != VK_NULL_HANDLE) {
		vk.DestroyPipelineLayout(device, pipeline.layout, nullptr);
	}
	if (pipeline.setLayout != VK_NULL_HANDLE) {
		vk.DestroyDescriptorSetLayout(device, pipeline.setLayout, nullptr);
	}
	if (pipeline.module != VK_NULL_HANDLE) {
		vk.DestroyShaderModule(device, pipeline.module, nullptr);
	}
	pipeline = Pipeline{};
}

bool Device::memoryType(std::uint32_t typeBits, VkMemoryPropertyFlags flags, std::uint32_t& out) const noexcept
{
	for (std::uint32_t i = 0; i < memory.memoryTypeCount; ++i) {
		if ((typeBits & (1u << i)) != 0 && (memory.memoryTypes[i].propertyFlags & flags) == flags) {
			out = i;
			return true;
		}
	}
	return false;
}

Error Device::open(const std::string& libraryPath)
{
	const std::vector<std::string> names = libraryPath.empty() ? defaultLibraryNames() : std::vector<std::string>{libraryPath};
	for (const std::string& name : names) {
		library = openLibrary(name);
		if (library != nullptr) {
			break;
		}
	}
	if (library == nullptr) {
		return Error::make(ErrorCode::MissingHostFeature, "No Vulkan loader library is installed").with("library", names.empty() ? std::string() : names.front());
	}
	vk.GetInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(librarySymbol(library, "vkGetInstanceProcAddr"));
	if (vk.GetInstanceProcAddr == nullptr) {
		return Error::make(ErrorCode::MissingHostFeature, "The library is not a Vulkan loader");
	}
	vk.CreateInstance = reinterpret_cast<PFN_vkCreateInstance>(vk.GetInstanceProcAddr(VK_NULL_HANDLE, "vkCreateInstance"));
	vk.EnumerateInstanceExtensionProperties = reinterpret_cast<PFN_vkEnumerateInstanceExtensionProperties>(vk.GetInstanceProcAddr(VK_NULL_HANDLE, "vkEnumerateInstanceExtensionProperties"));
	if (vk.CreateInstance == nullptr || vk.EnumerateInstanceExtensionProperties == nullptr) {
		return Error::make(ErrorCode::MissingHostFeature, "The Vulkan loader exports no instance functions");
	}
	if (Error e = createInstance()) {
		return e;
	}
	if (Error e = pickDevice()) {
		return e;
	}
	return createDevice();
}

Error Device::createInstance()
{
	std::uint32_t count = 0;
	vk.EnumerateInstanceExtensionProperties(nullptr, &count, nullptr);
	std::vector<VkExtensionProperties> extensions(count);
	if (count > 0) {
		vk.EnumerateInstanceExtensionProperties(nullptr, &count, extensions.data());
		extensions.resize(count);
	}
	// A portability driver (MoltenVK) is listed only to an instance that asks for it.
	std::vector<const char*> enabled;
	VkInstanceCreateFlags flags = 0;
	for (const VkExtensionProperties& extension : extensions) {
		if (std::strcmp(extension.extensionName, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME) == 0) {
			enabled.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
			flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
		}
	}
	VkApplicationInfo application{};
	application.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	application.pApplicationName = "OfxGrip";
	application.pEngineName = "OfxGrip";
	application.apiVersion = VK_API_VERSION_1_3;
	VkInstanceCreateInfo instanceInfo{};
	instanceInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	instanceInfo.flags = flags;
	instanceInfo.pApplicationInfo = &application;
	instanceInfo.enabledExtensionCount = static_cast<std::uint32_t>(enabled.size());
	instanceInfo.ppEnabledExtensionNames = enabled.data();
	VkResult result = vk.CreateInstance(&instanceInfo, nullptr, &instance);
	if (result == VK_ERROR_INCOMPATIBLE_DRIVER) {
		// A Vulkan 1.0 loader accepts only 1.0.
		application.apiVersion = VK_API_VERSION_1_0;
		result = vk.CreateInstance(&instanceInfo, nullptr, &instance);
	}
	if (result != VK_SUCCESS) {
		instance = VK_NULL_HANDLE;
		return Error::make(ErrorCode::MissingHostFeature, "Vulkan created no instance").with("vkResult", std::to_string(static_cast<int>(result)));
	}
#define X(name) vk.name = reinterpret_cast<PFN_vk##name>(vk.GetInstanceProcAddr(instance, "vk" #name));
	OFXGRIP_VK_INSTANCE_FUNCTIONS(X)
#undef X
	vk.GetPhysicalDeviceFeatures2 = reinterpret_cast<PFN_vkGetPhysicalDeviceFeatures2>(vk.GetInstanceProcAddr(instance, "vkGetPhysicalDeviceFeatures2"));
#define X(name) \
	if (vk.name == nullptr) { \
		return Error::make(ErrorCode::MissingHostFeature, "The Vulkan loader lacks an instance function").with("function", "vk" #name); \
	}
	OFXGRIP_VK_INSTANCE_FUNCTIONS(X)
#undef X
	return Error::none();
}

Error Device::pickDevice()
{
	std::uint32_t count = 0;
	vk.EnumeratePhysicalDevices(instance, &count, nullptr);
	std::vector<VkPhysicalDevice> devices(count);
	if (count > 0) {
		vk.EnumeratePhysicalDevices(instance, &count, devices.data());
		devices.resize(count);
	}
	int best = -1;
	for (VkPhysicalDevice candidate : devices) {
		std::uint32_t familyCount = 0;
		vk.GetPhysicalDeviceQueueFamilyProperties(candidate, &familyCount, nullptr);
		std::vector<VkQueueFamilyProperties> families(familyCount);
		vk.GetPhysicalDeviceQueueFamilyProperties(candidate, &familyCount, families.data());
		for (std::uint32_t family = 0; family < familyCount; ++family) {
			if ((families[family].queueFlags & VK_QUEUE_COMPUTE_BIT) == 0 || families[family].queueCount == 0) {
				continue;
			}
			VkPhysicalDeviceProperties properties{};
			vk.GetPhysicalDeviceProperties(candidate, &properties);
			const int rank = deviceRank(properties.deviceType);
			if (rank > best) {
				best = rank;
				physical = candidate;
				queueFamily = family;
			}
			break;
		}
	}
	if (physical == VK_NULL_HANDLE) {
		return Error::make(ErrorCode::MissingHostFeature, "No Vulkan device with a compute queue is installed").with("devices", std::to_string(devices.size()));
	}
	VkPhysicalDeviceProperties properties{};
	vk.GetPhysicalDeviceProperties(physical, &properties);
	limits = properties.limits;
	info.name = properties.deviceName;
	info.apiVersion = properties.apiVersion;
	info.software = properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU;
	VkFormatProperties format{};
	vk.GetPhysicalDeviceFormatProperties(physical, VK_FORMAT_R32G32B32A32_SFLOAT, &format);
	info.float32Linear = (format.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT) != 0;
	vk.GetPhysicalDeviceMemoryProperties(physical, &memory);
	return Error::none();
}

Error Device::createDevice()
{
	// The features a kernel's SPIR-V may need: slangc declares untyped storage images, and a
	// uniforms struct with 8-bit, 16-bit, or half members needs the small-type features. The
	// 1.1 and 1.2 feature structs exist from Vulkan 1.2 on.
	VkPhysicalDeviceFeatures supported{};
	VkPhysicalDeviceVulkan11Features supported11{};
	supported11.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES;
	VkPhysicalDeviceVulkan12Features supported12{};
	supported12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
	const bool chained = info.apiVersion >= VK_API_VERSION_1_2 && vk.GetPhysicalDeviceFeatures2 != nullptr;
	if (chained) {
		VkPhysicalDeviceFeatures2 query{};
		query.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
		query.pNext = &supported11;
		supported11.pNext = &supported12;
		vk.GetPhysicalDeviceFeatures2(physical, &query);
		supported = query.features;
	} else {
		vk.GetPhysicalDeviceFeatures(physical, &supported);
	}
	VkPhysicalDeviceFeatures2 enable{};
	enable.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
	enable.features.shaderStorageImageReadWithoutFormat = supported.shaderStorageImageReadWithoutFormat;
	enable.features.shaderStorageImageWriteWithoutFormat = supported.shaderStorageImageWriteWithoutFormat;
	enable.features.shaderInt16 = supported.shaderInt16;
	enable.features.shaderInt64 = supported.shaderInt64;
	enable.features.shaderFloat64 = supported.shaderFloat64;
	VkPhysicalDeviceVulkan11Features enable11{};
	enable11.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES;
	enable11.storageBuffer16BitAccess = supported11.storageBuffer16BitAccess;
	enable11.uniformAndStorageBuffer16BitAccess = supported11.uniformAndStorageBuffer16BitAccess;
	VkPhysicalDeviceVulkan12Features enable12{};
	enable12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
	enable12.shaderInt8 = supported12.shaderInt8;
	enable12.shaderFloat16 = supported12.shaderFloat16;
	enable12.storageBuffer8BitAccess = supported12.storageBuffer8BitAccess;
	enable12.uniformAndStorageBuffer8BitAccess = supported12.uniformAndStorageBuffer8BitAccess;
	enable.pNext = &enable11;
	enable11.pNext = &enable12;
	shaderFeatures.imageReadWithoutFormat = supported.shaderStorageImageReadWithoutFormat == VK_TRUE;
	shaderFeatures.imageWriteWithoutFormat = supported.shaderStorageImageWriteWithoutFormat == VK_TRUE;
	shaderFeatures.int16 = supported.shaderInt16 == VK_TRUE;
	shaderFeatures.int64 = supported.shaderInt64 == VK_TRUE;
	shaderFeatures.float64 = supported.shaderFloat64 == VK_TRUE;
	shaderFeatures.int8 = supported12.shaderInt8 == VK_TRUE;
	shaderFeatures.float16 = supported12.shaderFloat16 == VK_TRUE;
	shaderFeatures.uniform8 = supported12.uniformAndStorageBuffer8BitAccess == VK_TRUE;
	shaderFeatures.uniform16 = supported11.uniformAndStorageBuffer16BitAccess == VK_TRUE;

	std::uint32_t count = 0;
	vk.EnumerateDeviceExtensionProperties(physical, nullptr, &count, nullptr);
	std::vector<VkExtensionProperties> extensions(count);
	if (count > 0) {
		vk.EnumerateDeviceExtensionProperties(physical, nullptr, &count, extensions.data());
		extensions.resize(count);
	}
	// The specification requires a device that lists the portability subset to have it enabled.
	std::vector<const char*> enabled;
	for (const VkExtensionProperties& extension : extensions) {
		if (std::strcmp(extension.extensionName, "VK_KHR_portability_subset") == 0) {
			enabled.push_back("VK_KHR_portability_subset");
		}
	}
	const float priority = 1.0f;
	VkDeviceQueueCreateInfo queueInfo{};
	queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
	queueInfo.queueFamilyIndex = queueFamily;
	queueInfo.queueCount = 1;
	queueInfo.pQueuePriorities = &priority;
	VkDeviceCreateInfo deviceInfo{};
	deviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
	deviceInfo.queueCreateInfoCount = 1;
	deviceInfo.pQueueCreateInfos = &queueInfo;
	deviceInfo.enabledExtensionCount = static_cast<std::uint32_t>(enabled.size());
	deviceInfo.ppEnabledExtensionNames = enabled.data();
	if (chained) {
		deviceInfo.pNext = &enable;
	} else {
		deviceInfo.pEnabledFeatures = &enable.features;
	}
	if (VkResult r = vk.CreateDevice(physical, &deviceInfo, nullptr, &device); r != VK_SUCCESS) {
		device = VK_NULL_HANDLE;
		return Error::make(ErrorCode::MissingHostFeature, "Vulkan created no device").with("vkResult", std::to_string(static_cast<int>(r))).with("device", info.name);
	}
#define X(name) vk.name = reinterpret_cast<PFN_vk##name>(vk.GetDeviceProcAddr(device, "vk" #name));
	OFXGRIP_VK_DEVICE_FUNCTIONS(X)
#undef X
#define X(name) \
	if (vk.name == nullptr) { \
		return Error::make(ErrorCode::MissingHostFeature, "The Vulkan device lacks a function").with("function", "vk" #name); \
	}
	OFXGRIP_VK_DEVICE_FUNCTIONS(X)
#undef X
	vk.GetDeviceQueue(device, queueFamily, 0, &queue);

	VkSamplerCreateInfo samplerInfo{};
	samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	samplerInfo.magFilter = VK_FILTER_LINEAR;
	samplerInfo.minFilter = VK_FILTER_LINEAR;
	samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
	samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	samplerInfo.maxLod = 0.0f;
	if (VkResult r = vk.CreateSampler(device, &samplerInfo, nullptr, &sampler); r != VK_SUCCESS) {
		sampler = VK_NULL_HANDLE;
		return vkError("Vulkan created no sampler", r);
	}
	return Error::none();
}

Error Device::pipelineFor(const VulkanJob& job, Pipeline& out)
{
	const std::vector<std::uint32_t> words = spirvWords(job.spirv);
	if (words.empty()) {
		return Error::make(ErrorCode::InvalidParameter, "The kernel's bytes are not a SPIR-V module").with("kernel", job.cacheKey);
	}
	if (vulkanVersionForSpirv(words[1]) > info.apiVersion) {
		return Error::make(ErrorCode::Unsupported, "The Vulkan device is older than the kernel's SPIR-V version needs").with("device", info.name).with("spirvVersion", std::to_string((words[1] >> 16) & 0xffu) + "." + std::to_string((words[1] >> 8) & 0xffu));
	}
	if (const char* capability = missingCapability(words, shaderFeatures)) {
		return Error::make(ErrorCode::Unsupported, "The Vulkan device lacks a SPIR-V capability the kernel declares").with("device", info.name).with("capability", capability);
	}
	const std::string entry = entryPointOf(words);
	if (entry.empty()) {
		return Error::make(ErrorCode::InvalidParameter, "The SPIR-V module declares no compute entry point").with("kernel", job.cacheKey);
	}

	// The descriptor layout and the constants are part of the pipeline, so they join the key.
	std::string key = job.cacheKey + "|" + std::to_string(fnv1a(job.spirv)) + "|" + std::to_string(job.spirv.size());
	std::vector<VkDescriptorSetLayoutBinding> bindings;
	auto addBinding = [&](std::uint32_t binding, VkDescriptorType type, char tag) {
		VkDescriptorSetLayoutBinding entryBinding{};
		entryBinding.binding = binding;
		entryBinding.descriptorType = type;
		entryBinding.descriptorCount = 1;
		entryBinding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
		bindings.push_back(entryBinding);
		key += '|';
		key += tag;
		key += std::to_string(binding);
	};
	for (const VulkanBufferBinding& buffer : job.buffers) {
		addBinding(buffer.binding, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 'u');
	}
	for (const VulkanSourceBinding& source : job.sources) {
		addBinding(source.binding, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 't');
	}
	for (const std::uint32_t binding : job.samplers) {
		addBinding(binding, VK_DESCRIPTOR_TYPE_SAMPLER, 's');
	}
	addBinding(job.destinationBinding, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 'd');

	std::vector<VkSpecializationMapEntry> entries;
	std::vector<std::uint8_t> constantData;
	for (const VulkanConstant& constant : job.constants) {
		const std::size_t length = std::min(constant.length, constant.bytes.size());
		VkSpecializationMapEntry mapEntry{};
		mapEntry.constantID = constant.id;
		mapEntry.offset = static_cast<std::uint32_t>(constantData.size());
		mapEntry.size = length;
		entries.push_back(mapEntry);
		constantData.insert(constantData.end(), constant.bytes.begin(), constant.bytes.begin() + static_cast<std::ptrdiff_t>(length));
		key += "|c" + std::to_string(constant.id) + "=";
		for (std::size_t i = 0; i < length; ++i) {
			key += std::to_string(static_cast<unsigned>(constant.bytes[i])) + ".";
		}
	}

	std::lock_guard<std::mutex> lock(cacheMutex);
	if (auto it = pipelines.find(key); it != pipelines.end()) {
		out = it->second;
		return Error::none();
	}
	Pipeline built;
	VkShaderModuleCreateInfo moduleInfo{};
	moduleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	moduleInfo.codeSize = words.size() * sizeof(std::uint32_t);
	moduleInfo.pCode = words.data();
	if (VkResult r = vk.CreateShaderModule(device, &moduleInfo, nullptr, &built.module); r != VK_SUCCESS) {
		return vkError("Vulkan refused the kernel's SPIR-V module", r).with("kernel", job.cacheKey);
	}
	VkDescriptorSetLayoutCreateInfo setInfo{};
	setInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	setInfo.bindingCount = static_cast<std::uint32_t>(bindings.size());
	setInfo.pBindings = bindings.data();
	if (VkResult r = vk.CreateDescriptorSetLayout(device, &setInfo, nullptr, &built.setLayout); r != VK_SUCCESS) {
		destroy(built);
		return vkError("Vulkan created no descriptor set layout", r).with("kernel", job.cacheKey);
	}
	VkPipelineLayoutCreateInfo layoutInfo{};
	layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	layoutInfo.setLayoutCount = 1;
	layoutInfo.pSetLayouts = &built.setLayout;
	if (VkResult r = vk.CreatePipelineLayout(device, &layoutInfo, nullptr, &built.layout); r != VK_SUCCESS) {
		destroy(built);
		return vkError("Vulkan created no pipeline layout", r).with("kernel", job.cacheKey);
	}
	VkSpecializationInfo specialization{};
	specialization.mapEntryCount = static_cast<std::uint32_t>(entries.size());
	specialization.pMapEntries = entries.data();
	specialization.dataSize = constantData.size();
	specialization.pData = constantData.data();
	VkComputePipelineCreateInfo pipelineInfo{};
	pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
	pipelineInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	pipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
	pipelineInfo.stage.module = built.module;
	pipelineInfo.stage.pName = entry.c_str();
	pipelineInfo.stage.pSpecializationInfo = entries.empty() ? nullptr : &specialization;
	pipelineInfo.layout = built.layout;
	if (VkResult r = vk.CreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &built.pipeline); r != VK_SUCCESS) {
		built.pipeline = VK_NULL_HANDLE;
		destroy(built);
		return vkError("Vulkan compiled no compute pipeline for the kernel", r).with("kernel", job.cacheKey);
	}
	pipelines.emplace(std::move(key), built);
	out = built;
	return Error::none();
}

}  // namespace

// ---- VulkanContext ------------------------------------------------------------------------------

struct VulkanContext::Impl : Device {};

VulkanContext::VulkanContext(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

VulkanContext::~VulkanContext() = default;

namespace {

struct SharedDevice {
	std::once_flag once;
	std::shared_ptr<VulkanContext> context;
	std::string reason;
};

SharedDevice& sharedDevice()
{
	static SharedDevice device;
	return device;
}

}  // namespace

Error VulkanContext::open(const std::string& libraryPath, std::shared_ptr<VulkanContext>& out)
{
	auto impl = std::make_unique<Impl>();
	if (Error e = impl->open(libraryPath)) {
		return e;
	}
	out = std::shared_ptr<VulkanContext>(new VulkanContext(std::move(impl)));
	return Error::none();
}

std::shared_ptr<VulkanContext> VulkanContext::shared()
{
	SharedDevice& device = sharedDevice();
	std::call_once(device.once, [&device]() {
		if (Error e = open(std::string(), device.context)) {
			device.reason = e.describe();
		}
	});
	return device.context;
}

bool VulkanContext::available() noexcept
{
	try {
		return shared() != nullptr;
	} catch (...) {
		return false;
	}
}

std::string VulkanContext::unavailableReason()
{
	return sharedDevice().reason;
}

const VulkanDeviceInfo& VulkanContext::info() const noexcept
{
	return impl_->info;
}

std::size_t VulkanContext::cachedPipelineCount() const
{
	std::lock_guard<std::mutex> lock(impl_->cacheMutex);
	return impl_->pipelines.size();
}

Error VulkanContext::run(const VulkanJob& job)
{
	Device& c = *impl_;
	const Functions& vk = c.vk;
	if (job.destination == nullptr || job.width == 0 || job.height == 0) {
		return Error::make(ErrorCode::InvalidParameter, "The Vulkan job has no destination");
	}
	if (job.groupSize[0] == 0 || job.groupSize[1] == 0) {
		return Error::make(ErrorCode::InvalidParameter, "The Vulkan job has an empty threadgroup");
	}
	if (job.width > c.limits.maxImageDimension2D || job.height > c.limits.maxImageDimension2D) {
		return Error::make(ErrorCode::Unsupported, "The destination is larger than the Vulkan device's images").with("limit", std::to_string(c.limits.maxImageDimension2D));
	}
	Pipeline pipeline;
	if (Error e = c.pipelineFor(job, pipeline)) {
		return e;
	}

	// One host-visible buffer holds the uniform blocks, the source uploads, and the readback.
	const VkFormat sourceFormat = c.info.float32Linear ? VK_FORMAT_R32G32B32A32_SFLOAT : VK_FORMAT_R16G16B16A16_SFLOAT;
	const VkDeviceSize sourceTexelBytes = c.info.float32Linear ? 16 : 8;
	const VkDeviceSize alignment = std::max<VkDeviceSize>({16, c.limits.minUniformBufferOffsetAlignment, c.limits.optimalBufferCopyOffsetAlignment});
	VkDeviceSize cursor = 0;
	std::vector<VkDeviceSize> bufferOffsets;
	for (const VulkanBufferBinding& buffer : job.buffers) {
		bufferOffsets.push_back(cursor);
		cursor = alignUp(cursor + std::max<VkDeviceSize>(buffer.length, 16), alignment);
	}
	struct Source {
		VkDeviceSize offset;
		unsigned width;
		unsigned height;
	};
	std::vector<Source> sources;
	for (const VulkanSourceBinding& source : job.sources) {
		const bool bound = source.rgba != nullptr && source.width > 0 && source.height > 0;
		const unsigned width = bound ? source.width : 1;
		const unsigned height = bound ? source.height : 1;
		if (width > c.limits.maxImageDimension2D || height > c.limits.maxImageDimension2D) {
			return Error::make(ErrorCode::Unsupported, "A source is larger than the Vulkan device's images").with("limit", std::to_string(c.limits.maxImageDimension2D));
		}
		sources.push_back({cursor, width, height});
		cursor = alignUp(cursor + static_cast<VkDeviceSize>(width) * height * sourceTexelBytes, alignment);
	}
	const VkDeviceSize readbackOffset = cursor;
	const VkDeviceSize readbackBytes = static_cast<VkDeviceSize>(job.width) * job.height * 16;
	cursor += readbackBytes;

	JobResources r(c);
	VkBufferCreateInfo bufferInfo{};
	bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bufferInfo.size = cursor;
	bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
	bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	if (VkResult result = vk.CreateBuffer(c.device, &bufferInfo, nullptr, &r.staging); result != VK_SUCCESS) {
		r.staging = VK_NULL_HANDLE;
		return vkError("Vulkan created no staging buffer", result);
	}
	VkMemoryRequirements needs{};
	vk.GetBufferMemoryRequirements(c.device, r.staging, &needs);
	VkMemoryAllocateInfo allocation{};
	allocation.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	allocation.allocationSize = needs.size;
	if (!c.memoryType(needs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, allocation.memoryTypeIndex)) {
		return Error::make(ErrorCode::Memory, "The Vulkan device has no host-visible memory type");
	}
	if (VkResult result = vk.AllocateMemory(c.device, &allocation, nullptr, &r.stagingMemory); result != VK_SUCCESS) {
		r.stagingMemory = VK_NULL_HANDLE;
		return Error::make(ErrorCode::Memory, "Vulkan allocated no staging memory").with("vkResult", std::to_string(static_cast<int>(result)));
	}
	if (VkResult result = vk.BindBufferMemory(c.device, r.staging, r.stagingMemory, 0); result != VK_SUCCESS) {
		return vkError("Vulkan bound no staging memory", result);
	}
	if (VkResult result = vk.MapMemory(c.device, r.stagingMemory, 0, VK_WHOLE_SIZE, 0, &r.mapped); result != VK_SUCCESS) {
		r.mapped = nullptr;
		return vkError("Vulkan mapped no staging memory", result);
	}
	auto* bytes = static_cast<std::uint8_t*>(r.mapped);
	for (std::size_t i = 0; i < job.buffers.size(); ++i) {
		const VulkanBufferBinding& buffer = job.buffers[i];
		std::memset(bytes + bufferOffsets[i], 0, static_cast<std::size_t>(std::max<VkDeviceSize>(buffer.length, 16)));
		if (buffer.data != nullptr && buffer.length > 0) {
			std::memcpy(bytes + bufferOffsets[i], buffer.data, buffer.length);
		}
	}
	for (std::size_t i = 0; i < job.sources.size(); ++i) {
		const VulkanSourceBinding& source = job.sources[i];
		const std::size_t components = static_cast<std::size_t>(sources[i].width) * sources[i].height * 4;
		std::uint8_t* at = bytes + sources[i].offset;
		const bool bound = source.rgba != nullptr && source.width > 0 && source.height > 0;
		if (!bound) {
			std::memset(at, 0, static_cast<std::size_t>(sourceTexelBytes));
		} else if (c.info.float32Linear) {
			std::memcpy(at, source.rgba, components * sizeof(float));
		} else {
			for (std::size_t k = 0; k < components; ++k) {
				const std::uint16_t half = floatToHalf(source.rgba[k]);
				std::memcpy(at + k * sizeof half, &half, sizeof half);
			}
		}
	}

	std::vector<VkImage> sourceImages(job.sources.size(), VK_NULL_HANDLE);
	std::vector<VkImageView> sourceViews(job.sources.size(), VK_NULL_HANDLE);
	for (std::size_t i = 0; i < job.sources.size(); ++i) {
		if (Error e = r.image(sources[i].width, sources[i].height, sourceFormat, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, sourceImages[i], sourceViews[i])) {
			return e;
		}
	}
	VkImage destinationImage = VK_NULL_HANDLE;
	VkImageView destinationView = VK_NULL_HANDLE;
	if (Error e = r.image(job.width, job.height, VK_FORMAT_R32G32B32A32_SFLOAT, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, destinationImage, destinationView)) {
		return e;
	}

	// The descriptors.
	std::vector<VkDescriptorPoolSize> poolSizes;
	auto addPool = [&poolSizes](VkDescriptorType type, std::size_t count) {
		if (count > 0) {
			poolSizes.push_back({type, static_cast<std::uint32_t>(count)});
		}
	};
	addPool(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, job.buffers.size());
	addPool(VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, job.sources.size());
	addPool(VK_DESCRIPTOR_TYPE_SAMPLER, job.samplers.size());
	addPool(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1);
	VkDescriptorPoolCreateInfo poolInfo{};
	poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	poolInfo.maxSets = 1;
	poolInfo.poolSizeCount = static_cast<std::uint32_t>(poolSizes.size());
	poolInfo.pPoolSizes = poolSizes.data();
	if (VkResult result = vk.CreateDescriptorPool(c.device, &poolInfo, nullptr, &r.descriptorPool); result != VK_SUCCESS) {
		r.descriptorPool = VK_NULL_HANDLE;
		return vkError("Vulkan created no descriptor pool", result);
	}
	VkDescriptorSetAllocateInfo setInfo{};
	setInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	setInfo.descriptorPool = r.descriptorPool;
	setInfo.descriptorSetCount = 1;
	setInfo.pSetLayouts = &pipeline.setLayout;
	VkDescriptorSet set = VK_NULL_HANDLE;
	if (VkResult result = vk.AllocateDescriptorSets(c.device, &setInfo, &set); result != VK_SUCCESS) {
		return vkError("Vulkan allocated no descriptor set", result);
	}
	// The info arrays are sized up front: a write holds a pointer into them.
	std::vector<VkDescriptorBufferInfo> bufferInfos(job.buffers.size());
	std::vector<VkDescriptorImageInfo> imageInfos(job.sources.size() + job.samplers.size() + 1);
	std::vector<VkWriteDescriptorSet> writes;
	auto write = [&](std::uint32_t binding, VkDescriptorType type) -> VkWriteDescriptorSet& {
		VkWriteDescriptorSet entry{};
		entry.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		entry.dstSet = set;
		entry.dstBinding = binding;
		entry.descriptorCount = 1;
		entry.descriptorType = type;
		writes.push_back(entry);
		return writes.back();
	};
	writes.reserve(bufferInfos.size() + imageInfos.size());
	for (std::size_t i = 0; i < job.buffers.size(); ++i) {
		bufferInfos[i] = {r.staging, bufferOffsets[i], std::max<VkDeviceSize>(job.buffers[i].length, 16)};
		write(job.buffers[i].binding, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER).pBufferInfo = &bufferInfos[i];
	}
	std::size_t imageAt = 0;
	for (std::size_t i = 0; i < job.sources.size(); ++i) {
		imageInfos[imageAt] = {VK_NULL_HANDLE, sourceViews[i], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
		write(job.sources[i].binding, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE).pImageInfo = &imageInfos[imageAt++];
	}
	for (const std::uint32_t binding : job.samplers) {
		imageInfos[imageAt] = {c.sampler, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED};
		write(binding, VK_DESCRIPTOR_TYPE_SAMPLER).pImageInfo = &imageInfos[imageAt++];
	}
	imageInfos[imageAt] = {VK_NULL_HANDLE, destinationView, VK_IMAGE_LAYOUT_GENERAL};
	write(job.destinationBinding, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE).pImageInfo = &imageInfos[imageAt];
	vk.UpdateDescriptorSets(c.device, static_cast<std::uint32_t>(writes.size()), writes.data(), 0, nullptr);

	// The commands: upload, dispatch, read back.
	VkCommandPoolCreateInfo commandPoolInfo{};
	commandPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	commandPoolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
	commandPoolInfo.queueFamilyIndex = c.queueFamily;
	if (VkResult result = vk.CreateCommandPool(c.device, &commandPoolInfo, nullptr, &r.commandPool); result != VK_SUCCESS) {
		r.commandPool = VK_NULL_HANDLE;
		return vkError("Vulkan created no command pool", result);
	}
	VkCommandBufferAllocateInfo commandInfo{};
	commandInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	commandInfo.commandPool = r.commandPool;
	commandInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	commandInfo.commandBufferCount = 1;
	VkCommandBuffer command = VK_NULL_HANDLE;
	if (VkResult result = vk.AllocateCommandBuffers(c.device, &commandInfo, &command); result != VK_SUCCESS) {
		return vkError("Vulkan allocated no command buffer", result);
	}
	VkCommandBufferBeginInfo begin{};
	begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	if (VkResult result = vk.BeginCommandBuffer(command, &begin); result != VK_SUCCESS) {
		return vkError("Vulkan began no command buffer", result);
	}
	for (std::size_t i = 0; i < sourceImages.size(); ++i) {
		transition(vk, command, sourceImages[i], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
		const VkBufferImageCopy region = wholeImage(sources[i].offset, sources[i].width, sources[i].height);
		vk.CmdCopyBufferToImage(command, r.staging, sourceImages[i], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
		transition(vk, command, sourceImages[i], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
	}
	// A texel the kernel leaves unwritten reads back as transparent black on every device.
	transition(vk, command, destinationImage, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, 0, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
	const VkClearColorValue clear{};
	const VkImageSubresourceRange whole{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
	vk.CmdClearColorImage(command, destinationImage, VK_IMAGE_LAYOUT_GENERAL, &clear, 1, &whole);
	transition(vk, command, destinationImage, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
	vk.CmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.pipeline);
	vk.CmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.layout, 0, 1, &set, 0, nullptr);
	const std::uint32_t groupsX = (job.width + job.groupSize[0] - 1) / job.groupSize[0];
	const std::uint32_t groupsY = (job.height + job.groupSize[1] - 1) / job.groupSize[1];
	if (groupsX > c.limits.maxComputeWorkGroupCount[0] || groupsY > c.limits.maxComputeWorkGroupCount[1]) {
		return Error::make(ErrorCode::Unsupported, "The destination needs more threadgroups than the Vulkan device dispatches");
	}
	vk.CmdDispatch(command, groupsX, groupsY, 1);
	transition(vk, command, destinationImage, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
	const VkBufferImageCopy readback = wholeImage(readbackOffset, job.width, job.height);
	vk.CmdCopyImageToBuffer(command, destinationImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, r.staging, 1, &readback);
	if (VkResult result = vk.EndCommandBuffer(command); result != VK_SUCCESS) {
		return vkError("Vulkan ended no command buffer", result);
	}

	VkFenceCreateInfo fenceInfo{};
	fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	if (VkResult result = vk.CreateFence(c.device, &fenceInfo, nullptr, &r.fence); result != VK_SUCCESS) {
		r.fence = VK_NULL_HANDLE;
		return vkError("Vulkan created no fence", result);
	}
	VkSubmitInfo submit{};
	submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submit.commandBufferCount = 1;
	submit.pCommandBuffers = &command;
	{
		std::lock_guard<std::mutex> lock(c.queueMutex);
		if (VkResult result = vk.QueueSubmit(c.queue, 1, &submit, r.fence); result != VK_SUCCESS) {
			return vkError("Vulkan accepted no submission", result);
		}
	}
	if (VkResult result = vk.WaitForFences(c.device, 1, &r.fence, VK_TRUE, kJobTimeoutNanoseconds); result != VK_SUCCESS) {
		// The device still owns the job's resources; idle it before they are released.
		vk.DeviceWaitIdle(c.device);
		return vkError("The Vulkan job did not finish", result);
	}
	std::memcpy(job.destination, bytes + readbackOffset, static_cast<std::size_t>(readbackBytes));
	return Error::none();
}

std::string spirvComputeEntryPoint(std::string_view spirv)
{
	return entryPointOf(spirvWords(spirv));
}

}  // namespace vulkan

OFXGRIP_NAMESPACE_END
