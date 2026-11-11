// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
#include <ofxgrip/gpu/CudaWatermark.hpp>

#include <string>
#include <vector>

OFXGRIP_NAMESPACE_BEGIN

namespace cuda {

namespace {

Error failed(const char* what, CudaResult result)
{
	return Error::make(ErrorCode::Render, what).with("cudaResult", std::to_string(result));
}

}  // namespace

Error cudaWatermark(const GpuImage& image, const RectI& window, Point2D frameSize, Point2D frameOrigin, const Watermark& mark, const CudaDriver* driver)
{
	if (driver == nullptr) {
		driver = CudaDriver::system();
	}
	if (driver == nullptr || !driver->complete()) {
		return Error::make(ErrorCode::MissingHostFeature, "No CUDA driver is loaded for the watermark");
	}
	if (image.api != GpuApi::Cuda || image.handle == nullptr || image.rowBytes <= 0) {
		return Error::make(ErrorCode::Format, "The CUDA output frame is unusable for a watermark");
	}
	const RectI area = window.intersect(image.desc.bounds);
	if (area.empty() || mark.configuration().text.empty()) {
		return Error::none();
	}
	// The render's own writes are on the host's stream; they land before the frame is read.
	if (CudaResult r = driver->streamSynchronize(image.queue); r != kCudaSuccess) {
		return failed("The CUDA stream did not drain before the watermark", r);
	}
	const std::size_t pixelBytes = image.desc.pixelBytes();
	const std::size_t rowBytes = pixelBytes * static_cast<std::size_t>(area.width());
	std::vector<std::uint8_t> pixels(rowBytes * static_cast<std::size_t>(area.height()));
	CudaMemcpy2D copy;
	copy.sourceXBytes = static_cast<std::size_t>(area.x1 - image.desc.bounds.x1) * pixelBytes;
	copy.sourceY = static_cast<std::size_t>(area.y1 - image.desc.bounds.y1);
	copy.sourceType = kCudaMemoryDevice;
	copy.sourceDevice = static_cast<CudaDevicePointer>(reinterpret_cast<std::uintptr_t>(image.handle));
	copy.sourcePitch = static_cast<std::size_t>(image.rowBytes);
	copy.destinationType = kCudaMemoryHost;
	copy.destinationHost = pixels.data();
	copy.destinationPitch = rowBytes;
	copy.widthBytes = rowBytes;
	copy.height = static_cast<std::size_t>(area.height());
	if (CudaResult r = driver->memcpy2D(&copy); r != kCudaSuccess) {
		return failed("CUDA copied no frame to the host for the watermark", r);
	}
	ImageView view;
	view.desc = image.desc;
	view.desc.bounds = area;
	view.data = pixels.data();
	view.rowBytes = static_cast<std::ptrdiff_t>(rowBytes);
	mark.composite(view, area, frameSize, frameOrigin);
	CudaMemcpy2D back;
	back.sourceType = kCudaMemoryHost;
	back.sourceHost = pixels.data();
	back.sourcePitch = rowBytes;
	back.destinationXBytes = copy.sourceXBytes;
	back.destinationY = copy.sourceY;
	back.destinationType = kCudaMemoryDevice;
	back.destinationDevice = copy.sourceDevice;
	back.destinationPitch = static_cast<std::size_t>(image.rowBytes);
	back.widthBytes = rowBytes;
	back.height = copy.height;
	if (CudaResult r = driver->memcpy2D(&back); r != kCudaSuccess) {
		return failed("CUDA copied no watermarked frame back", r);
	}
	return Error::none();
}

}  // namespace cuda

OFXGRIP_NAMESPACE_END
