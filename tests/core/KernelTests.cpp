// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
// The kernel contract on the host-neutral side: the reflection binder over slangc's Metal and C++
// JSON, the metadata scanner over the Slang source, the tile, the value packer, and the catalog's
// resolution rules. The fixtures are slangc's real output for the two probe kernels.
#include <ofxgrip/core/KernelCatalog.hpp>
#include <ofxgrip/core/KernelLayout.hpp>
#include <ofxgrip/core/KernelMetadata.hpp>
#include <ofxgrip/core/KernelTile.hpp>
#include <ofxgrip/core/KernelValues.hpp>

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>

using namespace ofxgrip;

namespace {

std::string fixture(const char* name)
{
	std::ifstream in(std::filesystem::path(OFXGRIP_KERNEL_FIXTURES) / name, std::ios::binary);
	std::ostringstream out;
	out << in.rdbuf();
	return out.str();
}

KernelLayout layoutOf(const char* name)
{
	KernelLayout layout;
	Error e = KernelLayout::parse(fixture(name), layout);
	EXPECT_TRUE(e.ok()) << e.describe();
	return layout;
}

KernelMetadata metadataOf(const char* name)
{
	KernelMetadata metadata;
	Error e = KernelMetadata::scan(fixture(name), metadata);
	EXPECT_TRUE(e.ok()) << e.describe();
	return metadata;
}

}  // namespace

TEST(KernelLayout, ReadsTheMetalReflectionOfTheBlurKernel)
{
	const KernelLayout layout = layoutOf("GripBlur.metal.json");
	EXPECT_EQ(layout.version(), "1.1");
	EXPECT_EQ(layout.entryPoint(), "fxGripKernel");
	EXPECT_EQ(layout.threadGroupSize(), (std::array<unsigned, 3>{8, 8, 1}));
	EXPECT_EQ(layout.form(), KernelBindingForm::Slots);
	EXPECT_TRUE(layout.hasUniforms());
	EXPECT_EQ(layout.uniformsIndex(), 0);
	EXPECT_EQ(layout.uniformsName(), "uniforms");
	EXPECT_EQ(layout.uniformsLength(), 48u);
	ASSERT_EQ(layout.members().size(), 4u);
	EXPECT_EQ(layout.members()[0].name, "radius");
	EXPECT_EQ(layout.members()[0].offset, 0u);
	EXPECT_EQ(layout.members()[0].scalarType, KernelScalarType::Float32);
	EXPECT_EQ(layout.members()[0].componentCount, 1u);
	EXPECT_EQ(layout.members()[1].name, "tint");
	EXPECT_EQ(layout.members()[1].offset, 16u);
	EXPECT_EQ(layout.members()[1].componentCount, 3u);
	EXPECT_EQ(layout.members()[2].name, "offset");
	EXPECT_EQ(layout.members()[2].scalarType, KernelScalarType::Int32);
	EXPECT_EQ(layout.members()[2].componentCount, 2u);
	EXPECT_EQ(layout.members()[3].name, "enabled");
	EXPECT_EQ(layout.members()[3].scalarType, KernelScalarType::Bool);
	EXPECT_EQ(layout.members()[3].length, 1u);
	EXPECT_TRUE(layout.hasTile());
	EXPECT_EQ(layout.tileIndex(), 1);
	ASSERT_EQ(layout.textures().size(), 3u);
	EXPECT_EQ(layout.textures()[0].name, "source");
	EXPECT_EQ(layout.textures()[0].slotIndex, 0);
	EXPECT_EQ(layout.textures()[0].mapIndex, 0u);
	EXPECT_EQ(layout.textures()[1].name, "matte");
	EXPECT_EQ(layout.textures()[1].slotIndex, 1);
	EXPECT_EQ(layout.textures()[2].name, "destination");
	EXPECT_EQ(layout.textures()[2].access, KernelTextureAccess::ReadWrite);
	EXPECT_EQ(layout.destination().slotIndex, 2);
	EXPECT_EQ(layout.textureCount(), 3u);
	ASSERT_EQ(layout.samplers().size(), 1u);
	EXPECT_EQ(layout.samplers()[0].name, "bilinear");
	EXPECT_EQ(layout.samplers()[0].slotIndex, 0);
	ASSERT_EQ(layout.constants().size(), 2u);
	EXPECT_EQ(layout.constants()[0].name, "TAPS");
	EXPECT_EQ(layout.constants()[0].slotIndex, 0);
	EXPECT_EQ(layout.constants()[0].scalarType, KernelScalarType::Int32);
	EXPECT_EQ(layout.constants()[1].name, "INVERT");
	EXPECT_EQ(layout.constants()[1].scalarType, KernelScalarType::Bool);
	EXPECT_NE(layout.memberNamed("tint"), nullptr);
	EXPECT_EQ(layout.memberNamed("nope"), nullptr);
	EXPECT_NE(layout.textureNamed("matte"), nullptr);
	EXPECT_EQ(layout.textureNamed("nope"), nullptr);
	EXPECT_NE(layout.constantNamed("TAPS"), nullptr);
	EXPECT_EQ(layout.constantNamed("nope"), nullptr);
}

