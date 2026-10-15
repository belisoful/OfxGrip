// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
#include <ofxgrip/ofx/OfxHostServices.hpp>

#include <ofxgrip/ofx/Actions.hpp>
#include <ofxgrip/ofx/Descriptor.hpp>

#include "ofxImageEffect.h"
#include "ofxParam.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <thread>

OFXGRIP_NAMESPACE_BEGIN

// ---- directories --------------------------------------------------------------------------------

namespace {

std::filesystem::path homeDirectory()
{
#if defined(_WIN32)
	if (const char* profile = std::getenv("USERPROFILE")) {
		return profile;
	}
#else
	if (const char* home = std::getenv("HOME")) {
		return home;
	}
#endif
	return std::filesystem::temp_directory_path();
}

std::filesystem::path ensure(const std::filesystem::path& dir)
{
	std::error_code ec;
	std::filesystem::create_directories(dir, ec);
	return dir;
}

std::string safeName(std::string_view id)
{
	std::string out;
	for (char c : id) {
		const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-';
		out.push_back(ok ? c : '_');
	}
	return out.empty() ? std::string("plugin") : out;
}

}  // namespace

std::filesystem::path defaultLogDirectory()
{
	// Read once at load, never at render time: an override for tests and tooling.
	if (const char* override = std::getenv("OFXGRIP_LOG_DIR"); override != nullptr && *override != '\0') {
		return ensure(override);
	}
#if defined(__APPLE__)
	return ensure(homeDirectory() / "Library" / "Logs" / "OfxGrip");
#elif defined(_WIN32)
	if (const char* local = std::getenv("LOCALAPPDATA")) {
		return ensure(std::filesystem::path(local) / "OfxGrip" / "Logs");
	}
	return ensure(homeDirectory() / "OfxGrip" / "Logs");
#else
	if (const char* state = std::getenv("XDG_STATE_HOME")) {
		return ensure(std::filesystem::path(state) / "ofxgrip" / "log");
	}
	return ensure(homeDirectory() / ".local" / "state" / "ofxgrip" / "log");
#endif
}

std::filesystem::path defaultCacheDirectory(std::string_view pluginId)
{
#if defined(__APPLE__)
	return ensure(homeDirectory() / "Library" / "Caches" / "OfxGrip" / safeName(pluginId));
#elif defined(_WIN32)
	if (const char* local = std::getenv("LOCALAPPDATA")) {
		return ensure(std::filesystem::path(local) / "OfxGrip" / "Cache" / safeName(pluginId));
	}
	return ensure(homeDirectory() / "OfxGrip" / "Cache" / safeName(pluginId));
#else
	if (const char* cache = std::getenv("XDG_CACHE_HOME")) {
		return ensure(std::filesystem::path(cache) / "ofxgrip" / safeName(pluginId));
	}
	return ensure(homeDirectory() / ".cache" / "ofxgrip" / safeName(pluginId));
#endif
}

std::filesystem::path defaultUserSupportDirectory(std::string_view pluginId)
{
#if defined(__APPLE__)
	return ensure(homeDirectory() / "Library" / "Application Support" / "OfxGrip" / safeName(pluginId));
#elif defined(_WIN32)
	if (const char* roaming = std::getenv("APPDATA")) {
		return ensure(std::filesystem::path(roaming) / "OfxGrip" / safeName(pluginId));
	}
	return ensure(homeDirectory() / "OfxGrip" / safeName(pluginId));
#else
	if (const char* data = std::getenv("XDG_DATA_HOME")) {
		return ensure(std::filesystem::path(data) / "ofxgrip" / safeName(pluginId));
	}
	return ensure(homeDirectory() / ".local" / "share" / "ofxgrip" / safeName(pluginId));
#endif
}

// ---- FileLogger ---------------------------------------------------------------------------------

FileLogger::FileLogger(std::string name) : name_(std::move(name))
{
	path_ = defaultLogDirectory() / (safeName(name_) + ".log");
}

FileLogger::~FileLogger()
{
	if (file_ != nullptr) {
		std::fclose(static_cast<FILE*>(file_));
	}
}

void FileLogger::log(LogLevel level, std::string_view message)
{
	if (level < level_) {
		return;
	}
	std::lock_guard<std::mutex> lock(mutex_);
	if (file_ == nullptr) {
		file_ = std::fopen(path_.string().c_str(), "a");
	}
	const char* levelName = "INFO";
	switch (level) {
	case LogLevel::Debug: levelName = "DEBUG"; break;
	case LogLevel::Info: levelName = "INFO"; break;
	case LogLevel::Warning: levelName = "WARN"; break;
	case LogLevel::Error: levelName = "ERROR"; break;
	}
	const std::time_t now = std::time(nullptr);
	char stamp[32] = {0};
	std::tm tmValue{};
#if defined(_WIN32)
	localtime_s(&tmValue, &now);
#else
	localtime_r(&now, &tmValue);
#endif
	std::strftime(stamp, sizeof stamp, "%Y-%m-%d %H:%M:%S", &tmValue);
	if (file_ != nullptr) {
		std::fprintf(static_cast<FILE*>(file_), "%s %-5s [%s] %.*s\n", stamp, levelName, name_.c_str(), static_cast<int>(message.size()), message.data());
		std::fflush(static_cast<FILE*>(file_));
	}
	if (echo_ || file_ == nullptr) {
		std::fprintf(stderr, "%s %-5s [%s] %.*s\n", stamp, levelName, name_.c_str(), static_cast<int>(message.size()), message.data());
	}
}

