// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
// The licensing seam over the fakes: statuses and entitlements, the provider registry and the
// built-in providers, the parameters, and the extension's status, snapshot, menu, watermark,
// version gate, and update window.
#include <ofxgrip/core/Licensing.hpp>
#include <ofxgrip/core/LicensingProviders.hpp>

#include "FakeServices.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

using namespace ofxgrip;
using namespace ofxgrip::testing;

namespace {

enum : ParameterId { kGain = 1, kLicensed = 9996, kMenu = 9997 };

std::filesystem::path freshDirectory(const char* name)
{
	static int counter = 0;
	const std::filesystem::path dir = std::filesystem::temp_directory_path() / ("ofxgrip-licensing-" + std::to_string(++counter)) / name;
	std::error_code ec;
	std::filesystem::remove_all(dir.parent_path(), ec);
	std::filesystem::create_directories(dir, ec);
	return dir;
}

EffectManifest manifest(const LicensingSpec& spec, const LicensingProvider* provider)
{
	EffectManifest m;
	m.identity.ofxId = "com.test.licensing";
	m.identity.uuid = "44444444-4444-4444-8444-444444444444";
	m.identity.name = "Licensed Effect";
	m.version = Version{2, 1, 0};
	m.parameters.push_back(ParameterSpec::floatParam(kGain, "gain", "Gain", 1, 0, 2));
	m.parameters.push_back(licenseStatusParameter(kLicensed, "ofxgrip_licensed"));
	m.parameters.push_back(licensingMenuParameter(spec, provider, kMenu, "ofxgrip_licensing"));
	m.licensing = spec;
	return m;
}

struct Rig {
	std::shared_ptr<StaticLicensingProvider> provider;
	std::unique_ptr<FakeEffectHost> host;
	std::unique_ptr<ExtensionSystem> system;
	LicensingExtension* extension{nullptr};

	explicit Rig(LicensingSpec spec = {}, LicenseStatus initial = LicenseStatus::Licensed)
	{
		if (spec.provider.empty()) {
			spec.provider = "static";
		}
		provider = std::make_shared<StaticLicensingProvider>();
		provider->setStatus(initial);
		host = std::make_unique<FakeEffectHost>(manifest(spec, provider.get()));
		system = std::make_unique<ExtensionSystem>(*host);
		extension = static_cast<LicensingExtension*>(system->load(std::make_unique<LicensingExtension>(provider, spec, kLicensed, kMenu)));
		host->post(EventId::CreateInstance);
	}
	bool toggle() const { return host->params().valueRef(kLicensed).toBool(false); }
	void pickRow(std::string_view key)
	{
		const ParameterSpec& spec = host->params().specRef(kMenu);
		host->params().valueRef(kMenu) = Value(spec.optionIndex(key));
		host->change("ofxgrip_licensing");
	}
};

}  // namespace

