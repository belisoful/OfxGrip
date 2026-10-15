// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
#include <ofxgrip/core/LicensingProviders.hpp>

#include <ofxgrip/core/Digest.hpp>

#include <nlohmann/json.hpp>

#include <chrono>
#include <fstream>
#include <sstream>

OFXGRIP_NAMESPACE_BEGIN

namespace {

using Json = nlohmann::json;

std::int64_t wallClock() noexcept
{
	return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

std::string setting(const LicensingSettings& settings, const char* name, std::string fallback = {})
{
	auto it = settings.find(name);
	if (it == settings.end() || it->second.empty()) {
		return fallback;
	}
	return it->second.is<std::string>() ? it->second.as<std::string>() : it->second.toString();
}

std::int64_t fixedClock(const LicensingSettings& settings)
{
	std::int64_t now = 0;
	const std::string text = setting(settings, "now");
	if (!text.empty() && !parseIso8601(text, now)) {
		return 0;
	}
	return now;
}

std::string readFile(const std::filesystem::path& path)
{
	std::ifstream in(path, std::ios::binary);
	if (!in) {
		return {};
	}
	std::ostringstream out;
	out << in.rdbuf();
	return out.str();
}

}  // namespace

// ---- static -------------------------------------------------------------------------------------

StaticLicensingProvider::StaticLicensingProvider(const LicensingSettings& settings)
{
	parseLicenseStatus(setting(settings, "status", "licensed"), status_);
	Version v;
	if (Version::parse(setting(settings, "licensedVersion", "0"), v)) {
		licensedMajor = v.major;
	}
}

LicenseStatus StaticLicensingProvider::status(std::string_view)
{
	std::lock_guard<std::mutex> lock(mutex_);
	return status_;
}

LicenseStatus StaticLicensingProvider::statusForVersion(std::string_view productId, unsigned major)
{
	const LicenseStatus base = status(productId);
	if (licensedMajor != 0 && major > licensedMajor && licenseStatusUnlocks(base)) {
		return LicenseStatus::Unlicensed;
	}
	return base;
}

bool StaticLicensingProvider::upgradeAvailable(std::string_view productId)
{
	return licensedMajor != 0 && licenseStatusUnlocks(status(productId));
}

void StaticLicensingProvider::startObserving(std::string_view productId, LicenseStatusHandler handler)
{
	std::lock_guard<std::mutex> lock(mutex_);
	observers_[std::string(productId)] = std::move(handler);
}

void StaticLicensingProvider::stopObserving(std::string_view productId)
{
	std::lock_guard<std::mutex> lock(mutex_);
	observers_.erase(std::string(productId));
}

std::uint32_t StaticLicensingProvider::capabilities() const
{
	return LicensingCapability::Entitlement | LicensingCapability::VersionGating | LicensingCapability::Buy | LicensingCapability::ShowProduct | LicensingCapability::LicenseEntry | LicensingCapability::Deactivation | LicensingCapability::UpdateChecking;
}

std::optional<LicenseEntitlement> StaticLicensingProvider::entitlement(std::string_view)
{
	std::lock_guard<std::mutex> lock(mutex_);
	return entitlement_;
}

bool StaticLicensingProvider::buy(std::string_view)
{
	++buys;
	return true;
}

bool StaticLicensingProvider::showProduct(std::string_view)
{
	++shows;
	return true;
}

bool StaticLicensingProvider::showLicenseEntry(std::string_view)
{
	++entries;
	return true;
}

void StaticLicensingProvider::deactivateLicense(std::string_view productId, LicenseCompletion completion)
{
	++deactivations;
	setStatus(LicenseStatus::Unlicensed);
	completion(status(productId), Error::none());
}

void StaticLicensingProvider::checkForUpdates(std::string_view, std::string_view, bool, LicenseUpdateInfoHandler handler)
{
	++updateChecks;
	LicenseUpdateInfo info;
	info.newVersionFound = true;
	LicenseProductInfo product;
	product.latestVersion = "99.0.0";
	info.product = product;
	handler(info);
}

void StaticLicensingProvider::setStatus(LicenseStatus status)
{
	std::vector<LicenseStatusHandler> handlers;
	{
		std::lock_guard<std::mutex> lock(mutex_);
		status_ = status;
		if (entitlement_) {
			entitlement_->status = status;
		}
		for (const auto& [product, handler] : observers_) {
			handlers.push_back(handler);
		}
	}
	for (const LicenseStatusHandler& handler : handlers) {
		handler(status);
	}
}

void StaticLicensingProvider::setEntitlement(std::optional<LicenseEntitlement> entitlement)
{
	std::lock_guard<std::mutex> lock(mutex_);
	entitlement_ = std::move(entitlement);
}

// ---- trial --------------------------------------------------------------------------------------

TrialLicensingProvider::TrialLicensingProvider(const LicensingSettings& settings)
{
	directory_ = setting(settings, "directory");
	days_ = std::max(1, static_cast<int>(settings.count("days") != 0 ? settings.at("days").toDouble(14) : 14));
	fixedNow_ = fixedClock(settings);
}

std::int64_t TrialLicensingProvider::now() const
{
	return fixedNow_ != 0 ? fixedNow_ : wallClock();
}

std::int64_t TrialLicensingProvider::firstRun(std::string_view productId)
{
	const std::string key(productId);
	{
		std::lock_guard<std::mutex> lock(mutex_);
		if (auto it = starts_.find(key); it != starts_.end()) {
			return it->second;
		}
	}
	std::int64_t start = 0;
	if (!directory_.empty()) {
		const std::filesystem::path path = directory_ / ("trial-" + key + ".json");
		const std::string text = readFile(path);
		if (!text.empty()) {
			Json j = Json::parse(text, nullptr, false, true);
			if (j.is_object() && j.contains("firstRun")) {
				if (j["firstRun"].is_number()) {
					start = static_cast<std::int64_t>(j["firstRun"].get<double>());
				} else if (j["firstRun"].is_string()) {
					parseIso8601(j["firstRun"].get<std::string>(), start);
				}
			}
		}
		if (start == 0) {
			start = now();
			std::error_code ec;
			std::filesystem::create_directories(directory_, ec);
			std::ofstream out(path, std::ios::binary);
			if (out) {
				out << "{\n  \"productId\": " << Json(key).dump() << ",\n  \"firstRun\": \"" << formatIso8601(start) << "\"\n}\n";
			}
		}
	} else {
		start = now();
	}
	std::lock_guard<std::mutex> lock(mutex_);
	starts_[key] = start;
	return start;
}

double TrialLicensingProvider::remainingDays(std::string_view productId)
{
	const std::int64_t start = firstRun(productId);
	return static_cast<double>(days_) - static_cast<double>(now() - start) / 86400.0;
}

LicenseStatus TrialLicensingProvider::status(std::string_view productId)
{
	return remainingDays(productId) > 0.0 ? LicenseStatus::Trial : LicenseStatus::Expired;
}

void TrialLicensingProvider::startObserving(std::string_view, LicenseStatusHandler) {}

void TrialLicensingProvider::stopObserving(std::string_view) {}

std::optional<LicenseEntitlement> TrialLicensingProvider::entitlement(std::string_view productId)
{
	LicenseEntitlement e;
	e.productId = std::string(productId);
	e.providerName = name();
	e.status = status(productId);
	e.kind = LicenseKind::Trial;
	e.issuedAt = firstRun(productId);
	e.expiresAt = e.issuedAt + static_cast<std::int64_t>(days_) * 86400;
	return e;
}

// ---- file ---------------------------------------------------------------------------------------

FileLicensingProvider::FileLicensingProvider(const LicensingSettings& settings)
{
	directory_ = setting(settings, "directory");
	secret_ = setting(settings, "secret");
	machineId_ = setting(settings, "machineId");
	fixedNow_ = fixedClock(settings);
}

std::int64_t FileLicensingProvider::now() const
{
	return fixedNow_ != 0 ? fixedNow_ : wallClock();
}

std::filesystem::path FileLicensingProvider::licensePath(std::string_view productId) const
{
	return directory_ / (std::string(productId) + ".license");
}

std::string FileLicensingProvider::canonicalText(const LicenseEntitlement& e, unsigned licensedMajor)
{
	std::ostringstream out;
	out << "productId=" << e.productId << '\n' << "providerName=" << e.providerName << '\n' << "status=" << toString(e.status) << '\n' << "kind=" << toString(e.kind) << '\n' << "email=" << e.email << '\n' << "issuedAt=" << e.issuedAt << '\n'
	    << "expiresAt=" << e.expiresAt << '\n' << "updatesUntil=" << e.updatesUntil << '\n' << "activationId=" << e.activationId << '\n' << "machineId=" << e.machineId << '\n' << "licensedMajor=" << licensedMajor << '\n';
	return out.str();
}

Error FileLicensingProvider::writeLicenseFile(const std::filesystem::path& path, const LicenseEntitlement& entitlement, unsigned licensedMajor, std::string_view secret)
{
	Json j = Json::parse(entitlement.toJson(), nullptr, false, true);
	if (!j.is_object()) {
		return Error::make(ErrorCode::Codec, "The entitlement does not serialize");
	}
	j["licensedMajor"] = licensedMajor;
	j["signature"] = hexDigest(hmacSha256(secret, canonicalText(entitlement, licensedMajor)));
	std::error_code ec;
	std::filesystem::create_directories(path.parent_path(), ec);
	std::ofstream out(path, std::ios::binary);
	if (!out) {
		return Error::make(ErrorCode::Failed, "The license file cannot be written").with("path", path.string());
	}
	out << j.dump(2) << '\n';
	return Error::none();
}

FileLicensingProvider::Record FileLicensingProvider::read(std::string_view productId)
{
	Record record;
	record.entitlement.productId = std::string(productId);
	record.entitlement.providerName = name();
	auto revoked = [&record]() {
		record.status = LicenseStatus::Revoked;
		record.entitlement.status = LicenseStatus::Revoked;
		return record;
	};
	const std::string text = directory_.empty() ? std::string() : readFile(licensePath(productId));
	if (text.empty()) {
		record.status = LicenseStatus::Unlicensed;
		record.entitlement.status = LicenseStatus::Unlicensed;
		return record;
	}
	Json j = Json::parse(text, nullptr, false, true);
	LicenseEntitlement e;
	if (!j.is_object() || LicenseEntitlement::fromJson(text, e)) {
		return revoked();
	}
	record.licensedMajor = j.value("licensedMajor", 0u);
	Sha256Digest signature{};
	Sha256Digest expected{};
	parseHexDigest(hexDigest(hmacSha256(secret_, canonicalText(e, record.licensedMajor))), expected);
	if (!j.contains("signature") || !j["signature"].is_string() || !parseHexDigest(j["signature"].get<std::string>(), signature) || !digestsEqual(signature, expected)) {
		return revoked();
	}
	if (e.productId != productId || e.providerName != name() || (!e.machineId.empty() && e.machineId != machineId_)) {
		return revoked();
	}
	record.entitlement = e;
	record.status = e.status;
	if (e.expiresAt != 0 && now() > e.expiresAt && licenseStatusUnlocks(e.status)) {
		record.status = LicenseStatus::Expired;
	}
	record.entitlement.status = record.status;
	return record;
}

LicenseStatus FileLicensingProvider::status(std::string_view productId)
{
	const LicenseStatus s = read(productId).status;
	std::lock_guard<std::mutex> lock(mutex_);
	lastStatus_[std::string(productId)] = s;
	return s;
}

LicenseStatus FileLicensingProvider::statusForVersion(std::string_view productId, unsigned major)
{
	const Record record = read(productId);
	if (record.licensedMajor != 0 && major > record.licensedMajor && licenseStatusUnlocks(record.status)) {
		return LicenseStatus::Unlicensed;
	}
	return record.status;
}

bool FileLicensingProvider::upgradeAvailable(std::string_view productId)
{
	const Record record = read(productId);
	return record.licensedMajor != 0 && licenseStatusUnlocks(record.status);
}

void FileLicensingProvider::startObserving(std::string_view productId, LicenseStatusHandler handler)
{
	std::lock_guard<std::mutex> lock(mutex_);
	observers_[std::string(productId)] = std::move(handler);
}

void FileLicensingProvider::stopObserving(std::string_view productId)
{
	std::lock_guard<std::mutex> lock(mutex_);
	observers_.erase(std::string(productId));
}

std::uint32_t FileLicensingProvider::capabilities() const
{
	return LicensingCapability::Entitlement | LicensingCapability::VersionGating | LicensingCapability::LicenseEntry | LicensingCapability::Activation | LicensingCapability::Deactivation;
}

std::optional<LicenseEntitlement> FileLicensingProvider::entitlement(std::string_view productId)
{
	const Record record = read(productId);
	if (record.status == LicenseStatus::Unlicensed) {
		return std::nullopt;
	}
	return record.entitlement;
}

void FileLicensingProvider::activateLicenseKey(std::string_view key, std::string_view productId, LicenseCompletion completion)
{
	// A key is the license file's text; activation writes it in place.
	if (directory_.empty()) {
		completion(LicenseStatus::Unlicensed, Error::make(ErrorCode::Failed, "The license folder is not set"));
		return;
	}
	std::error_code ec;
	std::filesystem::create_directories(directory_, ec);
	std::ofstream out(licensePath(productId), std::ios::binary);
	if (!out) {
		completion(LicenseStatus::Unlicensed, Error::make(ErrorCode::Failed, "The license file cannot be written"));
		return;
	}
	out << key;
	out.close();
	reload(productId);
	completion(status(productId), Error::none());
}

void FileLicensingProvider::deactivateLicense(std::string_view productId, LicenseCompletion completion)
{
	std::error_code ec;
	if (!directory_.empty()) {
		std::filesystem::remove(licensePath(productId), ec);
	}
	reload(productId);
	completion(status(productId), Error::none());
}

void FileLicensingProvider::reload(std::string_view productId)
{
	const LicenseStatus fresh = read(productId).status;
	LicenseStatusHandler handler;
	{
		std::lock_guard<std::mutex> lock(mutex_);
		auto last = lastStatus_.find(std::string(productId));
		if (last != lastStatus_.end() && last->second == fresh) {
			return;
		}
		lastStatus_[std::string(productId)] = fresh;
		auto it = observers_.find(std::string(productId));
		if (it != observers_.end()) {
			handler = it->second;
		}
	}
	if (handler) {
		handler(fresh);
	}
}

// ---- registration -------------------------------------------------------------------------------

void registerBuiltInLicensingProviders()
{
	LicensingProviders::registerProvider("static", [](const LicensingSettings& settings) { return std::make_shared<StaticLicensingProvider>(settings); });
	LicensingProviders::registerProvider("trial", [](const LicensingSettings& settings) { return std::make_shared<TrialLicensingProvider>(settings); });
	LicensingProviders::registerProvider("file", [](const LicensingSettings& settings) { return std::make_shared<FileLicensingProvider>(settings); });
}

OFXGRIP_NAMESPACE_END