// ---- base64 for the hidden state parameter -------------------------------------------------------

namespace {

const char kBase64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string base64Encode(const Bytes& in)
{
	std::string out;
	out.reserve((in.size() + 2) / 3 * 4);
	std::size_t i = 0;
	while (i + 2 < in.size()) {
		const std::uint32_t v = (static_cast<std::uint32_t>(in[i]) << 16) | (static_cast<std::uint32_t>(in[i + 1]) << 8) | in[i + 2];
		out.push_back(kBase64[(v >> 18) & 63]);
		out.push_back(kBase64[(v >> 12) & 63]);
		out.push_back(kBase64[(v >> 6) & 63]);
		out.push_back(kBase64[v & 63]);
		i += 3;
	}
	if (i + 1 == in.size()) {
		const std::uint32_t v = static_cast<std::uint32_t>(in[i]) << 16;
		out.push_back(kBase64[(v >> 18) & 63]);
		out.push_back(kBase64[(v >> 12) & 63]);
		out += "==";
	} else if (i + 2 == in.size()) {
		const std::uint32_t v = (static_cast<std::uint32_t>(in[i]) << 16) | (static_cast<std::uint32_t>(in[i + 1]) << 8);
		out.push_back(kBase64[(v >> 18) & 63]);
		out.push_back(kBase64[(v >> 12) & 63]);
		out.push_back(kBase64[(v >> 6) & 63]);
		out.push_back('=');
	}
	return out;
}

bool base64Decode(std::string_view in, Bytes& out)
{
	out.clear();
	std::uint32_t buffer = 0;
	int bits = 0;
	for (char c : in) {
		if (c == '=') {
			break;
		}
		const char* p = std::char_traits<char>::find(kBase64, 64, c);
		if (p == nullptr) {
			return false;
		}
		buffer = (buffer << 6) | static_cast<std::uint32_t>(p - kBase64);
		bits += 6;
		if (bits >= 8) {
			bits -= 8;
			out.push_back(static_cast<std::uint8_t>((buffer >> bits) & 0xFF));
		}
	}
	return true;
}

constexpr const char* kStatePrefix = "OGST1:";

// The state blob: for each entry, u16 key length, key, u32 data length, data.
std::string encodeState(const std::map<std::string, Bytes>& blobs)
{
	Bytes raw;
	for (const auto& [key, data] : blobs) {
		const std::uint16_t klen = static_cast<std::uint16_t>(key.size());
		raw.push_back(static_cast<std::uint8_t>(klen & 0xFF));
		raw.push_back(static_cast<std::uint8_t>(klen >> 8));
		raw.insert(raw.end(), key.begin(), key.end());
		const std::uint32_t dlen = static_cast<std::uint32_t>(data.size());
		for (int i = 0; i < 4; ++i) {
			raw.push_back(static_cast<std::uint8_t>((dlen >> (8 * i)) & 0xFF));
		}
		raw.insert(raw.end(), data.begin(), data.end());
	}
	return std::string(kStatePrefix) + base64Encode(raw);
}

bool decodeState(std::string_view text, std::map<std::string, Bytes>& blobs)
{
	blobs.clear();
	if (text.empty()) {
		return true;
	}
	const std::string_view prefix(kStatePrefix);
	if (text.substr(0, prefix.size()) != prefix) {
		return false;
	}
	Bytes raw;
	if (!base64Decode(text.substr(prefix.size()), raw)) {
		return false;
	}
	std::size_t pos = 0;
	while (pos < raw.size()) {
		if (pos + 2 > raw.size()) {
			return false;
		}
		const std::size_t klen = raw[pos] | (static_cast<std::size_t>(raw[pos + 1]) << 8);
		pos += 2;
		if (pos + klen + 4 > raw.size()) {
			return false;
		}
		std::string key(reinterpret_cast<const char*>(raw.data() + pos), klen);
		pos += klen;
		std::uint32_t dlen = 0;
		for (int i = 0; i < 4; ++i) {
			dlen |= static_cast<std::uint32_t>(raw[pos + static_cast<std::size_t>(i)]) << (8 * i);
		}
		pos += 4;
		if (pos + dlen > raw.size()) {
			return false;
		}
		blobs[key] = Bytes(raw.begin() + static_cast<std::ptrdiff_t>(pos), raw.begin() + static_cast<std::ptrdiff_t>(pos + dlen));
		pos += dlen;
	}
	return true;
}

}  // namespace

// ---- the service implementations ----------------------------------------------------------------

struct OfxHostServices::Impl {
	explicit Impl(OfxHostServices& outer) : params(outer), persist(outer), feedback(outer), threads(outer), timing(outer), paths(outer) {}

	class Params final : public ParameterService {
	public:
		explicit Params(OfxHostServices& o) : outer_(o) {}

		Error get(ParameterId id, Time time, Value& out) override { return read(id, time, true, out); }
		Error getCurrent(ParameterId id, Value& out) override { return read(id, 0.0, false, out); }
		Error set(ParameterId id, Time time, const Value& value) override { return write(id, time, true, value); }
		Error setCurrent(ParameterId id, const Value& value) override { return write(id, 0.0, false, value); }

