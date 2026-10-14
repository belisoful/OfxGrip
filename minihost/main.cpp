// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
/*!
	@file  main.cpp
	@brief The `ofxgrip-minihost` command: loads a bundle, describes it, renders a frame, and
	       prints what happened.
*/
#include <ofxgrip/minihost/BundleLoader.hpp>
#include <ofxgrip/minihost/MiniHost.hpp>

#include "ofxParam.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using namespace ofxgrip;
using namespace ofxgrip::minihost;

namespace {

void usage()
{
	std::cout << "ofxgrip-minihost --bundle <path.ofx.bundle|plugin.ofx> [options]\n"
	             "  --plugin <identifier>     the plug-in to drive (default: the first)\n"
	             "  --profile spec|resolve21|nuke|natron\n"
	             "  --context <OfxImageEffectContext...> (default: the first described)\n"
	             "  --width N --height N      project size (default 64x64)\n"
	             "  --depth 8u|16u|16f|32f    output depth (default 32f)\n"
	             "  --scale S                 render scale (default 1)\n"
	             "  --tile N                  tile size in pixels (default: whole frame)\n"
	             "  --threads N               concurrent render threads (default 1)\n"
	             "  --time T                  frame to render (default 0)\n"
	             "  --set key=value           set a parameter (numbers comma separated, or text)\n"
	             "  --no-input                leave the Source clip disconnected\n"
	             "  --out <file.ppm>          write the rendered frame as 8-bit PPM\n"
	             "  --dump                    print the action log and the parameter list\n";
}

bool writePpm(const std::string& path, const ImageBuffer& image)
{
	std::ofstream out(path, std::ios::binary);
	if (!out) {
		return false;
	}
	const ImageView view = image.view();
	const int w = view.desc.bounds.width();
	const int h = view.desc.bounds.height();
	out << "P6\n" << w << " " << h << "\n255\n";
	const std::size_t n = componentCount(view.desc.components);
	for (int y = view.desc.bounds.y2 - 1; y >= view.desc.bounds.y1; --y) {
		for (int x = view.desc.bounds.x1; x < view.desc.bounds.x2; ++x) {
			const std::uint8_t* px = view.pixel(x, y);
			for (std::size_t c = 0; c < 3; ++c) {
				const float v = px ? readComponent(px, view.desc.depth, n == 1 ? 0 : (c < n ? c : 0)) : 0.0f;
				const int byte = static_cast<int>(std::max(0.0f, std::min(1.0f, v)) * 255.0f + 0.5f);
				out.put(static_cast<char>(byte));
			}
		}
	}
	return static_cast<bool>(out);
}

}  // namespace