TEST(Licensing, StatusesEntitlementsAndDates)
{
	EXPECT_TRUE(licenseStatusUnlocks(LicenseStatus::Licensed));
	EXPECT_TRUE(licenseStatusUnlocks(LicenseStatus::Trial));
	EXPECT_TRUE(licenseStatusUnlocks(LicenseStatus::OfflineGrace));
	EXPECT_FALSE(licenseStatusUnlocks(LicenseStatus::Expired));
	EXPECT_EQ(licenseStateFor(LicenseStatus::OfflineGrace), LicenseState::Licensed);
	EXPECT_EQ(licenseStateFor(LicenseStatus::Trial), LicenseState::Trial);
	EXPECT_EQ(licenseStateFor(LicenseStatus::Revoked), LicenseState::Unlicensed);
	EXPECT_EQ(licenseStateFor(LicenseStatus::Unknown), LicenseState::Unknown);
	LicenseStatus status{};
	for (LicenseStatus s : {LicenseStatus::Unknown, LicenseStatus::InvalidProduct, LicenseStatus::Unlicensed, LicenseStatus::Licensed, LicenseStatus::Trial, LicenseStatus::Expired, LicenseStatus::OfflineGrace, LicenseStatus::Revoked}) {
		ASSERT_TRUE(parseLicenseStatus(toString(s), status));
		EXPECT_EQ(status, s);
	}
	EXPECT_FALSE(parseLicenseStatus("maybe", status));
	LicenseKind kind{};
	EXPECT_TRUE(parseLicenseKind("subscription", kind));
	EXPECT_EQ(kind, LicenseKind::Subscription);
	EXPECT_FALSE(parseLicenseKind("rental", kind));
	std::int64_t seconds = 0;
	EXPECT_TRUE(parseIso8601("1970-01-02", seconds));
	EXPECT_EQ(seconds, 86400);
	EXPECT_TRUE(parseIso8601("2026-10-15T12:30:15Z", seconds));
	EXPECT_EQ(formatIso8601(seconds), "2026-10-15T12:30:15Z");
	EXPECT_TRUE(parseIso8601("2026-10-15 01:02", seconds));
	EXPECT_EQ(formatIso8601(seconds), "2026-10-15T01:02:00Z");
	EXPECT_FALSE(parseIso8601("2026-13-01", seconds));
	EXPECT_FALSE(parseIso8601("yesterday", seconds));
	EXPECT_FALSE(parseIso8601("2026-10-15X10:00", seconds));
	EXPECT_FALSE(parseIso8601("2026-10-15T1", seconds));
	EXPECT_EQ(formatIso8601(-1), "1969-12-31T23:59:59Z");

	LicenseEntitlement e;
	e.productId = "P";
	e.providerName = "file";
	e.status = LicenseStatus::Licensed;
	e.kind = LicenseKind::Subscription;
	e.email = "a@b.c";
	e.issuedAt = 1000;
	e.expiresAt = 2000;
	e.updatesUntil = 3000;
	e.willRenew = true;
	e.inBillingRetry = true;
	e.activationId = "act";
	e.machineId = "mac";
	EXPECT_TRUE(e.isLicensed());
	LicenseEntitlement back;
	ASSERT_TRUE(LicenseEntitlement::decode(e.encode(), back).ok());
	EXPECT_EQ(back, e);
	const Bytes bytes = e.encode();
	for (std::size_t cut = 0; cut < bytes.size(); ++cut) {
		EXPECT_FALSE(LicenseEntitlement::decode(std::span<const std::uint8_t>(bytes.data(), cut), back).ok());
	}
	Bytes badStatus = bytes;
	badStatus[4 + 2 + 4 + 1 + 4 + 4] = 99;  // the status byte follows the two strings
	EXPECT_EQ(LicenseEntitlement::decode(badStatus, back).code(), ErrorCode::Codec);
	const std::string json = e.toJson();
	LicenseEntitlement parsed;
	ASSERT_TRUE(LicenseEntitlement::fromJson(json, parsed).ok());
	EXPECT_EQ(parsed, e);
	EXPECT_EQ(LicenseEntitlement::fromJson("nope", parsed).code(), ErrorCode::Codec);
	EXPECT_EQ(LicenseEntitlement::fromJson("{}", parsed).code(), ErrorCode::Codec);
	EXPECT_EQ(LicenseEntitlement::fromJson(R"({"productId":"P","providerName":"x","status":"odd"})", parsed).code(), ErrorCode::Codec);
	EXPECT_EQ(LicenseEntitlement::fromJson(R"({"productId":"P","providerName":"x","kind":"odd"})", parsed).code(), ErrorCode::Codec);
	EXPECT_EQ(LicenseEntitlement::fromJson(R"({"productId":"P","providerName":"x","issuedAt":"soon"})", parsed).code(), ErrorCode::Codec);
	ASSERT_TRUE(LicenseEntitlement::fromJson(R"({"productId":"P","providerName":"x","issuedAt":5})", parsed).ok());
	EXPECT_EQ(parsed.issuedAt, 5);
	EXPECT_EQ(parsed.status, LicenseStatus::Unknown);
}