TEST(KernelLayout, ReadsTheCppReflectionAsABlockLayout)
{
	const KernelLayout layout = layoutOf("GripBlur.cpp.json");
	EXPECT_EQ(layout.form(), KernelBindingForm::Block);
	EXPECT_EQ(layout.globalsLength(), 53u);
	EXPECT_TRUE(layout.hasUniforms());
	EXPECT_EQ(layout.uniformsIndex(), kKernelNoIndex);
	EXPECT_EQ(layout.uniformsOffset(), 0u);
	EXPECT_TRUE(layout.hasTile());
	EXPECT_EQ(layout.tileOffset(), 8u);
	ASSERT_EQ(layout.textures().size(), 3u);
	EXPECT_EQ(layout.textures()[0].blockOffset, 16u);
	EXPECT_EQ(layout.textures()[0].mapIndex, 0u);
	EXPECT_EQ(layout.textures()[1].blockOffset, 24u);
	EXPECT_EQ(layout.textures()[1].mapIndex, 1u);
	EXPECT_EQ(layout.destination().blockOffset, 32u);
	EXPECT_EQ(layout.destination().mapIndex, 2u);
	ASSERT_EQ(layout.samplers().size(), 1u);
	EXPECT_EQ(layout.samplers()[0].blockOffset, 40u);
	ASSERT_EQ(layout.constants().size(), 2u);
	EXPECT_EQ(layout.constants()[0].blockOffset, 48u);
	EXPECT_EQ(layout.constants()[0].slotIndex, kKernelNoIndex);
	EXPECT_EQ(layout.constants()[1].blockOffset, 52u);
	// The C++ uniforms struct packs float3 at 4-byte alignment.
	EXPECT_EQ(layout.members()[1].offset, 4u);
	EXPECT_EQ(layout.uniformsLength(), 28u);
}

TEST(KernelLayout, ReadsTheInspectKernelWithEveryScalarKind)
{
	const KernelLayout layout = layoutOf("GripInspect.metal.json");
	ASSERT_EQ(layout.members().size(), 9u);
	EXPECT_EQ(layout.members()[2].name, "center");
	EXPECT_EQ(layout.members()[2].componentCount, 2u);
	EXPECT_EQ(layout.members()[4].scalarType, KernelScalarType::UInt32);
	EXPECT_EQ(layout.members()[5].scalarType, KernelScalarType::Float16);
	EXPECT_EQ(layout.members()[6].scalarType, KernelScalarType::Int16);
	EXPECT_EQ(layout.members()[7].scalarType, KernelScalarType::UInt8);
	EXPECT_EQ(layout.textures().size(), 5u);
	EXPECT_EQ(layout.constants().size(), 3u);
	EXPECT_EQ(layout.constants()[2].scalarType, KernelScalarType::Float32);
}