		Error getFlags(ParameterId id, HostFlags& out) override
		{
			const ParameterBinding* b = outer_.binding(id);
			if (b == nullptr) {
				return notFound(id);
			}
			HostFlags flags = b->spec.hostFlags;
			flags.set(HostFlag::Hidden, b->properties.getInt(kOfxParamPropSecret, 0, 0) != 0);
			flags.set(HostFlag::Disabled, b->properties.getInt(kOfxParamPropEnabled, 1, 0) == 0);
			ApiEvent event(EventId::ApiGetFlags, id);
			event.spec = &b->spec;
			event.hostFlags = flags;
			outer_.bus().post(event);
			out = event.hostFlags;
			return Error::none();
		}

		Error setFlags(ParameterId id, HostFlags flags) override
		{
			const ParameterBinding* b = outer_.binding(id);
			if (b == nullptr) {
				return notFound(id);
			}
			ApiEvent pre(EventId::ApiSetFlagsPre, id);
			pre.spec = &b->spec;
			pre.hostFlags = flags;
			if (Error e = outer_.bus().post(pre)) {
				return e;
			}
			if (pre.result) {
				return Error::none();
			}
			if (Error e = b->properties.setInt(kOfxParamPropSecret, pre.hostFlags.has(HostFlag::Hidden) ? 1 : 0)) {
				return e;
			}
			if (Error e = b->properties.setInt(kOfxParamPropEnabled, pre.hostFlags.has(HostFlag::Disabled) ? 0 : 1)) {
				return e;
			}
			ApiEvent post(EventId::ApiSetFlags, id);
			post.spec = &b->spec;
			post.hostFlags = pre.hostFlags;
			outer_.bus().post(post);
			return Error::none();
		}

		Error getLabel(ParameterId id, std::string& out) override
		{
			const ParameterBinding* b = outer_.binding(id);
			if (b == nullptr) {
				return notFound(id);
			}
			ApiEvent event(EventId::ApiGetLabel, id);
			event.spec = &b->spec;
			event.label = b->properties.getString(kOfxPropLabel, b->spec.label, 0);
			outer_.bus().post(event);
			out = event.label;
			return Error::none();
		}

		Error setLabel(ParameterId id, std::string_view label) override
		{
			const ParameterBinding* b = outer_.binding(id);
			if (b == nullptr) {
				return notFound(id);
			}
			ApiEvent pre(EventId::ApiSetLabelPre, id);
			pre.spec = &b->spec;
			pre.label = std::string(label);
			if (Error e = outer_.bus().post(pre)) {
				return e;
			}
			if (pre.result) {
				return Error::none();
			}
			if (Error e = b->properties.setString(kOfxPropLabel, sanitizeLabel(pre.label, true))) {
				return e;
			}
			ApiEvent post(EventId::ApiSetLabel, id);
			post.spec = &b->spec;
			post.label = pre.label;
			outer_.bus().post(post);
			return Error::none();
		}

		Error setHint(ParameterId id, std::string_view hint) override
		{
			const ParameterBinding* b = outer_.binding(id);
			if (b == nullptr) {
				return notFound(id);
			}
			return b->properties.setString(kOfxParamPropHint, sanitizeLabel(std::string(hint), false));
		}

		Error setOptions(ParameterId id, const std::vector<ChoiceOption>& options) override
		{
			const ParameterBinding* b = outer_.binding(id);
			if (b == nullptr) {
				return notFound(id);
			}
			if (outer_.profile().has(Quirk::NoDynamicChoices) || outer_.profile().has(Quirk::ChoiceOptionsFrozenAfterDescribe)) {
				return Error::make(ErrorCode::Unsupported, "This host cannot change choice options after describe").with("parameter", b->spec.key);
			}
			ApiEvent pre(EventId::ApiSetOptionsPre, id);
			pre.spec = &b->spec;
			pre.options = options;
			if (Error e = outer_.bus().post(pre)) {
				return e;
			}
			if (pre.result) {
				return Error::none();
			}
			std::vector<std::string> labels;
			for (const ChoiceOption& o : pre.options) {
				labels.push_back(sanitizeLabel(o.label.empty() ? o.key : o.label, true));
			}
			if (Error e = b->properties.setStrings(kOfxParamPropChoiceOption, labels)) {
				return e;
			}
			ApiEvent post(EventId::ApiSetOptions, id);
			post.spec = &b->spec;
			post.options = pre.options;
			outer_.bus().post(post);
			return Error::none();
		}

		Error setRange(ParameterId id, const std::vector<double>& minimum, const std::vector<double>& maximum) override
		{
			return writeRange(id, kOfxParamPropMin, kOfxParamPropMax, minimum, maximum);
		}

		Error setDisplayRange(ParameterId id, const std::vector<double>& minimum, const std::vector<double>& maximum) override
		{
			return writeRange(id, kOfxParamPropDisplayMin, kOfxParamPropDisplayMax, minimum, maximum);
		}

		Error keyframeCount(ParameterId id, int& out) override
		{
			const ParameterBinding* b = outer_.binding(id);
			if (b == nullptr) {
				return notFound(id);
			}
			unsigned int n = 0;
			const OfxStatus s = outer_.suites().parameter->paramGetNumKeys(b->handle, &n);
			out = static_cast<int>(n);
			return statusToError(s, "paramGetNumKeys");
		}

