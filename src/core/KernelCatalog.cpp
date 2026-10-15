// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
#include <ofxgrip/core/KernelCatalog.hpp>

OFXGRIP_NAMESPACE_BEGIN

int KernelImplementation::specificity() const noexcept
{
	if (universal) {
		return 0;
	}
	return 1 + (hostFamily.empty() ? 0 : 1) + (os.empty() ? 0 : 1);
}

bool KernelImplementation::matches(GpuApi wantedApi, std::string_view host, std::string_view osName) const noexcept
{
	if (api != wantedApi) {
		return false;
	}
	if (!hostFamily.empty() && hostFamily != host) {
		return false;
	}
	if (!os.empty() && os != osName) {
		return false;
	}
	return true;
}

std::string_view currentOsName() noexcept
{
#if defined(__APPLE__)
	return "macos";
#elif defined(_WIN32)
	return "windows";
#else
	return "linux";
#endif
}

void KernelCatalog::add(KernelRecord record)
{
	for (KernelRecord& existing : kernels_) {
		if (existing.name == record.name) {
			existing = std::move(record);
			return;
		}
	}
	kernels_.push_back(std::move(record));
}

const KernelRecord* KernelCatalog::find(std::string_view name) const noexcept
{
	for (const KernelRecord& record : kernels_) {
		if (record.name == name) {
			return &record;
		}
	}
	return nullptr;
}

const KernelImplementation* KernelCatalog::resolve(std::string_view name, GpuApi api, std::string_view host, std::string_view os) const noexcept
{
	const KernelRecord* record = find(name);
	if (record == nullptr) {
		return nullptr;
	}
	const KernelImplementation* best = nullptr;
	for (const KernelImplementation& candidate : record->implementations) {
		if (!candidate.matches(api, host, os)) {
			continue;
		}
		if (best == nullptr || candidate.specificity() > best->specificity()) {
			best = &candidate;
		}
	}
	return best;
}

bool KernelCatalog::everyKernelResolves(GpuApi api, std::string_view host, std::string_view os) const noexcept
{
	if (kernels_.empty()) {
		return false;
	}
	for (const KernelRecord& record : kernels_) {
		if (resolve(record.name, api, host, os) == nullptr) {
			return false;
		}
	}
	return true;
}

Capabilities KernelCatalog::capabilitiesFor(const Capabilities& base, std::string_view host, std::string_view os) const
{
	Capabilities out = base;
	out.metalRender = base.metalRender && everyKernelResolves(GpuApi::Metal, host, os);
	out.cudaRender = base.cudaRender && everyKernelResolves(GpuApi::Cuda, host, os);
	out.cudaStream = out.cudaRender && base.cudaStream;
	out.openCLRender = base.openCLRender && everyKernelResolves(GpuApi::OpenCL, host, os);
	out.openGLRender = base.openGLRender && everyKernelResolves(GpuApi::OpenGL, host, os);
	out.cpuRender = base.cpuRender && everyKernelResolves(GpuApi::None, host, os);
	return out;
}

OFXGRIP_NAMESPACE_END
