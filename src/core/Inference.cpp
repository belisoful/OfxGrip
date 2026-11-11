// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
#include <ofxgrip/core/Inference.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <list>
#include <mutex>
#include <thread>

OFXGRIP_NAMESPACE_BEGIN

InferenceImage InferenceResult::image(std::string_view name) const
{
	auto it = outputs.find(std::string(name));
	return it == outputs.end() ? nullptr : it->second.image;
}

PassthroughInferenceBackend::PassthroughInferenceBackend(std::string inputName, std::string outputName) : inputName_(std::move(inputName)), outputName_(std::move(outputName)) {}

Error PassthroughInferenceBackend::run(const InferenceRequest& request, InferenceResult& result)
{
	auto it = request.inputs.find(inputName_);
	if (it == request.inputs.end() || !it->second.isImage()) {
		return Error::make(ErrorCode::InvalidParameter, "The inference request has no image input").with("input", inputName_);
	}
	result.outputs[outputName_] = it->second;
	return Error::none();
}

// ---- InferenceCache -----------------------------------------------------------------------------

struct InferenceCache::Impl {
	struct Entry {
		std::int64_t frame;
		InferenceImage image;
	};
	mutable std::mutex mutex;
	std::size_t limit{0};
	std::size_t bytes{0};
	std::string signature;
	std::list<Entry> entries;  // most recently used first
	std::map<std::int64_t, std::list<Entry>::iterator> byFrame;

	void clearLocked()
	{
		entries.clear();
		byFrame.clear();
		bytes = 0;
	}
};

InferenceCache::InferenceCache(std::size_t byteLimit) : impl_(std::make_unique<Impl>())
{
	impl_->limit = byteLimit;
}

InferenceCache::~InferenceCache() = default;

InferenceImage InferenceCache::find(std::int64_t frame, std::string_view signature)
{
	std::lock_guard<std::mutex> lock(impl_->mutex);
	if (impl_->signature != signature) {
		return nullptr;
	}
	auto it = impl_->byFrame.find(frame);
	if (it == impl_->byFrame.end()) {
		return nullptr;
	}
	impl_->entries.splice(impl_->entries.begin(), impl_->entries, it->second);
	return it->second->image;
}

void InferenceCache::store(std::int64_t frame, std::string_view signature, InferenceImage image)
{
	if (!image) {
		return;
	}
	const std::size_t size = image->bytes().size();
	std::lock_guard<std::mutex> lock(impl_->mutex);
	if (impl_->signature != signature) {
		impl_->clearLocked();
		impl_->signature = std::string(signature);
	}
	if (auto it = impl_->byFrame.find(frame); it != impl_->byFrame.end()) {
		impl_->bytes -= it->second->image->bytes().size();
		impl_->entries.erase(it->second);
		impl_->byFrame.erase(it);
	}
	if (size > impl_->limit) {
		return;
	}
	impl_->entries.push_front({frame, std::move(image)});
	impl_->byFrame[frame] = impl_->entries.begin();
	impl_->bytes += size;
	while (impl_->bytes > impl_->limit && !impl_->entries.empty()) {
		const Impl::Entry& last = impl_->entries.back();
		impl_->bytes -= last.image->bytes().size();
		impl_->byFrame.erase(last.frame);
		impl_->entries.pop_back();
	}
}

void InferenceCache::clear()
{
	std::lock_guard<std::mutex> lock(impl_->mutex);
	impl_->clearLocked();
}

std::size_t InferenceCache::frameCount() const
{
	std::lock_guard<std::mutex> lock(impl_->mutex);
	return impl_->entries.size();
}

std::size_t InferenceCache::byteCount() const
{
	std::lock_guard<std::mutex> lock(impl_->mutex);
	return impl_->bytes;
}

std::size_t InferenceCache::byteLimit() const noexcept
{
	return impl_->limit;
}

// ---- InferenceRunner ----------------------------------------------------------------------------

struct InferenceRunner::Impl {
	struct Job {
		std::shared_ptr<InferenceBackend> backend;
		InferenceRequest request;
		Completion completion;
		std::mutex mutex;
		std::condition_variable finished;
		bool done{false};
		Error error;
		InferenceResult result;
	};
	mutable std::mutex mutex;
	std::condition_variable wake;
	std::deque<std::shared_ptr<Job>> queue;
	std::size_t pending{0};
	bool stopping{false};
	std::thread thread;

	void work()
	{
		for (;;) {
			std::shared_ptr<Job> job;
			{
				std::unique_lock<std::mutex> lock(mutex);
				wake.wait(lock, [this]() { return stopping || !queue.empty(); });
				if (stopping) {
					return;
				}
				job = std::move(queue.front());
				queue.pop_front();
			}
			Error error;
			InferenceResult result;
			try {
				error = job->backend->run(job->request, result);
			} catch (const std::exception& e) {
				error = Error::make(ErrorCode::Render, "The inference backend threw").with("what", e.what());
			} catch (...) {
				error = Error::make(ErrorCode::Render, "The inference backend threw");
			}
			if (job->completion) {
				job->completion(error, result);
			}
			{
				std::lock_guard<std::mutex> lock(job->mutex);
				job->error = std::move(error);
				job->result = std::move(result);
				job->done = true;
			}
			job->finished.notify_all();
			std::lock_guard<std::mutex> lock(mutex);
			--pending;
		}
	}
};

InferenceRunner::InferenceRunner() : impl_(std::make_unique<Impl>()) {}