TEST(Licensing, RegistryCreatesTheBuiltInProviders)
{
	const std::vector<std::string> names = LicensingProviders::names();
	EXPECT_NE(std::find(names.begin(), names.end(), "static"), names.end());
	EXPECT_NE(std::find(names.begin(), names.end(), "trial"), names.end());
	EXPECT_NE(std::find(names.begin(), names.end(), "file"), names.end());
	EXPECT_NE(LicensingProviders::create("Static"), nullptr);
	EXPECT_EQ(LicensingProviders::create("nonesuch"), nullptr);
	LicensingProviders::registerProvider("custom", [](const LicensingSettings&) { return std::make_shared<StaticLicensingProvider>(); });
	EXPECT_NE(LicensingProviders::create("CUSTOM"), nullptr);
	LicensingProviders::registerProvider("broken", nullptr);
	EXPECT_EQ(LicensingProviders::create("broken"), nullptr);
	LicensingSettings settings;
	settings["status"] = Value("trial");
	settings["licensedVersion"] = Value("1");
	std::shared_ptr<LicensingProvider> provider = LicensingProviders::create("static", settings);
	ASSERT_NE(provider, nullptr);
	EXPECT_EQ(provider->status("P"), LicenseStatus::Trial);
	EXPECT_EQ(provider->statusForVersion("P", 2), LicenseStatus::Unlicensed);
	EXPECT_EQ(provider->statusForVersion("P", 1), LicenseStatus::Trial);
	EXPECT_TRUE(provider->upgradeAvailable("P"));
	EXPECT_TRUE(provider->has(LicensingCapability::Buy));
	EXPECT_FALSE(provider->has(LicensingCapability::Support));
	// The base class's defaults for the capabilities a provider lacks.
	class Bare final : public LicensingProvider {
	public:
		std::string name() const override { return "bare"; }
		bool available() const override { return false; }
		LicenseStatus status(std::string_view) override { return LicenseStatus::Unknown; }
		void startObserving(std::string_view, LicenseStatusHandler) override {}
		void stopObserving(std::string_view) override {}
	};
	Bare bare;
	EXPECT_EQ(bare.capabilities(), 0u);
	EXPECT_FALSE(bare.entitlement("P").has_value());
	EXPECT_EQ(bare.statusForVersion("P", 1), LicenseStatus::Unknown);
	EXPECT_FALSE(bare.upgradeAvailable("P"));
	bool called = false;
	bare.fetchProductInfo("P", [&called](const std::optional<LicenseProductInfo>& info, const Error& e) {
		called = true;
		EXPECT_FALSE(info.has_value());
		EXPECT_EQ(e.code(), ErrorCode::Unsupported);
	});
	EXPECT_TRUE(called);
	EXPECT_FALSE(bare.buy("P"));
	EXPECT_FALSE(bare.showProduct("P"));
	EXPECT_FALSE(bare.updateCheckingEnabled("P"));
	bare.setUpdateCheckingEnabled("P", true);
	bare.checkForUpdates("P", "1.0", false, [](const LicenseUpdateInfo& info) { EXPECT_EQ(info.error.code(), ErrorCode::Unsupported); });
	EXPECT_FALSE(bare.showSupportForm("s", "m"));
	EXPECT_FALSE(bare.showLicenseEntry("P"));
	bare.activateLicenseKey("k", "P", [](LicenseStatus, const Error& e) { EXPECT_EQ(e.code(), ErrorCode::Unsupported); });
	bare.validateLicense("P", [](LicenseStatus s, const Error& e) {
		EXPECT_EQ(s, LicenseStatus::Unknown);
		EXPECT_TRUE(e.ok());
	});
	bare.deactivateLicense("P", [](LicenseStatus, const Error& e) { EXPECT_EQ(e.code(), ErrorCode::Unsupported); });
	EXPECT_TRUE(bare.hostConfigurationProblems("/nowhere").empty());
}

