// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
/*!
	@file  Licensing.hpp
	@brief The store-neutral licensing seam: the license status and entitlement, the provider
	       interface a store adapter implements, the provider registry, the manifest's licensing
	       declaration, and the extension that drives an instance's licensing.

	Introduced in OfxGrip 0.4.0. This is FxGrip's `FxGripLicensing`, `FxGripLicensingProvider`,
	and `FxGripLicenseEntitlement` in C++ (`Research/Comparison.md` §3.14). The extension owns
	one provider, caches the status so a render never waits on a store, puts the license state
	in every render snapshot, watermarks a frame by status after the render, and presents the
	store's actions through one momentary menu. Providers register by name; a plug-in selects
	one in its manifest's `licensing` declaration. Store-specific providers are a separate
	decision; the built-ins are `static` (a status set in code), `trial` (a timed trial in the
	support folder), and `file` (a signed license file).
*/
#pragma once

#include <ofxgrip/core/Error.hpp>
#include <ofxgrip/core/Extension.hpp>
#include <ofxgrip/core/Manifest.hpp>
#include <ofxgrip/core/RenderSnapshot.hpp>
#include <ofxgrip/core/Types.hpp>
#include <ofxgrip/core/Value.hpp>
#include <ofxgrip/core/Watermark.hpp>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

OFXGRIP_NAMESPACE_BEGIN

/*! The license status of one product. The first four values equal FxFactory's statuses. */
enum class LicenseStatus : std::uint8_t {
	Unknown = 0,       /*!< The provider is absent or has not answered. */
	InvalidProduct,    /*!< The product identifier is not one the provider knows. */
	Unlicensed,
	Licensed,
	Trial,             /*!< Licensed for a limited period. */
	Expired,           /*!< A trial or subscription has lapsed. */
	OfflineGrace,      /*!< The last validation is stale, and the offline grace period runs. */
	Revoked,           /*!< A refund, a chargeback, or a disabled key. */
};

/*! Whether a status unlocks the plug-in: Licensed, Trial, and OfflineGrace. */
constexpr bool licenseStatusUnlocks(LicenseStatus status) noexcept
{
	return status == LicenseStatus::Licensed || status == LicenseStatus::Trial || status == LicenseStatus::OfflineGrace;
}
/*! The snapshot's license state for a status. */
LicenseState licenseStateFor(LicenseStatus status) noexcept;
std::string_view toString(LicenseStatus status) noexcept;
bool parseLicenseStatus(std::string_view text, LicenseStatus& out) noexcept;

/*! The purchase model behind an entitlement. */
enum class LicenseKind : std::uint8_t {
	Perpetual = 0,
	Subscription,
	Trial,
};
std::string_view toString(LicenseKind kind) noexcept;
bool parseLicenseKind(std::string_view text, LicenseKind& out) noexcept;

/*! The value behind a status: who holds it, for what, and until when. Times are seconds since
    the Unix epoch; 0 means none. */
struct LicenseEntitlement {
	std::string productId;
	std::string providerName;
	LicenseStatus status{LicenseStatus::Unknown};
	LicenseKind kind{LicenseKind::Perpetual};
	std::string email;
	std::int64_t issuedAt{0};
	std::int64_t expiresAt{0};
	std::int64_t updatesUntil{0};   /*!< The last release date a perpetual license covers. */
	bool willRenew{false};
	bool inBillingRetry{false};
	std::string activationId;
	std::string machineId;
	bool operator==(const LicenseEntitlement&) const = default;

	bool isLicensed() const noexcept { return licenseStatusUnlocks(status); }
	/*! The byte form, as the render snapshot carries it. */
	Bytes encode() const;
	static Error decode(std::span<const std::uint8_t> bytes, LicenseEntitlement& out);
	/*! The JSON form, as a license file carries it: the fields by their names, dates as ISO
	    8601 strings. */
	std::string toJson() const;
	static Error fromJson(std::string_view json, LicenseEntitlement& out);
};

/*! The store's product information. */
struct LicenseProductInfo {
	std::string productName;
	std::string latestVersion;
	std::string requiredOsVersion;
	std::string requiredStoreVersion;
	bool discontinued{false};
	double priceUsd{0};
};

/*! What an update check reports. */
struct LicenseUpdateInfo {
	bool newVersionFound{false};
	bool postponed{false};
	Error error;
	std::optional<LicenseProductInfo> product;
};

/*! The optional capabilities a provider implements. The extension adds a menu row only for the
    ones present. */
