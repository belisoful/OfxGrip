// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
#include <ofxgrip/core/VersioningExtension.hpp>

#include <ofxgrip/core/Codec.hpp>

#include <algorithm>

OFXGRIP_NAMESPACE_BEGIN

namespace {
constexpr std::string_view kMagic = "OGVR";
constexpr std::uint16_t kCodecVersion = 1;
}  // namespace

std::vector<EventId> VersioningExtension::interests() const
{
	return {EventId::CreateInstance};
}

int VersioningExtension::priority(EventId id) const
{
	// Before the meta store loads, so a rename lands before anything reads the renamed parameter.
	return id == EventId::CreateInstance ? kPriorityFrameworkCapture - 1 : defaultPriority();
}

Bytes VersioningExtension::encode(const StoredVersion& version)
{
	Bytes out;
	ByteWriter w(out);
	w.raw(kMagic);
	w.u16(kCodecVersion);
	w.u32(version.schemaVersion);
	w.u32(version.pluginVersion.major);
	w.u32(version.pluginVersion.minor);
	w.u32(version.pluginVersion.patch);
	return out;
}

Error VersioningExtension::decode(const Bytes& bytes, StoredVersion& out)
{
	ByteReader r(bytes);
	std::uint16_t codec = 0;
	StoredVersion v;
	if (!r.magic(kMagic) || !r.u16(codec) || !r.u32(v.schemaVersion) || !r.u32(v.pluginVersion.major) || !r.u32(v.pluginVersion.minor) || !r.u32(v.pluginVersion.patch)) {
		return Error::make(ErrorCode::Codec, "The stored version record is unreadable");
	}
	if (codec > kCodecVersion) {
		return Error::make(ErrorCode::Codec, "The stored version record is from a newer OfxGrip");
	}
	out = v;
	return Error::none();
}

void VersioningExtension::handle(Event& event)
{
	if (event.id == EventId::CreateInstance) {
		checkVersion();
	}
}

void VersioningExtension::checkVersion()
{
	HostServices& services = host()->services();
	const EffectManifest& manifest = host()->manifest();
	const StoredVersion current{manifest.schemaVersion, services.identity().pluginVersion};
	PersistenceService& persistence = services.persistence();
	Bytes bytes;
	if (persistence.load(kPersistenceKey, bytes).ok()) {
		StoredVersion stored;
		if (Error e = decode(bytes, stored)) {
			services.log().warning("The stored version is unreadable; treating the instance as schema 0: " + e.describe());
			stored = StoredVersion{};
		}
		stored_ = stored;
	} else if (!persistence.keys().empty()) {
		// Other state exists but no version record: a document saved before versioning existed.
		stored_ = StoredVersion{};
	}
	if (!stored_) {
		record(current);
		return;
	}
	if (*stored_ == current) {
		return;
	}
	if (stored_->schemaVersion > current.schemaVersion) {
		services.log().warning("The document was saved by a newer schema (" + std::to_string(stored_->schemaVersion) + " > " + std::to_string(current.schemaVersion) + ")");
		return;
	}
	migrationError_ = runMigrations(*stored_, current);
	if (migrationError_) {
		services.log().error("Migration failed; the stored version stays at schema " + std::to_string(stored_->schemaVersion) + ": " + migrationError_.describe());
		return;
	}
	migrated_ = true;
	record(current);
}

Error VersioningExtension::runMigrations(const StoredVersion& from, const StoredVersion& to)
{
	HostServices& services = host()->services();
	ParameterService& params = services.parameters();
	std::vector<const Migration*> steps;
	for (const Migration& migration : host()->manifest().migrations) {
		if (migration.fromSchemaVersion >= from.schemaVersion && migration.fromSchemaVersion < to.schemaVersion) {
			steps.push_back(&migration);
		}
	}
	std::sort(steps.begin(), steps.end(), [](const Migration* a, const Migration* b) { return a->fromSchemaVersion < b->fromSchemaVersion; });
	for (const Migration* step : steps) {
		for (const auto& [oldKey, newKey] : step->renames) {
			const ParameterId oldId = params.idForKey(oldKey);
			const ParameterId newId = params.idForKey(newKey);
			if (oldId == kParameterIdNone || newId == kParameterIdNone) {
				services.log().warning("Rename " + oldKey + " → " + newKey + " skipped: a side is not declared on this host");
				continue;
			}
			Value value;
			if (Error e = params.getCurrent(oldId, value)) {
				return e.with("migration", oldKey + " → " + newKey);
			}
			if (Error e = params.setCurrent(newId, value)) {
				return e.with("migration", oldKey + " → " + newKey);
			}
		}
	}
	if (hook_) {
		return hook_(from.schemaVersion, to.schemaVersion, from.pluginVersion);
	}
	return Error::none();
}

void VersioningExtension::record(const StoredVersion& version)
{
	host()->services().persistence().store(kPersistenceKey, encode(version));
}

OFXGRIP_NAMESPACE_END
