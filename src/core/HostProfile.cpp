// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
#include <ofxgrip/core/HostProfile.hpp>

#include <sstream>

OFXGRIP_NAMESPACE_BEGIN

std::string_view toString(HostFamily family) noexcept
{
	switch (family) {
	case HostFamily::Unknown: return "unknown";
	case HostFamily::Resolve: return "resolve";
	case HostFamily::Fusion: return "fusion";
	case HostFamily::Nuke: return "nuke";
	case HostFamily::Natron: return "natron";
	case HostFamily::Vegas: return "vegas";
	case HostFamily::Catalyst: return "catalyst";
	case HostFamily::Flame: return "flame";
	case HostFamily::Baselight: return "baselight";
	case HostFamily::Scratch: return "scratch";
	case HostFamily::Tuttle: return "tuttle";
	case HostFamily::OfxGripMiniHost: return "minihost";
	}
	return "unknown";
}

namespace {

bool startsWith(std::string_view text, std::string_view prefix)
{
	return text.size() >= prefix.size() && text.substr(0, prefix.size()) == prefix;
}

bool contains(std::string_view text, std::string_view piece)
{
	return text.find(piece) != std::string_view::npos;
}

}  // namespace

void HostProfile::deriveIdentity()
{
	family = HostFamily::Unknown;
	edition = HostEdition::Unknown;
	if (startsWith(hostName, "DaVinciResolve")) {
		family = HostFamily::Resolve;
		// The free edition reported "DaVinciResolveLite" / "DaVinci Resolve Lite" in the 12.5 and
		// 16 dumps. Whether current releases still do is Q-RESOLVE-001; a plain name is not taken
		// as Studio until the probe confirms it.
		if (contains(hostName, "Lite") || contains(hostLabel, "Lite")) {
			edition = HostEdition::Free;
		}
	} else if (startsWith(hostName, "com.blackmagicdesign.Fusion") || startsWith(hostName, "com.eyeonline.Fusion") || contains(hostName, "Fusion")) {
		family = HostFamily::Fusion;
	} else if (startsWith(hostName, "uk.co.thefoundry.nuke")) {
		family = HostFamily::Nuke;
	} else if (startsWith(hostName, "fr.inria.Natron")) {
		family = HostFamily::Natron;
	} else if (contains(hostName, "Vegas") || contains(hostName, "vegas")) {
		family = HostFamily::Vegas;
	} else if (contains(hostName, "Catalyst")) {
		family = HostFamily::Catalyst;
	} else if (contains(hostName, "flame") || contains(hostName, "Flame")) {
		family = HostFamily::Flame;
	} else if (contains(hostName, "Baselight") || contains(hostName, "baselight")) {
		family = HostFamily::Baselight;
	} else if (contains(hostName, "Scratch") || contains(hostName, "scratch")) {
		family = HostFamily::Scratch;
	} else if (startsWith(hostName, "TuttleOfx") || startsWith(hostName, "fr.tuttleofx")) {
		family = HostFamily::Tuttle;
	} else if (startsWith(hostName, "com.belisoful.ofxgrip.minihost")) {
		family = HostFamily::OfxGripMiniHost;
	}
}

bool HostProfile::probed(Quirk quirk, bool& result) const noexcept
{
	switch (quirk) {
	case Quirk::NoTilesNoMultiResolution:
		result = !capabilities.supportsTiles && !capabilities.supportsMultiResolution;
		return true;
	case Quirk::NoCustomParamInteract:
		result = !capabilities.supportsCustomInteract;
		return true;
	case Quirk::NoParametricParams:
		result = !suites.parametric;
		return true;
	case Quirk::NullThreadSuite:
		result = !suites.multiThread;
		return true;
	case Quirk::MessageSuiteV2Broken:
		// A validated V2 suite overrides the table; an unvalidated one keeps the table's answer.
		if (suites.messageV2) {
			result = false;
			return true;
		}
		return false;
	case Quirk::OpenGLRenderAdvertisedNotUsed:
		if (!capabilities.openGLRender && !suites.openGLRender) {
			result = false;
			return true;
		}
		return false;
	default:
		return false;
	}
}

