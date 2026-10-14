// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
#include <ofxgrip/ofx/Suites.hpp>

OFXGRIP_NAMESPACE_BEGIN

const char* statusName(OfxStatus status) noexcept
{
	switch (status) {
	case kOfxStatOK: return "kOfxStatOK";
	case kOfxStatFailed: return "kOfxStatFailed";
	case kOfxStatErrFatal: return "kOfxStatErrFatal";
	case kOfxStatErrUnknown: return "kOfxStatErrUnknown";
	case kOfxStatErrMissingHostFeature: return "kOfxStatErrMissingHostFeature";
	case kOfxStatErrUnsupported: return "kOfxStatErrUnsupported";
	case kOfxStatErrExists: return "kOfxStatErrExists";
	case kOfxStatErrFormat: return "kOfxStatErrFormat";
	case kOfxStatErrMemory: return "kOfxStatErrMemory";
	case kOfxStatErrBadHandle: return "kOfxStatErrBadHandle";
	case kOfxStatErrBadIndex: return "kOfxStatErrBadIndex";
	case kOfxStatErrValue: return "kOfxStatErrValue";
	case kOfxStatReplyYes: return "kOfxStatReplyYes";
	case kOfxStatReplyNo: return "kOfxStatReplyNo";
	case kOfxStatReplyDefault: return "kOfxStatReplyDefault";
	default: return "unknown status";
	}
}

Error statusToError(OfxStatus status, const char* what)
{
	ErrorCode code = ErrorCode::None;
	switch (status) {
	case kOfxStatOK:
	case kOfxStatReplyYes:
	case kOfxStatReplyNo:
	case kOfxStatReplyDefault:
		return Error::none();
	case kOfxStatFailed: code = ErrorCode::Failed; break;
	case kOfxStatErrFatal: code = ErrorCode::Fatal; break;
	case kOfxStatErrUnknown: code = ErrorCode::Unknown; break;
	case kOfxStatErrMissingHostFeature: code = ErrorCode::MissingHostFeature; break;
	case kOfxStatErrUnsupported: code = ErrorCode::Unsupported; break;
	case kOfxStatErrExists: code = ErrorCode::Exists; break;
	case kOfxStatErrFormat: code = ErrorCode::Format; break;
	case kOfxStatErrMemory: code = ErrorCode::Memory; break;
	case kOfxStatErrBadHandle: code = ErrorCode::BadHandle; break;
	case kOfxStatErrBadIndex: code = ErrorCode::BadIndex; break;
	case kOfxStatErrValue: code = ErrorCode::Value; break;
	default: code = ErrorCode::Unknown; break;
	}
	Error e = Error::make(code, "Host call failed", what);
	e.with("status", statusName(status));
	return e;
}

OfxStatus errorToStatus(const Error& error) noexcept
{
	switch (error.code()) {
	case ErrorCode::None:
	case ErrorCode::Cancelled:
		return kOfxStatOK;
	case ErrorCode::Fatal: return kOfxStatErrFatal;
	case ErrorCode::Unknown: return kOfxStatErrUnknown;
	case ErrorCode::MissingHostFeature: return kOfxStatErrMissingHostFeature;
	case ErrorCode::Unsupported: return kOfxStatErrUnsupported;
	case ErrorCode::Exists: return kOfxStatErrExists;
	case ErrorCode::Format: return kOfxStatErrFormat;
	case ErrorCode::Memory: return kOfxStatErrMemory;
	case ErrorCode::BadHandle: return kOfxStatErrBadHandle;
	case ErrorCode::BadIndex: return kOfxStatErrBadIndex;
	case ErrorCode::Value: return kOfxStatErrValue;
	default: return kOfxStatFailed;
	}
}

Error Suites::fetch(const OfxHost* fromHost)
{
	host = fromHost;
	if (host == nullptr || host->fetchSuite == nullptr) {
		return Error::make(ErrorCode::MissingHostFeature, "The host provides no suite fetcher");
	}
	auto get = [&](const char* name, int version) { return host->fetchSuite(host->host, name, version); };
	property = static_cast<const OfxPropertySuiteV1*>(get(kOfxPropertySuite, 1));
	imageEffect = static_cast<const OfxImageEffectSuiteV1*>(get(kOfxImageEffectSuite, 1));
	parameter = static_cast<const OfxParameterSuiteV1*>(get(kOfxParameterSuite, 1));
	memory = static_cast<const OfxMemorySuiteV1*>(get(kOfxMemorySuite, 1));
	multiThread = static_cast<const OfxMultiThreadSuiteV1*>(get(kOfxMultiThreadSuite, 1));
	messageV1 = static_cast<const OfxMessageSuiteV1*>(get(kOfxMessageSuite, 1));
	messageV2 = static_cast<const OfxMessageSuiteV2*>(get(kOfxMessageSuite, 2));
	progressV1 = static_cast<const OfxProgressSuiteV1*>(get(kOfxProgressSuite, 1));
	progressV2 = static_cast<const OfxProgressSuiteV2*>(get(kOfxProgressSuite, 2));
	timeLine = static_cast<const OfxTimeLineSuiteV1*>(get(kOfxTimeLineSuite, 1));
	parametric = static_cast<const OfxParametricParameterSuiteV1*>(get(kOfxParametricParameterSuite, 1));
	openGLRender = static_cast<const OfxImageEffectOpenGLRenderSuiteV1*>(get(kOfxOpenGLRenderSuite, 1));
	draw = static_cast<const OfxDrawSuiteV1*>(get(kOfxDrawSuite, 1));
	interact = static_cast<const OfxInteractSuiteV1*>(get(kOfxInteractSuite, 1));
	dialog = static_cast<const OfxDialogSuiteV1*>(get(kOfxDialogSuite, 1));
	if (!ready()) {
		Error e = Error::make(ErrorCode::MissingHostFeature, "A required OpenFX suite is missing");
		e.with("property", property ? "yes" : "no").with("imageEffect", imageEffect ? "yes" : "no").with("parameter", parameter ? "yes" : "no").with("memory", memory ? "yes" : "no");
		return e;
	}
	return Error::none();
}

bool Suites::messageV2Usable() const noexcept
{
	return messageV2 != nullptr && messageV2->message != nullptr && messageV2->setPersistentMessage != nullptr && messageV2->clearPersistentMessage != nullptr;
}

SuiteAvailability Suites::availability() const noexcept
{
	SuiteAvailability a;
	a.property = property != nullptr;
	a.imageEffect = imageEffect != nullptr;
	a.parameter = parameter != nullptr;
	a.memory = memory != nullptr;
	a.multiThread = multiThread != nullptr && multiThread->multiThread != nullptr;
	a.messageV1 = messageV1 != nullptr && messageV1->message != nullptr;
	a.messageV2 = messageV2Usable();
	a.messageV2Vended = messageV2 != nullptr;
	a.progressV1 = progressV1 != nullptr && progressV1->progressStart != nullptr;
	a.progressV2 = progressV2 != nullptr && progressV2->progressStart != nullptr && progressV2->progressUpdate != nullptr && progressV2->progressEnd != nullptr;
	a.progressV2Vended = progressV2 != nullptr;
	a.timeLine = timeLine != nullptr && timeLine->getTime != nullptr;
	a.parametric = parametric != nullptr;
	a.openGLRender = openGLRender != nullptr;
	a.draw = draw != nullptr;
	a.interact = interact != nullptr;
	a.dialog = dialog != nullptr;
	return a;
}

OFXGRIP_NAMESPACE_END