enum class LicensingCapability : std::uint32_t {
	Entitlement = 1u << 0,
	VersionGating = 1u << 1,
	ProductInfo = 1u << 2,
	Buy = 1u << 3,
	ShowProduct = 1u << 4,
	UpdateChecking = 1u << 5,
	Support = 1u << 6,
	LicenseEntry = 1u << 7,
	Activation = 1u << 8,
	Deactivation = 1u << 9,
};
constexpr std::uint32_t operator|(LicensingCapability a, LicensingCapability b) noexcept { return static_cast<std::uint32_t>(a) | static_cast<std::uint32_t>(b); }
constexpr std::uint32_t operator|(std::uint32_t a, LicensingCapability b) noexcept { return a | static_cast<std::uint32_t>(b); }

using LicenseStatusHandler = std::function<void(LicenseStatus)>;
using LicenseCompletion = std::function<void(LicenseStatus, const Error&)>;
using LicenseProductInfoHandler = std::function<void(const std::optional<LicenseProductInfo>&, const Error&)>;
using LicenseUpdateInfoHandler = std::function<void(const LicenseUpdateInfo&)>;

/*! The store-specific half. The required methods never touch the network: `status` answers from
    the provider's cache, because the render path reads it. Every other method is a capability
    the provider declares in `capabilities()`. */
class LicensingProvider {
public:
	virtual ~LicensingProvider() = default;
	virtual std::string name() const = 0;
	/*! Whether the store runtime is reachable from this process. */
	virtual bool available() const = 0;
	virtual LicenseStatus status(std::string_view productId) = 0;
	/*! Starts reporting status changes for a product. A second start for the same product
	    replaces the first. The handler may run on any thread. */
	virtual void startObserving(std::string_view productId, LicenseStatusHandler handler) = 0;
	virtual void stopObserving(std::string_view productId) = 0;
	virtual std::uint32_t capabilities() const { return 0; }
	bool has(LicensingCapability capability) const noexcept { return (capabilities() & static_cast<std::uint32_t>(capability)) != 0; }

	virtual std::optional<LicenseEntitlement> entitlement(std::string_view productId) { (void)productId; return std::nullopt; }
	/*! Licensed means a license exists for this major version or a later one. */
	virtual LicenseStatus statusForVersion(std::string_view productId, unsigned major) { (void)major; return status(productId); }
	virtual bool upgradeAvailable(std::string_view productId) { (void)productId; return false; }
	virtual void fetchProductInfo(std::string_view productId, LicenseProductInfoHandler handler) { (void)productId; handler(std::nullopt, Error::make(ErrorCode::Unsupported, "The provider fetches no product information")); }
	virtual bool buy(std::string_view productId) { (void)productId; return false; }
	virtual bool showProduct(std::string_view productId) { (void)productId; return false; }
	virtual bool updateCheckingEnabled(std::string_view productId) { (void)productId; return false; }
	virtual void setUpdateCheckingEnabled(std::string_view productId, bool enabled) { (void)productId; (void)enabled; }
	virtual void checkForUpdates(std::string_view productId, std::string_view version, bool force, LicenseUpdateInfoHandler handler)
	{
		(void)productId;
		(void)version;
		(void)force;
		LicenseUpdateInfo info;
		info.error = Error::make(ErrorCode::Unsupported, "The provider checks for no updates");
		handler(info);
	}
	virtual bool showSupportForm(std::string_view subject, std::string_view message) { (void)subject; (void)message; return false; }
	virtual bool showLicenseEntry(std::string_view productId) { (void)productId; return false; }
	virtual void activateLicenseKey(std::string_view key, std::string_view productId, LicenseCompletion completion) { (void)key; completion(status(productId), Error::make(ErrorCode::Unsupported, "The provider activates no keys")); }
	virtual void validateLicense(std::string_view productId, LicenseCompletion completion) { completion(status(productId), Error::none()); }
	virtual void deactivateLicense(std::string_view productId, LicenseCompletion completion) { completion(status(productId), Error::make(ErrorCode::Unsupported, "The provider deactivates nothing")); }
	/*! The bundle configuration problems that would keep the store from working. */
	virtual std::vector<std::string> hostConfigurationProblems(const std::filesystem::path& bundle) { (void)bundle; return {}; }
};

/*! The registry of providers by name. */
class LicensingProviders {
public:
	using Factory = std::function<std::shared_ptr<LicensingProvider>(const LicensingSettings& settings)>;
	/*! Registers a factory under a name, replacing an earlier one. Names compare case-insensitively. */
	static void registerProvider(std::string_view name, Factory factory);
	/*! Creates the provider registered under a name, or null. The built-in providers register
	    on first use. */
	static std::shared_ptr<LicensingProvider> create(std::string_view name, const LicensingSettings& settings = {});
	static std::vector<std::string> names();
};

