// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
#include <ofxgrip/inference/InferKitBackend.hpp>

#include <nlohmann/json.hpp>

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#include <atomic>
#include <cstring>
#include <vector>

OFXGRIP_NAMESPACE_BEGIN

namespace {

using Json = nlohmann::json;

// The binary interface of NFKInferKitC.h, InferKit 0.4.0.
struct NfkBuffer {
	std::uint32_t structSize;
	std::int32_t kind;
	const void* bytes;
	std::uint64_t length;
	void* surface;
	const char* path;
	std::int32_t descriptor;
	std::uint32_t reserved;
};
struct NfkPayload {
	std::uint32_t structSize;
	std::uint32_t bufferCount;
	const char* json;
	const NfkBuffer* buffers;
};
static_assert(sizeof(NfkBuffer) == 48, "NfkBuffer must match nfk_buffer");
static_assert(sizeof(NfkPayload) == 24, "NfkPayload must match nfk_payload");

constexpr std::int32_t kNfkBufferBytes = 0;
// Core Video pixel formats, as InferKit names them.
constexpr std::uint32_t kFormatRGBAFloat = 1380410945;  // 'RGfA'
constexpr std::uint32_t kFormatRGBAHalf = 1380411457;   // 'RGhA'
constexpr std::uint32_t kFormatBGRA8 = 1111970369;      // 'BGRA'
constexpr std::uint32_t kFormatRGBA8 = 1380401729;      // 'RGBA'
constexpr std::uint32_t kFormatOneFloat = 1278226534;   // 'L00f'

struct Functions {
	std::uint32_t (*apiVersion)(){nullptr};
	void* (*backendCreate)(const char* identifier, const char* optionsJson, void** error){nullptr};
	std::int32_t (*backendPrepare)(void* backend, void** error){nullptr};
	void* (*backendRun)(void* backend, const NfkPayload* request, void** error){nullptr};
	void (*backendFree)(void* backend){nullptr};
	const char* (*resultJson)(const void* result){nullptr};
	std::uint32_t (*resultBufferCount)(const void* result){nullptr};
	const NfkBuffer* (*resultBuffer)(const void* result, std::uint32_t index){nullptr};
	void (*resultFree)(void* result){nullptr};
	const char* (*errorDomain)(const void* error){nullptr};
	std::int64_t (*errorCode)(const void* error){nullptr};
	const char* (*errorMessage)(const void* error){nullptr};
	void (*errorFree)(void* error){nullptr};