TEST(Licensing, ParametersListTheProvidersActions)
{
	const ParameterSpec toggle = licenseStatusParameter(kLicensed, "ofxgrip_licensed");
	EXPECT_EQ(toggle.type, ParameterType::Toggle);
	EXPECT_TRUE(toggle.hostFlags.has(HostFlag::Hidden));
	EXPECT_FALSE(toggle.hasState());
	StaticLicensingProvider provider;
	LicensingSpec spec;
	ParameterSpec menu = licensingMenuParameter(spec, &provider, kMenu, "ofxgrip_licensing");
	EXPECT_EQ(menu.type, ParameterType::Menu);
	ASSERT_EQ(menu.options.size(), 7u);  // title, buy, show, enter, deactivate, updates, refresh
	EXPECT_EQ(menu.options[1].key, kLicensingMenuKeyBuy);
	EXPECT_EQ(menu.options.back().key, kLicensingMenuKeyRefresh);
	spec.showBuyButton = false;
	spec.showProductButton = false;
	spec.autoChecking = false;
	menu = licensingMenuParameter(spec, &provider, kMenu, "ofxgrip_licensing");
	EXPECT_EQ(menu.options.size(), 4u);
	menu = licensingMenuParameter(spec, nullptr, kMenu, "ofxgrip_licensing");
	EXPECT_EQ(menu.options.size(), 2u);
	TrialLicensingProvider trial;
	menu = licensingMenuParameter(LicensingSpec{}, &trial, kMenu, "ofxgrip_licensing");
	EXPECT_EQ(menu.options.size(), 2u);
}

TEST(Licensing, ExtensionMirrorsTheProviderIntoTheToggleSnapshotAndWatermark)
{
	Rig rig;
	ASSERT_NE(rig.extension, nullptr);
	EXPECT_EQ(rig.extension->provider(), rig.provider.get());
	EXPECT_EQ(rig.extension->productId(), "44444444-4444-4444-8444-444444444444");
	EXPECT_EQ(rig.extension->productVersion(), "2.1.0");
	EXPECT_EQ(rig.extension->status(), LicenseStatus::Licensed);
	EXPECT_TRUE(rig.extension->isLicensed());
	EXPECT_TRUE(rig.toggle());
	EXPECT_EQ(rig.extension->statusChanges(), 1);
	EXPECT_FALSE(rig.extension->watermarkFor(LicenseStatus::Licensed).has_value());
	EXPECT_FALSE(rig.extension->watermarkFor(LicenseStatus::OfflineGrace).has_value());
	EXPECT_FALSE(rig.extension->watermarkFor(LicenseStatus::Trial).has_value());  // watermarkTrial off
	ASSERT_TRUE(rig.extension->watermarkFor(LicenseStatus::Unlicensed).has_value());
	EXPECT_EQ(rig.extension->watermarkFor(LicenseStatus::Unlicensed)->text, "Licensed Effect");
	// The provider's observation changes the status, the toggle, and the snapshot.
	rig.provider->setStatus(LicenseStatus::Trial);
	EXPECT_EQ(rig.extension->status(), LicenseStatus::Trial);
	EXPECT_TRUE(rig.toggle());
	rig.provider->setStatus(LicenseStatus::Revoked);
	EXPECT_FALSE(rig.toggle());
	EXPECT_EQ(rig.extension->statusChanges(), 3);
	RenderSnapshotBuilder builder;
	SnapshotEvent snapshot(builder);
	rig.host->bus().post(snapshot);
	EXPECT_EQ(builder.build().license(), LicenseState::Unlicensed);
	// An edit to the toggle is reverted.
	rig.host->params().valueRef(kLicensed) = Value(true);
	rig.host->change("ofxgrip_licensed");
	EXPECT_FALSE(rig.toggle());
	// The debug override forces a status and clears again.
	rig.extension->setDebugStatus(LicenseStatus::Licensed);
	EXPECT_TRUE(rig.extension->isLicensed());
	rig.extension->setDebugStatus(LicenseStatus::Unknown);
	EXPECT_EQ(rig.extension->status(), LicenseStatus::Revoked);
	// The menu rows run the provider's actions and snap back.
	rig.pickRow(kLicensingMenuKeyBuy);
	EXPECT_EQ(rig.provider->buys, 1);
	EXPECT_EQ(rig.host->params().valueRef(kMenu).toInt(), 0);
	rig.pickRow(kLicensingMenuKeyShowProduct);
	EXPECT_EQ(rig.provider->shows, 1);
	rig.pickRow(kLicensingMenuKeyEnterLicense);
	EXPECT_EQ(rig.provider->entries, 1);
	rig.pickRow(kLicensingMenuKeyCheckUpdates);
	EXPECT_EQ(rig.provider->updateChecks, 1);
	EXPECT_FALSE(rig.host->fakes().feedbackService.messages.empty());
	EXPECT_NE(rig.host->fakes().feedbackService.messages.back().find("99.0.0"), std::string::npos);
	rig.provider->setStatus(LicenseStatus::Licensed);
	rig.pickRow(kLicensingMenuKeyDeactivate);
	EXPECT_EQ(rig.provider->deactivations, 1);
	EXPECT_EQ(rig.extension->status(), LicenseStatus::Unlicensed);
	rig.pickRow(kLicensingMenuKeyRefresh);
	EXPECT_NE(rig.host->fakes().feedbackService.messages.back().find("unlicensed"), std::string::npos);
	rig.host->params().valueRef(kMenu) = Value(0);
	rig.host->change("ofxgrip_licensing");
	rig.extension->activate("nonesuch");
	// Destroying stops the observation.
	rig.host->post(EventId::DestroyInstance);
	rig.provider->setStatus(LicenseStatus::Trial);
	EXPECT_EQ(rig.extension->status(), LicenseStatus::Unlicensed);
}

