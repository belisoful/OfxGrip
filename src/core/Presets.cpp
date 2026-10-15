// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
#include <ofxgrip/core/Presets.hpp>

#include <ofxgrip/core/MetaExtension.hpp>

#include <algorithm>

OFXGRIP_NAMESPACE_BEGIN

// ---- helpers ------------------------------------------------------------------------------------

Preset resolveManifestPreset(const EffectManifest& manifest, const ManifestPreset& source)
{
	Preset out;
	out.name = source.name;
	out.tag = source.tag;
	out.framework = "OfxGrip";
	out.source = PresetSource::Manifest;
	out.pluginUuid = manifest.identity.uuid;
	out.pluginVersion = manifest.version.toString();
	out.pluginAuthor = manifest.identity.author;
	out.pluginLocalizedName = manifest.identity.name;
	out.productId = manifest.identity.productId;
	for (const auto& [key, value] : source.values) {
		if (const ParameterSpec* spec = manifest.find(key)) {
			out.values[spec->id] = value;
		}
	}
	for (const auto& [key, list] : source.tags) {
		if (const ParameterSpec* spec = manifest.find(key)) {
			out.tags[spec->id] = list;
		}
	}
	for (const auto& [key, entries] : source.meta) {
		if (const ParameterSpec* spec = manifest.find(key)) {
			out.meta[spec->id] = entries;
		}
	}
	return out;
}

bool coercePresetValue(const ParameterSpec& spec, const Value& in, Value& out)
{
	switch (spec.type) {
	case ParameterType::Menu:
	case ParameterType::Presets: {
		if (const std::string* key = in.tryAs<std::string>()) {
			const int index = spec.optionIndex(*key);
			if (index < 0) {
				return false;
			}
			out = Value(index);
			return true;
		}
		if (in.kind() == ValueKind::Int || in.kind() == ValueKind::Double || in.kind() == ValueKind::Bool) {
			out = Value(in.toInt(0));
			return true;
		}
		return false;
	}
	case ParameterType::Toggle:
	case ParameterType::Switch:
		if (in.kind() == ValueKind::Int || in.kind() == ValueKind::Double || in.kind() == ValueKind::Bool || in.kind() == ValueKind::String) {
			out = Value(in.toBool(false));
			return true;
		}
		return false;
	case ParameterType::Int:
	case ParameterType::Random:
		if (in.kind() == ValueKind::Int || in.kind() == ValueKind::Double || in.kind() == ValueKind::Bool) {
			out = Value(in.toInt(0));
			return true;
		}
		return false;
	case ParameterType::Float:
	case ParameterType::Angle:
	case ParameterType::Percent:
	case ParameterType::Progress:
	case ParameterType::TrackingOpacity:
		if (in.kind() == ValueKind::Int || in.kind() == ValueKind::Double || in.kind() == ValueKind::Bool) {
			out = Value(in.toDouble(0.0));
			return true;
		}
		return false;
	case ParameterType::Point:
		if (const Point2D* p = in.tryAs<Point2D>()) {
			out = Value(*p);
			return true;
		}
		if (const Point3D* p = in.tryAs<Point3D>()) {
			out = Value(Point2D{p->x, p->y});
			return true;
		}
		return false;
	case ParameterType::Point3D:
		if (const Point3D* p = in.tryAs<Point3D>()) {
			out = Value(*p);
			return true;
		}
		if (const Point2D* p = in.tryAs<Point2D>()) {
			out = Value(Point3D{p->x, p->y, 0.0});
			return true;
		}
		return false;
	case ParameterType::RGB:
		if (const RGB* c = in.tryAs<RGB>()) {
			out = Value(*c);
			return true;
		}
		if (const RGBA* c = in.tryAs<RGBA>()) {
			out = Value(RGB{c->r, c->g, c->b});
			return true;
		}
		return false;
	case ParameterType::RGBA:
		if (const RGBA* c = in.tryAs<RGBA>()) {
			out = Value(*c);
			return true;
		}
		if (const RGB* c = in.tryAs<RGB>()) {
			out = Value(RGBA{c->r, c->g, c->b, 1.0});
			return true;
		}
		return false;
	case ParameterType::String:
	case ParameterType::FontMenu:
	case ParameterType::PathID:
	case ParameterType::Banner:
	case ParameterType::Status:
	case ParameterType::Capsule:
	case ParameterType::Divider:
		if (in.kind() == ValueKind::Blob || in.kind() == ValueKind::Empty) {
			return false;
		}
		out = in.is<std::string>() ? in : Value(in.toString());
		return true;
	case ParameterType::Custom:
	case ParameterType::Blob:
		if (in.kind() == ValueKind::Blob || in.kind() == ValueKind::String) {
			out = in;
			return true;
		}
		return false;
	default:
		return false;
	}
}

