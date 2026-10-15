// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
// In-memory implementations of the host services for host-free tests of the parameter model and
// the framework extensions.
#pragma once

#include <ofxgrip/core/Extension.hpp>
#include <ofxgrip/core/HostServices.hpp>
#include <ofxgrip/core/Manifest.hpp>

#include <algorithm>
#include <map>
#include <string>
#include <vector>

namespace ofxgrip::testing {

/*! A parameter store over a manifest: values by ID (one value, no keyframes), flags, labels,
    and the API events a real service posts. */
class FakeParameterService final : public ParameterService {
public:
	explicit FakeParameterService(EventBus& bus) : bus_(bus) {}

	/*! Binds the manifest's flattened parameters, with their defaults. */
	void bind(const EffectManifest& manifest)
	{
		specs_.clear();
		order_.clear();
		values_.clear();
		flags_.clear();
		labels_.clear();
		for (const ParameterSpec& spec : manifest.flattenedParameters()) {
			specs_[spec.id] = spec;
			order_.push_back(spec.id);
			values_[spec.id] = spec.defaultValue;
			flags_[spec.id] = spec.hostFlags;
			labels_[spec.id] = spec.label;
		}
	}

	Error get(ParameterId id, Time, Value& out) override { return getCurrent(id, out); }
	Error getCurrent(ParameterId id, Value& out) override
	{
		auto it = values_.find(id);
		if (it == values_.end()) {
			return notFound(id);
		}
		ApiEvent event(EventId::ApiGetValue, id);
		event.value = it->second;
		bus_.post(event);
		out = event.value;
		return Error::none();
	}
	Error set(ParameterId id, Time, const Value& value) override { return setCurrent(id, value); }
	Error setCurrent(ParameterId id, const Value& value) override
	{
		auto it = values_.find(id);
		if (it == values_.end()) {
			return notFound(id);
		}
		if (!writable) {
			return Error::make(ErrorCode::WrongPhase, "Parameter writes are not allowed in this phase");
		}
		ApiEvent pre(EventId::ApiSetValuePre, id);
		pre.value = value;
		if (Error e = bus_.post(pre)) {
			return e;
		}
		if (pre.result) {
			return Error::none();
		}
		it->second = pre.value;
		writes.push_back(specs_[id].key);
		return Error::none();
	}
	Error getFlags(ParameterId id, HostFlags& out) override
	{
		auto it = flags_.find(id);
		if (it == flags_.end()) {
			return notFound(id);
		}
		out = it->second;
		return Error::none();
	}
	Error setFlags(ParameterId id, HostFlags flags) override
	{
		auto it = flags_.find(id);
		if (it == flags_.end()) {
			return notFound(id);
		}
		it->second = flags;
		flagWrites.push_back(specs_[id].key);
		return Error::none();
	}
	Error getLabel(ParameterId id, std::string& out) override
	{
		auto it = labels_.find(id);
		if (it == labels_.end()) {
			return notFound(id);
		}
		out = it->second;
		return Error::none();
	}
	Error setLabel(ParameterId id, std::string_view label) override
	{
		auto it = labels_.find(id);
		if (it == labels_.end()) {
			return notFound(id);
		}
		it->second = std::string(label);
		labelWrites.push_back(specs_[id].key);
		return Error::none();
	}
	Error setHint(ParameterId id, std::string_view hint) override
	{
		hints[id] = std::string(hint);
		return Error::none();
	}
	Error setOptions(ParameterId id, const std::vector<ChoiceOption>& options) override
	{
		optionWrites[id] = options;
		return Error::none();
	}
	Error setRange(ParameterId id, const std::vector<double>& minimum, const std::vector<double>& maximum) override
	{
		ranges[id] = {minimum, maximum};
		return Error::none();
	}
	Error setDisplayRange(ParameterId id, const std::vector<double>& minimum, const std::vector<double>& maximum) override
	{
		displayRanges[id] = {minimum, maximum};
		return Error::none();
	}
	Error keyframeCount(ParameterId, int& out) override
	{
		out = keyframes;
		return Error::none();
	}
	Error keyframeTime(ParameterId, int, Time& out) override
	{
		out = 0;
		return Error::none();
	}
	Error deleteAllKeyframes(ParameterId) override
	{
		keyframes = 0;
		return Error::none();
	}
	bool canWrite() const override { return writable; }
	const ParameterSpec* spec(ParameterId id) const override
	{
		auto it = specs_.find(id);
		return it == specs_.end() ? nullptr : &it->second;
	}
	ParameterId idForKey(std::string_view key) const override
	{
		for (const auto& [id, spec] : specs_) {
			if (spec.key == key) {
				return id;
			}
		}
		return kParameterIdNone;
	}
	std::vector<ParameterId> ids() const override { return order_; }

	/*! Test access. */
	Value& valueRef(ParameterId id) { return values_[id]; }
	HostFlags flagsOf(ParameterId id) const { return flags_.at(id); }
	std::string labelOf(ParameterId id) const { return labels_.at(id); }
	ParameterSpec& specRef(ParameterId id) { return specs_[id]; }