		Error keyframeTime(ParameterId id, int index, Time& out) override
		{
			const ParameterBinding* b = outer_.binding(id);
			if (b == nullptr) {
				return notFound(id);
			}
			return statusToError(outer_.suites().parameter->paramGetKeyTime(b->handle, static_cast<unsigned int>(index), &out), "paramGetKeyTime");
		}

		Error deleteAllKeyframes(ParameterId id) override
		{
			const ParameterBinding* b = outer_.binding(id);
			if (b == nullptr) {
				return notFound(id);
			}
			if (!canWrite()) {
				return wrongPhase();
			}
			return statusToError(outer_.suites().parameter->paramDeleteAllKeys(b->handle), "paramDeleteAllKeys");
		}

		bool canWrite() const override { return phaseAllowsParameterWrites(outer_.phase()) || editDepth_.load() > 0; }
		const ParameterSpec* spec(ParameterId id) const override
		{
			const ParameterBinding* b = outer_.binding(id);
			return b == nullptr ? nullptr : &b->spec;
		}
		ParameterId idForKey(std::string_view key) const override
		{
			auto it = outer_.idsByKey_.find(std::string(key));
			return it == outer_.idsByKey_.end() ? kParameterIdNone : it->second;
		}
		std::vector<ParameterId> ids() const override { return outer_.orderedIds_; }

		std::atomic<int> editDepth_{0};

	private:
		Error notFound(ParameterId id) const { return Error::make(ErrorCode::NotFound, "No such parameter").with("parameter", std::to_string(id)); }
		Error wrongPhase() const
		{
			return Error::make(ErrorCode::WrongPhase, "Parameter writes are not allowed in this phase").with("phase", std::string(toString(outer_.phase())));
		}

		Error writeRange(ParameterId id, const char* minName, const char* maxName, const std::vector<double>& minimum, const std::vector<double>& maximum)
		{
			const ParameterBinding* b = outer_.binding(id);
			if (b == nullptr) {
				return notFound(id);
			}
			const int dims = b->spec.dimensions();
			const bool integer = b->spec.type == ParameterType::Int || b->spec.type == ParameterType::Random;
			for (int i = 0; i < dims; ++i) {
				const std::size_t d = static_cast<std::size_t>(i);
				if (d < minimum.size()) {
					Error e = integer ? b->properties.setInt(minName, static_cast<int>(minimum[d]), i) : b->properties.setDouble(minName, minimum[d], i);
					if (e) {
						return e;
					}
				}
				if (d < maximum.size()) {
					Error e = integer ? b->properties.setInt(maxName, static_cast<int>(maximum[d]), i) : b->properties.setDouble(maxName, maximum[d], i);
					if (e) {
						return e;
					}
				}
			}
			return Error::none();
		}

		Error read(ParameterId id, Time time, bool timed, Value& out)
		{
			const ParameterBinding* b = outer_.binding(id);
			if (b == nullptr) {
				return notFound(id);
			}
			const OfxParameterSuiteV1* suite = outer_.suites().parameter;
			OfxStatus status = kOfxStatOK;
			Value value;
			switch (b->spec.dimensions()) {
			case 1: {
				if (b->spec.type == ParameterType::Int || b->spec.type == ParameterType::Random || b->spec.type == ParameterType::Menu || b->spec.type == ParameterType::Presets) {
					int v = 0;
					status = timed ? suite->paramGetValueAtTime(b->handle, time, &v) : suite->paramGetValue(b->handle, &v);
					value = Value(v);
				} else if (b->spec.type == ParameterType::Toggle || b->spec.type == ParameterType::Switch) {
					int v = 0;
					status = timed ? suite->paramGetValueAtTime(b->handle, time, &v) : suite->paramGetValue(b->handle, &v);
					value = Value(v != 0);
				} else {
					double v = 0;
					status = timed ? suite->paramGetValueAtTime(b->handle, time, &v) : suite->paramGetValue(b->handle, &v);
					value = Value(v);
				}
				break;
			}
			case 2: {
				double x = 0;
				double y = 0;
				status = timed ? suite->paramGetValueAtTime(b->handle, time, &x, &y) : suite->paramGetValue(b->handle, &x, &y);
				value = Value(Point2D{x, y});
				break;
			}
			case 3: {
				double a = 0;
				double c = 0;
				double d = 0;
				status = timed ? suite->paramGetValueAtTime(b->handle, time, &a, &c, &d) : suite->paramGetValue(b->handle, &a, &c, &d);
				value = b->spec.type == ParameterType::RGB ? Value(RGB{a, c, d}) : Value(Point3D{a, c, d});
				break;
			}
			case 4: {
				double r = 0;
				double g = 0;
				double bl = 0;
				double al = 0;
				status = timed ? suite->paramGetValueAtTime(b->handle, time, &r, &g, &bl, &al) : suite->paramGetValue(b->handle, &r, &g, &bl, &al);
				value = Value(RGBA{r, g, bl, al});
				break;
			}
			default: {
				const char* ofxType = ofxParameterTypeFor(b->spec.type, outer_.profile());
				if (ofxType != nullptr && (std::string_view(ofxType) == kOfxParamTypeString || std::string_view(ofxType) == kOfxParamTypeCustom)) {
					char* s = nullptr;
					status = timed ? suite->paramGetValueAtTime(b->handle, time, &s) : suite->paramGetValue(b->handle, &s);
					value = Value(std::string(s == nullptr ? "" : s));
				} else {
					return Error::make(ErrorCode::Unsupported, "The parameter carries no value").with("parameter", b->spec.key);
				}
				break;
			}
			}
			if (Error e = statusToError(status, "paramGetValue")) {
				return e.with("parameter", b->spec.key);
			}
			ApiEvent event(EventId::ApiGetValue, id);
			event.spec = &b->spec;
			event.time = time;
			event.value = std::move(value);
			outer_.bus().post(event);
			out = std::move(event.value);
			return Error::none();
		}

