// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
#include <ofxgrip/ofx/KernelEffect.hpp>

#if defined(OFXGRIP_HAS_METAL)
#include <ofxgrip/gpu/MetalKernelBackend.hpp>
#endif

#include <set>

OFXGRIP_NAMESPACE_BEGIN

void registerKernelBackends()
{
#if defined(OFXGRIP_HAS_METAL)
	MetalKernelBackend::registerWithEngine();
#endif
}

const CompiledKernel::Build* CompiledKernel::build(GpuApi api) const noexcept
{
	auto it = builds.find(api);
	return it == builds.end() ? nullptr : &it->second;
}

Error KernelSet::add(const KernelRecord& record, std::string_view hostFamily, std::string_view os)
{
	CompiledKernel compiled;
	compiled.record = record;
	if (Error e = KernelMetadata::scan(record.slangSource, compiled.metadata)) {
		return e.with("kernel", record.name);
	}
	catalog_.add(record);
	const KernelRecord* stored = catalog_.find(record.name);
	for (GpuApi api : {GpuApi::None, GpuApi::Metal, GpuApi::Cuda, GpuApi::OpenCL, GpuApi::Vulkan}) {
		const KernelImplementation* implementation = catalog_.resolve(record.name, api, hostFamily, os);
		if (implementation == nullptr) {
			continue;
		}
		CompiledKernel::Build build;
		build.implementation = implementation;
		if (implementation->universal || !implementation->layoutJson.empty()) {
			build.error = KernelLayout::parse(implementation->layoutJson, build.layout);
			if (build.error.ok()) {
				build.error = KernelBindingPlan::resolve(build.layout, compiled.metadata, build.plan);
			}
		}
		compiled.builds[api] = std::move(build);
	}
	(void)stored;
	for (CompiledKernel& existing : kernels_) {
		if (existing.record.name == record.name) {
			existing = std::move(compiled);
			return Error::none();
		}
	}
	kernels_.push_back(std::move(compiled));
	return Error::none();
}

const CompiledKernel* KernelSet::find(std::string_view name) const noexcept
{
	for (const CompiledKernel& kernel : kernels_) {
		if (kernel.record.name == name) {
			return &kernel;
		}
	}
	return nullptr;
}

Error KernelSet::collectParameters(std::vector<ParameterSpec>& out) const
{
	std::set<ParameterId> ids;
	std::set<std::string> keys;
	for (const CompiledKernel& kernel : kernels_) {
		for (const ParameterSpec& spec : kernel.metadata.parameters) {
			std::vector<ParameterSpec> flat{spec};
			for (const ParameterSpec& child : spec.children) {
				flat.push_back(child);
			}
			for (const ParameterSpec& each : flat) {
				if (!ids.insert(each.id).second) {
					return Error::make(ErrorCode::InvalidManifest, "Two kernels declare the same parameter identifier").with("parameter", std::to_string(each.id)).with("kernel", kernel.record.name);
				}
				if (!keys.insert(each.key).second) {
					return Error::make(ErrorCode::InvalidManifest, "Two kernels declare the same parameter key").with("key", each.key).with("kernel", kernel.record.name);
				}
			}
			out.push_back(spec);
		}
	}
	return Error::none();
}

std::vector<ClipSpec> KernelSet::imageClips() const
{
	std::vector<ClipSpec> clips;
	for (const CompiledKernel& kernel : kernels_) {
		for (const KernelBinding& binding : kernel.metadata.bindings) {
			if (binding.type != ParameterType::ImageRef) {
				continue;
			}
			bool present = false;
			for (const ClipSpec& clip : clips) {
				present = present || clip.name == binding.key;
			}
			if (present) {
				continue;
			}
			ClipSpec clip;
			clip.name = binding.key;
			const ParameterSpec* spec = nullptr;
			for (const ParameterSpec& candidate : kernel.metadata.parameters) {
				if (candidate.id == binding.id) {
					spec = &candidate;
				}
			}
			clip.label = spec != nullptr && !spec->label.empty() ? spec->label : binding.key;
			clip.optional = true;
			clips.push_back(std::move(clip));
		}
	}
	return clips;
}

// ---- KernelEffect -------------------------------------------------------------------------------

void KernelEffect::onPrepareDispatch(KernelDispatch&, const CompiledKernel&) {}

Error KernelEffect::render(RenderContext& context)
{
	if (kernels_ == nullptr || kernels_->primary() == nullptr) {
		return Error::make(ErrorCode::InvalidManifest, "The effect has no kernel");
	}
	return dispatchKernel(context, kernels_->primary()->record.name);
}