bool HostProfile::has(Quirk quirk) const noexcept
{
	if (auto it = overrides_.find(quirk); it != overrides_.end()) {
		return it->second;
	}
	bool probedResult = false;
	if (probed(quirk, probedResult)) {
		return probedResult;
	}
	return quirkMatchesHost(quirk, hostName, hostVersion);
}

void HostProfile::override(Quirk quirk, bool on)
{
	overrides_[quirk] = on;
}

void HostProfile::clearOverride(Quirk quirk)
{
	overrides_.erase(quirk);
}

std::vector<Quirk> HostProfile::activeQuirks() const
{
	std::vector<Quirk> active;
	for (const QuirkInfo& info : quirkRegistry()) {
		if (has(info.key)) {
			active.push_back(info.key);
		}
	}
	return active;
}

std::string HostProfile::describe() const
{
	std::ostringstream out;
	out << "host=" << hostName << " label=\"" << hostLabel << "\" version=" << hostVersion.toString() << " versionLabel=\"" << hostVersionLabel
	    << "\" api=" << apiVersionMajor << "." << apiVersionMinor << " family=" << toString(family);
	switch (edition) {
	case HostEdition::Free: out << " edition=free"; break;
	case HostEdition::Studio: out << " edition=studio"; break;
	case HostEdition::Unknown: out << " edition=unknown"; break;
	}
	out << "\nsuites: property=" << suites.property << " imageEffect=" << suites.imageEffect << " parameter=" << suites.parameter
	    << " memory=" << suites.memory << " multiThread=" << suites.multiThread << " messageV1=" << suites.messageV1
	    << " messageV2=" << suites.messageV2 << " messageV2Vended=" << suites.messageV2Vended << " progressV1=" << suites.progressV1 << " progressV2=" << suites.progressV2
	    << " progressV2Vended=" << suites.progressV2Vended
	    << " timeLine=" << suites.timeLine << " parametric=" << suites.parametric << " openGLRender=" << suites.openGLRender
	    << " draw=" << suites.draw << " interact=" << suites.interact << " dialog=" << suites.dialog;
	out << "\ncapabilities: tiles=" << capabilities.supportsTiles << " multiRes=" << capabilities.supportsMultiResolution
	    << " multiDepth=" << capabilities.supportsMultipleClipDepths << " multiPAR=" << capabilities.supportsMultipleClipPARs
	    << " overlays=" << capabilities.supportsOverlays << " customInteract=" << capabilities.supportsCustomInteract
	    << " choiceAnim=" << capabilities.supportsChoiceAnimation << " boolAnim=" << capabilities.supportsBooleanAnimation
	    << " stringAnim=" << capabilities.supportsStringAnimation << " customAnim=" << capabilities.supportsCustomAnimation
	    << " strChoice=" << capabilities.supportsStrChoice << " maxParams=" << capabilities.maxParameters
	    << " maxPages=" << capabilities.maxPages << " metal=" << capabilities.metalRender << " cuda=" << capabilities.cudaRender
	    << " opencl=" << capabilities.openCLRender << " opengl=" << capabilities.openGLRender
	    << " nativeOrigin=" << (capabilities.nativeOrigin.empty() ? std::string("<missing>") : capabilities.nativeOrigin)
	    << " contexts=" << capabilities.supportedContexts.size() << " components=" << capabilities.supportedComponents.size() << " depths=" << capabilities.supportedPixelDepths.size();
	for (const std::string& c : capabilities.supportedContexts) {
		out << " " << c;
	}
	for (const std::string& c : capabilities.supportedComponents) {
		out << " " << c;
	}
	for (const std::string& d : capabilities.supportedPixelDepths) {
		out << " " << d;
	}
	out << "\nquirks:";
	for (Quirk q : activeQuirks()) {
		const QuirkInfo& info = quirkInfo(q);
		out << " " << info.registryId << "(" << info.name << ")";
	}
	return out.str();
}

OFXGRIP_NAMESPACE_END