TEST(KernelLayout, RejectsWhatBreaksTheContract)
{
	KernelLayout out;
	EXPECT_EQ(KernelLayout::parse("not json", out).code(), ErrorCode::Codec);
	EXPECT_EQ(KernelLayout::parse("[]", out).code(), ErrorCode::Codec);
	EXPECT_EQ(KernelLayout::parse(R"({"version":"2.0","parameters":[],"entryPoints":[]})", out).code(), ErrorCode::Codec);
	EXPECT_EQ(KernelLayout::parse(R"({"version":"x","parameters":[],"entryPoints":[]})", out).code(), ErrorCode::Codec);
	EXPECT_EQ(KernelLayout::parse(R"({"version":"1.1","parameters":[],"entryPoints":[]})", out).code(), ErrorCode::InvalidParameter);
	const std::string entry = R"("entryPoints":[{"name":"k","stage":"compute","threadGroupSize":[8,8,1],"parameters":[]}])";
	const std::string destination = R"({"name":"d","binding":{"kind":"shaderResource","index":0},"type":{"kind":"resource","baseShape":"texture2D","access":"readWrite"}})";
	auto doc = [&](const std::string& params, const std::string& entries = {}) { return "{\"version\":\"1.1\",\"parameters\":[" + params + "]," + (entries.empty() ? entry : entries) + "}"; };
	EXPECT_TRUE(KernelLayout::parse(doc(destination), out).ok());
	EXPECT_EQ(KernelLayout::parse(doc(""), out).code(), ErrorCode::InvalidParameter);  // no destination
	EXPECT_EQ(KernelLayout::parse(doc(destination + "," + destination), out).code(), ErrorCode::InvalidParameter);  // two
	EXPECT_EQ(KernelLayout::parse(doc(destination, R"("entryPoints":[{"name":"","stage":"compute","threadGroupSize":[8,8,1]}])"), out).code(), ErrorCode::InvalidParameter);
	EXPECT_EQ(KernelLayout::parse(doc(destination, R"("entryPoints":[{"name":"k","stage":"fragment","threadGroupSize":[8,8,1]}])"), out).code(), ErrorCode::InvalidParameter);
	EXPECT_EQ(KernelLayout::parse(doc(destination, R"("entryPoints":[{"name":"k","stage":"compute","threadGroupSize":[8,0,1]}])"), out).code(), ErrorCode::InvalidParameter);
	EXPECT_EQ(KernelLayout::parse(doc(destination, R"("entryPoints":[{"name":"k","stage":"compute","threadGroupSize":[8,8]}])"), out).code(), ErrorCode::InvalidParameter);
	EXPECT_EQ(KernelLayout::parse(doc(destination, R"("entryPoints":[{"name":"k","stage":"compute","threadGroupSize":[8,8,1],"parameters":[{"name":"p","binding":{"kind":"uniform"}}]}])"), out).code(), ErrorCode::InvalidParameter);
	EXPECT_EQ(KernelLayout::parse(doc(R"({"name":"","binding":{"kind":"shaderResource","index":0}})"), out).code(), ErrorCode::InvalidParameter);
	EXPECT_EQ(KernelLayout::parse(doc(R"({"name":"x","binding":{"kind":"shaderResource"}})"), out).code(), ErrorCode::InvalidParameter);
	EXPECT_EQ(KernelLayout::parse(doc(R"({"name":"x","binding":{"kind":"weird","index":0}})"), out).code(), ErrorCode::InvalidParameter);
	EXPECT_EQ(KernelLayout::parse(doc(R"({"name":"x","binding":{"kind":"shaderResource","index":0},"type":{"kind":"resource","baseShape":"texture3D"}})"), out).code(), ErrorCode::InvalidParameter);
	EXPECT_EQ(KernelLayout::parse(doc(R"({"name":"x","binding":{"kind":"shaderResource","index":0},"type":{"kind":"resource","baseShape":"texture2D","access":"append"}})"), out).code(), ErrorCode::InvalidParameter);
	EXPECT_EQ(KernelLayout::parse(doc(R"({"name":"c","binding":{"kind":"specializationConstant","index":0},"type":{"kind":"vector","elementCount":2,"elementType":{"kind":"scalar","scalarType":"int32"}}})"), out).code(), ErrorCode::InvalidParameter);
	EXPECT_EQ(KernelLayout::parse(doc(R"({"name":"u","binding":{"kind":"constantBuffer","index":0},"type":{"kind":"constantBuffer","elementType":{"kind":"scalar"}}})"), out).code(), ErrorCode::InvalidParameter);
	EXPECT_EQ(KernelLayout::parse(doc(R"({"name":"u","binding":{"kind":"constantBuffer","index":0},"type":{"kind":"constantBuffer","elementType":{"kind":"struct","name":"U"}}})"), out).code(), ErrorCode::InvalidParameter);
	const std::string tile = R"({"name":"t","binding":{"kind":"constantBuffer","index":0},"type":{"kind":"constantBuffer","elementType":{"kind":"struct","name":"GripTile","sizes":[{"kind":"uniform","value":544}]}}})";
	const std::string smallTile = R"({"name":"t","binding":{"kind":"constantBuffer","index":0},"type":{"kind":"constantBuffer","elementType":{"kind":"struct","name":"GripTile","sizes":[{"kind":"uniform","value":16}]}}})";
	EXPECT_TRUE(KernelLayout::parse(doc(tile + "," + destination), out).ok());
	EXPECT_EQ(KernelLayout::parse(doc(tile + "," + tile + "," + destination), out).code(), ErrorCode::InvalidParameter);
	EXPECT_EQ(KernelLayout::parse(doc(smallTile + "," + destination), out).code(), ErrorCode::InvalidParameter);
	const std::string uniforms = R"({"name":"u","binding":{"kind":"constantBuffer","index":1},"type":{"kind":"constantBuffer","elementType":{"kind":"struct","name":"U","sizes":[{"kind":"uniform","value":8}],"fields":[{"name":"a","binding":{"kind":"uniform","offset":0,"size":4},"type":{"kind":"scalar","scalarType":"float32"}},{"name":"arr","binding":{"kind":"uniform","offset":4,"size":4},"type":{"kind":"array"}}]}}})";
	ASSERT_TRUE(KernelLayout::parse(doc(uniforms + "," + destination), out).ok());
	ASSERT_EQ(out.members().size(), 2u);
	EXPECT_EQ(out.members()[1].scalarType, KernelScalarType::None);
	EXPECT_EQ(out.members()[1].componentCount, 0u);
	EXPECT_EQ(KernelLayout::parse(doc(uniforms + "," + uniforms + "," + destination), out).code(), ErrorCode::InvalidParameter);
	const std::string badMember = R"({"name":"u","binding":{"kind":"constantBuffer","index":1},"type":{"kind":"constantBuffer","elementType":{"kind":"struct","name":"U","sizes":[{"kind":"uniform","value":8}],"fields":[{"name":"a"}]}}})";
	EXPECT_EQ(KernelLayout::parse(doc(badMember + "," + destination), out).code(), ErrorCode::InvalidParameter);
	// Block layouts: a global of an unbound type kind, and one without offsets.
	const std::string blockDest = R"({"name":"d","binding":{"kind":"uniform","offset":0,"size":8},"type":{"kind":"resource","baseShape":"texture2D","access":"readWrite"}})";
	EXPECT_TRUE(KernelLayout::parse(doc(blockDest), out).ok());
	EXPECT_EQ(out.form(), KernelBindingForm::Block);
	EXPECT_EQ(KernelLayout::parse(doc(blockDest + R"(,{"name":"x","binding":{"kind":"uniform","offset":8,"size":8},"type":{"kind":"array"}})"), out).code(), ErrorCode::InvalidParameter);
	EXPECT_EQ(KernelLayout::parse(doc(blockDest + R"(,{"name":"x","binding":{"kind":"uniform"},"type":{"kind":"scalar","scalarType":"int32"}})"), out).code(), ErrorCode::InvalidParameter);
	EXPECT_EQ(kernelScalarSize(KernelScalarType::None), 0u);
	EXPECT_EQ(toString(KernelScalarType::None), "none");
	EXPECT_EQ(parseKernelScalarType("float64"), KernelScalarType::None);
	EXPECT_EQ(parseKernelScalarType("uint16"), KernelScalarType::UInt16);
}

