// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
#include <ofxgrip/minihost/BundleLoader.hpp>

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

OFXGRIP_NAMESPACE_BEGIN
namespace minihost {

std::filesystem::path binaryPathInBundle(const std::filesystem::path& bundle)
{
	std::string name = bundle.filename().string();
	const std::string suffix = ".bundle";
	const bool isBundleName = name.size() > suffix.size() && name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0;
	if (!isBundleName && !std::filesystem::is_directory(bundle)) {
		return bundle;
	}
	if (isBundleName) {
		name.erase(name.size() - suffix.size());
	}
	const std::filesystem::path contents = bundle / "Contents";
#if defined(__APPLE__)
	const char* archDirs[] = {"MacOS", "MacOS-x86-64"};
#elif defined(_WIN32)
	const char* archDirs[] = {"Win64", "Win-arm64", "Win32"};
#else
	const char* archDirs[] = {"Linux-x86-64", "Linux-aarch64", "Linux-x86"};
#endif
	for (const char* arch : archDirs) {
		const std::filesystem::path candidate = contents / arch / name;
		if (std::filesystem::exists(candidate)) {
			return candidate;
		}
	}
	return contents / archDirs[0] / name;
}

Bundle::~Bundle()
{
	close();
}

Error Bundle::open(const std::filesystem::path& bundleOrBinary)
{
	close();
	binary_ = binaryPathInBundle(bundleOrBinary);
	if (!std::filesystem::exists(binary_)) {
		return Error::make(ErrorCode::NotFound, "The bundle binary does not exist").with("path", binary_.string());
	}
#if defined(_WIN32)
	HMODULE module = LoadLibraryW(binary_.wstring().c_str());
	if (module == nullptr) {
		return Error::make(ErrorCode::Failed, "LoadLibrary failed").with("path", binary_.string()).with("error", std::to_string(GetLastError()));
	}
	handle_ = module;
	getNumberOfPlugins_ = reinterpret_cast<int (*)()>(GetProcAddress(module, "OfxGetNumberOfPlugins"));
	getPlugin_ = reinterpret_cast<OfxPlugin* (*)(int)>(GetProcAddress(module, "OfxGetPlugin"));
	setHost_ = reinterpret_cast<OfxStatus (*)(const OfxHost*)>(GetProcAddress(module, "OfxSetHost"));
#else
	void* module = dlopen(binary_.string().c_str(), RTLD_NOW | RTLD_LOCAL);
	if (module == nullptr) {
		const char* why = dlerror();
		return Error::make(ErrorCode::Failed, "dlopen failed").with("path", binary_.string()).with("error", why ? why : "unknown");
	}
	handle_ = module;
	getNumberOfPlugins_ = reinterpret_cast<int (*)()>(dlsym(module, "OfxGetNumberOfPlugins"));
	getPlugin_ = reinterpret_cast<OfxPlugin* (*)(int)>(dlsym(module, "OfxGetPlugin"));
	setHost_ = reinterpret_cast<OfxStatus (*)(const OfxHost*)>(dlsym(module, "OfxSetHost"));
#endif
	if (getNumberOfPlugins_ == nullptr || getPlugin_ == nullptr) {
		close();
		return Error::make(ErrorCode::Format, "The binary exports no OfxGetNumberOfPlugins or OfxGetPlugin").with("path", binary_.string());
	}
	return Error::none();
}

void Bundle::close()
{
	if (handle_ != nullptr) {
#if defined(_WIN32)
		FreeLibrary(static_cast<HMODULE>(handle_));
#else
		dlclose(handle_);
#endif
		handle_ = nullptr;
	}
	getNumberOfPlugins_ = nullptr;
	getPlugin_ = nullptr;
	setHost_ = nullptr;
}

int Bundle::pluginCount() const
{
	return getNumberOfPlugins_ != nullptr ? getNumberOfPlugins_() : 0;
}

OfxPlugin* Bundle::plugin(int index) const
{
	return getPlugin_ != nullptr ? getPlugin_(index) : nullptr;
}

OfxStatus Bundle::setHost(const OfxHost* host) const
{
	return setHost_ != nullptr ? setHost_(host) : kOfxStatOK;
}

}  // namespace minihost
OFXGRIP_NAMESPACE_END
