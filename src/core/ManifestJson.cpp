// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
#include <ofxgrip/core/ManifestJson.hpp>

#include <nlohmann/json.hpp>

OFXGRIP_NAMESPACE_BEGIN

namespace {

using Json = nlohmann::json;

Error fail(const std::string& path, const std::string& why)
{
	return Error::make(ErrorCode::InvalidManifest, "Manifest JSON is invalid", why).with("path", path);
}

bool readValue(const Json& j, Value& out)
{
	if (j.is_null()) {
		out = Value();
		return true;
	}
	if (j.is_boolean()) {
		out = Value(j.get<bool>());
		return true;
	}
	if (j.is_number_integer()) {
		out = Value(j.get<int>());
		return true;
	}
	if (j.is_number_float()) {
		out = Value(j.get<double>());
		return true;
	}
	if (j.is_string()) {
		out = Value(j.get<std::string>());
		return true;
	}
	if (j.is_array() && !j.empty()) {
		for (const auto& e : j) {
			if (!e.is_number()) {
				return false;
			}
		}
		if (j.size() == 2) {
			out = Value(Point2D{j[0].get<double>(), j[1].get<double>()});
			return true;
		}
		if (j.size() == 3) {
			out = Value(Point3D{j[0].get<double>(), j[1].get<double>(), j[2].get<double>()});
			return true;
		}
		if (j.size() == 4) {
			out = Value(RGBA{j[0].get<double>(), j[1].get<double>(), j[2].get<double>(), j[3].get<double>()});
			return true;
		}
		return false;
	}
	if (j.is_object()) {
		if (j.contains("r") && j.contains("g") && j.contains("b")) {
			if (j.contains("a")) {
				out = Value(RGBA{j["r"].get<double>(), j["g"].get<double>(), j["b"].get<double>(), j["a"].get<double>()});
			} else {
				out = Value(RGB{j["r"].get<double>(), j["g"].get<double>(), j["b"].get<double>()});
			}
			return true;
		}
		if (j.contains("x") && j.contains("y")) {
			if (j.contains("z")) {
				out = Value(Point3D{j["x"].get<double>(), j["y"].get<double>(), j["z"].get<double>()});
			} else {
				out = Value(Point2D{j["x"].get<double>(), j["y"].get<double>()});
			}
			return true;
		}
	}
	return false;
}

Json writeValue(const Value& v)
{
	switch (v.kind()) {
	case ValueKind::Empty:
		return nullptr;
	case ValueKind::Bool:
		return v.as<bool>();
	case ValueKind::Int:
		return v.as<int>();
	case ValueKind::Double:
		return v.as<double>();
	case ValueKind::Point2D:
		return Json{{"x", v.as<Point2D>().x}, {"y", v.as<Point2D>().y}};
	case ValueKind::Point3D:
		return Json{{"x", v.as<Point3D>().x}, {"y", v.as<Point3D>().y}, {"z", v.as<Point3D>().z}};
	case ValueKind::RGB:
		return Json{{"r", v.as<RGB>().r}, {"g", v.as<RGB>().g}, {"b", v.as<RGB>().b}};
	case ValueKind::RGBA:
		return Json{{"r", v.as<RGBA>().r}, {"g", v.as<RGBA>().g}, {"b", v.as<RGBA>().b}, {"a", v.as<RGBA>().a}};
	case ValueKind::String:
		return v.as<std::string>();
	case ValueKind::Bytes:
		return Json::binary(v.as<Bytes>());
	}
	return nullptr;
}

bool readDoubles(const Json& j, std::vector<double>& out)
{
	out.clear();
	if (j.is_number()) {
		out.push_back(j.get<double>());
		return true;
	}
	if (j.is_array()) {
		for (const auto& e : j) {
			if (!e.is_number()) {
				return false;
			}
			out.push_back(e.get<double>());
		}
		return true;
	}
	return false;
}

Error readParameter(const Json& j, const std::string& path, ParameterSpec& out);

Error readParameters(const Json& j, const std::string& path, std::vector<ParameterSpec>& out)
{
	if (!j.is_array()) {
		return fail(path, "expected an array");
	}
	std::size_t index = 0;
	for (const auto& item : j) {
		ParameterSpec spec;
		if (Error e = readParameter(item, path + "[" + std::to_string(index) + "]", spec)) {
			return e;
		}
		out.push_back(std::move(spec));
		++index;
	}
	return Error::none();
}

Error readParameter(const Json& j, const std::string& path, ParameterSpec& out)
{
	if (!j.is_object()) {
		return fail(path, "expected an object");
	}
	if (j.contains("id")) {
		if (!j["id"].is_number_unsigned()) {
			return fail(path + ".id", "expected an unsigned integer");
		}
		out.id = j["id"].get<ParameterId>();
	}
	if (j.contains("key")) {
		out.key = j["key"].get<std::string>();
	}
	if (j.contains("type")) {
		if (!j["type"].is_string() || !parseParameterType(j["type"].get<std::string>(), out.type)) {
			return fail(path + ".type", "unknown parameter type");
		}
	}
	if (j.contains("label")) {
		out.label = j["label"].get<std::string>();
	} else if (j.contains("name")) {
		out.label = j["name"].get<std::string>();
	}
	if (j.contains("hint")) {
		out.hint = j["hint"].get<std::string>();
	} else if (j.contains("description")) {
		out.hint = j["description"].get<std::string>();
	}
	if (j.contains("parent")) {
		out.parent = j["parent"].get<ParameterId>();
	} else if (j.contains("parentid")) {
		out.parent = j["parentid"].get<ParameterId>();
	}
	if (j.contains("default") && !readValue(j["default"], out.defaultValue)) {
		return fail(path + ".default", "unreadable value");
	}
	if (j.contains("resetvalue") && !readValue(j["resetvalue"], out.resetValue)) {
		return fail(path + ".resetvalue", "unreadable value");
	}
	if (j.contains("range")) {
		const auto& r = j["range"];
		if (!r.is_array() || r.size() != 2) {
			return fail(path + ".range", "expected [min, max]");
		}
		if (!readDoubles(r[0], out.minimum) || !readDoubles(r[1], out.maximum)) {
			return fail(path + ".range", "expected numbers");
		}
	}
	if (j.contains("minimum") && !readDoubles(j["minimum"], out.minimum)) {
		return fail(path + ".minimum", "expected a number or array");
	}
	if (j.contains("maximum") && !readDoubles(j["maximum"], out.maximum)) {
		return fail(path + ".maximum", "expected a number or array");
	}
	if (j.contains("displayRange")) {
		const auto& r = j["displayRange"];
		if (!r.is_array() || r.size() != 2 || !readDoubles(r[0], out.displayMinimum) || !readDoubles(r[1], out.displayMaximum)) {
			return fail(path + ".displayRange", "expected [min, max]");
		}
	}
	if (j.contains("slidermin") && !readDoubles(j["slidermin"], out.displayMinimum)) {
		return fail(path + ".slidermin", "expected a number");
	}
	if (j.contains("slidermax") && !readDoubles(j["slidermax"], out.displayMaximum)) {
		return fail(path + ".slidermax", "expected a number");
	}
	if (j.contains("increment")) {
		out.increment = j["increment"].get<double>();
	}
	if (j.contains("digits")) {
		out.digits = j["digits"].get<int>();
	}
	if (j.contains("flags")) {
		const auto& f = j["flags"];
		ParsedFlags parsed;
		if (f.is_string()) {
			parsed = parseFlags(f.get<std::string>());
		} else if (f.is_array()) {
			std::vector<std::string> names;
			for (const auto& e : f) {
				if (!e.is_string()) {
					return fail(path + ".flags", "expected flag names");
				}
				names.push_back(e.get<std::string>());
			}
			parsed = parseFlags(names);
		} else if (f.is_object()) {
			std::vector<std::string> names;
			for (const auto& [name, on] : f.items()) {
				names.push_back((on.is_boolean() && !on.get<bool>() ? "-" : "+") + name);
			}
			parsed = parseFlags(names);
		} else {
			return fail(path + ".flags", "expected a string, array, or object");
		}
		if (!parsed.unknown.empty()) {
			return fail(path + ".flags", "unknown flag " + parsed.unknown.front());
		}
		out.hostFlags = parsed.host;
		out.appFlags = parsed.app;
	}
	if (j.contains("options") || j.contains("items")) {
		const auto& items = j.contains("options") ? j["options"] : j["items"];
		if (!items.is_array()) {
			return fail(path + ".options", "expected an array");
		}
		for (const auto& item : items) {
			ChoiceOption option;
			if (item.is_string()) {
				option.label = item.get<std::string>();
				option.key = option.label;
			} else if (item.is_object()) {
				option.key = item.value("key", "");
				option.label = item.value("label", option.key);
			} else {
				return fail(path + ".options", "expected strings or objects");
			}
			out.options.push_back(std::move(option));
		}
	}
	if (j.contains("tags")) {
		for (const auto& t : j["tags"]) {
			out.tags.push_back(t.get<std::string>());
		}
	}
	if (j.contains("meta") && j["meta"].is_object()) {
		for (const auto& [name, v] : j["meta"].items()) {
			Value value;
			if (!readValue(v, value)) {
				return fail(path + ".meta." + name, "unreadable value");
			}
			out.meta[name] = std::move(value);
		}
	}
	if (j.contains("visibleWhen")) {
		out.visibleWhen = j["visibleWhen"].get<std::string>();
	}
	if (j.contains("enabledWhen")) {
		out.enabledWhen = j["enabledWhen"].get<std::string>();
	}
	if (j.contains("selector")) {
		out.selector = j["selector"].get<std::string>();
	}
	if (j.contains("hosts") && j["hosts"].is_object()) {
		for (const auto& [host, o] : j["hosts"].items()) {
			HostOverride override;
			override.exclude = o.value("exclude", false);
			if (o.contains("label")) {
				override.label = o["label"].get<std::string>();
			}
			out.hosts[host] = std::move(override);
		}
	}
	if (j.contains("parameters")) {
		if (Error e = readParameters(j["parameters"], path + ".parameters", out.children)) {
			return e;
		}
	}
	return Error::none();
}

Json writeParameter(const ParameterSpec& spec)
{
	Json j;
	j["id"] = spec.id;
	j["key"] = spec.key;
	j["type"] = std::string(toString(spec.type));
	j["label"] = spec.label;
	if (!spec.hint.empty()) {
		j["hint"] = spec.hint;
	}
	if (spec.parent != kParameterIdRoot) {
		j["parent"] = spec.parent;
	}
	if (!spec.defaultValue.empty()) {
		j["default"] = writeValue(spec.defaultValue);
	}
	if (!spec.resetValue.empty()) {
		j["resetvalue"] = writeValue(spec.resetValue);
	}
	if (!spec.minimum.empty()) {
		j["minimum"] = spec.minimum;
	}
	if (!spec.maximum.empty()) {
		j["maximum"] = spec.maximum;
	}
	if (!spec.displayMinimum.empty()) {
		j["slidermin"] = spec.displayMinimum;
	}
	if (!spec.displayMaximum.empty()) {
		j["slidermax"] = spec.displayMaximum;
	}
	if (spec.increment != 0) {
		j["increment"] = spec.increment;
	}
	if (spec.digits != 2) {
		j["digits"] = spec.digits;
	}
	const std::vector<std::string> names = flagNames(spec.hostFlags, spec.appFlags);
	if (!names.empty()) {
		j["flags"] = names;
	}
	if (!spec.options.empty()) {
		Json options = Json::array();
		for (const ChoiceOption& o : spec.options) {
			options.push_back(Json{{"key", o.key}, {"label", o.label}});
		}
		j["options"] = options;
	}
	if (!spec.tags.empty()) {
		j["tags"] = spec.tags;
	}
	if (!spec.meta.empty()) {
		Json meta = Json::object();
		for (const auto& [name, v] : spec.meta) {
			meta[name] = writeValue(v);
		}
		j["meta"] = meta;
	}
	if (!spec.visibleWhen.empty()) {
		j["visibleWhen"] = spec.visibleWhen;
	}
	if (!spec.enabledWhen.empty()) {
		j["enabledWhen"] = spec.enabledWhen;
	}
	if (!spec.selector.empty()) {
		j["selector"] = spec.selector;
	}
	if (!spec.hosts.empty()) {
		Json hosts = Json::object();
		for (const auto& [host, o] : spec.hosts) {
			Json entry;
			entry["exclude"] = o.exclude;
			if (o.label) {
				entry["label"] = *o.label;
			}
			hosts[host] = entry;
		}
		j["hosts"] = hosts;
	}
	if (!spec.children.empty()) {
		Json children = Json::array();
		for (const ParameterSpec& c : spec.children) {
			children.push_back(writeParameter(c));
		}
		j["parameters"] = children;
	}
	return j;
}

}  // namespace

