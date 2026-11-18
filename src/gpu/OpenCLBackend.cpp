// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
#include <ofxgrip/gpu/OpenCLBackend.hpp>

#define CL_SILENCE_DEPRECATION 1
#define CL_TARGET_OPENCL_VERSION 120
#define CL_USE_DEPRECATED_OPENCL_1_2_APIS 1
#if defined(__APPLE__)
#include <OpenCL/opencl.h>
#else
#include <CL/cl.h>
#endif

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
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
namespace opencl {

namespace {

// Every OpenCL function the backend calls. The library is opened at run time, so the header
// supplies the types and the signatures only.
#define OFXGRIP_OPENCL_FUNCTIONS(X) \
	X(clGetPlatformIDs) \
	X(clGetDeviceIDs) \
	X(clGetDeviceInfo) \
	X(clCreateContext) \
	X(clRetainContext) \
	X(clReleaseContext) \
	X(clCreateCommandQueue) \
	X(clRetainCommandQueue) \
	X(clReleaseCommandQueue) \
	X(clGetCommandQueueInfo) \
	X(clCreateBuffer) \
	X(clReleaseMemObject) \
	X(clCreateProgramWithSource) \
	X(clBuildProgram) \
	X(clGetProgramBuildInfo) \
	X(clReleaseProgram) \
	X(clCreateKernel) \
	X(clReleaseKernel) \
	X(clSetKernelArg) \
	X(clEnqueueNDRangeKernel) \
	X(clEnqueueCopyBuffer) \
	X(clEnqueueReadBuffer) \
	X(clEnqueueWriteBuffer) \
	X(clFinish)

struct Functions {
#define OFXGRIP_OPENCL_MEMBER(name) decltype(&::name) name{nullptr};
	OFXGRIP_OPENCL_FUNCTIONS(OFXGRIP_OPENCL_MEMBER)
#undef OFXGRIP_OPENCL_MEMBER
};

void* openLibrary(const std::string& path) noexcept
{
#if defined(_WIN32)
	return static_cast<void*>(::LoadLibraryA(path.c_str()));
#else
	return ::dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
#endif
}

void closeLibrary(void* library) noexcept
{
#if defined(_WIN32)
	::FreeLibrary(static_cast<HMODULE>(library));
#else
	::dlclose(library);
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
	if (const char* named = std::getenv("OFXGRIP_OPENCL_LIBRARY"); named != nullptr && named[0] != '\0') {
		names.emplace_back(named);
		return names;
	}
#if defined(_WIN32)
	names.emplace_back("OpenCL.dll");
#elif defined(__APPLE__)
	names.emplace_back("/System/Library/Frameworks/OpenCL.framework/OpenCL");
#else
	// A host that renders with OpenCL has the library loaded, and the open finds that copy by
	// its name wherever the host keeps it.
	names.emplace_back("libOpenCL.so.1");
	names.emplace_back("libOpenCL.so");
#endif
	return names;
}

/*! Opens the first library of `names` that exists and resolves every function from it. The
    library stays open when the result is no error. */
Error loadFunctions(const std::vector<std::string>& names, Functions& out, void*& library)
{
	library = nullptr;
	std::string opened;
	for (const std::string& name : names) {
		library = openLibrary(name);
		if (library != nullptr) {
			opened = name;
			break;
		}
	}
	if (library == nullptr) {
		return Error::make(ErrorCode::MissingHostFeature, "No OpenCL library is installed").with("library", names.empty() ? std::string() : names.front());
	}
	const char* missing = nullptr;
#define OFXGRIP_OPENCL_RESOLVE(name) \
	out.name = reinterpret_cast<decltype(out.name)>(librarySymbol(library, #name)); \
	if (out.name == nullptr && missing == nullptr) { \
		missing = #name; \
	}
	OFXGRIP_OPENCL_FUNCTIONS(OFXGRIP_OPENCL_RESOLVE)
#undef OFXGRIP_OPENCL_RESOLVE
	if (missing != nullptr) {
		closeLibrary(library);
		library = nullptr;
		out = Functions{};
		return Error::make(ErrorCode::MissingHostFeature, "The library lacks an OpenCL function").with("library", opened).with("function", missing);
	}
	return Error::none();
}

/*! The process's OpenCL runtime: the function table, or the reason there is none. */
struct Runtime {
	Functions functions;
	bool loaded{false};
	std::string reason;
};

const Runtime& runtime()
{
	static const Runtime shared = [] {
		Runtime made;
		void* library = nullptr;
		if (Error e = loadFunctions(defaultLibraryNames(), made.functions, library)) {
			made.reason = e.describe();
			return made;
		}
		made.loaded = true;
		return made;
	}();
	return shared;
}

/*! The loaded functions. Every caller holds an OpenCL object, which exists only when the
    runtime loaded, or has checked `runtime().loaded`. */
const Functions& cl()
{
	return runtime().functions;
}

Error clError(cl_int status, const char* what)
{
	return Error::make(ErrorCode::Render, std::string("OpenCL: ") + what + " failed", "status " + std::to_string(status));
}

std::mutex& registryMutex()
{
	static std::mutex m;
	return m;
}

std::map<void*, std::shared_ptr<OpenCLContext>>& contexts()
{
	static std::map<void*, std::shared_ptr<OpenCLContext>> table;
	return table;
}

/*! The first GPU device of any platform. Off macOS, the first CPU device when no GPU has a
    driver (pocl on a machine or a CI runner with no GPU). */
cl_device_id firstDevice()
{
	if (!runtime().loaded) {
		return nullptr;
	}
	cl_uint platformCount = 0;
	if (cl().clGetPlatformIDs(0, nullptr, &platformCount) != CL_SUCCESS || platformCount == 0) {
		return nullptr;
	}
	std::vector<cl_platform_id> platforms(platformCount);
	cl().clGetPlatformIDs(platformCount, platforms.data(), nullptr);
	// Apple's CPU device, which an x86_64 process sees under Rosetta, refuses a kernel with a
	// struct argument (CL_INVALID_KERNEL_ARGS), so macOS takes a GPU device or none.
#if defined(__APPLE__)
	const std::vector<cl_device_type> types{CL_DEVICE_TYPE_GPU};
#else
	const std::vector<cl_device_type> types{CL_DEVICE_TYPE_GPU, CL_DEVICE_TYPE_CPU};
#endif
	for (const cl_device_type type : types) {
		for (cl_platform_id platform : platforms) {
			cl_device_id device = nullptr;
			cl_uint count = 0;
			if (cl().clGetDeviceIDs(platform, type, 1, &device, &count) == CL_SUCCESS && count > 0) {
				return device;
			}
		}
	}
	return nullptr;
}

}  // namespace

struct OpenCLKernel::Impl {
	cl_kernel kernel{nullptr};
	std::string name;
	~Impl()
	{
		if (kernel != nullptr) {
			cl().clReleaseKernel(kernel);
		}
	}
};

void* OpenCLKernel::handle() const noexcept
{
	return impl_ ? impl_->kernel : nullptr;
}

const std::string& OpenCLKernel::name() const noexcept
{
	static const std::string none;
	return impl_ ? impl_->name : none;
}

struct OpenCLBuffer::Impl {
	cl_mem buffer{nullptr};
	std::size_t length{0};
	std::weak_ptr<OpenCLContext> owner;
	bool pooled{false};
	~Impl();
};

void* OpenCLBuffer::handle() const noexcept
{
	return impl_ ? impl_->buffer : nullptr;
}

std::size_t OpenCLBuffer::length() const noexcept
{
	return impl_ ? impl_->length : 0;
}

struct OpenCLContext::Impl {
	cl_command_queue queue{nullptr};
	cl_context context{nullptr};
	cl_device_id device{nullptr};
	bool ownsQueue{false};
	std::mutex programMutex;
	std::map<std::string, cl_program> programs;
	std::mutex poolMutex;
	std::vector<std::pair<cl_mem, std::size_t>> pool;

	~Impl()
	{
		for (auto& [name, program] : programs) {
			cl().clReleaseProgram(program);
		}
		for (auto& [buffer, length] : pool) {
			cl().clReleaseMemObject(buffer);
		}
		if (queue != nullptr) {
			cl().clReleaseCommandQueue(queue);
		}
		if (context != nullptr) {
			cl().clReleaseContext(context);
		}
	}

	void returnToPool(cl_mem buffer, std::size_t length)
	{
		std::lock_guard<std::mutex> lock(poolMutex);
		pool.emplace_back(buffer, length);
	}
};

OpenCLBuffer::Impl::~Impl()
{
	if (buffer == nullptr) {
		return;
	}
	if (pooled) {
		if (std::shared_ptr<OpenCLContext> context = owner.lock()) {
			context->impl_->returnToPool(buffer, length);
			return;
		}
	}
	cl().clReleaseMemObject(buffer);
}

OpenCLContext::OpenCLContext() : impl_(std::make_unique<Impl>()) {}

OpenCLContext::~OpenCLContext() = default;

bool OpenCLContext::available() noexcept
{
	return systemDefault() != nullptr;
}

bool OpenCLContext::runtimeAvailable() noexcept
{
	return runtime().loaded;
}

std::string OpenCLContext::runtimeUnavailableReason()
{
	return runtime().reason;
}

Error OpenCLContext::probeRuntime(const std::string& libraryPath)
{
	Functions functions;
	void* library = nullptr;
	if (Error e = loadFunctions(libraryPath.empty() ? defaultLibraryNames() : std::vector<std::string>{libraryPath}, functions, library)) {
		return e;
	}
	closeLibrary(library);
	return Error::none();
}

std::shared_ptr<OpenCLContext> OpenCLContext::forQueue(void* commandQueue)
{
	if (commandQueue == nullptr || !runtime().loaded) {
		return nullptr;
	}
	std::lock_guard<std::mutex> lock(registryMutex());
	std::shared_ptr<OpenCLContext>& entry = contexts()[commandQueue];
	if (!entry) {
		auto queue = static_cast<cl_command_queue>(commandQueue);
		cl_context context = nullptr;
		cl_device_id device = nullptr;
		if (cl().clGetCommandQueueInfo(queue, CL_QUEUE_CONTEXT, sizeof context, &context, nullptr) != CL_SUCCESS || cl().clGetCommandQueueInfo(queue, CL_QUEUE_DEVICE, sizeof device, &device, nullptr) != CL_SUCCESS) {
			contexts().erase(commandQueue);
			return nullptr;
		}
		std::shared_ptr<OpenCLContext> made(new OpenCLContext());
		cl().clRetainCommandQueue(queue);
		cl().clRetainContext(context);
		made->impl_->queue = queue;
		made->impl_->context = context;
		made->impl_->device = device;
		entry = made;
	}
	return entry;
}

std::shared_ptr<OpenCLContext> OpenCLContext::systemDefault()
{
	static std::shared_ptr<OpenCLContext> context = []() -> std::shared_ptr<OpenCLContext> {
		cl_device_id device = firstDevice();
		if (device == nullptr) {
			return nullptr;
		}
		cl_int status = CL_SUCCESS;
		cl_context ctx = cl().clCreateContext(nullptr, 1, &device, nullptr, nullptr, &status);
		if (ctx == nullptr) {
			return nullptr;
		}
		cl_command_queue queue = cl().clCreateCommandQueue(ctx, device, 0, &status);
		if (queue == nullptr) {
			cl().clReleaseContext(ctx);
			return nullptr;
		}
		std::shared_ptr<OpenCLContext> made(new OpenCLContext());
		made->impl_->queue = queue;
		made->impl_->context = ctx;
		made->impl_->device = device;
		made->impl_->ownsQueue = true;
		std::lock_guard<std::mutex> lock(registryMutex());
		contexts()[queue] = made;
		return made;
	}();
	return context;
}

void* OpenCLContext::queue() const noexcept
{
	return impl_->queue;
}

void* OpenCLContext::context() const noexcept
{
	return impl_->context;
}

void* OpenCLContext::device() const noexcept
{
	return impl_->device;
}

std::string OpenCLContext::deviceName() const
{
	char name[256] = {};
	if (cl().clGetDeviceInfo(impl_->device, CL_DEVICE_NAME, sizeof name - 1, name, nullptr) != CL_SUCCESS) {
		return {};
	}
	return name;
}

Error OpenCLContext::kernel(std::string_view programName, std::string_view source, std::string_view kernelName, OpenCLKernel& out)
{
	cl_program program = nullptr;
	{
		std::lock_guard<std::mutex> lock(impl_->programMutex);
		const std::string key(programName);
		if (auto it = impl_->programs.find(key); it != impl_->programs.end()) {
			program = it->second;
		} else {
			const char* text = source.data();
			const std::size_t length = source.size();
			cl_int status = CL_SUCCESS;
			program = cl().clCreateProgramWithSource(impl_->context, 1, &text, &length, &status);
			if (program == nullptr) {
				return clError(status, "clCreateProgramWithSource").with("program", key);
			}
			status = cl().clBuildProgram(program, 1, &impl_->device, "", nullptr, nullptr);
			if (status != CL_SUCCESS) {
				std::size_t logLength = 0;
				cl().clGetProgramBuildInfo(program, impl_->device, CL_PROGRAM_BUILD_LOG, 0, nullptr, &logLength);
				std::string log(logLength, '\0');
				if (logLength > 0) {
					cl().clGetProgramBuildInfo(program, impl_->device, CL_PROGRAM_BUILD_LOG, logLength, log.data(), nullptr);
				}
				cl().clReleaseProgram(program);
				return Error::make(ErrorCode::Render, "The OpenCL program did not build", log).with("program", key);
			}
			impl_->programs[key] = program;
		}
	}
	cl_int status = CL_SUCCESS;
	const std::string name(kernelName);
	cl_kernel kernel = cl().clCreateKernel(program, name.c_str(), &status);
	if (kernel == nullptr) {
		return Error::make(ErrorCode::NotFound, "The OpenCL program has no such kernel").with("program", std::string(programName)).with("kernel", name);
	}
	out.impl_ = std::make_shared<OpenCLKernel::Impl>();
	out.impl_->kernel = kernel;
	out.impl_->name = name;
	return Error::none();
}

Error OpenCLContext::enqueue(const OpenCLDispatch& dispatch)
{
	if (!dispatch.kernel.valid()) {
		return Error::make(ErrorCode::BadHandle, "The dispatch has no kernel");
	}
	if (dispatch.globalSize[0] == 0 || dispatch.globalSize[1] == 0) {
		return Error::none();
	}
	auto kernel = static_cast<cl_kernel>(dispatch.kernel.handle());
	for (const OpenCLDispatch::BufferArg& arg : dispatch.buffers) {
		if (arg.handle == nullptr) {
			return Error::make(ErrorCode::BadHandle, "A dispatch buffer argument is null").with("index", std::to_string(arg.index));
		}
		auto buffer = static_cast<cl_mem>(arg.handle);
		if (cl_int status = cl().clSetKernelArg(kernel, arg.index, sizeof buffer, &buffer); status != CL_SUCCESS) {
			return clError(status, "clSetKernelArg").with("index", std::to_string(arg.index));
		}
	}
	for (const OpenCLDispatch::BytesArg& arg : dispatch.bytes) {
		if (cl_int status = cl().clSetKernelArg(kernel, arg.index, arg.length, arg.data); status != CL_SUCCESS) {
			return clError(status, "clSetKernelArg").with("index", std::to_string(arg.index));
		}
	}
	const bool local = dispatch.localSize[0] != 0 && dispatch.localSize[1] != 0;
	std::size_t global[2] = {dispatch.globalSize[0], dispatch.globalSize[1]};
	if (local) {
		// Round the grid up to whole workgroups; the kernel bounds-checks.
		global[0] = (global[0] + dispatch.localSize[0] - 1) / dispatch.localSize[0] * dispatch.localSize[0];
		global[1] = (global[1] + dispatch.localSize[1] - 1) / dispatch.localSize[1] * dispatch.localSize[1];
	}
	if (cl_int status = cl().clEnqueueNDRangeKernel(impl_->queue, kernel, 2, nullptr, global, local ? dispatch.localSize : nullptr, 0, nullptr, nullptr); status != CL_SUCCESS) {
		return clError(status, "clEnqueueNDRangeKernel");
	}
	return Error::none();
}

Error OpenCLContext::copy(void* sourceBuffer, std::size_t sourceOffset, void* destinationBuffer, std::size_t destinationOffset, std::size_t length)
{
	if (sourceBuffer == nullptr || destinationBuffer == nullptr) {
		return Error::make(ErrorCode::BadHandle, "A copy buffer is null");
	}
	if (length == 0) {
		return Error::none();
	}
	if (cl_int status = cl().clEnqueueCopyBuffer(impl_->queue, static_cast<cl_mem>(sourceBuffer), static_cast<cl_mem>(destinationBuffer), sourceOffset, destinationOffset, length, 0, nullptr, nullptr); status != CL_SUCCESS) {
		return clError(status, "clEnqueueCopyBuffer");
	}
	return Error::none();
}

Error OpenCLContext::fill(void* buffer, std::size_t offset, std::size_t length, std::uint8_t value)
{
	if (buffer == nullptr) {
		return Error::make(ErrorCode::BadHandle, "The fill buffer is null");
	}
	if (length == 0) {
		return Error::none();
	}
	// A blocking write of a host pattern: clEnqueueFillBuffer writes wrong bytes on some
	// implementations (seen on Apple's), and a fill is never on a render path.
	const std::vector<std::uint8_t> pattern(length, value);
	if (cl_int status = cl().clEnqueueWriteBuffer(impl_->queue, static_cast<cl_mem>(buffer), CL_TRUE, offset, length, pattern.data(), 0, nullptr, nullptr); status != CL_SUCCESS) {
		return clError(status, "clEnqueueWriteBuffer");
	}
	return Error::none();
}

Error OpenCLContext::read(void* buffer, std::size_t offset, std::size_t length, void* destination)
{
	if (buffer == nullptr || destination == nullptr) {
		return Error::make(ErrorCode::BadHandle, "A read operand is null");
	}
	if (length == 0) {
		return Error::none();
	}
	if (cl_int status = cl().clEnqueueReadBuffer(impl_->queue, static_cast<cl_mem>(buffer), CL_TRUE, offset, length, destination, 0, nullptr, nullptr); status != CL_SUCCESS) {
		return clError(status, "clEnqueueReadBuffer");
	}
	return Error::none();
}

Error OpenCLContext::write(void* buffer, std::size_t offset, std::size_t length, const void* source)
{
	if (buffer == nullptr || source == nullptr) {
		return Error::make(ErrorCode::BadHandle, "A write operand is null");
	}
	if (length == 0) {
		return Error::none();
	}
	if (cl_int status = cl().clEnqueueWriteBuffer(impl_->queue, static_cast<cl_mem>(buffer), CL_TRUE, offset, length, source, 0, nullptr, nullptr); status != CL_SUCCESS) {
		return clError(status, "clEnqueueWriteBuffer");
	}
	return Error::none();
}

Error OpenCLContext::finish()
{
	if (cl_int status = cl().clFinish(impl_->queue); status != CL_SUCCESS) {
		return clError(status, "clFinish");
	}
	return Error::none();
}

OpenCLBuffer OpenCLContext::scratch(std::size_t length)
{
	cl_mem found = nullptr;
	std::size_t foundLength = 0;
	{
		std::lock_guard<std::mutex> lock(impl_->poolMutex);
		auto best = impl_->pool.end();
		for (auto it = impl_->pool.begin(); it != impl_->pool.end(); ++it) {
			if (it->second >= length && (best == impl_->pool.end() || it->second < best->second)) {
				best = it;
			}
		}
		if (best != impl_->pool.end()) {
			found = best->first;
			foundLength = best->second;
			impl_->pool.erase(best);
		}
	}
	if (found == nullptr) {
		cl_int status = CL_SUCCESS;
		foundLength = std::max<std::size_t>(length, 16);
		found = cl().clCreateBuffer(impl_->context, CL_MEM_READ_WRITE, foundLength, nullptr, &status);
		if (found == nullptr) {
			return {};
		}
	}
	OpenCLBuffer buffer;
	buffer.impl_ = std::make_shared<OpenCLBuffer::Impl>();
	buffer.impl_->buffer = found;
	buffer.impl_->length = foundLength;
	buffer.impl_->owner = weak_from_this();
	buffer.impl_->pooled = true;
	return buffer;
}

OpenCLBuffer OpenCLContext::newBuffer(std::size_t length, const void* initial)
{
	const std::size_t size = std::max<std::size_t>(length, 16);
	cl_int status = CL_SUCCESS;
	cl_mem made = cl().clCreateBuffer(impl_->context, CL_MEM_READ_WRITE, size, nullptr, &status);
	if (made == nullptr) {
		return {};
	}
	OpenCLBuffer buffer;
	buffer.impl_ = std::make_shared<OpenCLBuffer::Impl>();
	buffer.impl_->buffer = made;
	buffer.impl_->length = size;
	buffer.impl_->pooled = false;
	if (initial != nullptr && length > 0) {
		write(made, 0, length, initial);
		if (length < size) {
			fill(made, length, size - length, 0);
		}
	} else {
		fill(made, 0, size, 0);
	}
	finish();
	return buffer;
}

std::size_t OpenCLContext::pooledBytes() const
{
	std::lock_guard<std::mutex> lock(impl_->poolMutex);
	std::size_t total = 0;
	for (const auto& [buffer, length] : impl_->pool) {
		total += length;
	}
	return total;
}

void OpenCLContext::drainPool()
{
	std::vector<std::pair<cl_mem, std::size_t>> drained;
	{
		std::lock_guard<std::mutex> lock(impl_->poolMutex);
		drained.swap(impl_->pool);
	}
	for (auto& [buffer, length] : drained) {
		cl().clReleaseMemObject(buffer);
	}
}

std::size_t OpenCLContext::programCount() const
{
	std::lock_guard<std::mutex> lock(impl_->programMutex);
	return impl_->programs.size();
}

}  // namespace opencl
OFXGRIP_NAMESPACE_END