	bool complete() const noexcept
	{
		return apiVersion != nullptr && backendCreate != nullptr && backendPrepare != nullptr && backendRun != nullptr && backendFree != nullptr && resultJson != nullptr && resultBufferCount != nullptr
		       && resultBuffer != nullptr && resultFree != nullptr && errorDomain != nullptr && errorCode != nullptr && errorMessage != nullptr && errorFree != nullptr;
	}
};

void* openLibrary(const std::string& path) noexcept
{
#if defined(_WIN32)
	return static_cast<void*>(::LoadLibraryA(path.c_str()));
#else
	return ::dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
#endif
}

template <typename F>
void resolve(void* library, const char* name, F& out) noexcept
{
#if defined(_WIN32)
	out = reinterpret_cast<F>(reinterpret_cast<void*>(::GetProcAddress(static_cast<HMODULE>(library), name)));
#else
	out = reinterpret_cast<F>(::dlsym(library, name));
#endif
}

Json jsonOf(const Value& value)
{
	switch (value.kind()) {
	case ValueKind::Bool: return value.as<bool>();
	case ValueKind::Int: return value.as<int>();
	case ValueKind::Double: return value.as<double>();
	case ValueKind::String: return value.as<std::string>();
	case ValueKind::Point2D: return Json::array({value.as<Point2D>().x, value.as<Point2D>().y});
	case ValueKind::Point3D: return Json::array({value.as<Point3D>().x, value.as<Point3D>().y, value.as<Point3D>().z});
	case ValueKind::RGB: return Json::array({value.as<RGB>().r, value.as<RGB>().g, value.as<RGB>().b});
	case ValueKind::RGBA: return Json::array({value.as<RGBA>().r, value.as<RGBA>().g, value.as<RGBA>().b, value.as<RGBA>().a});
	case ValueKind::Blob:
	case ValueKind::Empty:
		break;
	}
	return nullptr;
}

/*! The image as four float channels a pixel, the top row first. */
std::vector<float> topDownFloat(const ImageBuffer& image)
{
	const ImageView view = image.view();
	const RectI& b = view.desc.bounds;
	std::vector<float> out(static_cast<std::size_t>(b.width()) * static_cast<std::size_t>(b.height()) * 4);
	float* at = out.data();
	for (int y = b.y2 - 1; y >= b.y1; --y) {
		for (int x = b.x1; x < b.x2; ++x) {
			const std::uint8_t* pixel = view.pixel(x, y);
			const BitDepth depth = view.desc.depth;
			switch (view.desc.components) {
			case Components::RGBA:
				for (std::size_t c = 0; c < 4; ++c) {
					at[c] = readComponent(pixel, depth, c);
				}
				break;
			case Components::RGB:
				for (std::size_t c = 0; c < 3; ++c) {
					at[c] = readComponent(pixel, depth, c);
				}
				at[3] = 1.0f;
				break;
			default: {
				const float a = readComponent(pixel, depth, 0);
				at[0] = at[1] = at[2] = at[3] = a;
				break;
			}
			}
			at += 4;
		}
	}
	return out;
}

/*! Decodes a `pixelBuffer` entry into a bottom-up image. */
Error imageFrom(const Json& entry, const NfkBuffer& buffer, InferenceImage& out)
{
	const std::uint32_t format = entry.value("format", std::uint32_t{0});
	const int width = entry.value("width", 0);
	const int height = entry.value("height", 0);
	if (width <= 0 || height <= 0 || !entry.contains("planes") || !entry["planes"].is_array() || entry["planes"].empty()) {
		return Error::make(ErrorCode::Codec, "InferKit returned a pixel buffer with no geometry");
	}
	const Json& plane = entry["planes"][0];
	const std::uint64_t offset = plane.value("offset", std::uint64_t{0});
	const std::uint64_t rowBytes = plane.value("bytesPerRow", std::uint64_t{0});
	ImageDesc desc;
	desc.bounds = {0, 0, width, height};
	desc.regionOfDefinition = desc.bounds;
	std::size_t sourcePixelBytes = 0;
	bool swapRedBlue = false;
	switch (format) {
	case kFormatRGBAFloat: desc.depth = BitDepth::Float; desc.components = Components::RGBA; sourcePixelBytes = 16; break;
	case kFormatRGBAHalf: desc.depth = BitDepth::Half; desc.components = Components::RGBA; sourcePixelBytes = 8; break;
	case kFormatRGBA8: desc.depth = BitDepth::UInt8; desc.components = Components::RGBA; sourcePixelBytes = 4; break;
	case kFormatBGRA8: desc.depth = BitDepth::UInt8; desc.components = Components::RGBA; sourcePixelBytes = 4; swapRedBlue = true; break;
	case kFormatOneFloat: desc.depth = BitDepth::Float; desc.components = Components::Alpha; sourcePixelBytes = 4; break;
	default:
		return Error::make(ErrorCode::Unsupported, "InferKit returned a pixel format OfxGrip does not read").with("format", std::to_string(format));
	}
	const std::uint64_t needed = static_cast<std::uint64_t>(width) * sourcePixelBytes;
	if (buffer.bytes == nullptr || rowBytes < needed || offset + rowBytes * static_cast<std::uint64_t>(height - 1) + needed > buffer.length) {
		return Error::make(ErrorCode::Codec, "InferKit returned a pixel buffer shorter than its geometry");
	}
	auto image = std::make_shared<ImageBuffer>(desc);
	const ImageView view = image->view();
	const auto* bytes = static_cast<const std::uint8_t*>(buffer.bytes) + offset;
	for (int row = 0; row < height; ++row) {
		// InferKit's rows run top-down; OpenFX's run bottom-up.
		std::uint8_t* to = view.pixel(0, height - 1 - row);
		std::memcpy(to, bytes + static_cast<std::uint64_t>(row) * rowBytes, static_cast<std::size_t>(needed));
		if (swapRedBlue) {
			for (int x = 0; x < width; ++x) {
				std::swap(to[x * 4], to[x * 4 + 2]);
			}
		}
	}
	out = std::move(image);
	return Error::none();
}

}  // namespace