TEST(Licensing, ExtensionGatesByVersionAndUpdateWindowAndKeepsTheEntitlement)
{
	LicensingSpec spec;
	spec.provider = "static";
	spec.licensedVersion = "*";  // the manifest major, 2
	spec.releaseDate = "2026-10-15";
	spec.watermarkTrial = true;
	WatermarkConfiguration trial = WatermarkConfiguration::trial("");
	trial.opacity = 0.9;
	spec.trialWatermark = trial;
	WatermarkConfiguration unlicensed = WatermarkConfiguration::unlicensed("Custom");
	spec.unlicensedWatermark = unlicensed;
	Rig rig(spec);
	// The holder is licensed for 1.x only: 2.x reads Unlicensed with an upgrade on offer.
	rig.provider->licensedMajor = 1;
	rig.extension->refresh();
	EXPECT_EQ(rig.extension->status(), LicenseStatus::Unlicensed);
	EXPECT_TRUE(rig.extension->upgradeAvailable());
	rig.provider->licensedMajor = 2;
	rig.extension->refresh();
	EXPECT_EQ(rig.extension->status(), LicenseStatus::Licensed);
	// A perpetual license whose update window closed before this release reads Expired.
	LicenseEntitlement e;
	e.productId = rig.extension->productId();
	e.providerName = "static";
	e.status = LicenseStatus::Licensed;
	ASSERT_TRUE(parseIso8601("2026-01-01", e.updatesUntil));
	rig.provider->setEntitlement(e);
	rig.extension->refresh();
	EXPECT_EQ(rig.extension->status(), LicenseStatus::Expired);
	EXPECT_TRUE(rig.extension->upgradeAvailable());
	ASSERT_TRUE(rig.extension->entitlement().has_value());
	EXPECT_EQ(rig.host->fakes().persist.blobs.count(std::string(LicensingExtension::kPersistenceKey)), 1u);
	RenderSnapshotBuilder builder;
	SnapshotEvent snapshot(builder);
	rig.host->bus().post(snapshot);
	const RenderSnapshot built = builder.build();
	EXPECT_EQ(built.license(), LicenseState::Unlicensed);
	ASSERT_NE(built.extra(LicensingExtension::kSnapshotKey), nullptr);
	LicenseEntitlement carried;
	ASSERT_TRUE(LicenseEntitlement::decode(*built.extra(LicensingExtension::kSnapshotKey), carried).ok());
	EXPECT_EQ(carried.productId, e.productId);
	// A window still open keeps the status; a subscription is never window-gated.
	ASSERT_TRUE(parseIso8601("2027-01-01", e.updatesUntil));
	rig.provider->setEntitlement(e);
	rig.extension->refresh();
	EXPECT_EQ(rig.extension->status(), LicenseStatus::Licensed);
	e.kind = LicenseKind::Subscription;
	ASSERT_TRUE(parseIso8601("2026-01-01", e.updatesUntil));
	rig.provider->setEntitlement(e);
	rig.extension->refresh();
	EXPECT_EQ(rig.extension->status(), LicenseStatus::Licensed);
	// The watermarks come from the spec, with the name filled in.
	ASSERT_TRUE(rig.extension->watermarkFor(LicenseStatus::Trial).has_value());
	EXPECT_EQ(rig.extension->watermarkFor(LicenseStatus::Trial)->text, "Licensed Effect Trial");
	EXPECT_EQ(rig.extension->watermarkFor(LicenseStatus::Trial)->opacity, 0.9);
	EXPECT_EQ(rig.extension->watermarkFor(LicenseStatus::Expired)->text, "Custom");
	// A cached entitlement in the document seeds a new instance before the provider answers.
	FakeEffectHost second(manifest(spec, rig.provider.get()));
	second.fakes().persist.blobs = rig.host->fakes().persist.blobs;
	ExtensionSystem system(second);
	auto* ext = static_cast<LicensingExtension*>(system.load(std::make_unique<LicensingExtension>(nullptr, spec, kLicensed, kMenu)));
	second.post(EventId::CreateInstance);
	ASSERT_TRUE(ext->entitlement().has_value());
	EXPECT_EQ(ext->status(), LicenseStatus::Unknown);  // no provider
	EXPECT_FALSE(ext->buy());
	EXPECT_FALSE(ext->showProduct());
	EXPECT_FALSE(ext->showLicenseEntry());
	ext->deactivate();
	ext->checkForUpdates();
	// Bad dates log and leave the window unchecked; no watermarks when switched off.
	LicensingSpec off;
	off.provider = "static";
	off.releaseDate = "someday";
	off.watermarkUnlicensed = false;
	Rig quiet(off, LicenseStatus::Unlicensed);
	EXPECT_TRUE(quiet.host->fakes().logService.contains("release date"));
	EXPECT_FALSE(quiet.extension->watermarkFor(LicenseStatus::Unlicensed).has_value());
	EXPECT_FALSE(quiet.extension->watermarkFor(LicenseStatus::Trial).has_value());
}