std::filesystem::path bundleDirectoryFor(const std::filesystem::path& reported)
{
	if (reported.empty()) {
		return {};
	}
	for (std::filesystem::path p = reported; !p.empty() && p != p.root_path(); p = p.parent_path()) {
		if (p.extension() == ".bundle") {
			return p;
		}
		if (p.parent_path() == p) {
			break;
		}
	}
	std::error_code ec;
	if (std::filesystem::is_directory(reported, ec)) {
		return reported;
	}
	return reported.parent_path();
}

std::vector<Preset> presetsInDirectory(const std::filesystem::path& directory, std::string_view tag, PresetSource source)
{
	std::vector<Preset> out;
	std::error_code ec;
	if (directory.empty() || !std::filesystem::is_directory(directory, ec)) {
		return out;
	}
	for (const auto& entry : std::filesystem::directory_iterator(directory, ec)) {
		if (!entry.is_regular_file(ec) || entry.path().extension() != presetkeys::kExtension) {
			continue;
		}
		Preset preset;
		if (!Preset::load(entry.path(), preset).ok()) {
			continue;
		}
		preset.source = source;
		if (preset.tag.empty()) {
			preset.tag = std::string(tag);
		}
		out.push_back(std::move(preset));
	}
	std::sort(out.begin(), out.end(), [](const Preset& a, const Preset& b) { return a.name < b.name; });
	return out;
}

std::vector<PresetMenuEntry> presetMenuEntries(const std::vector<Preset>& user, const std::vector<Preset>& plugin)
{
	std::vector<PresetMenuEntry> out;
	out.push_back({PresetMenuEntry::Kind::Default, "Default", ""});
	auto section = [&out](const std::vector<Preset>& list, PresetMenuEntry::Kind kind) {
		if (list.empty()) {
			return;
		}
		out.push_back({PresetMenuEntry::Kind::Separator, "\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80", ""});
		for (const Preset& preset : list) {
			out.push_back({kind, preset.name, preset.name});
		}
	};
	section(user, PresetMenuEntry::Kind::User);
	section(plugin, PresetMenuEntry::Kind::Plugin);
	out.push_back({PresetMenuEntry::Kind::Separator, "\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80", ""});
	out.push_back({PresetMenuEntry::Kind::Reveal, "Reveal User Presets...", ""});
	out.push_back({PresetMenuEntry::Kind::Save, "Save Preset", ""});
	return out;
}

std::vector<ChoiceOption> presetMenuOptions(const std::vector<PresetMenuEntry>& entries)
{
	std::vector<ChoiceOption> out;
	int separators = 0;
	for (const PresetMenuEntry& entry : entries) {
		std::string key;
		switch (entry.kind) {
		case PresetMenuEntry::Kind::Default: key = "default"; break;
		case PresetMenuEntry::Kind::Separator: key = "separator" + std::to_string(++separators); break;
		case PresetMenuEntry::Kind::User: key = "user:" + entry.name; break;
		case PresetMenuEntry::Kind::Plugin: key = "plugin:" + entry.name; break;
		case PresetMenuEntry::Kind::Reveal: key = "reveal"; break;
		case PresetMenuEntry::Kind::Save: key = "save"; break;
		}
		out.push_back({key, entry.label});
	}
	return out;
}