struct InferKitBackend::Impl {
	Functions nfk;
	void* backend{nullptr};
	std::string catalogIdentifier;
	std::atomic<bool> ready{false};

	Error failure(const char* what, void* error) const
	{
		Error e = Error::make(ErrorCode::Render, what).with("model", catalogIdentifier);
		if (error != nullptr) {
			const char* domain = nfk.errorDomain(error);
			const char* message = nfk.errorMessage(error);
			e = e.with("domain", domain != nullptr ? domain : "").with("code", std::to_string(nfk.errorCode(error))).with("message", message != nullptr ? message : "");
			nfk.errorFree(error);
		}
		return e;
	}
};

InferKitBackend::InferKitBackend(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

InferKitBackend::~InferKitBackend()
{
	if (impl_->backend != nullptr) {
		impl_->nfk.backendFree(impl_->backend);
	}
	// The library stays open: InferKit may keep threads of its own.
}

Error InferKitBackend::create(const std::string& libraryPath, const std::string& catalogIdentifier, const std::string& optionsJson, std::shared_ptr<InferKitBackend>& out)
{
	void* library = openLibrary(libraryPath);
	if (library == nullptr) {
		return Error::make(ErrorCode::MissingHostFeature, "The InferKit library did not open").with("library", libraryPath);
	}
	auto impl = std::make_unique<Impl>();
	impl->catalogIdentifier = catalogIdentifier;
	resolve(library, "nfk_api_version", impl->nfk.apiVersion);
	resolve(library, "nfk_backend_create", impl->nfk.backendCreate);
	resolve(library, "nfk_backend_prepare", impl->nfk.backendPrepare);
	resolve(library, "nfk_backend_run", impl->nfk.backendRun);
	resolve(library, "nfk_backend_free", impl->nfk.backendFree);
	resolve(library, "nfk_result_json", impl->nfk.resultJson);
	resolve(library, "nfk_result_buffer_count", impl->nfk.resultBufferCount);
	resolve(library, "nfk_result_buffer", impl->nfk.resultBuffer);
	resolve(library, "nfk_result_free", impl->nfk.resultFree);
	resolve(library, "nfk_error_domain", impl->nfk.errorDomain);
	resolve(library, "nfk_error_code", impl->nfk.errorCode);
	resolve(library, "nfk_error_message", impl->nfk.errorMessage);
	resolve(library, "nfk_error_free", impl->nfk.errorFree);
	if (!impl->nfk.complete()) {
		return Error::make(ErrorCode::MissingHostFeature, "The library lacks InferKit's C interface").with("library", libraryPath);
	}
	void* error = nullptr;
	impl->backend = impl->nfk.backendCreate(catalogIdentifier.c_str(), optionsJson.empty() ? nullptr : optionsJson.c_str(), &error);
	if (impl->backend == nullptr) {
		return impl->failure("InferKit built no backend for the model", error);
	}
	out = std::shared_ptr<InferKitBackend>(new InferKitBackend(std::move(impl)));
	return Error::none();
}

bool InferKitBackend::isReady() const noexcept
{
	return impl_->ready.load();
}

std::string InferKitBackend::identifier() const
{
	return "inferkit:" + impl_->catalogIdentifier;
}

std::uint32_t InferKitBackend::apiVersion() const noexcept
{
	return impl_->nfk.apiVersion();
}

Error InferKitBackend::prepare()
{
	void* error = nullptr;
	if (impl_->nfk.backendPrepare(impl_->backend, &error) == 0) {
		return impl_->failure("InferKit did not load the model", error);
	}
	impl_->ready = true;
	return Error::none();
}

Error InferKitBackend::run(const InferenceRequest& request, InferenceResult& result)
{
	// The request: tagged JSON over buffers that stay alive until the run returns.
	std::vector<std::vector<float>> images;
	std::vector<NfkBuffer> buffers;
	auto attach = [&buffers](const void* bytes, std::size_t length) {
		buffers.push_back(NfkBuffer{sizeof(NfkBuffer), kNfkBufferBytes, bytes, length, nullptr, nullptr, -1, 0});
		return buffers.size() - 1;
	};
	images.reserve(request.inputs.size());
	Json inputs = Json::object();
	for (const auto& [name, item] : request.inputs) {
		if (item.isImage()) {
			const RectI& b = item.image->desc().bounds;
			images.push_back(topDownFloat(*item.image));
			const std::size_t length = images.back().size() * sizeof(float);
			const std::size_t index = attach(images.back().data(), length);
			inputs[name] = {{"$nfk", "buffer"}, {"index", index}, {"as", "pixelBuffer"}, {"length", length}, {"format", kFormatRGBAFloat}, {"width", b.width()}, {"height", b.height()},
			                {"planes", Json::array({{{"offset", 0}, {"bytesPerRow", static_cast<std::size_t>(b.width()) * 16}, {"rows", b.height()}}})}};
		} else if (const Bytes* bytes = item.value.tryAs<Bytes>()) {
			const std::size_t index = attach(bytes->data(), bytes->size());
			inputs[name] = {{"$nfk", "buffer"}, {"index", index}, {"as", "data"}, {"length", bytes->size()}};
		} else {
			inputs[name] = jsonOf(item.value);
		}
	}
	Json parameters = Json::object();
	for (const auto& [name, value] : request.parameters) {
		parameters[name] = jsonOf(value);
	}
	const std::string json = Json{{"inputs", inputs}, {"parameters", parameters}}.dump();
	const NfkPayload payload{sizeof(NfkPayload), static_cast<std::uint32_t>(buffers.size()), json.c_str(), buffers.data()};
	void* error = nullptr;
	void* reply = impl_->nfk.backendRun(impl_->backend, &payload, &error);
	if (reply == nullptr) {
		return impl_->failure("The InferKit run failed", error);
	}
	struct Release {
		const Functions& nfk;
		void* reply;
		~Release() { nfk.resultFree(reply); }
	} release{impl_->nfk, reply};

	const char* text = impl_->nfk.resultJson(reply);
	const Json document = Json::parse(text != nullptr ? text : "", nullptr, false);
	if (!document.is_object() || !document.contains("outputs") || !document["outputs"].is_object()) {
		return Error::make(ErrorCode::Codec, "InferKit returned a result with no outputs").with("model", impl_->catalogIdentifier);
	}
	const std::uint32_t count = impl_->nfk.resultBufferCount(reply);
	for (const auto& [name, entry] : document["outputs"].items()) {
		if (entry.is_object() && entry.value("$nfk", std::string()) == "buffer") {
			const std::uint32_t index = entry.value("index", std::uint32_t{0});
			const NfkBuffer* buffer = index < count ? impl_->nfk.resultBuffer(reply, index) : nullptr;
			if (buffer == nullptr) {
				return Error::make(ErrorCode::Codec, "InferKit named a buffer its result does not carry").with("output", name);
			}
			const std::string as = entry.value("as", std::string());
			if (as == "pixelBuffer") {
				InferenceImage image;
				if (Error e = imageFrom(entry, *buffer, image)) {
					return e.with("output", name);
				}
				result.outputs[name] = InferenceItem(std::move(image));
			} else if (as == "cgImage" || as == "image") {
				return Error::make(ErrorCode::Unsupported, "InferKit returned a PNG-encoded image; ask the model for a pixel buffer").with("output", name);
			} else {
				const std::uint64_t length = std::min<std::uint64_t>(entry.value("length", buffer->length), buffer->length);
				const auto* bytes = static_cast<const std::uint8_t*>(buffer->bytes);
				result.outputs[name] = InferenceItem(Value(bytes != nullptr ? Bytes(bytes, bytes + length) : Bytes()));
			}
		} else if (entry.is_boolean()) {
			result.outputs[name] = InferenceItem(Value(entry.get<bool>()));
		} else if (entry.is_number_integer()) {
			result.outputs[name] = InferenceItem(Value(static_cast<int>(entry.get<std::int64_t>())));
		} else if (entry.is_number()) {
			result.outputs[name] = InferenceItem(Value(entry.get<double>()));
		} else if (entry.is_string()) {
			result.outputs[name] = InferenceItem(Value(entry.get<std::string>()));
		} else {
			result.outputs[name] = InferenceItem(Value(entry.dump()));
		}
	}
	return Error::none();
}

OFXGRIP_NAMESPACE_END