int main(int argc, char** argv)
{
	std::string bundlePath;
	std::string pluginId;
	std::string profile = "spec";
	std::string context;
	int width = 64;
	int height = 64;
	BitDepth depth = BitDepth::Float;
	double scale = 1.0;
	int tile = 0;
	unsigned threads = 1;
	double time = 0.0;
	std::vector<std::pair<std::string, std::string>> sets;
	bool noInput = false;
	std::string outPath;
	bool dump = false;

	for (int i = 1; i < argc; ++i) {
		const std::string arg = argv[i];
		auto next = [&](std::string& into) {
			if (i + 1 >= argc) {
				usage();
				std::exit(2);
			}
			into = argv[++i];
		};
		std::string value;
		if (arg == "--bundle") {
			next(bundlePath);
		} else if (arg == "--plugin") {
			next(pluginId);
		} else if (arg == "--profile") {
			next(profile);
		} else if (arg == "--context") {
			next(context);
		} else if (arg == "--width") {
			next(value);
			width = std::stoi(value);
		} else if (arg == "--height") {
			next(value);
			height = std::stoi(value);
		} else if (arg == "--depth") {
			next(value);
			if (!parseBitDepth(value, depth)) {
				std::cerr << "unknown depth " << value << "\n";
				return 2;
			}
		} else if (arg == "--scale") {
			next(value);
			scale = std::stod(value);
		} else if (arg == "--tile") {
			next(value);
			tile = std::stoi(value);
		} else if (arg == "--threads") {
			next(value);
			threads = static_cast<unsigned>(std::stoul(value));
		} else if (arg == "--time") {
			next(value);
			time = std::stod(value);
		} else if (arg == "--set") {
			next(value);
			const std::size_t eq = value.find('=');
			if (eq == std::string::npos) {
				std::cerr << "--set needs key=value\n";
				return 2;
			}
			sets.emplace_back(value.substr(0, eq), value.substr(eq + 1));
		} else if (arg == "--no-input") {
			noInput = true;
		} else if (arg == "--out") {
			next(outPath);
		} else if (arg == "--dump") {
			dump = true;
		} else if (arg == "--help" || arg == "-h") {
			usage();
			return 0;
		} else {
			std::cerr << "unknown option " << arg << "\n";
			usage();
			return 2;
		}
	}
	if (bundlePath.empty()) {
		usage();
		return 2;
	}

	HostOptions options = HostOptions::spec();
	if (profile == "resolve21") {
		options = HostOptions::resolve21();
	} else if (profile == "nuke") {
		options = HostOptions::nuke();
	} else if (profile == "natron") {
		options = HostOptions::natron();
	} else if (profile != "spec") {
		std::cerr << "unknown profile " << profile << "\n";
		return 2;
	}

	Bundle bundle;
	if (Error e = bundle.open(bundlePath)) {
		std::cerr << e.describe() << "\n";
		return 1;
	}
	std::cout << "binary: " << bundle.binaryPath() << "\nplugins: " << bundle.pluginCount() << "\n";
	OfxPlugin* plugin = nullptr;
	for (int i = 0; i < bundle.pluginCount(); ++i) {
		OfxPlugin* p = bundle.plugin(i);
		if (p == nullptr) {
			continue;
		}
		std::cout << "  [" << i << "] " << p->pluginIdentifier << " " << p->pluginVersionMajor << "." << p->pluginVersionMinor << " api=" << p->pluginApi << "/" << p->apiVersion << "\n";
		if (plugin == nullptr && (pluginId.empty() || pluginId == p->pluginIdentifier)) {
			plugin = p;
		}
	}
	if (plugin == nullptr) {
		std::cerr << "no matching plug-in\n";
		return 1;
	}

	MiniHost host(options);
	bundle.setHost(host.ofxHost());
	OfxStatus status = host.load(plugin, bundlePath);
	if (status != kOfxStatOK) {
		std::cerr << "load failed: " << status << "\n";
		return 1;
	}
	status = host.describe();
	if (status != kOfxStatOK) {
		std::cerr << "describe failed: " << status << "\n";
		return 1;
	}
	const std::vector<std::string> contexts = host.describedContexts();
	std::cout << "label: " << host.descriptorProps()->getString(kOfxPropLabel) << "\ngrouping: " << host.descriptorProps()->getString(kOfxImageEffectPluginPropGrouping) << "\ncontexts:";
	for (const std::string& c : contexts) {
		std::cout << " " << c;
	}
	std::cout << "\n";
	if (contexts.empty()) {
		std::cerr << "the plug-in described no context this host supports\n";
		return 1;
	}
	if (context.empty()) {
		context = contexts.front();
	}
	if (dump) {
		const ParamSet* params = host.contextParams(context);
		std::cout << "parameters in " << context << ":\n";
		if (params != nullptr) {
			for (const auto& p : params->params()) {
				std::cout << "  " << p->name() << " : " << p->type() << " label=\"" << p->props().getString(kOfxPropLabel) << "\" secret=" << p->props().getInt(kOfxParamPropSecret, -1)
				          << " animates=" << p->props().getInt(kOfxParamPropAnimates, -1) << " parent=" << p->props().getString(kOfxParamPropParent, "-");
				const std::vector<double> def = p->props().getDoubles(kOfxParamPropDefault);
				if (!def.empty()) {
					std::cout << " default=";
					for (std::size_t i = 0; i < def.size(); ++i) {
						std::cout << (i ? "," : "") << def[i];
					}
				} else {
					std::cout << " default=\"" << p->props().getString(kOfxParamPropDefault) << "\"";
				}
				const std::vector<double> mn = p->props().getDoubles(kOfxParamPropMin);
				const std::vector<double> mx = p->props().getDoubles(kOfxParamPropMax);
				if (!mn.empty()) {
					std::cout << " range=[" << mn[0] << "," << (mx.empty() ? 0.0 : mx[0]) << "]";
				}
				std::cout << "\n";
			}
		}
		std::cout << "clips in " << context << ":";
		for (const std::string& c : host.contextClips(context)) {
			std::cout << " " << c;
		}
		std::cout << "\n";
	}

	Project project;
	project.width = width;
	project.height = height;
	std::unique_ptr<Instance> instance = host.createInstance(context, project);
	if (!instance) {
		std::cerr << "create instance failed\n";
		return 1;
	}
	if (!noInput) {
		for (const std::string& clipName : host.contextClips(context)) {
			if (clipName != kOfxImageEffectOutputClipName) {
				instance->setInput(clipName, makeRampImage(static_cast<int>(width * scale), static_cast<int>(height * scale), depth));
			}
		}
	}
	for (const auto& [key, value] : sets) {
		Param* p = instance->param(key);
		if (p == nullptr) {
			std::cerr << "no parameter " << key << "\n";
			return 1;
		}
		if (p->isString()) {
			instance->setStringParam(key, value);
		} else {
			std::vector<double> numbers;
			std::stringstream ss(value);
			std::string piece;
			while (std::getline(ss, piece, ',')) {
				numbers.push_back(std::stod(piece));
			}
			instance->setParam(key, numbers);
		}
		instance->instanceChanged(key, time);
	}

	RenderRequest request;
	request.time = time;
	request.renderScale = {scale, scale};
	request.depth = depth;
	request.tileSize = tile;
	request.threads = threads;
	RenderResult result = instance->render(request);
	std::cout << "render: status=" << result.status << " tiles=" << result.tileCount << " identity=" << result.wasIdentity;
	int failed = 0;
	for (OfxStatus s : result.tileStatuses) {
		failed += s != kOfxStatOK;
	}
	std::cout << " failedTiles=" << failed << "\n";
	for (const std::string& m : instance->messages()) {
		std::cout << "message: " << m << "\n";
	}
	if (!outPath.empty()) {
		if (!writePpm(outPath, result.image)) {
			std::cerr << "cannot write " << outPath << "\n";
			return 1;
		}
		std::cout << "wrote " << outPath << "\n";
	}
	instance.reset();
	host.unload();
	if (dump) {
		std::cout << "actions:\n";
		for (const std::string& a : host.actionLog()) {
			std::cout << "  " << a << "\n";
		}
	}
	return result.ok() ? 0 : 1;
}
