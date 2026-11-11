// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
// The InferKit backend against a stand-in library that exports InferKit's C interface: what a
// request carries across, every image form a result comes back in, and each failure. The real
// engine is not part of these tests.
#include <ofxgrip/inference/InferKitBackend.hpp>

#include <gtest/gtest.h>

using namespace ofxgrip;

namespace {

InferenceImage ramp(int width, int height, BitDepth depth = BitDepth::Float, Components components = Components::RGBA)
{
	ImageDesc desc;
	desc.bounds = {0, 0, width, height};
	desc.regionOfDefinition = desc.bounds;
	desc.depth = depth;
	desc.components = components;
	auto buffer = std::make_shared<ImageBuffer>(desc);
	const ImageView v = buffer->view();
	for (int y = 0; y < height; ++y) {
		for (int x = 0; x < width; ++x) {
			// Red is 0.25 on the bottom row and 0.75 above it; green is 0.5; alpha is 1.
			const float values[4] = {y == 0 ? 0.25f : 0.75f, 0.5f, 0.0f, 1.0f};
			for (std::size_t c = 0; c < componentCount(components); ++c) {
				writeComponent(v.pixel(x, y), depth, c, components == Components::Alpha ? 0.25f : values[c]);
			}
		}
	}
	return buffer;
}

float at(const ImageBuffer& buffer, int x, int y, std::size_t c)
{
	return readComponent(buffer.view().pixel(x, y), buffer.desc().depth, c);
}

std::shared_ptr<InferKitBackend> open(const std::string& model = "fake/invert", const std::string& options = {})
{
	std::shared_ptr<InferKitBackend> backend;
	const Error e = InferKitBackend::create(OFXGRIP_FAKE_INFERKIT, model, options, backend);
	EXPECT_TRUE(e.ok()) << e.describe();
	return backend;
}

InferenceRequest requestWith(InferenceImage image)
{
	InferenceRequest request;
	request.inputs["image"] = InferenceItem(std::move(image));
	return request;
}

}  // namespace

TEST(InferKitBackend, OpensPreparesAndRunsAModel)
{
	std::shared_ptr<InferKitBackend> backend = open();
	ASSERT_NE(backend, nullptr);
	EXPECT_EQ(backend->identifier(), "inferkit:fake/invert");
	EXPECT_EQ(backend->apiVersion(), 4u);
	EXPECT_FALSE(backend->isReady());
	ASSERT_TRUE(backend->prepare().ok());
	EXPECT_TRUE(backend->isReady());

	InferenceRequest request = requestWith(ramp(6, 2));
	request.inputs["prompt"] = InferenceItem(Value("a lighthouse"));
	request.inputs["blob"] = InferenceItem(Value(Bytes{1, 2, 3, 250}));
	request.parameters["strength"] = Value(0.5);
	request.parameters["steps"] = Value(12);
	request.parameters["tile"] = Value(true);
	request.parameters["center"] = Value(Point2D{3.0, 4.0});
	request.parameters["anchor"] = Value(Point3D{1.0, 2.0, 3.0});
	request.parameters["tint"] = Value(RGB{0.1, 0.2, 0.3});
	request.parameters["wash"] = Value(RGBA{0.1, 0.2, 0.3, 0.4});
	request.parameters["nothing"] = Value();
	InferenceResult result;
	const Error e = backend->run(request, result);
	ASSERT_TRUE(e.ok()) << e.describe();

	// The image crossed top row first and came back the right way up, inverted.
	const InferenceImage image = result.image("image");
	ASSERT_NE(image, nullptr);
	EXPECT_EQ(image->desc().bounds, (RectI{0, 0, 6, 2}));
	EXPECT_EQ(image->desc().depth, BitDepth::Float);
	EXPECT_FLOAT_EQ(at(*image, 0, 0, 0), 0.75f);
	EXPECT_FLOAT_EQ(at(*image, 5, 1, 0), 0.25f);
	EXPECT_FLOAT_EQ(at(*image, 2, 1, 1), 0.5f);
	EXPECT_FLOAT_EQ(at(*image, 2, 1, 3), 1.0f);
	// Scalars come back as values, bytes as bytes, and anything else as its JSON text.
	EXPECT_TRUE(result.outputs.at("prepared").value.as<bool>());
	EXPECT_EQ(result.outputs.at("buffers").value.as<int>(), 2);
	EXPECT_DOUBLE_EQ(result.outputs.at("score").value.as<double>(), 0.75);
	EXPECT_EQ(result.outputs.at("label").value.as<std::string>(), "inverted");
	EXPECT_EQ(result.outputs.at("blob").value.as<Bytes>(), (Bytes{1, 2, 3, 250}));
	const std::string echo = result.outputs.at("echo").value.as<std::string>();
	EXPECT_NE(echo.find("\"prompt\":\"a lighthouse\""), std::string::npos) << echo;
	EXPECT_NE(echo.find("\"strength\":0.5"), std::string::npos) << echo;
	EXPECT_NE(echo.find("\"steps\":12"), std::string::npos) << echo;
	EXPECT_NE(echo.find("\"tile\":true"), std::string::npos) << echo;
	EXPECT_NE(echo.find("\"center\":[3.0,4.0]"), std::string::npos) << echo;
	EXPECT_NE(echo.find("\"anchor\":[1.0,2.0,3.0]"), std::string::npos) << echo;
	EXPECT_NE(echo.find("\"tint\":[0.1,0.2,0.3]"), std::string::npos) << echo;
	EXPECT_NE(echo.find("\"wash\":[0.1,0.2,0.3,0.4]"), std::string::npos) << echo;
	EXPECT_NE(echo.find("\"nothing\":null"), std::string::npos) << echo;
	EXPECT_NE(echo.find("\"as\":\"data\""), std::string::npos) << echo;
}