TEST(KernelMetadata, ConvertsJson5)
{
	EXPECT_EQ(json5ToJson("{ a: 1, b: 'x', c: [1, 2,], } // end"), R"({ "a": 1, "b": "x", "c": [1, 2] } )");
	EXPECT_EQ(json5ToJson("{ a: .5, b: 5., c: +3, d: -.5, e: 0x1F, f: true, g: null, h: 1e3 }"), R"({ "a": 0.5, "b": 5.0, "c": 3, "d": -0.5, "e": 31, "f": true, "g": null, "h": 1e3 })");
	EXPECT_EQ(json5ToJson("{ 'it\\'s': \"q\\\"x\", y: 'say \"hi\"' /* c */ }"), R"({ "it's": "q\"x", "y": "say \"hi\""  })");
	EXPECT_EQ(json5ToJson("{ $k_1: Infinity }"), R"({ "$k_1": Infinity })");
}

TEST(KernelMetadata, ScansTheBlurBlock)
{
	const KernelMetadata metadata = metadataOf("GripBlur.slang");
	EXPECT_EQ(metadata.blockCount, 1);
	EXPECT_EQ(metadata.colorSpace.transfer, "linear");
	EXPECT_EQ(metadata.colorSpace.primaries, "rec709");
	ASSERT_EQ(metadata.parameters.size(), 5u);
	EXPECT_EQ(metadata.parameters[0].key, "radius");
	EXPECT_EQ(metadata.parameters[0].label, "Radius");
	EXPECT_EQ(metadata.parameters[0].type, ParameterType::Float);
	EXPECT_EQ(metadata.parameters[0].defaultValue.toDouble(), 8.0);
	EXPECT_EQ(metadata.parameters[0].maximum, std::vector<double>{100.0});
	EXPECT_EQ(metadata.parameters[1].type, ParameterType::Int);
	EXPECT_EQ(metadata.parameters[2].type, ParameterType::Toggle);
	EXPECT_EQ(metadata.parameters[3].type, ParameterType::Group);
	EXPECT_EQ(metadata.parameters[3].key, "Tint");  // derived from the label
	ASSERT_EQ(metadata.parameters[3].children.size(), 1u);
	EXPECT_EQ(metadata.parameters[3].children[0].defaultValue, Value(RGB{1, 1, 1}));
	EXPECT_EQ(metadata.parameters[4].type, ParameterType::ImageRef);
	ASSERT_EQ(metadata.bindings.size(), 5u);
	EXPECT_EQ(metadata.bindings[0].key, "radius");
	EXPECT_TRUE(metadata.bindings[0].pixelUnits);
	EXPECT_FALSE(metadata.bindings[0].constant);
	EXPECT_EQ(metadata.bindings[1].key, "TAPS");
	EXPECT_TRUE(metadata.bindings[1].constant);
	EXPECT_EQ(metadata.bindings[2].key, "INVERT");
	EXPECT_EQ(metadata.bindings[3].key, "tint");
	EXPECT_EQ(metadata.bindings[3].id, 11u);
	EXPECT_EQ(metadata.bindings[4].key, "matte");
	EXPECT_EQ(metadata.constantKeys(), (std::vector<std::string>{"TAPS", "INVERT"}));
	EXPECT_NE(metadata.bindingForKey("tint"), nullptr);
	EXPECT_EQ(metadata.bindingForKey("nope"), nullptr);
	EXPECT_NE(metadata.bindingForId(12), nullptr);
	EXPECT_EQ(metadata.bindingForId(99), nullptr);
}

TEST(KernelMetadata, ScansSeveralBlocksAndRejectsBrokenOnes)
{
	KernelMetadata out;
	EXPECT_TRUE(KernelMetadata::scan("void main() {}", out).ok());
	EXPECT_EQ(out.blockCount, 0);
	EXPECT_TRUE(out.parameters.empty());
	const char* two = "/* FxGripShader { parameters: [ { id: 1, type: 'float', name: 'A', key: 'a' } ], note: 'x', n: 2, f: 1.5, on: true, list: [1] } */\n"
	                  "  /* FxGripShader { parameters: [ { id: 2, type: 'integer', name: 'B', key: 'b', constant: true } ], colorSpace: { primaries: 'rec2020' } } */\n"
	                  "x /* FxGripShader { ignored: 1 } */\n"
	                  "/* FxGripShaderX {} */\n";
	ASSERT_TRUE(KernelMetadata::scan(two, out).ok());
	EXPECT_EQ(out.blockCount, 2);
	ASSERT_EQ(out.parameters.size(), 2u);
	EXPECT_EQ(out.bindings[1].constant, true);
	EXPECT_EQ(out.colorSpace.primaries, "rec2020");
	EXPECT_EQ(out.properties.at("note"), Value("x"));
	EXPECT_EQ(out.properties.at("n"), Value(2));
	EXPECT_EQ(out.properties.at("f"), Value(1.5));
	EXPECT_EQ(out.properties.at("on"), Value(true));
	EXPECT_EQ(out.properties.at("list"), Value("[1]"));
	EXPECT_EQ(KernelMetadata::scan("/* FxGripShader { broken */", out).code(), ErrorCode::Codec);
	EXPECT_EQ(KernelMetadata::scan("/* FxGripShader [1] */", out).code(), ErrorCode::Codec);
	EXPECT_EQ(KernelMetadata::scan("/* FxGripShader { parameters: 3 } */", out).code(), ErrorCode::InvalidParameter);
	EXPECT_EQ(KernelMetadata::scan("/* FxGripShader { note: 1 } */\n/* FxGripShader { note: 2 } */", out).code(), ErrorCode::InvalidParameter);
	EXPECT_EQ(KernelMetadata::scan("/* FxGripShader { colorSpace: 1 } */", out).code(), ErrorCode::InvalidParameter);
	EXPECT_EQ(KernelMetadata::scan("/* FxGripShader { parameters: [ { id: 1, type: 'point', key: 'p', constant: true } ] } */", out).code(), ErrorCode::InvalidParameter);
	EXPECT_EQ(KernelMetadata::scan("/* FxGripShader { parameters: [ { id: 1, type: 'float', key: 'p', constant: true } ] } */", out).code(), ErrorCode::InvalidParameter);
	EXPECT_TRUE(KernelMetadata::scan("/* FxGripShader { parameters: [ { id: 1, type: 'float', key: 'p', constant: true, allowContinuousConstant: true } ] } */", out).ok());
	EXPECT_EQ(KernelMetadata::scan("/* FxGripShader { parameters: [ { id: 1, type: 'float', key: 'p', constant: 'yes' } ] } */", out).code(), ErrorCode::InvalidManifest);
	EXPECT_TRUE(KernelMetadata::scan("/* FxGripShader { parameters: [ { id: 1, type: 'float', name: 'no key' }, { id: 2, type: 'float', name: '9 lives' }, { id: 3, type: 'float' } ] } */", out).ok());
	EXPECT_TRUE(out.bindings.empty());
	EXPECT_EQ(out.parameters[0].key, "no_key");
	EXPECT_EQ(out.parameters[1].key, "p2_9_lives");
	EXPECT_EQ(out.parameters[2].key, "p3");
}

TEST(KernelTile, MapsDestinationPixelsIntoTextures)
{
	const GripTileMap ascend = gripTileMapFor(RectI{10, 20, 30, 60}, true);
	// Pixel (10, 20) maps to (0, 0); (30, 60) maps to (1, 1).
	EXPECT_NEAR(ascend.u[0] * 10.0f + ascend.u[2], 0.0f, 1e-6f);
	EXPECT_NEAR(ascend.u[0] * 30.0f + ascend.u[2], 1.0f, 1e-6f);
	EXPECT_NEAR(ascend.v[1] * 20.0f + ascend.v[2], 0.0f, 1e-6f);
	EXPECT_NEAR(ascend.v[1] * 60.0f + ascend.v[2], 1.0f, 1e-6f);
	const GripTileMap descend = gripTileMapFor(RectI{10, 20, 30, 60}, false);
	EXPECT_NEAR(descend.v[1] * 60.0f + descend.v[2], 0.0f, 1e-6f);
	EXPECT_NEAR(descend.v[1] * 20.0f + descend.v[2], 1.0f, 1e-6f);
	const GripTileMap degenerate = gripTileMapFor(RectI{0, 0, 0, 0}, true);
	EXPECT_FLOAT_EQ(degenerate.u[0], 1.0f);
	const GripTile tile = makeGripTile(RectI{2, 3, 12, 8}, {0.5, 0.5}, {RectI{0, 0, 16, 16}, std::nullopt, RectI{2, 3, 12, 8}}, true);
	EXPECT_EQ(tile.destinationBounds[0], 2.0f);
	EXPECT_EQ(tile.destinationBounds[3], 8.0f);
	EXPECT_EQ(tile.pixelScale[0], 0.5f);
	EXPECT_EQ(tile.textureCount, 3u);
	EXPECT_EQ(tile.reserved, kGripTileRowsAscend);
	EXPECT_EQ(tile.textures[1].u[0], 0.0f);
	EXPECT_FLOAT_EQ(tile.textures[0].u[0], 1.0f / 16.0f);
	unsigned tx = 0;
	unsigned ty = 0;
	EXPECT_TRUE(gripTexelFor(tile, 2, 3, tx, ty));
	EXPECT_EQ(tx, 0u);
	EXPECT_EQ(ty, 0u);
	EXPECT_TRUE(gripTexelFor(tile, 11, 7, tx, ty));
	EXPECT_EQ(tx, 9u);
	EXPECT_EQ(ty, 4u);
	EXPECT_FALSE(gripTexelFor(tile, 12, 7, tx, ty));
	EXPECT_FALSE(gripTexelFor(tile, 2, 2, tx, ty));
	const GripTile topDown = makeGripTile(RectI{2, 3, 12, 8}, {1, 1}, {}, false);
	EXPECT_TRUE(gripTexelFor(topDown, 2, 7, tx, ty));
	EXPECT_EQ(ty, 0u);
	EXPECT_EQ(topDown.textureCount, 0u);
	std::vector<std::optional<RectI>> many(20, RectI{0, 0, 1, 1});
	EXPECT_EQ(makeGripTile(RectI{0, 0, 1, 1}, {1, 1}, many, true).textureCount, 16u);
}

TEST(KernelValues, ResolvesAndPacksTheBlurKernel)
{
	const KernelLayout layout = layoutOf("GripBlur.metal.json");
	const KernelMetadata metadata = metadataOf("GripBlur.slang");
	KernelBindingPlan plan;
	Error e = KernelBindingPlan::resolve(layout, metadata, plan);
	ASSERT_TRUE(e.ok()) << e.describe();
	ASSERT_EQ(plan.members.size(), 2u);  // radius, tint; offset and enabled have no record
	EXPECT_EQ(plan.members[0].key, "radius");
	EXPECT_TRUE(plan.members[0].pixelUnits);
	EXPECT_EQ(plan.members[1].key, "tint");
	EXPECT_EQ(plan.members[1].componentCount, 3u);
	ASSERT_EQ(plan.textures.size(), 2u);
	EXPECT_EQ(plan.textures[0].positional, 0);
	EXPECT_EQ(plan.textures[0].id, kParameterIdNone);
	EXPECT_EQ(plan.textures[1].id, 12u);
	EXPECT_EQ(plan.textures[1].positional, -1);
	ASSERT_EQ(plan.constants.size(), 2u);
	EXPECT_EQ(plan.constants[0].key, "TAPS");
	EXPECT_EQ(plan.constants[0].slotIndex, 0);
	EXPECT_EQ(plan.constants[1].scalarType, KernelScalarType::Bool);

	KernelValueMap values;
	values[1] = Value(4.0);
	values[11] = Value(RGB{0.5, 0.25, 1.0});
	values[2] = Value(5);
	values[3] = Value(true);
	const Bytes uniforms = packKernelUniforms(layout, plan, values, 2.0);
	ASSERT_EQ(uniforms.size(), 48u);
	EXPECT_EQ(readKernelComponents(KernelScalarType::Float32, 1, uniforms.data())[0], 8.0);  // radius × pixel scale
	const std::vector<double> tint = readKernelComponents(KernelScalarType::Float32, 3, uniforms.data() + 16);
	EXPECT_EQ(tint, (std::vector<double>{0.5, 0.25, 1.0}));
	EXPECT_EQ(uniforms[40], 0);  // enabled stays zero
	const std::vector<KernelConstantValue> constants = packKernelConstants(plan, values, 2.0);
	ASSERT_EQ(constants.size(), 2u);
	EXPECT_EQ(readKernelComponents(KernelScalarType::Int32, 1, constants[0].bytes.data())[0], 5.0);
	EXPECT_EQ(constants[1].bytes[0], 1);
	EXPECT_EQ(constants[1].length, 1u);
	const std::string signature = kernelConstantSignature(constants);
	EXPECT_EQ(signature, "0=5.0.0.0;1=1;");
	values.erase(2);
	EXPECT_EQ(kernelConstantSignature(packKernelConstants(plan, values, 1.0)), "0=0.0.0.0;1=1;");
	// A block layout packs the same values at the C++ offsets.
	const KernelLayout block = layoutOf("GripBlur.cpp.json");
	KernelBindingPlan blockPlan;
	ASSERT_TRUE(KernelBindingPlan::resolve(block, metadata, blockPlan).ok());
	const Bytes blockUniforms = packKernelUniforms(block, blockPlan, values, 1.0);
	ASSERT_EQ(blockUniforms.size(), 28u);
	EXPECT_EQ(readKernelComponents(KernelScalarType::Float32, 3, blockUniforms.data() + 4), (std::vector<double>{0.5, 0.25, 1.0}));
	EXPECT_EQ(blockPlan.constants[0].blockOffset, 48u);
	EXPECT_NE(kernelConstantSignature(packKernelConstants(blockPlan, values, 1.0)).find("@48="), std::string::npos);
}

TEST(KernelValues, ConvertsEveryScalarType)
{
	std::uint8_t bytes[16] = {};
	writeKernelComponents(KernelScalarType::Float16, 2, {1.5, -2.0}, bytes);
	EXPECT_EQ(readKernelComponents(KernelScalarType::Float16, 2, bytes), (std::vector<double>{1.5, -2.0}));
	writeKernelComponents(KernelScalarType::UInt32, 1, {-3.0}, bytes);
	EXPECT_EQ(readKernelComponents(KernelScalarType::UInt32, 1, bytes)[0], 0.0);
	writeKernelComponents(KernelScalarType::Int16, 1, {-7.4}, bytes);
	EXPECT_EQ(readKernelComponents(KernelScalarType::Int16, 1, bytes)[0], -7.0);
	writeKernelComponents(KernelScalarType::UInt16, 1, {65535.0}, bytes);
	EXPECT_EQ(readKernelComponents(KernelScalarType::UInt16, 1, bytes)[0], 65535.0);
	writeKernelComponents(KernelScalarType::Int8, 1, {-5.0}, bytes);
	EXPECT_EQ(readKernelComponents(KernelScalarType::Int8, 1, bytes)[0], -5.0);
	writeKernelComponents(KernelScalarType::UInt8, 1, {250.0}, bytes);
	EXPECT_EQ(readKernelComponents(KernelScalarType::UInt8, 1, bytes)[0], 250.0);
	writeKernelComponents(KernelScalarType::Bool, 1, {2.0}, bytes);
	EXPECT_EQ(readKernelComponents(KernelScalarType::Bool, 1, bytes)[0], 1.0);
	writeKernelComponents(KernelScalarType::None, 1, {2.0}, bytes);
	EXPECT_TRUE(readKernelComponents(KernelScalarType::None, 1, bytes).empty());
	EXPECT_TRUE(readKernelComponents(KernelScalarType::Int32, 1, nullptr).empty());
	EXPECT_EQ(kernelComponents(Value(true)), std::vector<double>{1.0});
	EXPECT_EQ(kernelComponents(Value(3)), std::vector<double>{3.0});
	EXPECT_EQ(kernelComponents(Value(Point2D{1, 2})), (std::vector<double>{1.0, 2.0}));
	EXPECT_EQ(kernelComponents(Value(Point3D{1, 2, 3})), (std::vector<double>{1.0, 2.0, 3.0}));
	EXPECT_EQ(kernelComponents(Value(RGBA{1, 2, 3, 4})), (std::vector<double>{1.0, 2.0, 3.0, 4.0}));
	EXPECT_TRUE(kernelComponents(Value("x")).empty());
}

TEST(KernelValues, RejectsBindingsTheKernelCannotFill)
{
	const KernelLayout layout = layoutOf("GripBlur.metal.json");
	KernelMetadata metadata;
	KernelBindingPlan plan;
	// A string record named like a member.
	ASSERT_TRUE(KernelMetadata::scan("/* FxGripShader { parameters: [ { id: 1, type: 'string', key: 'radius' } ] } */", metadata).ok());
	EXPECT_EQ(KernelBindingPlan::resolve(layout, metadata, plan).code(), ErrorCode::InvalidParameter);
	// A point record on a scalar member.
	ASSERT_TRUE(KernelMetadata::scan("/* FxGripShader { parameters: [ { id: 1, type: 'point', key: 'radius' } ] } */", metadata).ok());
	EXPECT_EQ(KernelBindingPlan::resolve(layout, metadata, plan).code(), ErrorCode::InvalidParameter);
	// The destination keyed to a parameter.
	ASSERT_TRUE(KernelMetadata::scan("/* FxGripShader { parameters: [ { id: 1, type: 'imageref', key: 'destination' } ] } */", metadata).ok());
	EXPECT_EQ(KernelBindingPlan::resolve(layout, metadata, plan).code(), ErrorCode::InvalidParameter);
	// A float keyed to a texture.
	ASSERT_TRUE(KernelMetadata::scan("/* FxGripShader { parameters: [ { id: 1, type: 'float', key: 'matte' } ] } */", metadata).ok());
	EXPECT_EQ(KernelBindingPlan::resolve(layout, metadata, plan).code(), ErrorCode::InvalidParameter);
	// A constant with a key the kernel lacks is ignored; one on a vector record fails.
	ASSERT_TRUE(KernelMetadata::scan("/* FxGripShader { parameters: [ { id: 1, type: 'integer', key: 'NOPE', constant: true } ] } */", metadata).ok());
	ASSERT_TRUE(KernelBindingPlan::resolve(layout, metadata, plan).ok());
	EXPECT_TRUE(plan.constants.empty());
	EXPECT_EQ(plan.textures.size(), 2u);
	EXPECT_EQ(plan.textures[1].positional, 1);
}

TEST(KernelCatalog, ResolvesTheMostSpecificImplementation)
{
	KernelCatalog catalog;
	KernelRecord blur;
	blur.name = "blur";
	blur.implementations.push_back(KernelImplementation{GpuApi::Metal, true, "", "", "msl", "{}", "k", nullptr});
	blur.implementations.push_back(KernelImplementation{GpuApi::Metal, false, "resolve", "macos", "native-resolve", "", "k", nullptr});
	blur.implementations.push_back(KernelImplementation{GpuApi::Metal, false, "", "macos", "native-mac", "", "k", nullptr});
	blur.implementations.push_back(KernelImplementation{GpuApi::OpenCL, false, "", "", "cl", "", "k", nullptr});
	blur.implementations.push_back(KernelImplementation{GpuApi::None, true, "", "", "cpp", "{}", "k", nullptr});
	catalog.add(blur);
	KernelRecord sharpen;
	sharpen.name = "sharpen";
	sharpen.implementations.push_back(KernelImplementation{GpuApi::Metal, true, "", "", "msl", "{}", "k", nullptr});
	catalog.add(sharpen);
	catalog.add(sharpen);  // a second add replaces
	EXPECT_EQ(catalog.kernels().size(), 2u);
	EXPECT_NE(catalog.find("blur"), nullptr);
	EXPECT_EQ(catalog.find("nope"), nullptr);
	EXPECT_EQ(catalog.resolve("blur", GpuApi::Metal, "resolve", "macos")->source, "native-resolve");
	EXPECT_EQ(catalog.resolve("blur", GpuApi::Metal, "nuke", "macos")->source, "native-mac");
	EXPECT_EQ(catalog.resolve("blur", GpuApi::Metal, "nuke", "linux")->source, "msl");
	EXPECT_EQ(catalog.resolve("blur", GpuApi::OpenCL, "nuke", "linux")->source, "cl");
	EXPECT_EQ(catalog.resolve("blur", GpuApi::None, "", "")->source, "cpp");
	EXPECT_EQ(catalog.resolve("blur", GpuApi::Cuda, "", ""), nullptr);
	EXPECT_EQ(catalog.resolve("nope", GpuApi::Metal, "", ""), nullptr);
	EXPECT_TRUE(catalog.everyKernelResolves(GpuApi::Metal, "resolve", "macos"));
	EXPECT_FALSE(catalog.everyKernelResolves(GpuApi::OpenCL, "", ""));  // sharpen has no OpenCL build
	EXPECT_FALSE(catalog.everyKernelResolves(GpuApi::None, "", ""));
	EXPECT_FALSE(KernelCatalog().everyKernelResolves(GpuApi::Metal, "", ""));
	Capabilities base;
	base.metalRender = true;
	base.openCLRender = true;
	base.cudaRender = true;
	base.cudaStream = true;
	base.openGLRender = true;
	base.cpuRender = true;
	const Capabilities declared = catalog.capabilitiesFor(base, "resolve", "macos");
	EXPECT_TRUE(declared.metalRender);
	EXPECT_FALSE(declared.openCLRender);
	EXPECT_FALSE(declared.cudaRender);
	EXPECT_FALSE(declared.cudaStream);
	EXPECT_FALSE(declared.openGLRender);
	EXPECT_FALSE(declared.cpuRender);
	EXPECT_EQ(KernelImplementation{}.specificity(), 1);
	EXPECT_FALSE(currentOsName().empty());
}