	bool writable{true};
	int keyframes{0};
	std::vector<std::string> writes;
	std::vector<std::string> flagWrites;
	std::vector<std::string> labelWrites;
	std::map<ParameterId, std::string> hints;
	std::map<ParameterId, std::vector<ChoiceOption>> optionWrites;
	std::map<ParameterId, std::pair<std::vector<double>, std::vector<double>>> ranges;
	std::map<ParameterId, std::pair<std::vector<double>, std::vector<double>>> displayRanges;

private:
	static Error notFound(ParameterId id) { return Error::make(ErrorCode::NotFound, "No such parameter").with("parameter", std::to_string(id)); }
	EventBus& bus_;
	std::map<ParameterId, ParameterSpec> specs_;
	std::vector<ParameterId> order_;
	std::map<ParameterId, Value> values_;
	std::map<ParameterId, HostFlags> flags_;
	std::map<ParameterId, std::string> labels_;
};

class FakePersistence final : public PersistenceService {
public:
	Error store(std::string_view key, const Bytes& data) override
	{
		blobs[std::string(key)] = data;
		return Error::none();
	}
	Error load(std::string_view key, Bytes& out) override
	{
		auto it = blobs.find(std::string(key));
		if (it == blobs.end()) {
			return Error::make(ErrorCode::NotFound, "No stored blob");
		}
		out = it->second;
		return Error::none();
	}
	Error remove(std::string_view key) override
	{
		blobs.erase(std::string(key));
		return Error::none();
	}
	std::vector<std::string> keys() override
	{
		std::vector<std::string> out;
		for (const auto& [k, v] : blobs) {
			out.push_back(k);
		}
		return out;
	}
	Error commit() override
	{
		commits++;
		return Error::none();
	}
	std::map<std::string, Bytes> blobs;
	int commits{0};
};

class FakeFeedback final : public FeedbackService {
public:
	Error message(MessageKind, std::string_view, std::string_view text) override
	{
		messages.emplace_back(text);
		return Error::none();
	}
	Error setPersistentMessage(MessageKind, std::string_view, std::string_view text) override
	{
		persistent = std::string(text);
		return Error::none();
	}
	Error clearPersistentMessage() override
	{
		persistent.clear();
		return Error::none();
	}
	bool abortRequested() override { return abort; }
	Error progressStart(std::string_view) override { return Error::none(); }
	Error progressUpdate(double) override { return Error::none(); }
	Error progressEnd() override { return Error::none(); }
	std::vector<std::string> messages;
	std::string persistent;
	bool abort{false};
};

class FakeThreading final : public ThreadingService {
public:
	unsigned cpuCount() override { return 2; }
	Error parallelFor(unsigned count, const std::function<void(unsigned, unsigned)>& work) override
	{
		for (unsigned i = 0; i < count; ++i) {
			work(i, count);
		}
		return Error::none();
	}
	bool isSpawnedThread() override { return false; }
};

class FakeLog final : public LogService {
public:
	void log(LogLevel level, std::string_view message) override { lines.emplace_back(level, std::string(message)); }
	bool contains(std::string_view text) const
	{
		return std::any_of(lines.begin(), lines.end(), [text](const auto& line) { return line.second.find(text) != std::string::npos; });
	}
	std::vector<std::pair<LogLevel, std::string>> lines;
};

class FakeTime final : public TimeService {
public:
	Time currentTime() override { return now; }
	std::pair<Time, Time> timeBounds() override { return {0, 100}; }
	double frameRate() override { return 24.0; }
	Point2D projectSize() override { return {1920, 1080}; }
	Point2D projectOffset() override { return {}; }
	double projectPixelAspect() override { return 1.0; }
	Time now{0};
};

class FakePaths final : public PathService {
public:
	std::filesystem::path cacheDirectory() override { return std::filesystem::temp_directory_path(); }
	std::filesystem::path userSupportDirectory() override { return std::filesystem::temp_directory_path(); }
	std::filesystem::path logDirectory() override { return std::filesystem::temp_directory_path(); }
};

/*! The complete seam over the fakes. */
class FakeHostServices final : public HostServices {
public:
	explicit FakeHostServices(EventBus& bus) : params(bus) {}
	const Identity& identity() const override { return ident; }
	const HostProfile& profile() const override { return hostProfile; }
	Phase phase() const override { return currentPhase; }
	ParameterService& parameters() override { return params; }
	PersistenceService& persistence() override { return persist; }
	FeedbackService& feedback() override { return feedbackService; }
	ThreadingService& threading() override { return threads; }
	LogService& log() override { return logService; }
	TimeService& time() override { return timeService; }
	PathService& paths() override { return pathService; }

	Identity ident;
	HostProfile hostProfile;
	Phase currentPhase{Phase::CreateInstance};
	FakeParameterService params;
	FakePersistence persist;
	FakeFeedback feedbackService;
	FakeThreading threads;
	FakeLog logService;
	FakeTime timeService;
	FakePaths pathService;
};

/*! An effect host over the fakes: a bus, a manifest, and the services bound to it. */
class FakeEffectHost final : public EffectHost {
public:
	explicit FakeEffectHost(EffectManifest m) : manifest_(std::move(m)), services_(bus_)
	{
		services_.params.bind(manifest_);
		services_.ident.pluginId = manifest_.identity.ofxId;
		services_.ident.pluginVersion = manifest_.version;
	}
	HostServices& services() override { return services_; }
	EventBus& bus() override { return bus_; }
	const EffectManifest& manifest() const override { return manifest_; }
	FakeHostServices& fakes() { return services_; }
	FakeParameterService& params() { return services_.params; }
	EffectManifest& manifestRef() { return manifest_; }

	/*! Posts a change for a parameter key. */
	Error change(std::string_view key, ChangeReason reason = ChangeReason::UserEdited)
	{
		ChangeEvent event(EventId::InstanceChanged, services_.params.idForKey(key), std::string(key), 0, reason);
		return bus_.post(event);
	}
	Error post(EventId id)
	{
		PlainEvent event(id);
		return bus_.post(event);
	}

private:
	EventBus bus_;
	EffectManifest manifest_;
	FakeHostServices services_;
};

}  // namespace ofxgrip::testing