TEST(InferKitBackend, ReadsEveryImageFormAndEveryInputLayout)
{
	struct Form {
		const char* name;
		BitDepth depth;
		Components components;
		float tolerance;
	};
	for (const Form& form : {Form{"float", BitDepth::Float, Components::RGBA, 0.0f}, Form{"half", BitDepth::Half, Components::RGBA, 0.001f}, Form{"rgba", BitDepth::UInt8, Components::RGBA, 0.003f},
	                         Form{"bgra", BitDepth::UInt8, Components::RGBA, 0.003f}, Form{"matte", BitDepth::Float, Components::Alpha, 0.0f}}) {
		std::shared_ptr<InferKitBackend> backend = open("fake/invert", std::string("{\"output\": \"") + form.name + "\"}");
		ASSERT_NE(backend, nullptr);
		InferenceResult result;
		const Error e = backend->run(requestWith(ramp(5, 2)), result);
		ASSERT_TRUE(e.ok()) << form.name << ": " << e.describe();
		const InferenceImage image = result.image("image");
		ASSERT_NE(image, nullptr) << form.name;
		EXPECT_EQ(image->desc().depth, form.depth) << form.name;
		EXPECT_EQ(image->desc().components, form.components) << form.name;
		EXPECT_NEAR(at(*image, 4, 0, 0), 0.75f, form.tolerance) << form.name;
		EXPECT_NEAR(at(*image, 4, 1, 0), 0.25f, form.tolerance) << form.name;
		if (form.components == Components::RGBA) {
			EXPECT_NEAR(at(*image, 1, 1, 1), 0.5f, form.tolerance) << form.name;
			EXPECT_NEAR(at(*image, 1, 1, 2), 1.0f, form.tolerance) << form.name;
			EXPECT_NEAR(at(*image, 1, 1, 3), 1.0f, form.tolerance) << form.name;
		}
	}
	// An input of any depth and layout crosses as four float channels.
	std::shared_ptr<InferKitBackend> backend = open();
	for (BitDepth depth : {BitDepth::UInt8, BitDepth::UInt16, BitDepth::Half}) {
		InferenceResult result;
		ASSERT_TRUE(backend->run(requestWith(ramp(4, 2, depth)), result).ok()) << toString(depth);
		EXPECT_NEAR(at(*result.image("image"), 0, 0, 0), 0.75f, 0.003f) << toString(depth);
	}
	InferenceResult rgb;
	ASSERT_TRUE(backend->run(requestWith(ramp(4, 2, BitDepth::Float, Components::RGB)), rgb).ok());
	EXPECT_FLOAT_EQ(at(*rgb.image("image"), 0, 1, 0), 0.25f);
	EXPECT_FLOAT_EQ(at(*rgb.image("image"), 0, 1, 3), 1.0f);
	InferenceResult alpha;
	ASSERT_TRUE(backend->run(requestWith(ramp(4, 2, BitDepth::Float, Components::Alpha)), alpha).ok());
	EXPECT_FLOAT_EQ(at(*alpha.image("image"), 0, 1, 0), 0.75f);
	EXPECT_FLOAT_EQ(at(*alpha.image("image"), 0, 1, 3), 0.25f);
}