std::vector<PresetMenuEntry> presetMenuEntriesForTag(const EffectManifest& manifest, const std::filesystem::path& bundlePath, const std::filesystem::path& userSupportDirectory, std::string_view tag)
{
	std::vector<Preset> plugin;
	for (const ManifestPreset& preset : manifest.presets) {
		if (preset.tag == tag) {
			plugin.push_back(resolveManifestPreset(manifest, preset));
		}
	}
	if (!bundlePath.empty() && !tag.empty()) {
		const std::vector<Preset> bundled = presetsInDirectory(bundleDirectoryFor(bundlePath) / "Contents" / "Resources" / "Presets" / std::string(tag), tag, PresetSource::Bundle);
		plugin.insert(plugin.end(), bundled.begin(), bundled.end());
	}
	std::vector<Preset> user;
	if (!userSupportDirectory.empty() && !tag.empty()) {
		user = presetsInDirectory(userSupportDirectory / "Presets" / std::string(tag), tag, PresetSource::User);
	}
	return presetMenuEntries(user, plugin);
}

// ---- PresetManager ------------------------------------------------------------------------------

PresetManager::PresetManager(EffectHost& host, MetaManager* meta) : host_(host), meta_(meta) {}

Preset PresetManager::capture(std::string_view name, std::string_view tag) const
{
	const EffectManifest& manifest = host_.manifest();
	ParameterService& params = host_.services().parameters();
	Preset out;
	out.name = std::string(name);
	out.tag = std::string(tag);
	out.framework = "OfxGrip";
	out.uuid = Preset::makeUuid();
	out.createdTime = Preset::nowIso8601();
	out.pluginUuid = manifest.identity.uuid;
	out.pluginVersion = manifest.version.toString();
	out.pluginAuthor = manifest.identity.author;
	out.pluginLocalizedName = manifest.identity.name;
	out.productId = manifest.identity.productId;
	for (ParameterId id : params.ids()) {
		const ParameterSpec* spec = params.spec(id);
		if (spec == nullptr || !spec->hasState() || spec->type == ParameterType::Presets || isReservedParameterId(id)) {
			continue;
		}
		if (!spec->appFlags.has(AppFlag::PresetNoValue)) {
			Value value;
			if (params.getCurrent(id, value).ok()) {
				out.values[id] = std::move(value);
			}
		}
		if (!spec->appFlags.has(AppFlag::PresetNoTags)) {
			const std::vector<std::string> tags = meta_ != nullptr && meta_->has(id) ? meta_->tags(id) : spec->tags;
			if (!tags.empty()) {
				out.tags[id] = tags;
			}
		}
		if (!spec->appFlags.has(AppFlag::PresetNoMeta)) {
			std::map<std::string, Value> entries = meta_ != nullptr && meta_->has(id) ? meta_->metaOf(id) : spec->meta;
			entries.erase(std::string(MetaManager::kSelectedPresetKey));
			if (!entries.empty()) {
				out.meta[id] = std::move(entries);
			}
		}
	}
	return out;
}

bool PresetManager::compatible(const Preset& preset) const
{
	if (preset.pluginUuid.empty()) {
		return true;
	}
	const EffectManifest& manifest = host_.manifest();
	if (preset.pluginUuid == manifest.identity.uuid) {
		return true;
	}
	return std::find(manifest.priorUuids.begin(), manifest.priorUuids.end(), preset.pluginUuid) != manifest.priorUuids.end();
}

