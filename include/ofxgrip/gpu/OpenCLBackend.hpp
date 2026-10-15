// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
/*!
	@file  OpenCLBackend.hpp
	@brief The OpenCL backend: a context per host command queue with a program cache and a
	       scratch-buffer pool, and dispatches of native OpenCL C kernels over `cl_mem` frames.

	Introduced in OfxGrip 0.4.0. Slang emits no OpenCL C, so an OpenCL path takes native kernels
	(host libraries plan, section 10). The OpenFX contract (`ofxGPURender.h`): the host passes
	`cl_mem` frames and its own `cl_command_queue`; the plug-in enqueues on that queue and
	returns without waiting. `OpenCLContext::forQueue` keeps one context per queue for the life
	of the process, so compiled programs and pooled scratch buffers are shared by every instance
	that renders on it. Every type here is opaque; nothing in this header includes an OpenCL
	header.
*/
#pragma once

#include <ofxgrip/core/Error.hpp>
#include <ofxgrip/core/Types.hpp>

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

OFXGRIP_NAMESPACE_BEGIN
namespace opencl {

class OpenCLContext;

/*! A kernel object of a compiled program. One per dispatch, because a `cl_kernel` carries its
    arguments. Released with the last copy. */
class OpenCLKernel {
public:
	OpenCLKernel() = default;
	bool valid() const noexcept { return impl_ != nullptr; }
	/*! The `cl_kernel`. */
	void* handle() const noexcept;
	const std::string& name() const noexcept;

	struct Impl;

private:
	friend class OpenCLContext;
	std::shared_ptr<Impl> impl_;
};

/*! A buffer the backend owns: a pooled scratch buffer or a fresh allocation. */
class OpenCLBuffer {
public:
	OpenCLBuffer() = default;
	bool valid() const noexcept { return impl_ != nullptr; }
	/*! The `cl_mem`. */
	void* handle() const noexcept;
	std::size_t length() const noexcept;

	struct Impl;

private:
	friend class OpenCLContext;
	std::shared_ptr<Impl> impl_;
};

/*! One kernel dispatch: the kernel, the global work size, and the buffer and byte arguments. */
struct OpenCLDispatch {
	struct BufferArg {
		void* handle{nullptr};
		unsigned index{0};
	};
	struct BytesArg {
		const void* data{nullptr};
		std::size_t length{0};
		unsigned index{0};
	};
	OpenCLKernel kernel;
	std::size_t globalSize[2]{1, 1};
	std::size_t localSize[2]{0, 0};  /*!< Zeros let the runtime choose. */
	std::vector<BufferArg> buffers;
	std::vector<BytesArg> bytes;
};

/*! The context over one command queue. */
class OpenCLContext : public std::enable_shared_from_this<OpenCLContext> {
public:
	~OpenCLContext();
	OpenCLContext(const OpenCLContext&) = delete;
	OpenCLContext& operator=(const OpenCLContext&) = delete;

	/*! Whether the process has an OpenCL GPU device. */
	static bool available() noexcept;
	/*! The context for a host's `cl_command_queue`. One per queue for the process lifetime; null
	    for a null queue. */
	static std::shared_ptr<OpenCLContext> forQueue(void* commandQueue);
	/*! A context over the first GPU device and a queue the backend owns, for hosts and tests.
	    One per process. Null without a device. */
	static std::shared_ptr<OpenCLContext> systemDefault();

	void* queue() const noexcept;
	void* context() const noexcept;
	void* device() const noexcept;
	std::string deviceName() const;

	/*! A kernel of `programName`, compiled once per context from the OpenCL C `source`. The
	    build log of a failed compile is the error's developer message. */
	Error kernel(std::string_view programName, std::string_view source, std::string_view kernelName, OpenCLKernel& out);
	/*! Enqueues a dispatch on the queue without waiting. */
	Error enqueue(const OpenCLDispatch& dispatch);
	/*! Enqueues a copy between buffers. */
	Error copy(void* sourceBuffer, std::size_t sourceOffset, void* destinationBuffer, std::size_t destinationOffset, std::size_t length);
	/*! Enqueues a fill of a byte range. */
	Error fill(void* buffer, std::size_t offset, std::size_t length, std::uint8_t value);
	/*! Reads a buffer into host memory, waiting for the queue. */
	Error read(void* buffer, std::size_t offset, std::size_t length, void* destination);
	/*! Writes host memory into a buffer, waiting for the write. */
	Error write(void* buffer, std::size_t offset, std::size_t length, const void* source);
	/*! Waits until every command enqueued so far has completed. */
	Error finish();
	/*! A buffer of at least `length` bytes from the pool. */
	OpenCLBuffer scratch(std::size_t length);
	/*! A fresh buffer, filled from `initial` when given and zeroed otherwise. */
	OpenCLBuffer newBuffer(std::size_t length, const void* initial = nullptr);
	std::size_t pooledBytes() const;
	void drainPool();
	std::size_t programCount() const;

private:
	friend struct OpenCLBuffer::Impl;
	OpenCLContext();
	struct Impl;
	std::unique_ptr<Impl> impl_;
};

}  // namespace opencl
OFXGRIP_NAMESPACE_END
