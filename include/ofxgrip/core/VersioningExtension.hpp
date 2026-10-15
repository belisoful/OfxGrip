// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
/*!
	@file  VersioningExtension.hpp
	@brief The framework extension that records the plug-in version and manifest schema version
	       an instance was saved with, and runs migrations when the plug-in is newer.

	Introduced in OfxGrip 0.2.0. This is FxGrip's `checkVersion:` and `upgradeFromVersion:` over
	the persistence service: the stored version lives in the hidden state parameter. On creation
	the extension compares the stored schema version with the manifest's. An older one runs the
	manifest's migrations (renames) in order, then the effect's migration hook, and records the
	new version only after both succeed, so a failed migration runs again on the next open. A
	freshly created instance records the current version at once.
*/
#pragma once

#include <ofxgrip/core/Extension.hpp>

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

OFXGRIP_NAMESPACE_BEGIN

/*! The stored version record. */
struct StoredVersion {
	unsigned schemaVersion{0};
	Version pluginVersion;
	bool operator==(const StoredVersion&) const = default;
};

/*! The extension. */
class VersioningExtension final : public Extension {
public:
	/*! The persistence key the version record is stored under. */
	static constexpr std::string_view kPersistenceKey = "ofxgrip.version";
	/*! The migration hook: `(fromSchema, toSchema, fromPluginVersion)`. Runs after the manifest's
	    renames. An error leaves the stored version unchanged. */
	using MigrateHook = std::function<Error(unsigned fromSchema, unsigned toSchema, Version fromVersion)>;

	explicit VersioningExtension(MigrateHook hook = {}) : hook_(std::move(hook)) {}

	std::string name() const override { return "Versioning"; }
	std::vector<EventId> interests() const override;
	int priority(EventId id) const override;
	void handle(Event& event) override;

	/*! The version read from the document on creation, or none for a fresh instance. */
	const std::optional<StoredVersion>& stored() const noexcept { return stored_; }
	/*! Whether a migration ran on creation. */
	bool migrated() const noexcept { return migrated_; }
	/*! The error of a failed migration, or success. */
	const Error& migrationError() const noexcept { return migrationError_; }

	/*! Encodes and decodes the record. */
	static Bytes encode(const StoredVersion& version);
	static Error decode(const Bytes& bytes, StoredVersion& out);

private:
	void checkVersion();
	Error runMigrations(const StoredVersion& from, const StoredVersion& to);
	void record(const StoredVersion& version);

	MigrateHook hook_;
	std::optional<StoredVersion> stored_;
	bool migrated_{false};
	Error migrationError_;
};

OFXGRIP_NAMESPACE_END
