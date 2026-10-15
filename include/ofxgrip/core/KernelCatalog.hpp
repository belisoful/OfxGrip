// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
/*!
	@file  KernelCatalog.hpp
	@brief The kernels an effect ships, each with its implementations per backend, and the
	       resolution rules that pick one.

	Introduced in OfxGrip 0.4.0. The host libraries plan (section 10) resolves each kernel from
	the most specific implementation: native for the backend, host, and OS; native for the
	backend; the universal Slang build for the backend; then the CPU build. An effect declares to
	the host only the GPU paths on which every kernel resolves, so a missing kernel fails at
	describe time and never at render time.
*/
#pragma once

#include <ofxgrip/core/Error.hpp>
#include <ofxgrip/core/Image.hpp>
#include <ofxgrip/core/Manifest.hpp>
#include <ofxgrip/core/Types.hpp>

#include <string>
#include <string_view>
#include <vector>

OFXGRIP_NAMESPACE_BEGIN

/*! One implementation of a kernel. `api` is `GpuApi::None` for the CPU build. */
struct KernelImplementation {
	GpuApi api{GpuApi::None};
	bool universal{false};        /*!< Emitted by slangc from the Slang source. */
	std::string hostFamily;       /*!< A profile family name (`resolve`, `nuke`, …); empty for every host. */
	std::string os;               /*!< `macos`, `windows`, `linux`; empty for every OS. */
	std::string source;           /*!< The kernel source or binary, as the backend loads it. */
	std::string layoutJson;       /*!< The reflection JSON of a universal build. */
	std::string entryPoint;       /*!< The function name. */
	void* cpuEntry{nullptr};      /*!< The CPU build's entry function, as the CPU backend calls it. */
	/*! The specificity the resolution rules rank by: 3 for backend + host + OS, 2 for backend
	    + host or backend + OS, 1 for backend only, 0 for a universal build. */
	int specificity() const noexcept;
	bool matches(GpuApi wantedApi, std::string_view host, std::string_view osName) const noexcept;
};

/*! A kernel and its implementations. */
struct KernelRecord {
	std::string name;
	std::string slangSource;      /*!< The Slang source, whose metadata block declares the parameters. */
	std::vector<KernelImplementation> implementations;
};

/*! The name of the operating system this build targets: `macos`, `windows`, or `linux`. */
std::string_view currentOsName() noexcept;

/*! The catalog. */
class KernelCatalog {
public:
	void add(KernelRecord record);
	const std::vector<KernelRecord>& kernels() const noexcept { return kernels_; }
	const KernelRecord* find(std::string_view name) const noexcept;
	/*! The implementation the rules pick for a kernel on a backend, host, and OS, or null. */
	const KernelImplementation* resolve(std::string_view name, GpuApi api, std::string_view host, std::string_view os) const noexcept;
	/*! Whether every kernel resolves on a backend. An empty catalog resolves nowhere. */
	bool everyKernelResolves(GpuApi api, std::string_view host, std::string_view os) const noexcept;
	/*! The capabilities an effect declares: `base` with each GPU flag set only where every
	    kernel resolves, and `cpuRender` where every kernel has a CPU build. */
	Capabilities capabilitiesFor(const Capabilities& base, std::string_view host, std::string_view os) const;

private:
	std::vector<KernelRecord> kernels_;
};

OFXGRIP_NAMESPACE_END
