// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
/*!
	@file  BundleLoader.hpp
	@brief Loads an OpenFX bundle's binary and resolves its entry symbols.

	Introduced in OfxGrip 0.1.0. Accepts a `.ofx.bundle` directory, in which case the binary is
	looked up in the platform's architecture folder, or a bare `.ofx` binary.
*/
#pragma once

#include <ofxgrip/core/Error.hpp>

#include "ofxCore.h"

#include <filesystem>
#include <string>

OFXGRIP_NAMESPACE_BEGIN
namespace minihost {

/*! The binary inside a bundle for this platform, or the path itself when it is a file. */
std::filesystem::path binaryPathInBundle(const std::filesystem::path& bundle);

class Bundle {
public:
	Bundle() = default;
	~Bundle();
	Bundle(const Bundle&) = delete;
	Bundle& operator=(const Bundle&) = delete;

	/*! Opens the bundle. The binary stays loaded until the `Bundle` is destroyed. */
	Error open(const std::filesystem::path& bundleOrBinary);
	void close();
	bool isOpen() const noexcept { return handle_ != nullptr; }
	const std::filesystem::path& binaryPath() const noexcept { return binary_; }

	int pluginCount() const;
	OfxPlugin* plugin(int index) const;
	/*! Calls the optional `OfxSetHost` export. Returns `kOfxStatOK` when the export is absent. */
	OfxStatus setHost(const OfxHost* host) const;

private:
	void* handle_{nullptr};
	std::filesystem::path binary_;
	int (*getNumberOfPlugins_)() {nullptr};
	OfxPlugin* (*getPlugin_)(int) {nullptr};
	OfxStatus (*setHost_)(const OfxHost*) {nullptr};
};

}  // namespace minihost
OFXGRIP_NAMESPACE_END
