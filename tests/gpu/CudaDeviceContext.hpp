// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
// A CUDA context for the tests that run on a real device. A host makes its own context current
// before it calls a plug-in, so the product code never creates one.
#pragma once

#include <string>

namespace ofxgrip::testing {

/*! Makes the primary context of CUDA device 0 current on the calling thread, and releases it
    on destruction. `current()` is false on a machine without an NVIDIA driver or device. */
class CudaDeviceContext {
public:
	CudaDeviceContext();
	CudaDeviceContext(const CudaDeviceContext&) = delete;
	CudaDeviceContext& operator=(const CudaDeviceContext&) = delete;
	~CudaDeviceContext();

	bool current() const noexcept { return context_ != nullptr; }
	const std::string& unavailableReason() const noexcept { return reason_; }

private:
	void* library_{nullptr};
	void* context_{nullptr};
	int device_{0};
	std::string reason_;
};

}  // namespace ofxgrip::testing