		Error write(ParameterId id, Time time, bool timed, const Value& value)
		{
			const ParameterBinding* b = outer_.binding(id);
			if (b == nullptr) {
				return notFound(id);
			}
			if (!canWrite()) {
				return wrongPhase().with("parameter", b->spec.key);
			}
			ApiEvent pre(EventId::ApiSetValuePre, id);
			pre.spec = &b->spec;
			pre.time = time;
			pre.value = value;
			if (Error e = outer_.bus().post(pre)) {
				return e;
			}
			if (pre.result) {
				return Error::none();
			}
			const Value& v = pre.value;
			const OfxParameterSuiteV1* suite = outer_.suites().parameter;
			OfxStatus status = kOfxStatOK;
			switch (b->spec.dimensions()) {
			case 1:
				if (b->spec.type == ParameterType::Int || b->spec.type == ParameterType::Random || b->spec.type == ParameterType::Menu || b->spec.type == ParameterType::Presets
				    || b->spec.type == ParameterType::Toggle || b->spec.type == ParameterType::Switch) {
					const int i = v.toInt(0);
					status = timed ? suite->paramSetValueAtTime(b->handle, time, i) : suite->paramSetValue(b->handle, i);
				} else {
					const double d = v.toDouble(0.0);
					status = timed ? suite->paramSetValueAtTime(b->handle, time, d) : suite->paramSetValue(b->handle, d);
				}
				break;
			case 2: {
				const Point2D p = v.tryAs<Point2D>() ? v.as<Point2D>() : Point2D{};
				status = timed ? suite->paramSetValueAtTime(b->handle, time, p.x, p.y) : suite->paramSetValue(b->handle, p.x, p.y);
				break;
			}
			case 3: {
				double a = 0;
				double c = 0;
				double d = 0;
				if (const RGB* rgb = v.tryAs<RGB>()) {
					a = rgb->r;
					c = rgb->g;
					d = rgb->b;
				} else if (const Point3D* p = v.tryAs<Point3D>()) {
					a = p->x;
					c = p->y;
					d = p->z;
				}
				status = timed ? suite->paramSetValueAtTime(b->handle, time, a, c, d) : suite->paramSetValue(b->handle, a, c, d);
				break;
			}
			case 4: {
				const RGBA c = v.tryAs<RGBA>() ? v.as<RGBA>() : RGBA{};
				status = timed ? suite->paramSetValueAtTime(b->handle, time, c.r, c.g, c.b, c.a) : suite->paramSetValue(b->handle, c.r, c.g, c.b, c.a);
				break;
			}
			default: {
				const std::string s = v.tryAs<std::string>() ? v.as<std::string>() : v.toString();
				status = timed ? suite->paramSetValueAtTime(b->handle, time, s.c_str()) : suite->paramSetValue(b->handle, s.c_str());
				break;
			}
			}
			if (Error e = statusToError(status, "paramSetValue")) {
				return e.with("parameter", b->spec.key);
			}
			ApiEvent post(EventId::ApiSetValue, id);
			post.spec = &b->spec;
			post.time = time;
			post.value = v;
			outer_.bus().post(post);
			return Error::none();
		}

		OfxHostServices& outer_;
	};

	class Persist final : public PersistenceService {
	public:
		explicit Persist(OfxHostServices& o) : outer_(o) {}
		Error store(std::string_view key, const Bytes& data) override
		{
			std::lock_guard<std::mutex> lock(mutex_);
			blobs_[std::string(key)] = data;
			dirty_ = true;
			return Error::none();
		}
		Error load(std::string_view key, Bytes& out) override
		{
			std::lock_guard<std::mutex> lock(mutex_);
			auto it = blobs_.find(std::string(key));
			if (it == blobs_.end()) {
				return Error::make(ErrorCode::NotFound, "No stored blob").with("key", std::string(key));
			}
			out = it->second;
			return Error::none();
		}
		Error remove(std::string_view key) override
		{
			std::lock_guard<std::mutex> lock(mutex_);
			dirty_ = blobs_.erase(std::string(key)) > 0 || dirty_;
			return Error::none();
		}
		std::vector<std::string> keys() override
		{
			std::lock_guard<std::mutex> lock(mutex_);
			std::vector<std::string> out;
			for (const auto& [k, v] : blobs_) {
				out.push_back(k);
			}
			return out;
		}
		Error commit() override
		{
			std::lock_guard<std::mutex> lock(mutex_);
			if (!dirty_) {
				return Error::none();
			}
			const ParameterBinding* b = outer_.binding(kParameterIdInstanceState);
			if (b == nullptr) {
				return Error::make(ErrorCode::NotFound, "The instance state parameter is not bound");
			}
			if (!outer_.parameters().canWrite()) {
				// The write waits for the next phase that allows it; the state stays dirty.
				return Error::none();
			}
			const std::string text = encodeState(blobs_);
			const OfxStatus status = outer_.suites().parameter->paramSetValue(b->handle, text.c_str());
			if (Error e = statusToError(status, "paramSetValue(state)")) {
				return e;
			}
			dirty_ = false;
			return Error::none();
		}
		void loadFromHost()
		{
			std::lock_guard<std::mutex> lock(mutex_);
			const ParameterBinding* b = outer_.binding(kParameterIdInstanceState);
			if (b == nullptr) {
				return;
			}
			char* text = nullptr;
			if (outer_.suites().parameter->paramGetValue(b->handle, &text) != kOfxStatOK || text == nullptr) {
				return;
			}
			if (!decodeState(text, blobs_)) {
				outer_.log().warning("The stored instance state is unreadable and was discarded");
				blobs_.clear();
			}
			dirty_ = false;
		}

