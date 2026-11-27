// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
#include <ofxgrip/gpu/KernelColor.hpp>

#include <ofxgrip/core/KernelMetadata.hpp>
#include <ofxgrip/gpu/KernelCpu.hpp>

#include <map>

OFXGRIP_DECLARE_KERNEL(GripColorConvert);

OFXGRIP_NAMESPACE_BEGIN

namespace {

// The identifiers of the kernel's metadata block.
constexpr ParameterId kRow0 = 1;
constexpr ParameterId kRow1 = 2;
constexpr ParameterId kRow2 = 3;
constexpr ParameterId kDecode = 4;
constexpr ParameterId kEncode = 5;
constexpr ParameterId kPremultiplied = 6;

struct Table {
	KernelCatalog catalog;
	std::map<GpuApi, KernelColorBuild> builds;
};

const Table& table()
{
	static const Table* built = []() {
		auto* out = new Table;
		out->catalog.add(kernelRecordFor(OFXGRIP_KERNEL(GripColorConvert)));
		const KernelRecord* record = out->catalog.find(kColorConvertKernelName);
		KernelMetadata metadata;
		if (record == nullptr || KernelMetadata::scan(record->slangSource, metadata)) {
			return out;
		}
		for (GpuApi api : {GpuApi::None, GpuApi::Metal, GpuApi::Cuda, GpuApi::Vulkan}) {
			const KernelImplementation* implementation = out->catalog.resolve(kColorConvertKernelName, api, {}, currentOsName());
			if (implementation == nullptr) {
				continue;
			}
			KernelColorBuild build;
			build.implementation = implementation;
			if (KernelLayout::parse(implementation->layoutJson, build.layout) || KernelBindingPlan::resolve(build.layout, metadata, build.plan)) {
				continue;
			}
			out->builds.emplace(api, std::move(build));
		}
		return out;
	}();
	return *built;
}

}  // namespace

const KernelRecord& colorConvertKernelRecord()
{
	return *table().catalog.find(kColorConvertKernelName);
}

const KernelColorBuild* kernelColorBuild(GpuApi api)
{
	const Table& t = table();
	auto it = t.builds.find(api);
	return it == t.builds.end() ? nullptr : &it->second;
}

KernelValueMap colorConvertValues(const ColorConversion& conversion, bool premultiplied)
{
	const ColorMatrix& m = conversion.matrix;
	KernelValueMap values;
	values[kRow0] = Value(RGB{m.at(0, 0), m.at(0, 1), m.at(0, 2)});
	values[kRow1] = Value(RGB{m.at(1, 0), m.at(1, 1), m.at(1, 2)});
	values[kRow2] = Value(RGB{m.at(2, 0), m.at(2, 1), m.at(2, 2)});
	values[kDecode] = Value(static_cast<int>(conversion.decode));
	values[kEncode] = Value(static_cast<int>(conversion.encode));
	values[kPremultiplied] = Value(premultiplied ? 1 : 0);
	return values;
}

OFXGRIP_NAMESPACE_END
