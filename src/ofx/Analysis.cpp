// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
#include <ofxgrip/ofx/Analysis.hpp>

#include <ofxgrip/ofx/Actions.hpp>
#include <ofxgrip/ofx/Effect.hpp>

#include "ofxImageEffect.h"

#include <cmath>

OFXGRIP_NAMESPACE_BEGIN

namespace {

struct FetchedImage {
	const Suites* suites{nullptr};
	OfxPropertySetHandle handle{nullptr};
	~FetchedImage()
	{
		if (handle != nullptr && suites != nullptr) {
			suites->imageEffect->clipReleaseImage(handle);
		}
	}
};

bool viewOf(const Suites& suites, OfxPropertySetHandle imageHandle, ImageView& out)
{
	PropertySet props(suites.property, imageHandle);
	ImageDesc desc;
	const std::vector<int> bounds = props.getInts(kOfxImagePropBounds);
	const std::vector<int> rod = props.getInts(kOfxImagePropRegionOfDefinition);
	if (bounds.size() == 4) {
		desc.bounds = RectI{bounds[0], bounds[1], bounds[2], bounds[3]};
	}
	desc.regionOfDefinition = rod.size() == 4 ? RectI{rod[0], rod[1], rod[2], rod[3]} : desc.bounds;
	parseOfxBitDepth(props.getString(kOfxImageEffectPropPixelDepth, std::string(kOfxBitDepthFloat), 0), desc.depth);
	parseOfxComponents(props.getString(kOfxImageEffectPropComponents, std::string(kOfxImageComponentRGBA), 0), desc.components);
	parseOfxPremultiplication(props.getString(kOfxImageEffectPropPreMultiplication, std::string(kOfxImagePreMultiplied), 0), desc.premultiplication);
	const double par = props.getDouble(kOfxImagePropPixelAspectRatio, 1.0, 0);
	desc.pixelAspect = par > 0.0 ? par : 1.0;
	void* data = props.getPointer(kOfxImagePropData).value_or(nullptr);
	const int rowBytes = props.getInt(kOfxImagePropRowBytes, 0, 0);
	out.desc = desc;
	out.data = static_cast<std::uint8_t*>(data);
	out.rowBytes = rowBytes != 0 ? rowBytes : static_cast<std::ptrdiff_t>(desc.minimumRowBytes());
	return data != nullptr;
}

}  // namespace

AnalysisResult runAnalysis(Effect& effect, const AnalysisRequest& request)
{
	AnalysisResult result;
	AnalysisExtension* analysis = effect.analysisExtension();
	if (analysis == nullptr) {
		result.error = Error::make(ErrorCode::Unsupported, "The effect has no analysis store; declare an analyzer control or set the analysis property");
		return result;
	}
	OfxHostServices& services = effect.ofxServices();
	OfxImageClipHandle clip = services.clipHandle(request.clip);
	if (clip == nullptr) {
		result.error = Error::make(ErrorCode::NotFound, "No such clip").with("clip", request.clip);
		return result;
	}
	const std::pair<Time, Time> bounds = services.time().timeBounds();
	const Time first = request.first;
	const Time last = request.last < request.first ? bounds.second : request.last;
	const double step = request.step > 0.0 ? request.step : 1.0;
	if (last < first) {
		result.error = Error::make(ErrorCode::Value, "The analysis range is empty");
		return result;
	}
	FrameData& data = analysis->data();
	if (request.clearFirst) {
		data.clear();
	}
	const long count = static_cast<long>(std::floor((last - first) / step)) + 1;
	FeedbackService& feedback = services.feedback();
	feedback.progressStart("Analyzing");
	const Suites& suites = services.suites();
	for (long i = 0; i < count; ++i) {
		const Time time = request.backward ? last - static_cast<double>(i) * step : first + static_cast<double>(i) * step;
		if (feedback.abortRequested()) {
			result.aborted = true;
			break;
		}
		FetchedImage fetched{&suites, nullptr};
		if (suites.imageEffect->clipGetImage(clip, time, nullptr, &fetched.handle) != kOfxStatOK || fetched.handle == nullptr) {
			result.error = Error::make(ErrorCode::HostCall, "The host supplied no image for analysis").with("time", std::to_string(time));
			break;
		}
		ImageView view;
		if (!viewOf(suites, fetched.handle, view)) {
			result.error = Error::make(ErrorCode::HostCall, "The analysis image has no data").with("time", std::to_string(time));
			break;
		}
		const long frameIndex = static_cast<long>(std::floor(time + 1e-9));
		Bytes record;
		bool store = false;
		if (Error e = effect.onAnalyzeFrame(view, time, frameIndex, record, store)) {
			result.error = e.with("time", std::to_string(time));
			break;
		}
		++result.framesAnalyzed;
		if (store) {
			data.set(frameIndex, std::move(record));
			++result.framesStored;
		}
		feedback.progressUpdate(static_cast<double>(i + 1) / static_cast<double>(count));
	}
	feedback.progressEnd();
	analysis->markDirty();
	return result;
}

OFXGRIP_NAMESPACE_END