InferenceRunner::~InferenceRunner()
{
	std::vector<std::shared_ptr<Impl::Job>> dropped;
	{
		std::lock_guard<std::mutex> lock(impl_->mutex);
		impl_->stopping = true;
		dropped.assign(impl_->queue.begin(), impl_->queue.end());
		impl_->queue.clear();
	}
	impl_->wake.notify_all();
	if (impl_->thread.joinable()) {
		impl_->thread.join();
	}
	for (const std::shared_ptr<Impl::Job>& job : dropped) {
		{
			std::lock_guard<std::mutex> lock(job->mutex);
			job->error = Error::cancelled();
			job->done = true;
		}
		job->finished.notify_all();
	}
}

std::size_t InferenceRunner::pendingCount() const
{
	std::lock_guard<std::mutex> lock(impl_->mutex);
	return impl_->pending;
}

Error InferenceRunner::run(std::shared_ptr<InferenceBackend> backend, InferenceRequest request, InferenceResult& result, const std::function<bool()>& abort, Completion completion)
{
	if (!backend) {
		return Error::make(ErrorCode::InvalidParameter, "The inference run has no backend");
	}
	auto job = std::make_shared<Impl::Job>();
	job->backend = std::move(backend);
	job->request = std::move(request);
	job->completion = std::move(completion);
	{
		std::lock_guard<std::mutex> lock(impl_->mutex);
		if (impl_->stopping) {
			return Error::cancelled();
		}
		if (!impl_->thread.joinable()) {
			impl_->thread = std::thread([impl = impl_.get()]() { impl->work(); });
		}
		impl_->queue.push_back(job);
		++impl_->pending;
	}
	impl_->wake.notify_one();
	std::unique_lock<std::mutex> lock(job->mutex);
	while (!job->done) {
		job->finished.wait_for(lock, std::chrono::milliseconds(20));
		if (!job->done && abort && abort()) {
			return Error::cancelled();
		}
	}
	if (job->error.ok()) {
		result = std::move(job->result);
	}
	return job->error;
}

// ---- convertInferenceImage ----------------------------------------------------------------------

namespace {

std::array<float, 4> texelAt(const ImageView& view, int x, int y) noexcept
{
	const std::uint8_t* pixel = view.pixel(x, y);
	if (pixel == nullptr) {
		return {0.0f, 0.0f, 0.0f, 0.0f};
	}
	const BitDepth depth = view.desc.depth;
	switch (view.desc.components) {
	case Components::RGBA:
		return {readComponent(pixel, depth, 0), readComponent(pixel, depth, 1), readComponent(pixel, depth, 2), readComponent(pixel, depth, 3)};
	case Components::RGB:
		return {readComponent(pixel, depth, 0), readComponent(pixel, depth, 1), readComponent(pixel, depth, 2), 1.0f};
	case Components::Alpha: {
		const float a = readComponent(pixel, depth, 0);
		return {a, a, a, a};
	}
	case Components::None:
		break;
	}
	return {0.0f, 0.0f, 0.0f, 0.0f};
}

}  // namespace

Error convertInferenceImage(const ImageView& from, const ImageView& to, const RectI& window)
{
	if (!from.valid() || !to.valid()) {
		return Error::make(ErrorCode::InvalidParameter, "An inference image conversion needs two images");
	}
	const RectI area = window.empty() ? to.desc.bounds : window.intersect(to.desc.bounds);
	const RectI& source = from.desc.bounds;
	const RectI& target = to.desc.bounds;
	const bool sameSize = source.width() == target.width() && source.height() == target.height();
	const double scaleX = static_cast<double>(source.width()) / static_cast<double>(target.width());
	const double scaleY = static_cast<double>(source.height()) / static_cast<double>(target.height());
	const std::size_t components = componentCount(to.desc.components);
	for (int y = area.y1; y < area.y2; ++y) {
		for (int x = area.x1; x < area.x2; ++x) {
			std::array<float, 4> texel{};
			if (sameSize) {
				texel = texelAt(from, source.x1 + (x - target.x1), source.y1 + (y - target.y1));
			} else {
				// Pixel centers map onto pixel centers; the edge texel holds outside.
				const double fx = std::clamp((static_cast<double>(x - target.x1) + 0.5) * scaleX - 0.5, 0.0, static_cast<double>(source.width() - 1));
				const double fy = std::clamp((static_cast<double>(y - target.y1) + 0.5) * scaleY - 0.5, 0.0, static_cast<double>(source.height() - 1));
				const int x0 = static_cast<int>(std::floor(fx));
				const int y0 = static_cast<int>(std::floor(fy));
				const int x1 = std::min(x0 + 1, source.width() - 1);
				const int y1 = std::min(y0 + 1, source.height() - 1);
				const float tx = static_cast<float>(fx - static_cast<double>(x0));
				const float ty = static_cast<float>(fy - static_cast<double>(y0));
				const std::array<float, 4> a = texelAt(from, source.x1 + x0, source.y1 + y0);
				const std::array<float, 4> b = texelAt(from, source.x1 + x1, source.y1 + y0);
				const std::array<float, 4> c = texelAt(from, source.x1 + x0, source.y1 + y1);
				const std::array<float, 4> d = texelAt(from, source.x1 + x1, source.y1 + y1);
				for (std::size_t i = 0; i < 4; ++i) {
					const float low = a[i] + (b[i] - a[i]) * tx;
					const float high = c[i] + (d[i] - c[i]) * tx;
					texel[i] = low + (high - low) * ty;
				}
			}
			std::uint8_t* pixel = to.pixel(x, y);
			for (std::size_t i = 0; i < components; ++i) {
				writeComponent(pixel, to.desc.depth, i, to.desc.components == Components::Alpha ? texel[3] : texel[i]);
			}
		}
	}
	return Error::none();
}

OFXGRIP_NAMESPACE_END