TEST(Licensing, TrialProviderCountsDaysFromTheFirstRun)
{
	const std::filesystem::path dir = freshDirectory("trial");
	LicensingSettings settings;
	settings["directory"] = Value(dir.string());
	settings["days"] = Value(10);
	settings["now"] = Value("2026-10-15T00:00:00Z");
	TrialLicensingProvider provider(settings);
	EXPECT_EQ(provider.status("P"), LicenseStatus::Trial);
	EXPECT_NEAR(provider.remainingDays("P"), 10.0, 1e-9);
	EXPECT_TRUE(std::filesystem::exists(dir / "trial-P.json"));
	ASSERT_TRUE(provider.entitlement("P").has_value());
	EXPECT_EQ(provider.entitlement("P")->kind, LicenseKind::Trial);
	EXPECT_EQ(provider.entitlement("P")->expiresAt - provider.entitlement("P")->issuedAt, 10 * 86400);
	// A later run reads the recorded start.
	settings["now"] = Value("2026-10-26T00:00:00Z");
	TrialLicensingProvider later(settings);
	EXPECT_EQ(later.status("P"), LicenseStatus::Expired);
	EXPECT_LT(later.remainingDays("P"), 0.0);
	settings["now"] = Value("2026-10-20T00:00:00Z");
	TrialLicensingProvider midway(settings);
	EXPECT_EQ(midway.status("P"), LicenseStatus::Trial);
	EXPECT_NEAR(midway.remainingDays("P"), 5.0, 1e-9);
	// A damaged record restarts the trial; no directory keeps it in memory.
	std::ofstream(dir / "trial-Q.json") << "not json";
	EXPECT_EQ(midway.status("Q"), LicenseStatus::Trial);
	TrialLicensingProvider memory;
	EXPECT_EQ(memory.status("P"), LicenseStatus::Trial);
	memory.startObserving("P", [](LicenseStatus) {});
	memory.stopObserving("P");
	EXPECT_EQ(memory.name(), "trial");
	EXPECT_TRUE(memory.available());
	LicensingSettings badNow;
	badNow["now"] = Value("whenever");
	TrialLicensingProvider wall(badNow);
	EXPECT_EQ(wall.status("P"), LicenseStatus::Trial);
}