	private:
		OfxHostServices& outer_;
		std::mutex mutex_;
		std::map<std::string, Bytes> blobs_;
		bool dirty_{false};
	};

	class Feedback final : public FeedbackService {
	public:
		explicit Feedback(OfxHostServices& o) : outer_(o) {}
		Error message(MessageKind kind, std::string_view id, std::string_view text) override
		{
			const std::string idText(id);
			const std::string body(text);
			const Suites& s = outer_.suites();
			if (s.messageV1 != nullptr && s.messageV1->message != nullptr) {
				const OfxStatus status = s.messageV1->message(outer_.instanceHandle(), kindName(kind), idText.c_str(), "%s", body.c_str());
				if (kind == MessageKind::Question) {
					return status == kOfxStatReplyYes ? Error::none() : Error::make(ErrorCode::Value, "No");
				}
				return statusToError(status, "message");
			}
			outer_.log().info(body);
			return Error::none();
		}
		Error setPersistentMessage(MessageKind kind, std::string_view id, std::string_view text) override
		{
			const Suites& s = outer_.suites();
			// Q-RESOLVE-007: the V2 suite is used only when every pointer is non-null and the host is
			// not in the known-bad range.
			if (s.messageV2Usable() && !outer_.profile().has(Quirk::MessageSuiteV2Broken)) {
				const std::string idText(id);
				const std::string body(text);
				return statusToError(s.messageV2->setPersistentMessage(outer_.instanceHandle(), kindName(kind), idText.c_str(), "%s", body.c_str()), "setPersistentMessage");
			}
			return message(kind, id, text);
		}
		Error clearPersistentMessage() override
		{
			const Suites& s = outer_.suites();
			if (s.messageV2Usable() && !outer_.profile().has(Quirk::MessageSuiteV2Broken)) {
				return statusToError(s.messageV2->clearPersistentMessage(outer_.instanceHandle()), "clearPersistentMessage");
			}
			return Error::none();
		}
		bool abortRequested() override
		{
			const Suites& s = outer_.suites();
			return s.imageEffect->abort != nullptr && s.imageEffect->abort(outer_.instanceHandle()) != 0;
		}
		Error progressStart(std::string_view label) override
		{
			const Suites& s = outer_.suites();
			const std::string text(label);
			if (s.progressV2 != nullptr && s.progressV2->progressStart != nullptr) {
				return statusToError(s.progressV2->progressStart(outer_.instanceHandle(), text.c_str(), "ofxgrip.progress"), "progressStart");
			}
			if (s.progressV1 != nullptr && s.progressV1->progressStart != nullptr) {
				return statusToError(s.progressV1->progressStart(outer_.instanceHandle(), text.c_str()), "progressStart");
			}
			return Error::none();
		}
		Error progressUpdate(double fraction) override
		{
			const Suites& s = outer_.suites();
			if (s.progressV2 != nullptr && s.progressV2->progressUpdate != nullptr) {
				return statusToError(s.progressV2->progressUpdate(outer_.instanceHandle(), fraction), "progressUpdate");
			}
			if (s.progressV1 != nullptr && s.progressV1->progressUpdate != nullptr) {
				return statusToError(s.progressV1->progressUpdate(outer_.instanceHandle(), fraction), "progressUpdate");
			}
			return Error::none();
		}
		Error progressEnd() override
		{
			const Suites& s = outer_.suites();
			if (s.progressV2 != nullptr && s.progressV2->progressEnd != nullptr) {
				return statusToError(s.progressV2->progressEnd(outer_.instanceHandle()), "progressEnd");
			}
			if (s.progressV1 != nullptr && s.progressV1->progressEnd != nullptr) {
				return statusToError(s.progressV1->progressEnd(outer_.instanceHandle()), "progressEnd");
			}
			return Error::none();
		}

	private:
		static const char* kindName(MessageKind kind) noexcept
		{
			switch (kind) {
			case MessageKind::Log: return kOfxMessageLog;
			case MessageKind::Message: return kOfxMessageMessage;
			case MessageKind::Warning: return kOfxMessageWarning;
			case MessageKind::Error: return kOfxMessageError;
			case MessageKind::Question: return kOfxMessageQuestion;
			}
			return kOfxMessageMessage;
		}
		OfxHostServices& outer_;
	};

