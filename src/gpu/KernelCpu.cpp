// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
#include <ofxgrip/gpu/KernelCpu.hpp>

OFXGRIP_NAMESPACE_BEGIN

namespace {

std::string text(const unsigned char* data, std::size_t length)
{
	return data == nullptr ? std::string() : std::string(reinterpret_cast<const char*>(data), length);
}

}  // namespace

KernelRecord kernelRecordFor(const EmbeddedKernel& kernel)
{
	KernelRecord record;
	record.name = kernel.name;
	record.slangSource = text(kernel.slangSource, kernel.slangLength);
	if (kernel.metalSource != nullptr && kernel.metalLength > 0) {
		KernelImplementation metal;
		metal.api = GpuApi::Metal;
		metal.universal = true;
		metal.source = text(kernel.metalSource, kernel.metalLength);
		metal.layoutJson = text(kernel.metalLayout, kernel.metalLayoutLength);
		metal.entryPoint = kernel.entryPoint;
		record.implementations.push_back(std::move(metal));
	}
	if (kernel.cpuEntry != nullptr) {
		KernelImplementation cpu;
		cpu.api = GpuApi::None;
		cpu.universal = true;
		cpu.layoutJson = text(kernel.cppLayout, kernel.cppLayoutLength);
		cpu.entryPoint = kernel.entryPoint;
		cpu.cpuEntry = kernel.cpuEntry;
		record.implementations.push_back(std::move(cpu));
	}
	return record;
}

OFXGRIP_NAMESPACE_END