Error parseManifestJson(std::string_view json, EffectManifest& out)
{
	Json root = Json::parse(json, nullptr, false, true);
	if (root.is_discarded()) {
		return fail("$", "not valid JSON");
	}
	if (!root.is_object()) {
		return fail("$", "expected an object");
	}
	EffectManifest m;
	m.schemaVersion = root.value("schemaVersion", 1u);
	if (root.contains("effect")) {
		const auto& e = root["effect"];
		if (!e.is_object()) {
			return fail("$.effect", "expected an object");
		}
		m.identity.ofxId = e.value("ofxId", "");
		m.identity.uuid = e.value("uuid", "");
		m.identity.name = e.value("name", "");
		m.identity.shortName = e.value("shortName", "");
		m.identity.grouping = e.value("grouping", e.value("group", ""));
		m.identity.description = e.value("description", "");
		if (e.contains("kind") && !parseEffectKind(e["kind"].get<std::string>(), m.identity.kind)) {
			return fail("$.effect.kind", "unknown kind");
		}
		if (e.contains("contexts")) {
			for (const auto& c : e["contexts"]) {
				EffectKind kind{};
				if (!parseEffectKind(c.get<std::string>(), kind)) {
					return fail("$.effect.contexts", "unknown kind");
				}
				m.identity.extraContexts.push_back(kind);
			}
		}
		if (e.contains("version")) {
			const auto& v = e["version"];
			if (v.is_string()) {
				if (!Version::parse(v.get<std::string>(), m.version)) {
					return fail("$.effect.version", "expected major.minor.patch");
				}
			} else if (v.is_object()) {
				m.version = Version{v.value("major", 1u), v.value("minor", 0u), v.value("patch", 0u)};
			} else if (v.is_number_unsigned()) {
				m.version = Version{v.get<unsigned>(), 0, 0};
			} else {
				return fail("$.effect.version", "expected a string, object, or integer");
			}
		}
		if (e.contains("deprecatedMajors")) {
			for (const auto& d : e["deprecatedMajors"]) {
				m.deprecatedMajors.push_back(d.get<unsigned>());
			}
		}
		if (e.contains("priorUuids")) {
			for (const auto& u : e["priorUuids"]) {
				m.priorUuids.push_back(u.get<std::string>());
			}
		}
		if (e.contains("capabilities")) {
			const auto& c = e["capabilities"];
			Capabilities& cap = m.capabilities;
			cap.supportsTiles = c.value("tiles", cap.supportsTiles);
			cap.supportsMultiResolution = c.value("multiResolution", cap.supportsMultiResolution);
			cap.temporalClipAccess = c.value("temporalAccess", cap.temporalClipAccess);
			cap.hostFrameThreading = c.value("hostFrameThreading", cap.hostFrameThreading);
			cap.noSpatialAwareness = c.value("noSpatialAwareness", cap.noSpatialAwareness);
			cap.cpuRender = c.value("cpuFallback", cap.cpuRender);
			if (c.contains("depths")) {
				cap.depths.clear();
				for (const auto& d : c["depths"]) {
					BitDepth depth{};
					if (!parseBitDepth(d.get<std::string>(), depth)) {
						return fail("$.effect.capabilities.depths", "unknown depth");
					}
					cap.depths.push_back(depth);
				}
			}
			if (c.contains("gpu")) {
				for (const auto& g : c["gpu"]) {
					const std::string name = g.get<std::string>();
					if (name == "metal") {
						cap.metalRender = true;
					} else if (name == "cuda") {
						cap.cudaRender = true;
					} else if (name == "opencl") {
						cap.openCLRender = true;
					} else if (name == "opengl") {
						cap.openGLRender = true;
					} else {
						return fail("$.effect.capabilities.gpu", "unknown GPU API " + name);
					}
				}
			}
			if (c.contains("threadSafety")) {
				const std::string s = c["threadSafety"].get<std::string>();
				if (s == "unsafe") {
					cap.threadSafety = ThreadSafety::Unsafe;
				} else if (s == "instance") {
					cap.threadSafety = ThreadSafety::InstanceSafe;
				} else if (s == "full") {
					cap.threadSafety = ThreadSafety::FullySafe;
				} else {
					return fail("$.effect.capabilities.threadSafety", "expected unsafe, instance, or full");
				}
			}
		}
		if (e.contains("properties") && e["properties"].is_object()) {
			for (const auto& [name, v] : e["properties"].items()) {
				Value value;
				if (!readValue(v, value)) {
					return fail("$.effect.properties." + name, "unreadable value");
				}
				m.properties[name] = std::move(value);
			}
		}
	}
	if (root.contains("clips")) {
		if (!root["clips"].is_array()) {
			return fail("$.clips", "expected an array");
		}
		for (const auto& c : root["clips"]) {
			ClipSpec clip;
			clip.name = c.value("name", "");
			clip.label = c.value("label", clip.name);
			clip.optional = c.value("optional", false);
			clip.isMask = c.value("mask", false);
			clip.temporalAccess = c.value("temporalAccess", false);
			m.clips.push_back(std::move(clip));
		}
	}
	if (root.contains("parameters")) {
		if (Error e = readParameters(root["parameters"], "$.parameters", m.parameters)) {
			return e;
		}
	}
	if (root.contains("migrations")) {
		for (const auto& mig : root["migrations"]) {
			Migration migration;
			migration.fromSchemaVersion = mig.value("from", 0u);
			if (mig.contains("rename") && mig["rename"].is_object()) {
				for (const auto& [from, to] : mig["rename"].items()) {
					migration.renames[from] = to.get<std::string>();
				}
			}
			m.migrations.push_back(std::move(migration));
		}
	}
	out = std::move(m);
	return Error::none();
}