Error PresetManager::apply(const Preset& preset, const PresetApplyOptions& options, std::optional<Time> time)
{
	if (!options.ignoreCompatibility && !compatible(preset)) {
		return Error::make(ErrorCode::Unsupported, "The preset was made for another plug-in").with("presetPlugin", preset.pluginUuid);
	}
	ParameterService& params = host_.services().parameters();
	Error first;
	auto note = [&first](Error e) {
		if (e && first.ok()) {
			first = std::move(e);
		}
	};
	auto inBoundary = [&](ParameterId id, const ParameterSpec& spec) {
		if (options.ignoreTagBoundary || preset.tag.empty()) {
			return true;
		}
		const std::vector<std::string> tags = meta_ != nullptr && meta_->has(id) ? meta_->tags(id) : spec.tags;
		return std::find(tags.begin(), tags.end(), preset.tag) != tags.end();
	};
	for (const auto& [id, value] : preset.values) {
		const ParameterSpec* spec = params.spec(id);
		if (spec == nullptr || id == preset.createdBy || spec->type == ParameterType::Presets || !inBoundary(id, *spec)) {
			continue;
		}
		Value source = value;
		if (options.remap) {
			if (std::optional<Value> remapped = options.remap(*spec, value)) {
				source = std::move(*remapped);
			}
		}
		Value coerced;
		if (!coercePresetValue(*spec, source, coerced)) {
			continue;
		}
		note(time ? params.set(id, *time, coerced) : params.setCurrent(id, coerced));
	}
	if (meta_ != nullptr && !options.ignoreTags) {
		for (const auto& [id, tags] : preset.tags) {
			const ParameterSpec* spec = params.spec(id);
			if (spec == nullptr || spec->appFlags.has(AppFlag::PresetNoTags) || !inBoundary(id, *spec)) {
				continue;
			}
			meta_->setTags(id, tags);
		}
	}
	if (meta_ != nullptr && !options.ignoreMeta) {
		for (const auto& [id, entries] : preset.meta) {
			const ParameterSpec* spec = params.spec(id);
			if (spec == nullptr || spec->appFlags.has(AppFlag::PresetNoMeta) || !inBoundary(id, *spec)) {
				continue;
			}
			for (const auto& [key, value] : entries) {
				meta_->setMeta(id, key, value);
			}
		}
	}
	return first;
}

std::vector<Preset> PresetManager::manifestPresets(std::string_view tag) const
{
	std::vector<Preset> out;
	for (const ManifestPreset& preset : host_.manifest().presets) {
		if (preset.tag == tag) {
			out.push_back(resolveManifestPreset(host_.manifest(), preset));
		}
	}
	return out;
}

std::filesystem::path PresetManager::bundlePresetDirectory(std::string_view tag) const
{
	const std::filesystem::path bundle = bundleDirectoryFor(host_.services().identity().bundlePath);
	if (bundle.empty() || tag.empty()) {
		return {};
	}
	return bundle / "Contents" / "Resources" / "Presets" / std::string(tag);
}

std::filesystem::path PresetManager::userPresetDirectory(std::string_view tag) const
{
	if (tag.empty()) {
		return {};
	}
	return host_.services().paths().userSupportDirectory() / "Presets" / std::string(tag);
}

std::vector<Preset> PresetManager::bundlePresets(std::string_view tag) const
{
	return presetsInDirectory(bundlePresetDirectory(tag), tag, PresetSource::Bundle);
}

std::vector<Preset> PresetManager::pluginPresets(std::string_view tag) const
{
	std::vector<Preset> out = manifestPresets(tag);
	const std::vector<Preset> bundled = bundlePresets(tag);
	out.insert(out.end(), bundled.begin(), bundled.end());
	return out;
}

std::vector<Preset> PresetManager::userPresets(std::string_view tag) const
{
	return presetsInDirectory(userPresetDirectory(tag), tag, PresetSource::User);
}

std::vector<Preset> PresetManager::presetsForTag(std::string_view tag) const
{
	std::vector<Preset> out = pluginPresets(tag);
	const std::vector<Preset> user = userPresets(tag);
	out.insert(out.end(), user.begin(), user.end());
	return out;
}

