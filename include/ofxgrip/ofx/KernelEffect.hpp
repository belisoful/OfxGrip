// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
/*!
	@file  KernelEffect.hpp
	@brief The effect base whose parameters, clips, capabilities, and render come from Slang
	       kernels built against the GripKernel module.

	Introduced in OfxGrip 0.4.0. This is FxGrip's `FxGripKernelEffect` on OpenFX. A factory
	takes a base manifest and the kernels `ofxgrip_add_kernels` embedded, scans each kernel's
	metadata block for its parameters, adds a clip for each `imageref` record, declares to the
	host the GPU paths on which every kernel resolves, and parses each build's reflection layout
	once. A render packs the snapshot's values, binds the clips, and dispatches the primary
	kernel on the frame's backend through `KernelEngine`.
*/
#pragma once

#include <ofxgrip/core/KernelCatalog.hpp>
#include <ofxgrip/core/KernelLayout.hpp>
#include <ofxgrip/core/KernelMetadata.hpp>
#include <ofxgrip/core/KernelValues.hpp>
#include <ofxgrip/gpu/KernelCpu.hpp>
#include <ofxgrip/gpu/KernelEngine.hpp>
#include <ofxgrip/ofx/Effect.hpp>

#include <map>
#include <memory>
#include <string>
#include <vector>

OFXGRIP_NAMESPACE_BEGIN

/*! One kernel with its metadata and a parsed build per API. */
struct CompiledKernel {
	struct Build {
		const KernelImplementation* implementation{nullptr};
		KernelLayout layout;
		KernelBindingPlan plan;
		Error error;  /*!< Set when the layout or the plan failed; renders on this API fail with it. */
	};
	KernelRecord record;
	KernelMetadata metadata;
	std::map<GpuApi, Build> builds;
	/*! The build for an API, or null. */
	const Build* build(GpuApi api) const noexcept;
};

/*! The kernels of one effect, shared by every instance. */
class KernelSet {
public:
	/*! Adds a kernel and parses its builds for every API it has an implementation for on this
	    host family and OS. The first kernel is the primary kernel. */
	Error add(const KernelRecord& record, std::string_view hostFamily = {}, std::string_view os = currentOsName());
	const std::vector<CompiledKernel>& kernels() const noexcept { return kernels_; }
	const CompiledKernel* find(std::string_view name) const noexcept;
	const CompiledKernel* primary() const noexcept { return kernels_.empty() ? nullptr : &kernels_.front(); }
	const KernelCatalog& catalog() const noexcept { return catalog_; }
	/*! The parameters every kernel declares, in kernel order. Fails on a duplicate ID or key. */
	Error collectParameters(std::vector<ParameterSpec>& out) const;
	/*! The clips the `imageref` records declare, named by their keys. */
	std::vector<ClipSpec> imageClips() const;

private:
	KernelCatalog catalog_;
	std::vector<CompiledKernel> kernels_;
};

/*! The effect. A subclass can override `render` to run several kernels through
    `dispatchKernel`, or `onPrepareDispatch` to adjust one dispatch. */
class KernelEffect : public Effect {
public:
	/*! Renders the primary kernel. */
	Error render(RenderContext& context) override;
	/*! Runs one kernel over the render context. `values` replaces the snapshot's values when
	    given. The destination is the context's output; the sources are the context's inputs in
	    clip order, with the `imageref` clips bound by key. */
	Error dispatchKernel(RenderContext& context, std::string_view kernel, const KernelValueMap* values = nullptr);
	/*! Edits a dispatch before it runs. The default does nothing. */
	virtual void onPrepareDispatch(KernelDispatch& dispatch, const CompiledKernel& kernel);
	const KernelSet* kernels() const noexcept { return kernels_; }
	/*! Set by the factory before the instance attaches. */
	void setKernels(const KernelSet* kernels) noexcept { kernels_ = kernels; }

private:
	const KernelSet* kernels_{nullptr};
};

/*! The factory: builds the manifest from a base and the kernels, and creates instances. */
class KernelEffectFactory : public EffectFactory {
public:
	/*! `base` carries the identity, version, base clips, and parameters the kernels do not
	    declare. The kernel parameters append after them. */
	KernelEffectFactory(EffectManifest base, std::vector<KernelRecord> kernels);
	const EffectManifest& manifest() const override { return manifest_; }
	/*! The manifest assembly's failure, if any. Describe reports it. */
	const Error& error() const noexcept { return error_; }
	const KernelSet& kernels() const noexcept { return kernels_; }
	void onLoad(const HostProfile& profile, LogService& log) override;

protected:
	/*! Sets the kernel set on a fresh instance. Subclasses call it from `create`. */
	std::unique_ptr<Effect> finish(std::unique_ptr<KernelEffect> effect);

private:
	EffectManifest manifest_;
	KernelSet kernels_;
	Error error_;
};

/*! A factory that default-constructs a kernel effect class. */
template <typename E>
class KernelEffectFactoryFor final : public KernelEffectFactory {
public:
	KernelEffectFactoryFor(EffectManifest base, std::vector<KernelRecord> kernels) : KernelEffectFactory(std::move(base), std::move(kernels)) {}
	std::unique_ptr<Effect> create() override { return finish(std::make_unique<E>()); }
};

/*! Registers the GPU kernel backends this build links with the engine. Idempotent. */
void registerKernelBackends();

OFXGRIP_NAMESPACE_END