TEST(InferKitBackend, ReportsEveryFailure)
{
	std::shared_ptr<InferKitBackend> backend;
	// No library, and a library that is not InferKit.
	EXPECT_EQ(InferKitBackend::create("/nonexistent/ofxgrip/libInferKitC.dylib", "fake/invert", {}, backend).code(), ErrorCode::MissingHostFeature);
#if defined(__APPLE__)
	const char* foreign = "/usr/lib/libz.dylib";
#elif defined(_WIN32)
	const char* foreign = "kernel32.dll";
#else
	const char* foreign = "libm.so.6";
#endif
	const Error notInferKit = InferKitBackend::create(foreign, "fake/invert", {}, backend);
	EXPECT_EQ(notInferKit.code(), ErrorCode::MissingHostFeature);
	EXPECT_NE(notInferKit.describe().find("lacks InferKit's C interface"), std::string::npos);
	// InferKit's own error crosses with its domain, code, and message.
	const Error missing = InferKitBackend::create(OFXGRIP_FAKE_INFERKIT, "fake/missing", {}, backend);
	EXPECT_EQ(missing.code(), ErrorCode::Render);
	EXPECT_NE(missing.describe().find("NFKModelCatalogErrorDomain"), std::string::npos) << missing.describe();
	EXPECT_NE(missing.describe().find("No model is listed under fake/missing"), std::string::npos) << missing.describe();
	EXPECT_EQ(InferKitBackend::create(OFXGRIP_FAKE_INFERKIT, "fake/silent", {}, backend).code(), ErrorCode::Render);
	EXPECT_EQ(backend, nullptr);

	std::shared_ptr<InferKitBackend> unprepared = open("fake/invert", "{\"prepare\": \"fail\"}");
	const Error prepare = unprepared->prepare();
	EXPECT_EQ(prepare.code(), ErrorCode::Render);
	EXPECT_NE(prepare.describe().find("The weights are missing"), std::string::npos);
	EXPECT_FALSE(unprepared->isReady());

	struct Case {
		const char* output;
		ErrorCode code;
	};
	for (const Case& c : {Case{"garbage", ErrorCode::Codec}, Case{"nooutputs", ErrorCode::Codec}, Case{"dangling", ErrorCode::Codec}, Case{"png", ErrorCode::Unsupported},
	                      Case{"unknown", ErrorCode::Unsupported}, Case{"short", ErrorCode::Codec}, Case{"nogeometry", ErrorCode::Codec}}) {
		std::shared_ptr<InferKitBackend> odd = open("fake/invert", std::string("{\"output\": \"") + c.output + "\"}");
		InferenceResult result;
		EXPECT_EQ(odd->run(requestWith(ramp(4, 2)), result).code(), c.code) << c.output;
	}
	// A run the engine refuses.
	std::shared_ptr<InferKitBackend> invert = open();
	InferenceRequest noImage;
	noImage.inputs["prompt"] = InferenceItem(Value("only words"));
	InferenceResult words;
	ASSERT_TRUE(invert->run(noImage, words).ok());
	EXPECT_EQ(words.image("image"), nullptr);
}