/*! The rows of the licensing menu, by stable key. */
inline constexpr std::string_view kLicensingMenuKeyStatus = "status";
inline constexpr std::string_view kLicensingMenuKeyBuy = "buy";
inline constexpr std::string_view kLicensingMenuKeyShowProduct = "showProduct";
inline constexpr std::string_view kLicensingMenuKeyEnterLicense = "enterLicense";
inline constexpr std::string_view kLicensingMenuKeyDeactivate = "deactivate";
inline constexpr std::string_view kLicensingMenuKeyCheckUpdates = "checkUpdates";
inline constexpr std::string_view kLicensingMenuKeyRefresh = "refresh";

/*! The hidden toggle that mirrors whether the product is licensed. Carries no state. */
ParameterSpec licenseStatusParameter(ParameterId id, std::string key);
/*! The momentary menu of the store's actions the provider implements. Row 0 is the title. */
ParameterSpec licensingMenuParameter(const LicensingSpec& spec, const LicensingProvider* provider, ParameterId id, std::string key);

/*! Parses an ISO 8601 date or date-time (`2026-10-11`, `2026-10-11T12:00:00Z`) into Unix
    seconds. Returns false for anything else. */
bool parseIso8601(std::string_view text, std::int64_t& out) noexcept;
/*! Renders Unix seconds as an ISO 8601 date-time in UTC. */
std::string formatIso8601(std::int64_t seconds);

/*! The extension. Loaded by the effect when the manifest declares a provider. */
class LicensingExtension final : public Extension {
public:
	/*! The persistence key of the cached entitlement. */
	static constexpr std::string_view kPersistenceKey = "ofxgrip.license";
	/*! The snapshot extra that carries the entitlement. */
	static constexpr std::string_view kSnapshotKey = "ofxgrip.license";

	/*! `debugMenuParameter` names the debug menu whose licensing rows force a status, or
	    `kParameterIdNone` when a release build offers none. */
	LicensingExtension(std::shared_ptr<LicensingProvider> provider, LicensingSpec spec, ParameterId statusParameter, ParameterId menuParameter, ParameterId debugMenuParameter = kParameterIdNone);
	std::string name() const override { return "Licensing"; }
	std::vector<EventId> interests() const override;
	int priority(EventId id) const override;
	bool load(EffectHost& host, int index) override;
	void handle(Event& event) override;

	const LicensingSpec& spec() const noexcept { return spec_; }
	LicensingProvider* provider() const noexcept { return provider_.get(); }
	const std::string& productId() const noexcept { return productId_; }
	const std::string& productVersion() const noexcept { return productVersion_; }
	/*! The cached status, after the version gate and the update window. */
	LicenseStatus status() const;
	bool isLicensed() const { return licenseStatusUnlocks(status()); }
	/*! Whether the holder is licensed for an earlier version only, or the update window closed
	    before this release. */
	bool upgradeAvailable() const;
	std::optional<LicenseEntitlement> entitlement() const;
	/*! Re-reads the provider's status and applies it. */
	void refresh();
	/*! The watermark a frame rendered under a status carries, or none. */
	std::optional<WatermarkConfiguration> watermarkFor(LicenseStatus status) const;
	/*! The watermark for the state a render snapshot carries: none when licensed, the trial mark
	    for a trial, the unlicensed mark otherwise. */
	std::optional<WatermarkConfiguration> watermarkFor(LicenseState state) const;
	/*! Forces a status, for debug builds. `Unknown` clears the override. */
	void setDebugStatus(LicenseStatus status);

	/*! The store's actions. Each returns whether the provider took it. */
	bool buy();
	bool showProduct();
	bool showLicenseEntry();
	void deactivate();
	void checkForUpdates(bool force = false);
	/*! Runs the action of a menu row by key. */
	void activate(std::string_view rowKey);

	/*! The status changes seen so far, for tests. */
	int statusChanges() const noexcept { return changes_; }

private:
	void applyStatus(LicenseStatus raw, bool fromProvider);
	LicenseStatus gatedStatus(LicenseStatus raw) const;
	void writeToggle();

	std::shared_ptr<LicensingProvider> provider_;
	LicensingSpec spec_;
	ParameterId statusParameter_;
	ParameterId menuParameter_;
	ParameterId debugMenuParameter_;
	std::string productId_;
	std::string productVersion_;
	unsigned licensedMajor_{0};
	std::int64_t releaseDate_{0};
	mutable std::mutex mutex_;
	LicenseStatus raw_{LicenseStatus::Unknown};
	LicenseStatus status_{LicenseStatus::Unknown};
	std::optional<LicenseStatus> debugOverride_;
	std::optional<LicenseEntitlement> entitlement_;
	bool upgrade_{false};
	int changes_{0};
	bool observing_{false};
};

OFXGRIP_NAMESPACE_END