	class Threads final : public ThreadingService {
	public:
		explicit Threads(OfxHostServices& o) : outer_(o) {}
		bool useHostSuite() const
		{
			const Suites& s = outer_.suites();
			return s.multiThread != nullptr && s.multiThread->multiThread != nullptr && !outer_.profile().has(Quirk::ThreadSuiteDummy) && !outer_.profile().has(Quirk::NullThreadSuite);
		}
		unsigned cpuCount() override
		{
			const Suites& s = outer_.suites();
			if (useHostSuite() && s.multiThread->multiThreadNumCPUs != nullptr) {
				unsigned n = 0;
				if (s.multiThread->multiThreadNumCPUs(&n) == kOfxStatOK && n > 0) {
					return n;
				}
			}
			const unsigned hw = std::thread::hardware_concurrency();
			return hw == 0 ? 1 : hw;
		}
		Error parallelFor(unsigned count, const std::function<void(unsigned index, unsigned count)>& work) override
		{
			if (count == 0) {
				return Error::none();
			}
			if (count == 1) {
				work(0, 1);
				return Error::none();
			}
			if (useHostSuite()) {
				struct Context {
					const std::function<void(unsigned, unsigned)>* work;
				} context{&work};
				const OfxStatus status = outer_.suites().multiThread->multiThread(
				    [](unsigned int index, unsigned int total, void* arg) {
					    auto* c = static_cast<Context*>(arg);
					    (*c->work)(index, total);
				    },
				    count, &context);
				if (status == kOfxStatOK) {
					return Error::none();
				}
				// A failing host suite falls through to the plug-in's own pool.
			}
			const unsigned workers = std::min(count, cpuCount());
			std::atomic<unsigned> next{0};
			std::vector<std::thread> pool;
			pool.reserve(workers);
			auto loop = [&]() {
				spawned_ = true;
				for (unsigned i = next.fetch_add(1); i < count; i = next.fetch_add(1)) {
					work(i, count);
				}
				spawned_ = false;
			};
			for (unsigned w = 1; w < workers; ++w) {
				pool.emplace_back(loop);
			}
			loop();
			for (std::thread& t : pool) {
				t.join();
			}
			return Error::none();
		}
		bool isSpawnedThread() override
		{
			const Suites& s = outer_.suites();
			if (useHostSuite() && s.multiThread->multiThreadIsSpawnedThread != nullptr) {
				return s.multiThread->multiThreadIsSpawnedThread() != 0;
			}
			return spawned_;
		}

	private:
		OfxHostServices& outer_;
		static thread_local bool spawned_;
	};

	class Timing final : public TimeService {
	public:
		explicit Timing(OfxHostServices& o) : outer_(o) {}
		Time currentTime() override
		{
			const Suites& s = outer_.suites();
			double t = 0;
			if (s.timeLine != nullptr && s.timeLine->getTime != nullptr && s.timeLine->getTime(outer_.instanceHandle(), &t) == kOfxStatOK) {
				return t;
			}
			return 0.0;
		}
		std::pair<Time, Time> timeBounds() override
		{
			const Suites& s = outer_.suites();
			double first = 0;
			double last = 0;
			if (s.timeLine != nullptr && s.timeLine->getTimeBounds != nullptr && s.timeLine->getTimeBounds(outer_.instanceHandle(), &first, &last) == kOfxStatOK) {
				return {first, last};
			}
			const std::vector<double> range = outer_.instanceProperties().getDoubles(kOfxImageEffectPropFrameRange);
			if (range.size() == 2) {
				return {range[0], range[1]};
			}
			return {0.0, 0.0};
		}
		double frameRate() override { return outer_.instanceProperties().getDouble(kOfxImageEffectPropFrameRate, 24.0, 0); }
		Point2D projectSize() override
		{
			const std::vector<double> v = outer_.instanceProperties().getDoubles(kOfxImageEffectPropProjectSize);
			return v.size() == 2 ? Point2D{v[0], v[1]} : Point2D{};
		}
		Point2D projectOffset() override
		{
			const std::vector<double> v = outer_.instanceProperties().getDoubles(kOfxImageEffectPropProjectOffset);
			return v.size() == 2 ? Point2D{v[0], v[1]} : Point2D{};
		}
		double projectPixelAspect() override
		{
			const double par = outer_.instanceProperties().getDouble(kOfxImageEffectPropProjectPixelAspectRatio, 1.0, 0);
			return par > 0.0 ? par : 1.0;  // Q-OFX-ANY-017
		}

	private:
		OfxHostServices& outer_;
	};

	class Paths final : public PathService {
	public:
		explicit Paths(OfxHostServices& o) : outer_(o) {}
		std::filesystem::path cacheDirectory() override { return defaultCacheDirectory(outer_.identity().pluginId); }
		std::filesystem::path userSupportDirectory() override { return defaultUserSupportDirectory(outer_.identity().pluginId); }
		std::filesystem::path logDirectory() override { return defaultLogDirectory(); }

	private:
		OfxHostServices& outer_;
	};

	Params params;
	Persist persist;
	Feedback feedback;
	Threads threads;
	Timing timing;
	Paths paths;
};

