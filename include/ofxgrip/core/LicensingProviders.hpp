// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
/*!
	@file  LicensingProviders.hpp
	@brief The built-in licensing providers: a status set in code, a timed trial kept in the
	       support folder, and a signed license file.

	Introduced in OfxGrip 0.4.0. None of them talks to a store; they are the seam's reference
	implementations and the fallback for a product sold without a store runtime. A store's
	provider (FxFactory, the App Store, a key server) registers beside them under its own name.
*/
#pragma once

#include <ofxgrip/core/Licensing.hpp>

#include <filesystem>
#include <map>
#include <mutex>
#include <string>

OFXGRIP_NAMESPACE_BEGIN

/*! `static`: the status the settings name (`status`, default `licensed`), with an optional
    entitlement. For tests, previews, and builds that ship unlocked. */
class StaticLicensingProvider final : public LicensingProvider {
public:
	explicit StaticLicensingProvider(const LicensingSettings& settings = {});
	std::string name() const override { return "static"; }
	bool available() const override { return true; }
	LicenseStatus status(std::string_view productId) override;
	LicenseStatus statusForVersion(std::string_view productId, unsigned major) override;
	bool upgradeAvailable(std::string_view productId) override;
	void startObserving(std::string_view productId, LicenseStatusHandler handler) override;
	void stopObserving(std::string_view productId) override;
	std::uint32_t capabilities() const override;
	std::optional<LicenseEntitlement> entitlement(std::string_view productId) override;
	bool buy(std::string_view productId) override;
	bool showProduct(std::string_view productId) override;
	bool showLicenseEntry(std::string_view productId) override;
	void deactivateLicense(std::string_view productId, LicenseCompletion completion) override;
	void checkForUpdates(std::string_view productId, std::string_view version, bool force, LicenseUpdateInfoHandler handler) override;

	/*! Changes the status and reports it to the observers. */
	void setStatus(LicenseStatus status);
	void setEntitlement(std::optional<LicenseEntitlement> entitlement);
	/*! The licensed major version; 0 licenses every version. */
	unsigned licensedMajor{0};
	int buys{0};
	int shows{0};
	int entries{0};
	int deactivations{0};
	int updateChecks{0};

private:
	std::mutex mutex_;
	LicenseStatus status_{LicenseStatus::Licensed};
	std::optional<LicenseEntitlement> entitlement_;
	std::map<std::string, LicenseStatusHandler> observers_;
};

/*! `trial`: Trial for `days` (default 14) from the first run, then Expired. The first-run date
    lives in `<directory>/trial-<productId>.json`; the directory comes from the settings
    (`directory`) or, when empty, from the effect's user support folder set by
    `setDirectory`. A `now` setting (ISO 8601) fixes the clock, for tests. */
class TrialLicensingProvider final : public LicensingProvider {
public:
	explicit TrialLicensingProvider(const LicensingSettings& settings = {});
	std::string name() const override { return "trial"; }
	bool available() const override { return true; }
	LicenseStatus status(std::string_view productId) override;
	void startObserving(std::string_view productId, LicenseStatusHandler handler) override;
	void stopObserving(std::string_view productId) override;
	std::uint32_t capabilities() const override { return static_cast<std::uint32_t>(LicensingCapability::Entitlement); }
	std::optional<LicenseEntitlement> entitlement(std::string_view productId) override;
	void setDirectory(std::filesystem::path directory) { directory_ = std::move(directory); }
	const std::filesystem::path& directory() const noexcept { return directory_; }
	/*! The trial's remaining days for a product; negative after expiry. */
	double remainingDays(std::string_view productId);

private:
	std::int64_t now() const;
	std::int64_t firstRun(std::string_view productId);
	std::filesystem::path directory_;
	int days_{14};
	std::int64_t fixedNow_{0};
	std::mutex mutex_;
	std::map<std::string, std::int64_t> starts_;
};

/*! `file`: a license file `<directory>/<productId>.license` holding an entitlement's JSON plus
    a `signature` field: the HMAC-SHA256 of the canonical fields under the `secret` setting,
    which the vendor embeds in the plug-in. A missing file reads Unlicensed; a bad signature,
    product, or kind reads Revoked; a lapsed `expiresAt` reads Expired. The `machineId` field,
    when present, must equal the `machineId` setting. `showLicenseEntry` reveals the folder. */
class FileLicensingProvider final : public LicensingProvider {
public:
	explicit FileLicensingProvider(const LicensingSettings& settings = {});
	std::string name() const override { return "file"; }
	bool available() const override { return true; }
	LicenseStatus status(std::string_view productId) override;
	LicenseStatus statusForVersion(std::string_view productId, unsigned major) override;
	bool upgradeAvailable(std::string_view productId) override;
	void startObserving(std::string_view productId, LicenseStatusHandler handler) override;
	void stopObserving(std::string_view productId) override;
	std::uint32_t capabilities() const override;
	std::optional<LicenseEntitlement> entitlement(std::string_view productId) override;
	void activateLicenseKey(std::string_view key, std::string_view productId, LicenseCompletion completion) override;
	void deactivateLicense(std::string_view productId, LicenseCompletion completion) override;
	void setDirectory(std::filesystem::path directory) { directory_ = std::move(directory); }
	const std::filesystem::path& directory() const noexcept { return directory_; }
	/*! The path a product's license file lives at. */
	std::filesystem::path licensePath(std::string_view productId) const;
	/*! Re-reads the file and reports a changed status to the observer. */
	void reload(std::string_view productId);

	/*! The canonical text a license's signature covers: the entitlement's JSON fields in a
	    fixed order, without the signature. */
	static std::string canonicalText(const LicenseEntitlement& entitlement, unsigned licensedMajor);
	/*! Writes a signed license file for an entitlement. The vendor's tooling calls this. */
	static Error writeLicenseFile(const std::filesystem::path& path, const LicenseEntitlement& entitlement, unsigned licensedMajor, std::string_view secret);

private:
	struct Record {
		LicenseEntitlement entitlement;
		unsigned licensedMajor{0};
		LicenseStatus status{LicenseStatus::Unlicensed};
	};
	Record read(std::string_view productId);
	std::int64_t now() const;
	std::filesystem::path directory_;
	std::string secret_;
	std::string machineId_;
	std::int64_t fixedNow_{0};
	std::mutex mutex_;
	std::map<std::string, LicenseStatusHandler> observers_;
	std::map<std::string, LicenseStatus> lastStatus_;
};

/*! Registers the three built-in providers. `LicensingProviders::create` calls it once. */
void registerBuiltInLicensingProviders();

OFXGRIP_NAMESPACE_END
