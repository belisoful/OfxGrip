// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
#include <ofxgrip/core/Licensing.hpp>

#include <ofxgrip/core/Codec.hpp>
#include <ofxgrip/core/Debug.hpp>
#include <ofxgrip/core/LicensingProviders.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <ctime>

OFXGRIP_NAMESPACE_BEGIN

namespace {

using Json = nlohmann::json;

constexpr std::string_view kEntitlementMagic = "OGLE";
constexpr std::uint16_t kEntitlementVersion = 1;

std::string lower(std::string_view text)
{
	std::string out(text);
	for (char& c : out) {
		c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	}
	return out;
}

std::int64_t daysFromCivil(std::int64_t y, unsigned m, unsigned d) noexcept
{
	// Howard Hinnant's algorithm.
	y -= m <= 2 ? 1 : 0;
	const std::int64_t era = (y >= 0 ? y : y - 399) / 400;
	const std::int64_t yoe = y - era * 400;
	const std::int64_t doy = (153 * (static_cast<std::int64_t>(m) + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
	const std::int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
	return era * 146097 + doe - 719468;
}

void civilFromDays(std::int64_t z, std::int64_t& y, unsigned& m, unsigned& d) noexcept
{
	z += 719468;
	const std::int64_t era = (z >= 0 ? z : z - 146096) / 146097;
	const std::int64_t doe = z - era * 146097;
	const std::int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
	y = yoe + era * 400;
	const std::int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
	const std::int64_t mp = (5 * doy + 2) / 153;
	d = static_cast<unsigned>(doy - (153 * mp + 2) / 5 + 1);
	m = static_cast<unsigned>(mp < 10 ? mp + 3 : mp - 9);
	y += m <= 2 ? 1 : 0;
}

bool digits(std::string_view text, std::size_t from, std::size_t count, std::int64_t& out) noexcept
{
	if (from + count > text.size()) {
		return false;
	}
	out = 0;
	for (std::size_t i = from; i < from + count; ++i) {
		if (text[i] < '0' || text[i] > '9') {
			return false;
		}
		out = out * 10 + (text[i] - '0');
	}
	return true;
}

}  // namespace

bool parseIso8601(std::string_view text, std::int64_t& out) noexcept
{
	std::int64_t year = 0;
	std::int64_t month = 0;
	std::int64_t day = 0;
	if (!digits(text, 0, 4, year) || text.size() < 10 || text[4] != '-' || !digits(text, 5, 2, month) || text[7] != '-' || !digits(text, 8, 2, day)) {
		return false;
	}
	if (month < 1 || month > 12 || day < 1 || day > 31) {
		return false;
	}
	std::int64_t hour = 0;
	std::int64_t minute = 0;
	std::int64_t second = 0;
	if (text.size() > 10) {
		if (text[10] != 'T' && text[10] != ' ') {
			return false;
		}
		if (!digits(text, 11, 2, hour) || text.size() < 16 || text[13] != ':' || !digits(text, 14, 2, minute)) {
			return false;
		}
		if (text.size() >= 19 && text[16] == ':') {
			if (!digits(text, 17, 2, second)) {
				return false;
			}
		}
	}
	out = daysFromCivil(year, static_cast<unsigned>(month), static_cast<unsigned>(day)) * 86400 + hour * 3600 + minute * 60 + second;
	return true;
}

std::string formatIso8601(std::int64_t seconds)
{
	const std::int64_t days = seconds >= 0 ? seconds / 86400 : (seconds - 86399) / 86400;
	const std::int64_t rest = seconds - days * 86400;
	std::int64_t y = 0;
	unsigned m = 0;
	unsigned d = 0;
	civilFromDays(days, y, m, d);
	char buffer[32];
	std::snprintf(buffer, sizeof buffer, "%04lld-%02u-%02uT%02lld:%02lld:%02lldZ", static_cast<long long>(y), m, d, static_cast<long long>(rest / 3600), static_cast<long long>(rest % 3600 / 60), static_cast<long long>(rest % 60));
	return buffer;
}

LicenseState licenseStateFor(LicenseStatus status) noexcept
{
	switch (status) {
	case LicenseStatus::Licensed:
	case LicenseStatus::OfflineGrace:
		return LicenseState::Licensed;
	case LicenseStatus::Trial:
		return LicenseState::Trial;
	case LicenseStatus::Unknown:
		return LicenseState::Unknown;
	default:
		return LicenseState::Unlicensed;
	}
}

std::string_view toString(LicenseStatus status) noexcept
{
	switch (status) {
	case LicenseStatus::Unknown: return "unknown";
	case LicenseStatus::InvalidProduct: return "invalidProduct";
	case LicenseStatus::Unlicensed: return "unlicensed";
	case LicenseStatus::Licensed: return "licensed";
	case LicenseStatus::Trial: return "trial";
	case LicenseStatus::Expired: return "expired";
	case LicenseStatus::OfflineGrace: return "offlineGrace";
	case LicenseStatus::Revoked: return "revoked";
	}
	return "unknown";
}

bool parseLicenseStatus(std::string_view text, LicenseStatus& out) noexcept
{
	for (LicenseStatus s : {LicenseStatus::Unknown, LicenseStatus::InvalidProduct, LicenseStatus::Unlicensed, LicenseStatus::Licensed, LicenseStatus::Trial, LicenseStatus::Expired, LicenseStatus::OfflineGrace, LicenseStatus::Revoked}) {
		if (toString(s) == text) {
			out = s;
			return true;
		}
	}
	return false;
}

std::string_view toString(LicenseKind kind) noexcept
{
	switch (kind) {
	case LicenseKind::Perpetual: return "perpetual";
	case LicenseKind::Subscription: return "subscription";
	case LicenseKind::Trial: return "trial";
	}
	return "perpetual";
}

bool parseLicenseKind(std::string_view text, LicenseKind& out) noexcept
{
	for (LicenseKind k : {LicenseKind::Perpetual, LicenseKind::Subscription, LicenseKind::Trial}) {
		if (toString(k) == text) {
			out = k;
			return true;
		}
	}
	return false;
}

// ---- entitlement --------------------------------------------------------------------------------

Bytes LicenseEntitlement::encode() const
{
	Bytes out;
	ByteWriter w(out);
	w.raw(kEntitlementMagic);
	w.u16(kEntitlementVersion);
	w.str(productId);
	w.str(providerName);
	w.u8(static_cast<std::uint8_t>(status));
	w.u8(static_cast<std::uint8_t>(kind));
	w.str(email);
	w.f64(static_cast<double>(issuedAt));
	w.f64(static_cast<double>(expiresAt));
	w.f64(static_cast<double>(updatesUntil));
	w.u8(willRenew ? 1 : 0);
	w.u8(inBillingRetry ? 1 : 0);
	w.str(activationId);
	w.str(machineId);
	return out;
}

Error LicenseEntitlement::decode(std::span<const std::uint8_t> bytes, LicenseEntitlement& out)
{
	ByteReader r(bytes);
	std::uint16_t version = 0;
	LicenseEntitlement e;
	std::uint8_t status = 0;
	std::uint8_t kind = 0;
	std::uint8_t renew = 0;
	std::uint8_t retry = 0;
	double issued = 0;
	double expires = 0;
	double updates = 0;
	if (!r.magic(kEntitlementMagic) || !r.u16(version) || version != kEntitlementVersion || !r.str(e.productId) || !r.str(e.providerName) || !r.u8(status) || !r.u8(kind) || !r.str(e.email) || !r.f64(issued) || !r.f64(expires) || !r.f64(updates)
	    || !r.u8(renew) || !r.u8(retry) || !r.str(e.activationId) || !r.str(e.machineId)) {
		return Error::make(ErrorCode::Codec, "The entitlement bytes are unreadable");
	}
	if (status > static_cast<std::uint8_t>(LicenseStatus::Revoked) || kind > static_cast<std::uint8_t>(LicenseKind::Trial)) {
		return Error::make(ErrorCode::Codec, "The entitlement carries an unknown status or kind");
	}
	e.status = static_cast<LicenseStatus>(status);
	e.kind = static_cast<LicenseKind>(kind);
	e.issuedAt = static_cast<std::int64_t>(issued);
	e.expiresAt = static_cast<std::int64_t>(expires);
	e.updatesUntil = static_cast<std::int64_t>(updates);
	e.willRenew = renew != 0;
	e.inBillingRetry = retry != 0;
	out = std::move(e);
	return Error::none();
}

std::string LicenseEntitlement::toJson() const
{
	Json j;
	j["productId"] = productId;
	j["providerName"] = providerName;
	j["status"] = std::string(toString(status));
	j["kind"] = std::string(toString(kind));
	if (!email.empty()) {
		j["email"] = email;
	}
	if (issuedAt != 0) {
		j["issuedAt"] = formatIso8601(issuedAt);
	}
	if (expiresAt != 0) {
		j["expiresAt"] = formatIso8601(expiresAt);
	}
	if (updatesUntil != 0) {
		j["updatesUntil"] = formatIso8601(updatesUntil);
	}
	j["willRenew"] = willRenew;
	j["inBillingRetry"] = inBillingRetry;
	if (!activationId.empty()) {
		j["activationId"] = activationId;
	}
	if (!machineId.empty()) {
		j["machineId"] = machineId;
	}
	return j.dump(2);
}

Error LicenseEntitlement::fromJson(std::string_view json, LicenseEntitlement& out)
{
	Json j = Json::parse(json, nullptr, false, true);
	if (j.is_discarded() || !j.is_object()) {
		return Error::make(ErrorCode::Codec, "The entitlement JSON is unreadable");
	}
	LicenseEntitlement e;
	e.productId = j.value("productId", "");
	e.providerName = j.value("providerName", "");
	if (e.productId.empty() || e.providerName.empty()) {
		return Error::make(ErrorCode::Codec, "The entitlement names no product or provider");
	}
	if (!parseLicenseStatus(j.value("status", "unknown"), e.status)) {
		return Error::make(ErrorCode::Codec, "The entitlement carries an unknown status");
	}
	if (!parseLicenseKind(j.value("kind", "perpetual"), e.kind)) {
		return Error::make(ErrorCode::Codec, "The entitlement carries an unknown kind");
	}
	e.email = j.value("email", "");
	for (const auto& [name, target] : {std::pair<const char*, std::int64_t*>{"issuedAt", &e.issuedAt}, {"expiresAt", &e.expiresAt}, {"updatesUntil", &e.updatesUntil}}) {
		if (!j.contains(name)) {
			continue;
		}
		if (j[name].is_number()) {
			*target = static_cast<std::int64_t>(j[name].get<double>());
		} else if (!j[name].is_string() || !parseIso8601(j[name].get<std::string>(), *target)) {
			return Error::make(ErrorCode::Codec, "The entitlement carries an unreadable date").with("field", name);
		}
	}
	e.willRenew = j.value("willRenew", false);
	e.inBillingRetry = j.value("inBillingRetry", false);
	e.activationId = j.value("activationId", "");
	e.machineId = j.value("machineId", "");
	out = std::move(e);
	return Error::none();
}

// ---- registry -----------------------------------------------------------------------------------

namespace {

std::mutex& registryMutex()
{
	static std::mutex m;
	return m;
}

std::map<std::string, LicensingProviders::Factory>& registry()
{
	static std::map<std::string, LicensingProviders::Factory> table;
	return table;
}

void ensureBuiltIns()
{
	static std::once_flag once;
	std::call_once(once, []() { registerBuiltInLicensingProviders(); });
}

}  // namespace

void LicensingProviders::registerProvider(std::string_view name, Factory factory)
{
	std::lock_guard<std::mutex> lock(registryMutex());
	registry()[lower(name)] = std::move(factory);
}

std::shared_ptr<LicensingProvider> LicensingProviders::create(std::string_view name, const LicensingSettings& settings)
{
	ensureBuiltIns();
	Factory factory;
	{
		std::lock_guard<std::mutex> lock(registryMutex());
		auto it = registry().find(lower(name));
		if (it == registry().end()) {
			return nullptr;
		}
		factory = it->second;
	}
	return factory ? factory(settings) : nullptr;
}

std::vector<std::string> LicensingProviders::names()
{
	ensureBuiltIns();
	std::lock_guard<std::mutex> lock(registryMutex());
	std::vector<std::string> out;
	for (const auto& [name, factory] : registry()) {
		out.push_back(name);
	}
	return out;
}

// ---- parameters ---------------------------------------------------------------------------------

ParameterSpec licenseStatusParameter(ParameterId id, std::string key)
{
	ParameterSpec spec = ParameterSpec::boolParam(id, std::move(key), "Product Licensed", false);
	spec.hostFlags = HostFlag::Hidden | HostFlag::NotAnimatable | HostFlag::NoEvaluateOnChange | HostFlag::NoUndo | HostFlag::DontSave;
	spec.appFlags = AppFlag::NoState | AppFlag::PresetNoValue | AppFlag::PresetNoTags | AppFlag::PresetNoMeta;
	return spec;
}

ParameterSpec licensingMenuParameter(const LicensingSpec& spec, const LicensingProvider* provider, ParameterId id, std::string key)
{
	std::vector<ChoiceOption> options;
	options.push_back({std::string(kLicensingMenuKeyStatus), "Licensing"});
	if (provider != nullptr) {
		if (spec.showBuyButton && provider->has(LicensingCapability::Buy)) {
			options.push_back({std::string(kLicensingMenuKeyBuy), "Buy..."});
		}
		if (spec.showProductButton && provider->has(LicensingCapability::ShowProduct)) {
			options.push_back({std::string(kLicensingMenuKeyShowProduct), "Show Product..."});
		}
		if (provider->has(LicensingCapability::LicenseEntry)) {
			options.push_back({std::string(kLicensingMenuKeyEnterLicense), "Enter License..."});
		}
		if (provider->has(LicensingCapability::Deactivation)) {
			options.push_back({std::string(kLicensingMenuKeyDeactivate), "Deactivate This Machine"});
		}
		if (spec.autoChecking && provider->has(LicensingCapability::UpdateChecking)) {
			options.push_back({std::string(kLicensingMenuKeyCheckUpdates), "Check for Updates"});
		}
	}
	options.push_back({std::string(kLicensingMenuKeyRefresh), "Refresh License Status"});
	ParameterSpec menu = ParameterSpec::menuParam(id, std::move(key), "Licensing", std::move(options), 0);
	menu.hostFlags = HostFlag::NotAnimatable | HostFlag::NoEvaluateOnChange | HostFlag::NoUndo | HostFlag::DontSave;
	menu.appFlags = AppFlag::NoState | AppFlag::PresetNoValue | AppFlag::PresetNoTags | AppFlag::PresetNoMeta;
	return menu;
}

// ---- extension ----------------------------------------------------------------------------------

LicensingExtension::LicensingExtension(std::shared_ptr<LicensingProvider> provider, LicensingSpec spec, ParameterId statusParameter, ParameterId menuParameter, ParameterId debugMenuParameter)
: provider_(std::move(provider)), spec_(std::move(spec)), statusParameter_(statusParameter), menuParameter_(menuParameter), debugMenuParameter_(debugMenuParameter)
{
}

std::vector<EventId> LicensingExtension::interests() const
{
	return {EventId::CreateInstance, EventId::InstanceChanged, EventId::BuildSnapshot, EventId::Render, EventId::DestroyInstance};
}

int LicensingExtension::priority(EventId id) const
{
	switch (id) {
	case EventId::CreateInstance: return -16;
	case EventId::InstanceChanged: return -13;  // before the debug menu resets its row
	case EventId::Render: return kPriorityLast;  // the watermark goes on after every other observer
	default: return defaultPriority();
	}
}

bool LicensingExtension::load(EffectHost& host, int index)
{
	if (!Extension::load(host, index)) {
		return false;
	}
	const EffectManifest& manifest = host.manifest();
	productId_ = spec_.productId.empty() ? manifest.identity.uuid : spec_.productId;
	productVersion_ = spec_.productVersion.empty() ? manifest.version.toString() : spec_.productVersion;
	if (spec_.licensedVersion == "*") {
		licensedMajor_ = manifest.version.major;
	} else if (!spec_.licensedVersion.empty()) {
		Version v;
		if (Version::parse(spec_.licensedVersion, v)) {
			licensedMajor_ = v.major;
		}
	}
	if (!spec_.releaseDate.empty() && !parseIso8601(spec_.releaseDate, releaseDate_)) {
		host.services().log().warning("The licensing release date does not parse: " + spec_.releaseDate);
	}
	return true;
}

void LicensingExtension::handle(Event& event)
{
	switch (event.id) {
	case EventId::CreateInstance: {
		// The cached entitlement from the document answers before the provider does.
		Bytes stored;
		if (host()->services().persistence().load(kPersistenceKey, stored).ok()) {
			LicenseEntitlement e;
			if (LicenseEntitlement::decode(stored, e).ok()) {
				std::lock_guard<std::mutex> lock(mutex_);
				entitlement_ = e;
			}
		}
		refresh();
		if (provider_ && !observing_) {
			observing_ = true;
			provider_->startObserving(productId_, [this](LicenseStatus status) { applyStatus(status, true); });
		}
		break;
	}
	case EventId::InstanceChanged: {
		const auto& change = static_cast<const ChangeEvent&>(event);
		ParameterService& params = host()->services().parameters();
		if (change.parameter == statusParameter_ && change.reason == ChangeReason::UserEdited) {
			// An edit to the toggle is reverted to the true status.
			writeToggle();
		} else if (change.parameter == menuParameter_ && change.reason != ChangeReason::PluginEdited) {
			Value current;
			if (params.getCurrent(menuParameter_, current).ok()) {
				const ParameterSpec* spec = params.spec(menuParameter_);
				const int index = current.toInt(0);
				if (spec != nullptr && index > 0) {
					activate(spec->optionKey(index));
					params.setCurrent(menuParameter_, Value(0));
				}
			}
		} else if (debugMenuParameter_ != kParameterIdNone && change.parameter == debugMenuParameter_ && change.reason != ChangeReason::PluginEdited) {
			Value current;
			const ParameterSpec* spec = params.spec(debugMenuParameter_);
			if (spec != nullptr && params.getCurrent(debugMenuParameter_, current).ok()) {
				const std::string_view row = spec->optionKey(current.toInt(0));
				if (row == kDebugMenuKeyLicenseLicensed) {
					setDebugStatus(LicenseStatus::Licensed);
				} else if (row == kDebugMenuKeyLicenseTrial) {
					setDebugStatus(LicenseStatus::Trial);
				} else if (row == kDebugMenuKeyLicenseUnlicensed) {
					setDebugStatus(LicenseStatus::Unlicensed);
				} else if (row == kDebugMenuKeyLicenseClear) {
					setDebugStatus(LicenseStatus::Unknown);
				}
			}
		}
		break;
	}
	case EventId::BuildSnapshot: {
		auto& snapshot = static_cast<SnapshotEvent&>(event);
		std::lock_guard<std::mutex> lock(mutex_);
		snapshot.builder.setLicense(licenseStateFor(status_));
		if (entitlement_) {
			snapshot.builder.setExtra(std::string(kSnapshotKey), entitlement_->encode());
		}
		break;
	}
	case EventId::DestroyInstance:
		if (provider_ && observing_) {
			provider_->stopObserving(productId_);
			observing_ = false;
		}
		break;
	default:
		break;
	}
}

LicenseStatus LicensingExtension::gatedStatus(LicenseStatus raw) const
{
	LicenseStatus status = raw;
	// A perpetual license covers the releases inside its update window; a build released after
	// the window closed reads Expired.
	if (releaseDate_ != 0 && entitlement_ && entitlement_->updatesUntil != 0 && licenseStatusUnlocks(status) && entitlement_->kind == LicenseKind::Perpetual && releaseDate_ > entitlement_->updatesUntil) {
		status = LicenseStatus::Expired;
	}
	return status;
}

void LicensingExtension::applyStatus(LicenseStatus raw, bool fromProvider)
{
	bool changed = false;
	{
		std::lock_guard<std::mutex> lock(mutex_);
		raw_ = raw;
		if (provider_ && provider_->has(LicensingCapability::Entitlement)) {
			entitlement_ = provider_->entitlement(productId_);
		}
		upgrade_ = provider_ && provider_->has(LicensingCapability::VersionGating) && provider_->upgradeAvailable(productId_);
		LicenseStatus gated = gatedStatus(raw);
		if (gated == LicenseStatus::Expired && raw != LicenseStatus::Expired) {
			upgrade_ = true;
		}
		if (debugOverride_) {
			gated = *debugOverride_;
		}
		changed = gated != status_;
		status_ = gated;
	}
	if (changed) {
		++changes_;
		HostServices& services = host()->services();
		if (entitlement_) {
			services.persistence().store(kPersistenceKey, entitlement_->encode());
		}
		writeToggle();
		services.log().info(std::string("License status: ") + std::string(toString(status_)) + (fromProvider ? " (provider)" : ""));
	}
}

void LicensingExtension::writeToggle()
{
	if (host() == nullptr) {
		return;
	}
	ParameterService& params = host()->services().parameters();
	if (params.spec(statusParameter_) == nullptr) {
		return;
	}
	const bool licensed = isLicensed();
	Value current;
	if (params.getCurrent(statusParameter_, current).ok() && current.toBool(false) == licensed) {
		return;
	}
	if (params.canWrite()) {
		params.setCurrent(statusParameter_, Value(licensed));
	} else {
		EditScope scope(host()->services(), "Licensing", false);
		params.setCurrent(statusParameter_, Value(licensed));
	}
}

void LicensingExtension::refresh()
{
	LicenseStatus raw = LicenseStatus::Unknown;
	if (provider_) {
		raw = licensedMajor_ != 0 && provider_->has(LicensingCapability::VersionGating) ? provider_->statusForVersion(productId_, licensedMajor_) : provider_->status(productId_);
	}
	applyStatus(raw, false);
}

LicenseStatus LicensingExtension::status() const
{
	std::lock_guard<std::mutex> lock(mutex_);
	return status_;
}

bool LicensingExtension::upgradeAvailable() const
{
	std::lock_guard<std::mutex> lock(mutex_);
	return upgrade_;
}

std::optional<LicenseEntitlement> LicensingExtension::entitlement() const
{
	std::lock_guard<std::mutex> lock(mutex_);
	return entitlement_;
}

std::optional<WatermarkConfiguration> LicensingExtension::watermarkFor(LicenseStatus status) const
{
	const std::string name = host() != nullptr ? host()->manifest().identity.name : std::string("Unlicensed");
	switch (status) {
	case LicenseStatus::Licensed:
	case LicenseStatus::OfflineGrace:
		return std::nullopt;
	case LicenseStatus::Trial:
		if (!spec_.watermarkTrial) {
			return std::nullopt;
		}
		if (spec_.trialWatermark) {
			WatermarkConfiguration c = *spec_.trialWatermark;
			if (c.text.empty()) {
				c.text = name + " Trial";
			}
			return c;
		}
		return WatermarkConfiguration::trial(name + " Trial");
	default:
		if (!spec_.watermarkUnlicensed) {
			return std::nullopt;
		}
		if (spec_.unlicensedWatermark) {
			WatermarkConfiguration c = *spec_.unlicensedWatermark;
			if (c.text.empty()) {
				c.text = name;
			}
			return c;
		}
		return WatermarkConfiguration::unlicensed(name);
	}
}

std::optional<WatermarkConfiguration> LicensingExtension::watermarkFor(LicenseState state) const
{
	switch (state) {
	case LicenseState::Licensed: return std::nullopt;
	case LicenseState::Trial: return watermarkFor(LicenseStatus::Trial);
	default: return watermarkFor(LicenseStatus::Unlicensed);
	}
}

void LicensingExtension::setDebugStatus(LicenseStatus status)
{
	{
		std::lock_guard<std::mutex> lock(mutex_);
		if (status == LicenseStatus::Unknown) {
			debugOverride_.reset();
		} else {
			debugOverride_ = status;
		}
	}
	LicenseStatus raw = LicenseStatus::Unknown;
	{
		std::lock_guard<std::mutex> lock(mutex_);
		raw = raw_;
	}
	applyStatus(raw, false);
}

bool LicensingExtension::buy()
{
	return provider_ && provider_->has(LicensingCapability::Buy) && provider_->buy(productId_);
}

bool LicensingExtension::showProduct()
{
	return provider_ && provider_->has(LicensingCapability::ShowProduct) && provider_->showProduct(productId_);
}

bool LicensingExtension::showLicenseEntry()
{
	return provider_ && provider_->has(LicensingCapability::LicenseEntry) && provider_->showLicenseEntry(productId_);
}

void LicensingExtension::deactivate()
{
	if (!provider_ || !provider_->has(LicensingCapability::Deactivation)) {
		return;
	}
	provider_->deactivateLicense(productId_, [this](LicenseStatus status, const Error& e) {
		if (e && host() != nullptr) {
			host()->services().log().warning("Deactivation failed: " + e.describe());
		}
		applyStatus(status, true);
	});
}

void LicensingExtension::checkForUpdates(bool force)
{
	if (!provider_ || !provider_->has(LicensingCapability::UpdateChecking)) {
		return;
	}
	provider_->checkForUpdates(productId_, productVersion_, force, [this](const LicenseUpdateInfo& info) {
		if (host() == nullptr) {
			return;
		}
		if (info.error) {
			host()->services().log().warning("Update check failed: " + info.error.describe());
		} else if (info.newVersionFound) {
			host()->services().feedback().message(MessageKind::Message, "ofxgrip.licensing.update", "A newer version is available" + (info.product && !info.product->latestVersion.empty() ? ": " + info.product->latestVersion : std::string()));
		}
	});
}

void LicensingExtension::activate(std::string_view rowKey)
{
	HostServices& services = host()->services();
	auto report = [&services](bool taken, std::string_view what) {
		if (!taken) {
			services.feedback().message(MessageKind::Message, "ofxgrip.licensing", "The store cannot " + std::string(what) + " from here");
		}
	};
	if (rowKey == kLicensingMenuKeyBuy) {
		report(buy(), "buy the product");
	} else if (rowKey == kLicensingMenuKeyShowProduct) {
		report(showProduct(), "show the product");
	} else if (rowKey == kLicensingMenuKeyEnterLicense) {
		report(showLicenseEntry(), "enter a license");
	} else if (rowKey == kLicensingMenuKeyDeactivate) {
		deactivate();
	} else if (rowKey == kLicensingMenuKeyCheckUpdates) {
		checkForUpdates(true);
	} else if (rowKey == kLicensingMenuKeyRefresh) {
		refresh();
		services.feedback().message(MessageKind::Message, "ofxgrip.licensing", "License status: " + std::string(toString(status())));
	}
}

OFXGRIP_NAMESPACE_END
