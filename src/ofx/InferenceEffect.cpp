// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
#include <ofxgrip/ofx/InferenceEffect.hpp>

#include <cmath>

OFXGRIP_NAMESPACE_BEGIN

InferenceEffect::InferenceEffect() : backend_(std::make_shared<PassthroughInferenceBackend>()) {}

InferenceEffect::~InferenceEffect() = default;

void InferenceEffect::setInferenceBackend(std::shared_ptr<InferenceBackend> backend)
{
	std::lock_guard<std::mutex> lock(mutex_);
	backend_ = backend ? std::move(backend) : std::make_shared<PassthroughInferenceBackend>(inputImageName(), outputImageName());
}

std::shared_ptr<InferenceBackend> InferenceEffect::inferenceBackend() const
{
	std::lock_guard<std::mutex> lock(mutex_);
	return backend_;
}

void InferenceEffect::setDiskCacheLimit(std::uint64_t bytes)
{
	std::lock_guard<std::mutex> lock(mutex_);
	if (bytes != diskLimit_) {
		diskLimit_ = bytes;
		disk_.reset();
	}
}

std::uint64_t InferenceEffect::diskCacheLimit() const
{
	std::lock_guard<std::mutex> lock(mutex_);
	return disk_ ? disk_->byteLimit() : diskLimit_;
}

void InferenceEffect::setDiskCache(std::shared_ptr<InferenceDiskCache> cache)
{
	std::lock_guard<std::mutex> lock(mutex_);
	diskLimit_ = cache ? cache->byteLimit() : 0;
	disk_ = std::move(cache);
}

std::shared_ptr<InferenceDiskCache> InferenceEffect::diskCache() const
{
	std::lock_guard<std::mutex> lock(mutex_);
	return disk_;
}

std::shared_ptr<InferenceDiskCache> InferenceEffect::diskTier(HostServices& services)
{
	std::lock_guard<std::mutex> lock(mutex_);
	if (!disk_ && diskLimit_ > 0) {
		disk_ = std::make_shared<InferenceDiskCache>(services.paths().cacheDirectory() / "inference", diskLimit_);
	}
	return disk_;
}

std::map<std::string, Value> InferenceEffect::inferenceParameters(const RenderSnapshot&) const
{
	return {};
}

std::string InferenceEffect::cacheSignature(const RenderSnapshot& snapshot, const InferenceBackend& backend) const
{
	std::string signature = backend.identifier();
	signature += "|" + std::to_string(snapshot.info().renderScale.x) + "x" + std::to_string(snapshot.info().renderScale.y);
	for (const auto& [key, value] : inferenceParameters(snapshot)) {
		signature += "|" + key + "=" + value.toString();
	}
	return signature;
}

Error InferenceEffect::render(RenderContext& context)
{
	if (context.outputIsGpu() || !context.output.valid()) {
		return Error::make(ErrorCode::Unsupported, "An inference effect renders CPU frames");
	}
	const InputImage* source = context.source();
	const RectI window = context.args.renderWindow.empty() ? context.output.desc.bounds : context.args.renderWindow.intersect(context.output.desc.bounds);
	if (source == nullptr || !source->connected || source->isGpu() || !source->view.valid()) {
		const ImageView target = context.output.subView(window);
		if (target.valid()) {
			target.clear();
		}
		return Error::none();
	}
	const std::shared_ptr<InferenceBackend> backend = inferenceBackend();
	if (!backend->isReady()) {
		return convertInferenceImage(source->view, context.output, window);
	}
	// Milliframes: two renders of one frame agree, and a fractional time stays distinct.
	const std::int64_t frame = static_cast<std::int64_t>(std::llround(context.args.time * 1000.0));
	const std::string signature = cacheSignature(context.snapshot, *backend);
	if (cacheEnabled_) {
		if (InferenceImage stored = cache_.find(frame, signature)) {
			return convertInferenceImage(stored->view(), context.output, window);
		}
	}
	auto input = std::make_shared<ImageBuffer>(source->view.desc);
	if (Error e = input->view().copyFrom(source->view)) {
		return e;
	}
	const std::shared_ptr<InferenceDiskCache> disk = cacheEnabled_ ? diskTier(context.services) : nullptr;
	std::string diskKey;
	if (disk) {
		diskKey = InferenceDiskCache::key(signature, input->view());
		if (InferenceImage stored = disk->find(diskKey)) {
			cache_.store(frame, signature, stored);
			return convertInferenceImage(stored->view(), context.output, window);
		}
	}
	InferenceRequest request;
	request.inputs[inputImageName()] = InferenceItem(InferenceImage(std::move(input)));
	request.parameters = inferenceParameters(context.snapshot);
	const std::string outputName = outputImageName();
	InferenceRunner::Completion keep;
	if (cacheEnabled_) {
		// The cache outlives the runner's thread: the runner is destroyed first.
		keep = [this, frame, signature, outputName, disk, diskKey, log = &context.services.log()](const Error& error, const InferenceResult& finished) {
			if (!error.ok()) {
				return;
			}
			const InferenceImage image = finished.image(outputName);
			cache_.store(frame, signature, image);
			if (disk && image) {
				if (Error e = disk->store(diskKey, image->view())) {
					log->warning("The inference output was not kept on disk: " + e.describe());
				}
			}
		};
	}
	InferenceResult result;
	if (Error e = runner_.run(backend, std::move(request), result, [&context]() { return context.abortRequested(); }, std::move(keep))) {
		return e;
	}
	const InferenceImage output = result.image(outputName);
	if (!output) {
		return Error::make(ErrorCode::Render, "The inference result has no image output").with("output", outputName).with("backend", backend->identifier());
	}
	return convertInferenceImage(output->view(), context.output, window);
}

OFXGRIP_NAMESPACE_END
