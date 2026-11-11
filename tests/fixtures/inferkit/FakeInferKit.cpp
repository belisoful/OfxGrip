// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
// A stand-in for InferKit's C library, for the InferKitBackend tests. It exports the functions
// of NFKInferKitC.h that OfxGrip calls. Its one model inverts the colors of the `image` input.
// The catalog identifier and the options steer the form of the result and the failures.
#include <nlohmann/json.hpp>

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#if defined(_WIN32)
#define FAKE_EXPORT extern "C" __declspec(dllexport)
#else
#define FAKE_EXPORT extern "C" __attribute__((visibility("default")))
#endif

namespace {

using Json = nlohmann::json;

struct nfk_buffer {
	std::uint32_t struct_size;
	std::int32_t kind;
	const void* bytes;
	std::uint64_t length;
	void* surface;
	const char* path;
	std::int32_t descriptor;
	std::uint32_t reserved;
};
struct nfk_payload {
	std::uint32_t struct_size;
	std::uint32_t buffer_count;
	const char* json;
	const nfk_buffer* buffers;
};
struct nfk_error {
	std::string domain;
	std::int64_t code;
	std::string message;
};
struct nfk_backend {
	std::string identifier;
	Json options;
	bool prepared{false};
};
struct nfk_result {
	std::string json;
	std::vector<std::vector<std::uint8_t>> storage;
	std::vector<nfk_buffer> buffers;
};

void fail(nfk_error** out, const char* domain, std::int64_t code, const std::string& message)
{
	if (out != nullptr) {
		*out = new nfk_error{domain, code, message};
	}
}

std::uint16_t halfOf(float f)
{
	// Enough for the values the tests use: 0, 0.25, 0.5, 0.75, 1.
	std::uint32_t bits = 0;
	std::memcpy(&bits, &f, sizeof bits);
	if (f == 0.0f) {
		return 0;
	}
	const std::int32_t exponent = static_cast<std::int32_t>((bits >> 23) & 0xff) - 127 + 15;
	return static_cast<std::uint16_t>((static_cast<std::uint32_t>(exponent) << 10) | ((bits >> 13) & 0x3ff));
}

}  // namespace

FAKE_EXPORT std::uint32_t nfk_api_version()
{
	return (0u << 16) | 4u;
}

FAKE_EXPORT nfk_backend* nfk_backend_create(const char* identifier, const char* optionsJson, nfk_error** error)
{
	const std::string name = identifier != nullptr ? identifier : "";
	if (name == "fake/missing") {
		fail(error, "NFKModelCatalogErrorDomain", 1, "No model is listed under fake/missing");
		return nullptr;
	}
	if (name == "fake/silent") {
		return nullptr;  // a failure with no error object
	}
	auto* backend = new nfk_backend{name, Json::object()};
	if (optionsJson != nullptr) {
		backend->options = Json::parse(optionsJson, nullptr, false);
	}
	return backend;
}

FAKE_EXPORT std::int32_t nfk_backend_prepare(nfk_backend* backend, nfk_error** error)
{
	if (backend->options.value("prepare", std::string()) == "fail") {
		fail(error, "NFKInferenceErrorDomain", 7, "The weights are missing");
		return 0;
	}
	backend->prepared = true;
	return 1;
}

FAKE_EXPORT void nfk_backend_free(nfk_backend* backend)
{
	delete backend;
}