std::optional<Preset> PresetManager::find(std::string_view tag, std::string_view name) const
{
	for (const Preset& preset : userPresets(tag)) {
		if (preset.name == name) {
			return preset;
		}
	}
	for (const Preset& preset : pluginPresets(tag)) {
		if (preset.name == name) {
			return preset;
		}
	}
	return std::nullopt;
}

std::vector<PresetMenuEntry> PresetManager::menuEntries(std::string_view tag) const
{
	return presetMenuEntries(userPresets(tag), pluginPresets(tag));
}

Error PresetManager::saveUserPreset(Preset& preset) const
{
	const std::filesystem::path dir = userPresetDirectory(preset.tag);
	if (dir.empty()) {
		return Error::make(ErrorCode::Value, "A user preset needs a tag").with("preset", preset.name);
	}
	const std::filesystem::path file = dir / Preset::fileNameFor(preset.name);
	if (Error e = preset.save(file)) {
		return e;
	}
	preset.path = file;
	preset.source = PresetSource::User;
	return Error::none();
}

// ---- PresetsExtension ---------------------------------------------------------------------------

std::vector<EventId> PresetsExtension::interests() const
{
	return {EventId::CreateInstance, EventId::InstanceChanged};
}

int PresetsExtension::priority(EventId id) const
{
	switch (id) {
	case EventId::CreateInstance:
		return kPriorityFrameworkReconstruct + 1;
	case EventId::InstanceChanged:
		return MetaExtension::kPriorityTargetPresets - 2;
	default:
		return defaultPriority();
	}
}

bool PresetsExtension::load(EffectHost& host, int index)
{
	if (!Extension::load(host, index)) {
		return false;
	}
	ParameterService& params = host.services().parameters();
	for (ParameterId id : params.ids()) {
		const ParameterSpec* spec = params.spec(id);
		if (spec == nullptr || spec->type != ParameterType::Presets) {
			continue;
		}
		controls_.push_back(id);
		tags_[id] = spec->tags.empty() ? std::string() : spec->tags.front();
		lastIndex_[id] = 0;
	}
	manager_ = std::make_unique<PresetManager>(host, meta_);
	return true;
}

void PresetsExtension::handle(Event& event)
{
	switch (event.id) {
	case EventId::CreateInstance:
		restoreSelections();
		break;
	case EventId::InstanceChanged:
		onChange(static_cast<const ChangeEvent&>(event));
		break;
	default:
		break;
	}
}

std::vector<PresetMenuEntry> PresetsExtension::entries(ParameterId control) const
{
	auto it = tags_.find(control);
	return it == tags_.end() ? std::vector<PresetMenuEntry>{} : manager_->menuEntries(it->second);
}

std::string PresetsExtension::tagOf(ParameterId control) const
{
	auto it = tags_.find(control);
	return it == tags_.end() ? std::string() : it->second;
}

std::string PresetsExtension::selectedName(ParameterId control) const
{
	if (meta_ == nullptr) {
		return {};
	}
	std::optional<Value> v = meta_->meta(control, MetaManager::kSelectedPresetKey);
	return v && v->is<std::string>() ? v->as<std::string>() : std::string();
}

void PresetsExtension::record(ParameterId control, std::string_view name)
{
	if (meta_ != nullptr) {
		meta_->setMeta(control, MetaManager::kSelectedPresetKey, Value(std::string(name)));
	}
}