thread_local bool OfxHostServices::Impl::Threads::spawned_ = false;
thread_local Phase OfxHostServices::threadPhase_ = Phase::None;

// ---- OfxHostServices ----------------------------------------------------------------------------

OfxHostServices::OfxHostServices(const Suites& suites,
                                 const HostProfile& profile,
                                 const EffectManifest& manifest,
                                 Identity identity,
                                 EventBus& bus,
                                 LogService& logger,
                                 OfxImageEffectHandle instance)
: suites_(suites), profile_(profile), manifest_(manifest), identity_(std::move(identity)), bus_(bus), logger_(logger), instance_(instance), impl_(std::make_unique<Impl>(*this))
{
	EditScope::Hooks hooks;
	hooks.begin = [this](std::string_view name) {
		impl_->params.editDepth_++;
		if (suites_.parameter->paramEditBegin != nullptr && paramSet_ != nullptr) {
			const std::string n(name);
			return statusToError(suites_.parameter->paramEditBegin(paramSet_, n.c_str()), "paramEditBegin");
		}
		return Error::none();
	};
	hooks.end = [this]() {
		if (impl_->params.editDepth_.load() > 0) {
			impl_->params.editDepth_--;
		}
		if (suites_.parameter->paramEditEnd != nullptr && paramSet_ != nullptr) {
			return statusToError(suites_.parameter->paramEditEnd(paramSet_), "paramEditEnd");
		}
		return Error::none();
	};
	hooks.flush = [this]() {
		PlainEvent event(EventId::Flush);
		bus_.post(event);
		impl_->persist.commit();
	};
	EditScope::setHooks(*this, std::move(hooks));
}

OfxHostServices::~OfxHostServices()
{
	EditScope::setHooks(*this, {});
}

ParameterService& OfxHostServices::parameters()
{
	return impl_->params;
}

PersistenceService& OfxHostServices::persistence()
{
	return impl_->persist;
}

FeedbackService& OfxHostServices::feedback()
{
	return impl_->feedback;
}

ThreadingService& OfxHostServices::threading()
{
	return impl_->threads;
}

TimeService& OfxHostServices::time()
{
	return impl_->timing;
}

PathService& OfxHostServices::paths()
{
	return impl_->paths;
}

PropertySet OfxHostServices::instanceProperties() const
{
	OfxPropertySetHandle handle = nullptr;
	if (suites_.imageEffect->getPropertySet(instance_, &handle) != kOfxStatOK) {
		return {};
	}
	return PropertySet(suites_.property, handle);
}

Error OfxHostServices::bind()
{
	if (suites_.imageEffect->getParamSet(instance_, &paramSet_) != kOfxStatOK || paramSet_ == nullptr) {
		return Error::make(ErrorCode::BadHandle, "The instance has no parameter set");
	}
	bindings_.clear();
	orderedIds_.clear();
	idsByKey_.clear();
	std::vector<ParameterSpec> flat = manifest_.flattenedParameters();
	for (ParameterSpec& extra : frameworkParameters()) {
		flat.push_back(std::move(extra));
	}
	for (ParameterSpec& spec : flat) {
		if (ofxParameterTypeFor(spec.type, profile_) == nullptr) {
			continue;
		}
		OfxParamHandle handle = nullptr;
		OfxPropertySetHandle props = nullptr;
		if (suites_.parameter->paramGetHandle(paramSet_, spec.key.c_str(), &handle, &props) != kOfxStatOK || handle == nullptr) {
			logger_.debug("Parameter not found on the instance: " + spec.key);
			continue;
		}
		ParameterBinding binding;
		binding.spec = spec;
		binding.handle = handle;
		binding.properties = PropertySet(suites_.property, props);
		idsByKey_[spec.key] = spec.id;
		orderedIds_.push_back(spec.id);
		bindings_[spec.id] = std::move(binding);
	}
	clips_.clear();
	std::vector<ClipSpec> clips = manifest_.effectiveClips();
	clips.push_back(ClipSpec{kOfxImageEffectOutputClipName, "Output", false, false, false, {}});
	for (const ClipSpec& clip : clips) {
		OfxImageClipHandle handle = nullptr;
		OfxPropertySetHandle props = nullptr;
		if (suites_.imageEffect->clipGetHandle(instance_, clip.name.c_str(), &handle, &props) == kOfxStatOK && handle != nullptr) {
			clips_[clip.name] = handle;
		}
	}
	impl_->persist.loadFromHost();
	return Error::none();
}

OfxImageClipHandle OfxHostServices::clipHandle(std::string_view name) const noexcept
{
	auto it = clips_.find(std::string(name));
	return it == clips_.end() ? nullptr : it->second;
}

PropertySet OfxHostServices::clipProperties(std::string_view name) const
{
	OfxImageClipHandle handle = clipHandle(name);
	if (handle == nullptr) {
		return {};
	}
	OfxPropertySetHandle props = nullptr;
	if (suites_.imageEffect->clipGetPropertySet(handle, &props) != kOfxStatOK) {
		return {};
	}
	return PropertySet(suites_.property, props);
}

const ParameterBinding* OfxHostServices::binding(ParameterId id) const noexcept
{
	auto it = bindings_.find(id);
	return it == bindings_.end() ? nullptr : &it->second;
}

OFXGRIP_NAMESPACE_END