FAKE_EXPORT nfk_result* nfk_backend_run(nfk_backend* backend, const nfk_payload* request, nfk_error** error)
{
	const Json document = Json::parse(request->json, nullptr, false);
	if (!document.is_object() || !document.contains("inputs")) {
		fail(error, "NFKInferenceErrorDomain", 2, "The request lacks inputs");
		return nullptr;
	}
	const std::string form = backend->options.value("output", std::string("float"));
	auto* result = new nfk_result;
	if (form == "garbage") {
		result->json = "not json";
		return result;
	}
	Json outputs = Json::object();
	const Json& inputs = document["inputs"];
	if (form == "dangling") {
		outputs["image"] = {{"$nfk", "buffer"}, {"index", 5}, {"as", "pixelBuffer"}};
	} else if (inputs.contains("image")) {
		const Json& image = inputs["image"];
		const int width = image.value("width", 0);
		const int height = image.value("height", 0);
		const std::uint32_t index = image.value("index", std::uint32_t{0});
		if (index >= request->buffer_count || image.value("format", std::uint32_t{0}) != 1380410945u) {
			delete result;
			fail(error, "NFKInferenceErrorDomain", 4, "The image is not a float pixel buffer");
			return nullptr;
		}
		const auto* source = static_cast<const float*>(request->buffers[index].bytes);
		const std::size_t texels = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
		std::vector<float> inverted(texels * 4);
		for (std::size_t i = 0; i < texels; ++i) {
			inverted[i * 4] = 1.0f - source[i * 4];
			inverted[i * 4 + 1] = 1.0f - source[i * 4 + 1];
			inverted[i * 4 + 2] = 1.0f - source[i * 4 + 2];
			inverted[i * 4 + 3] = source[i * 4 + 3];
		}
		std::vector<std::uint8_t> bytes;
		std::uint32_t format = 1380410945u;
		std::size_t pixelBytes = 16;
		const std::size_t padding = 8;  // each row carries padding, as a real pixel buffer may
		if (form == "half") {
			format = 1380411457u;
			pixelBytes = 8;
		} else if (form == "bgra" || form == "rgba") {
			format = form == "bgra" ? 1111970369u : 1380401729u;
			pixelBytes = 4;
		} else if (form == "matte") {
			format = 1278226534u;
			pixelBytes = 4;
		} else if (form == "unknown") {
			format = 0x12345678u;
		}
		const std::size_t rowBytes = static_cast<std::size_t>(width) * pixelBytes + padding;
		const std::size_t offset = 16;
		bytes.assign(offset + rowBytes * static_cast<std::size_t>(height), 0);
		for (int y = 0; y < height; ++y) {
			for (int x = 0; x < width; ++x) {
				const float* texel = &inverted[(static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)) * 4];
				std::uint8_t* to = bytes.data() + offset + static_cast<std::size_t>(y) * rowBytes + static_cast<std::size_t>(x) * pixelBytes;
				if (format == 1380410945u || format == 0x12345678u) {
					std::memcpy(to, texel, 16);
				} else if (format == 1380411457u) {
					for (int c = 0; c < 4; ++c) {
						const std::uint16_t half = halfOf(texel[c]);
						std::memcpy(to + c * 2, &half, 2);
					}
				} else if (format == 1278226534u) {
					std::memcpy(to, &texel[0], 4);
				} else {
					const int order[4] = {format == 1111970369u ? 2 : 0, 1, format == 1111970369u ? 0 : 2, 3};
					for (int c = 0; c < 4; ++c) {
						to[c] = static_cast<std::uint8_t>(texel[order[c]] * 255.0f + 0.5f);
					}
				}
			}
		}
		std::size_t length = bytes.size();
		if (form == "short") {
			length = 8;
		}
		result->storage.push_back(std::move(bytes));
		outputs["image"] = {{"$nfk", "buffer"}, {"index", 0}, {"as", form == "png" ? "image" : "pixelBuffer"}, {"length", length}, {"format", format}, {"width", form == "nogeometry" ? 0 : width},
		                    {"height", height}, {"planes", Json::array({{{"offset", offset}, {"bytesPerRow", rowBytes}, {"rows", height}}})}};
		result->buffers.push_back(nfk_buffer{sizeof(nfk_buffer), 0, result->storage.back().data(), length, nullptr, nullptr, -1, 0});
	}
	// The other inputs and the parameters come back so a test sees what crossed.
	outputs["echo"] = Json{{"inputs", inputs}, {"parameters", document.value("parameters", Json::object())}};
	outputs["prepared"] = backend->prepared;
	outputs["buffers"] = static_cast<int>(request->buffer_count);
	outputs["score"] = 0.75;
	outputs["label"] = "inverted";
	if (inputs.contains("blob")) {
		const std::uint32_t index = inputs["blob"].value("index", std::uint32_t{0});
		const auto* data = static_cast<const std::uint8_t*>(request->buffers[index].bytes);
		const std::size_t length = static_cast<std::size_t>(request->buffers[index].length);
		result->storage.emplace_back(data, data + length);
		outputs["blob"] = {{"$nfk", "buffer"}, {"index", result->buffers.size()}, {"as", "data"}, {"length", length}};
		result->buffers.push_back(nfk_buffer{sizeof(nfk_buffer), 0, result->storage.back().data(), length, nullptr, nullptr, -1, 0});
	}
	result->json = form == "nooutputs" ? std::string("{\"other\": 1}") : Json{{"outputs", outputs}}.dump();
	return result;
}

FAKE_EXPORT const char* nfk_result_json(const nfk_result* result)
{
	return result->json.c_str();
}

FAKE_EXPORT std::uint32_t nfk_result_buffer_count(const nfk_result* result)
{
	return static_cast<std::uint32_t>(result->buffers.size());
}

FAKE_EXPORT const nfk_buffer* nfk_result_buffer(const nfk_result* result, std::uint32_t index)
{
	return index < result->buffers.size() ? &result->buffers[index] : nullptr;
}

FAKE_EXPORT void nfk_result_free(nfk_result* result)
{
	delete result;
}

FAKE_EXPORT const char* nfk_error_domain(const nfk_error* error)
{
	return error->domain.c_str();
}

FAKE_EXPORT std::int64_t nfk_error_code(const nfk_error* error)
{
	return error->code;
}

FAKE_EXPORT const char* nfk_error_message(const nfk_error* error)
{
	return error->message.c_str();
}

FAKE_EXPORT void nfk_error_free(nfk_error* error)
{
	delete error;
}