Error KernelEffect::dispatchKernel(RenderContext& context, std::string_view name, const KernelValueMap* values)
{
	if (kernels_ == nullptr) {
		return Error::make(ErrorCode::InvalidManifest, "The effect has no kernel set");
	}
	const CompiledKernel* kernel = kernels_->find(name);
	if (kernel == nullptr) {
		return Error::make(ErrorCode::NotFound, "The effect has no such kernel").with("kernel", std::string(name));
	}
	const GpuApi api = context.outputIsGpu() ? context.outputGpu.api : GpuApi::None;
	const CompiledKernel::Build* build = kernel->build(api);
	if (build == nullptr) {
		return Error::make(ErrorCode::Unsupported, "The kernel has no build for this API").with("kernel", std::string(name)).with("api", std::string(toString(api)));
	}
	if (build->error) {
		return build->error;
	}
	const KernelValueMap& valueMap = values != nullptr ? *values : context.snapshot.values();
	const double pixelScale = context.args.renderScale.x > 0.0 ? context.args.renderScale.x : 1.0;
	KernelDispatch dispatch;
	dispatch.layout = &build->layout;
	dispatch.implementation = build->implementation;
	dispatch.plan = &build->plan;
	dispatch.cacheKey = services().identity().pluginId + "." + kernel->record.name;
	dispatch.uniforms = packKernelUniforms(build->layout, build->plan, valueMap, pixelScale);
	dispatch.constants = packKernelConstants(build->plan, valueMap, pixelScale);
	dispatch.window = context.args.renderWindow;
	dispatch.pixelScale = context.args.renderScale;
	dispatch.destination.connected = true;
	if (context.outputIsGpu()) {
		dispatch.destination.gpu = context.outputGpu;
	} else {
		dispatch.destination.cpu = context.output;
	}
	// The imageref clips bind by key; every other input is positional, in clip order.
	for (const InputImage& input : context.inputs) {
		KernelImage image;
		image.connected = input.connected;
		if (input.isGpu()) {
			image.gpu = input.gpu;
		} else {
			image.cpu = input.view;
		}
		const KernelBinding* binding = kernel->metadata.bindingForKey(input.clip);
		if (binding != nullptr && binding->type == ParameterType::ImageRef) {
			dispatch.keyedImages[binding->id] = image;
		} else {
			dispatch.sources.push_back(image);
		}
	}
	dispatch.threading = &services().threading();
	dispatch.abort = [&context]() { return context.abortRequested(); };
	onPrepareDispatch(dispatch, *kernel);
	return KernelEngine::shared().run(dispatch);
}

// ---- KernelEffectFactory ------------------------------------------------------------------------

KernelEffectFactory::KernelEffectFactory(EffectManifest base, std::vector<KernelRecord> kernels) : manifest_(std::move(base))
{
	registerKernelBackends();
	for (const KernelRecord& record : kernels) {
		if (Error e = kernels_.add(record)) {
			error_ = e;
			return;
		}
	}
	std::vector<ParameterSpec> parameters;
	if (Error e = kernels_.collectParameters(parameters)) {
		error_ = e;
		return;
	}
	for (ParameterSpec& spec : parameters) {
		manifest_.parameters.push_back(std::move(spec));
	}
	std::vector<ClipSpec> clips = manifest_.effectiveClips();
	for (ClipSpec& clip : kernels_.imageClips()) {
		bool present = false;
		for (const ClipSpec& existing : clips) {
			present = present || existing.name == clip.name;
		}
		if (!present) {
			clips.push_back(std::move(clip));
		}
	}
	manifest_.clips = std::move(clips);
	// The host sees only the paths on which every kernel resolves.
	Capabilities allowed = manifest_.capabilities;
	allowed.metalRender = true;
	allowed.cudaRender = true;
	allowed.openCLRender = true;
	allowed.cpuRender = true;
	manifest_.capabilities = kernels_.catalog().capabilitiesFor(allowed, "", currentOsName());
	manifest_.capabilities.openGLRender = false;
	if (Error e = manifest_.validate()) {
		error_ = e;
	}
}

void KernelEffectFactory::onLoad(const HostProfile&, LogService& log)
{
	if (error_) {
		log.error("The kernel effect's manifest is invalid: " + error_.describe());
	}
	for (const CompiledKernel& kernel : kernels_.kernels()) {
		for (const auto& [api, build] : kernel.builds) {
			if (build.error) {
				log.error("Kernel " + kernel.record.name + " on " + std::string(toString(api)) + ": " + build.error.describe());
			}
		}
	}
}

std::unique_ptr<Effect> KernelEffectFactory::finish(std::unique_ptr<KernelEffect> effect)
{
	if (effect) {
		effect->setKernels(&kernels_);
	}
	return effect;
}

OFXGRIP_NAMESPACE_END