TEST(Licensing, FileProviderVerifiesSignedLicenseFiles)
{
	const std::filesystem::path dir = freshDirectory("file");
	LicensingSettings settings;
	settings["directory"] = Value(dir.string());
	settings["secret"] = Value("s3cret");
	settings["machineId"] = Value("MAC-1");
	settings["now"] = Value("2026-10-15T00:00:00Z");
	FileLicensingProvider provider(settings);
	EXPECT_EQ(provider.name(), "file");
	EXPECT_TRUE(provider.available());
	EXPECT_EQ(provider.status("P"), LicenseStatus::Unlicensed);
	EXPECT_FALSE(provider.entitlement("P").has_value());
	EXPECT_EQ(provider.licensePath("P"), dir / "P.license");
	LicenseEntitlement e;
	e.productId = "P";
	e.providerName = "file";
	e.status = LicenseStatus::Licensed;
	e.email = "who@example.com";
	ASSERT_TRUE(parseIso8601("2026-01-01", e.issuedAt));
	ASSERT_TRUE(FileLicensingProvider::writeLicenseFile(provider.licensePath("P"), e, 2, "s3cret").ok());
	int reported = 0;
	LicenseStatus last = LicenseStatus::Unknown;
	provider.startObserving("P", [&](LicenseStatus s) {
		++reported;
		last = s;
	});
	provider.reload("P");
	EXPECT_EQ(reported, 1);
	EXPECT_EQ(last, LicenseStatus::Licensed);
	EXPECT_EQ(provider.status("P"), LicenseStatus::Licensed);
	ASSERT_TRUE(provider.entitlement("P").has_value());
	EXPECT_EQ(provider.entitlement("P")->email, "who@example.com");
	EXPECT_EQ(provider.statusForVersion("P", 2), LicenseStatus::Licensed);
	EXPECT_EQ(provider.statusForVersion("P", 3), LicenseStatus::Unlicensed);
	EXPECT_TRUE(provider.upgradeAvailable("P"));
	provider.reload("P");
	EXPECT_EQ(reported, 1);  // unchanged: no report
	// The wrong secret, a wrong product, a wrong machine, and a tampered field read Revoked.
	LicensingSettings wrong = settings;
	wrong["secret"] = Value("other");
	EXPECT_EQ(FileLicensingProvider(wrong).status("P"), LicenseStatus::Revoked);
	ASSERT_TRUE(FileLicensingProvider::writeLicenseFile(provider.licensePath("Q"), e, 0, "s3cret").ok());
	EXPECT_EQ(provider.status("Q"), LicenseStatus::Revoked);
	LicensingSettings elsewhere = settings;
	elsewhere["machineId"] = Value("MAC-2");
	e.machineId = "MAC-1";
	ASSERT_TRUE(FileLicensingProvider::writeLicenseFile(provider.licensePath("P"), e, 0, "s3cret").ok());
	EXPECT_EQ(provider.status("P"), LicenseStatus::Licensed);
	EXPECT_EQ(FileLicensingProvider(elsewhere).status("P"), LicenseStatus::Revoked);
	EXPECT_FALSE(provider.upgradeAvailable("P"));
	{
		std::string text;
		{
			std::ifstream in(provider.licensePath("P"));
			std::getline(in, text, '\0');
		}
		const std::size_t at = text.find("who@example.com");
		ASSERT_NE(at, std::string::npos);
		text.replace(at, 3, "you");
		std::ofstream(provider.licensePath("P")) << text;
	}
	EXPECT_EQ(provider.status("P"), LicenseStatus::Revoked);
	ASSERT_TRUE(provider.entitlement("P").has_value());
	EXPECT_EQ(provider.entitlement("P")->status, LicenseStatus::Revoked);
	std::ofstream(provider.licensePath("P")) << "{ not json";
	EXPECT_EQ(provider.status("P"), LicenseStatus::Revoked);
	std::ofstream(provider.licensePath("P")) << R"({"productId":"P","providerName":"file","status":"licensed","signature":"zz"})";
	EXPECT_EQ(provider.status("P"), LicenseStatus::Revoked);
	// An expired license, a key activated in place, and a deactivation that removes the file.
	e.expiresAt = e.issuedAt + 86400;
	ASSERT_TRUE(FileLicensingProvider::writeLicenseFile(provider.licensePath("P"), e, 0, "s3cret").ok());
	EXPECT_EQ(provider.status("P"), LicenseStatus::Expired);
	e.expiresAt = 0;
	ASSERT_TRUE(FileLicensingProvider::writeLicenseFile(dir / "key.license", e, 0, "s3cret").ok());
	std::string key;
	{
		std::ifstream in(dir / "key.license");
		std::getline(in, key, '\0');
	}
	provider.activateLicenseKey(key, "P", [](LicenseStatus s, const Error& err) {
		EXPECT_EQ(s, LicenseStatus::Licensed);
		EXPECT_TRUE(err.ok());
	});
	EXPECT_EQ(last, LicenseStatus::Licensed);
	provider.deactivateLicense("P", [](LicenseStatus s, const Error& err) {
		EXPECT_EQ(s, LicenseStatus::Unlicensed);
		EXPECT_TRUE(err.ok());
	});
	EXPECT_FALSE(std::filesystem::exists(provider.licensePath("P")));
	EXPECT_EQ(last, LicenseStatus::Unlicensed);
	provider.stopObserving("P");
	EXPECT_TRUE(provider.has(LicensingCapability::Activation));
	FileLicensingProvider nowhere;
	EXPECT_EQ(nowhere.status("P"), LicenseStatus::Unlicensed);
	nowhere.activateLicenseKey("k", "P", [](LicenseStatus, const Error& err) { EXPECT_EQ(err.code(), ErrorCode::Failed); });
	nowhere.deactivateLicense("P", [](LicenseStatus s, const Error&) { EXPECT_EQ(s, LicenseStatus::Unlicensed); });
	const std::filesystem::path blocker = freshDirectory("blocker") / "file";
	std::ofstream(blocker) << "a file, not a folder";
	EXPECT_EQ(FileLicensingProvider::writeLicenseFile(blocker / "x.license", e, 0, "s").code(), ErrorCode::Failed);
}