std::string writeManifestJson(const EffectManifest& m)
{
	Json root;
	root["schemaVersion"] = m.schemaVersion;
	Json effect;
	effect["ofxId"] = m.identity.ofxId;
	effect["uuid"] = m.identity.uuid;
	effect["name"] = m.identity.name;
	if (!m.identity.shortName.empty()) {
		effect["shortName"] = m.identity.shortName;
	}
	effect["grouping"] = m.identity.grouping;
	if (!m.identity.description.empty()) {
		effect["description"] = m.identity.description;
	}
	effect["kind"] = std::string(toString(m.identity.kind));
	if (!m.identity.extraContexts.empty()) {
		Json contexts = Json::array();
		for (EffectKind k : m.identity.extraContexts) {
			contexts.push_back(std::string(toString(k)));
		}
		effect["contexts"] = contexts;
	}
	effect["version"] = m.version.toString();
	if (!m.deprecatedMajors.empty()) {
		effect["deprecatedMajors"] = m.deprecatedMajors;
	}
	if (!m.priorUuids.empty()) {
		effect["priorUuids"] = m.priorUuids;
	}
	Json cap;
	cap["tiles"] = m.capabilities.supportsTiles;
	cap["multiResolution"] = m.capabilities.supportsMultiResolution;
	cap["temporalAccess"] = m.capabilities.temporalClipAccess;
	cap["hostFrameThreading"] = m.capabilities.hostFrameThreading;
	cap["noSpatialAwareness"] = m.capabilities.noSpatialAwareness;
	cap["cpuFallback"] = m.capabilities.cpuRender;
	Json depths = Json::array();
	for (BitDepth d : m.capabilities.depths) {
		depths.push_back(std::string(toString(d)));
	}
	cap["depths"] = depths;
	Json gpu = Json::array();
	if (m.capabilities.metalRender) {
		gpu.push_back("metal");
	}
	if (m.capabilities.cudaRender) {
		gpu.push_back("cuda");
	}
	if (m.capabilities.openCLRender) {
		gpu.push_back("opencl");
	}
	if (m.capabilities.openGLRender) {
		gpu.push_back("opengl");
	}
	cap["gpu"] = gpu;
	switch (m.capabilities.threadSafety) {
	case ThreadSafety::Unsafe: cap["threadSafety"] = "unsafe"; break;
	case ThreadSafety::InstanceSafe: cap["threadSafety"] = "instance"; break;
	case ThreadSafety::FullySafe: cap["threadSafety"] = "full"; break;
	}
	effect["capabilities"] = cap;
	if (!m.properties.empty()) {
		Json props = Json::object();
		for (const auto& [name, v] : m.properties) {
			props[name] = writeValue(v);
		}
		effect["properties"] = props;
	}
	root["effect"] = effect;
	if (!m.clips.empty()) {
		Json clips = Json::array();
		for (const ClipSpec& c : m.clips) {
			clips.push_back(Json{{"name", c.name}, {"label", c.label}, {"optional", c.optional}, {"mask", c.isMask}, {"temporalAccess", c.temporalAccess}});
		}
		root["clips"] = clips;
	}
	Json parameters = Json::array();
	for (const ParameterSpec& p : m.parameters) {
		parameters.push_back(writeParameter(p));
	}
	root["parameters"] = parameters;
	if (!m.migrations.empty()) {
		Json migrations = Json::array();
		for (const Migration& mig : m.migrations) {
			Json entry;
			entry["from"] = mig.fromSchemaVersion;
			Json rename = Json::object();
			for (const auto& [from, to] : mig.renames) {
				rename[from] = to;
			}
			entry["rename"] = rename;
			migrations.push_back(entry);
		}
		root["migrations"] = migrations;
	}
	return root.dump(2);
}

OFXGRIP_NAMESPACE_END