void PresetsExtension::restoreSelections()
{
	ParameterService& params = host()->services().parameters();
	for (ParameterId control : controls_) {
		const std::string name = selectedName(control);
		Value current;
		if (!params.getCurrent(control, current).ok()) {
			continue;
		}
		lastIndex_[control] = current.toInt(0);
		if (name.empty()) {
			continue;
		}
		// The host persists a choice by index; the recorded name keeps the selection stable when
		// the menu grows or shrinks between sessions.
		const std::vector<PresetMenuEntry> list = entries(control);
		for (std::size_t i = 0; i < list.size(); ++i) {
			const bool match = (list[i].kind == PresetMenuEntry::Kind::User || list[i].kind == PresetMenuEntry::Kind::Plugin) ? list[i].name == name : (list[i].kind == PresetMenuEntry::Kind::Default && name == "Default");
			if (match) {
				if (static_cast<int>(i) != current.toInt(0) && params.canWrite()) {
					params.setCurrent(control, Value(static_cast<int>(i)));
				}
				lastIndex_[control] = static_cast<int>(i);
				break;
			}
		}
	}
}

void PresetsExtension::onChange(const ChangeEvent& change)
{
	if (change.reason == ChangeReason::PluginEdited || std::find(controls_.begin(), controls_.end(), change.parameter) == controls_.end()) {
		return;
	}
	Value current;
	if (!host()->services().parameters().getCurrent(change.parameter, current).ok()) {
		return;
	}
	select(change.parameter, current.toInt(0));
}

void PresetsExtension::select(ParameterId control, int index)
{
	HostServices& services = host()->services();
	ParameterService& params = services.parameters();
	const std::vector<PresetMenuEntry> list = entries(control);
	auto restore = [&]() {
		const int previous = lastIndex_[control];
		if (previous != index) {
			params.setCurrent(control, Value(previous));
		}
	};
	if (index < 0 || static_cast<std::size_t>(index) >= list.size()) {
		restore();
		return;
	}
	const PresetMenuEntry& entry = list[static_cast<std::size_t>(index)];
	const std::string tag = tagOf(control);
	switch (entry.kind) {
	case PresetMenuEntry::Kind::Default:
		record(control, "Default");
		lastIndex_[control] = index;
		return;
	case PresetMenuEntry::Kind::Separator:
		restore();
		return;
	case PresetMenuEntry::Kind::User:
	case PresetMenuEntry::Kind::Plugin: {
		std::optional<Preset> preset;
		const std::vector<Preset> candidates = entry.kind == PresetMenuEntry::Kind::User ? manager_->userPresets(tag) : manager_->pluginPresets(tag);
		for (const Preset& p : candidates) {
			if (p.name == entry.name) {
				preset = p;
				break;
			}
		}
		if (!preset) {
			services.log().warning("The preset " + entry.name + " is no longer available");
			restore();
			return;
		}
		Preset copy = *preset;
		copy.createdBy = control;
		if (Error e = manager_->apply(copy)) {
			services.log().warning("Preset " + entry.name + " did not apply cleanly: " + e.describe());
		}
		record(control, entry.name);
		lastIndex_[control] = index;
		return;
	}
	case PresetMenuEntry::Kind::Reveal: {
		const std::filesystem::path dir = manager_->userPresetDirectory(tag);
		std::error_code ec;
		std::filesystem::create_directories(dir, ec);
		if (!services.paths().reveal(dir)) {
			services.feedback().message(MessageKind::Message, "ofxgrip.presets.reveal", "User presets live in " + dir.string());
		}
		restore();
		return;
	}
	case PresetMenuEntry::Kind::Save: {
		Preset preset = manager_->capture("Preset " + Preset::nowIso8601(), tag);
		preset.createdBy = control;
		if (Error e = manager_->saveUserPreset(preset)) {
			services.feedback().message(MessageKind::Error, "ofxgrip.presets.save", "The preset could not be saved: " + e.message());
		} else if (services.profile().has(Quirk::NoDynamicChoices) || services.profile().has(Quirk::ChoiceOptionsFrozenAfterDescribe)) {
			services.feedback().message(MessageKind::Message, "ofxgrip.presets.save", "Saved " + preset.name + ". It is listed after the host restarts.");
		} else {
			params.setOptions(control, presetMenuOptions(entries(control)));
		}
		restore();
		return;
	}
	}
}

OFXGRIP_NAMESPACE_END
