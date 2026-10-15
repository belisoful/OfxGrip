// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
#include <ofxgrip/ofx/Effect.hpp>

#include <algorithm>

OFXGRIP_NAMESPACE_BEGIN

const InputImage* RenderContext::input(std::string_view clip) const noexcept
{
	for (const InputImage& image : inputs) {
		if (image.clip == clip) {
			return &image;
		}
	}
	return nullptr;
}

Error RenderContext::forEachBand(const std::function<void(const RectI& band)>& work, unsigned bandCount)
{
	ThreadingService& threading = services.threading();
	if (bandCount == 0) {
		bandCount = std::max(1u, threading.cpuCount() * 2);
	}
	const RectI window = args.renderWindow.empty() ? output.desc.bounds : args.renderWindow.intersect(output.desc.bounds);
	return processBands(
	    window, bandCount,
	    [&threading](unsigned count, const std::function<void(unsigned)>& fn) { threading.parallelFor(count, [&fn](unsigned index, unsigned) { fn(index); }); },
	    [this]() { return abortRequested(); }, work);
}

Effect::Effect() : bus_(std::make_unique<EventBus>()), extensions_(std::make_unique<ExtensionSystem>(*this)) {}

Effect::~Effect()
{
	extensions_->unloadAll();
	metaExtension_ = nullptr;
	visibilityExtension_ = nullptr;
	versioningExtension_ = nullptr;
}

HostServices& Effect::services()
{
	if (!services_) {
		throw OfxGripException(Error::make(ErrorCode::BadHandle, "The effect is not attached to a host"));
	}
	return *services_;
}

const EffectManifest& Effect::manifest() const
{
	return services_ ? services_->manifest() : emptyManifest_;
}

void Effect::loadExtensions(ExtensionSystem&) {}
Error Effect::onCreate()
{
	return Error::none();
}
Error Effect::onMigrate(unsigned, unsigned, Version)
{
	return Error::none();
}
Error Effect::onInstanceChanged(const ChangeEvent&)
{
	return Error::none();
}
Error Effect::onParameterClicked(ParameterId)
{
	return Error::none();
}
Error Effect::onBuildSnapshot(RenderSnapshotBuilder&, const RenderInfo&)
{
	return Error::none();
}
Error Effect::onRegionOfDefinition(const RegionArgs&, const RenderSnapshot&, RectD&, bool& handled)
{
	handled = false;
	return Error::none();
}
Error Effect::onRegionsOfInterest(const RegionArgs&, const RenderSnapshot&, std::map<std::string, RectD>&, bool& handled)
{
	handled = false;
	return Error::none();
}
Error Effect::onIsIdentity(const IsIdentityArgs&, const RenderSnapshot&, std::string&, Time&, bool& identity)
{
	identity = false;
	return Error::none();
}
Error Effect::onClipPreferences(ClipPreferences&, bool& handled)
{
	handled = false;
	return Error::none();
}
Error Effect::onBeginSequenceRender(const SequenceRenderArgs&)
{
	return Error::none();
}
Error Effect::onEndSequenceRender(const SequenceRenderArgs&)
{
	return Error::none();
}

void Effect::attach(std::unique_ptr<OfxHostServices> services, const CreateInstanceArgs& args)
{
	services_ = std::move(services);
	services_->setResolvePage(args.resolvePage);
	parameters_ = std::make_unique<ParameterModel>(services_->parameters());
	loadFrameworkExtensions();
}

void Effect::loadFrameworkExtensions()
{
	const EffectManifest& m = services_->manifest();
	auto enabled = [&m](const char* property) {
		auto it = m.properties.find(property);
		return it == m.properties.end() || it->second.toBool(true);
	};
	versioningExtension_ = static_cast<VersioningExtension*>(
	    extensions_->load(std::make_unique<VersioningExtension>([this](unsigned from, unsigned to, Version fromVersion) { return onMigrate(from, to, fromVersion); })));
	if (enabled("manageMeta")) {
		metaExtension_ = static_cast<MetaExtension*>(extensions_->load(std::make_unique<MetaExtension>()));
		if (metaExtension_ != nullptr) {
			parameters_->setMetaManager(&metaExtension_->manager());
		}
	}
	if (enabled("manageVisibility")) {
		visibilityExtension_ = static_cast<VisibilityExtension*>(extensions_->load(std::make_unique<VisibilityExtension>()));
	}
}

ParameterModel& Effect::parameters()
{
	if (!parameters_) {
		throw OfxGripException(Error::make(ErrorCode::BadHandle, "The effect is not attached to a host"));
	}
	return *parameters_;
}

Parameter* Effect::param(ParameterId id)
{
	return parameters_ ? parameters_->find(id) : nullptr;
}

Parameter* Effect::param(std::string_view key)
{
	return parameters_ ? parameters_->find(key) : nullptr;
}

MetaManager* Effect::meta()
{
	return metaExtension_ == nullptr ? nullptr : &metaExtension_->manager();
}

RenderSnapshot Effect::buildSnapshot(const RenderInfo& info)
{
	RenderSnapshotBuilder builder;
	RenderInfo full = info;
	full.resolvePage = services_->resolvePage();
	builder.setInfo(full);
	ParameterService& params = services_->parameters();
	for (const auto& [id, binding] : services_->bindings()) {
		if (!binding.spec.hasState()) {
			continue;
		}
		Value value;
		if (params.get(id, info.time, value).ok()) {
			builder.set(id, std::move(value));
		}
	}
	builder.setLicense(LicenseState::Unknown);
	onBuildSnapshot(builder, full);
	SnapshotEvent event(builder);
	bus_->post(event);
	return builder.build();
}

Error Effect::flush()
{
	PlainEvent event(EventId::Flush);
	Error e = bus_->post(event);
	Error commit = services_->persistence().commit();
	return e ? e : commit;
}

OFXGRIP_NAMESPACE_END
