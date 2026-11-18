// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
#include <ofxgrip/ofx/GlFunctions.hpp>

#if defined(__APPLE__)
#define GL_SILENCE_DEPRECATION 1
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#elif defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <GL/gl.h>
#else
#include <dlfcn.h>
#endif

OFXGRIP_NAMESPACE_BEGIN

bool GlFunctions::complete() const noexcept
{
	return begin != nullptr && end != nullptr && vertex2d != nullptr && color4f != nullptr && lineWidth != nullptr && enable != nullptr && disable != nullptr && lineStipple != nullptr && matrixMode != nullptr && hasCurrentContext != nullptr;
}

namespace {

#if defined(__APPLE__) || defined(_WIN32)

static_assert(gl::kLines == GL_LINES && gl::kLineLoop == GL_LINE_LOOP && gl::kLineStrip == GL_LINE_STRIP);
static_assert(gl::kQuads == GL_QUADS && gl::kPolygon == GL_POLYGON);
static_assert(gl::kLineStipple == GL_LINE_STIPPLE && gl::kModelView == GL_MODELVIEW);

bool fill(GlFunctions& table) noexcept
{
	table.begin = [](unsigned mode) { ::glBegin(mode); };
	table.end = []() { ::glEnd(); };
	table.vertex2d = [](double x, double y) { ::glVertex2d(x, y); };
	table.color4f = [](float red, float green, float blue, float alpha) { ::glColor4f(red, green, blue, alpha); };
	table.lineWidth = [](float width) { ::glLineWidth(width); };
	table.enable = [](unsigned capability) { ::glEnable(capability); };
	table.disable = [](unsigned capability) { ::glDisable(capability); };
	table.lineStipple = [](int factor, unsigned short pattern) { ::glLineStipple(factor, pattern); };
	table.matrixMode = [](unsigned mode) { ::glMatrixMode(mode); };
#if defined(__APPLE__)
	table.hasCurrentContext = []() { return ::CGLGetCurrentContext() != nullptr; };
#else
	table.hasCurrentContext = []() { return ::wglGetCurrentContext() != nullptr; };
#endif
	return true;
}

#else

using CurrentContextFn = void* (*)();

// A GLX context and an EGL context are separate; a host has one or the other.
CurrentContextFn glxCurrentContext = nullptr;
CurrentContextFn eglCurrentContext = nullptr;

/*! The library when the process has it loaded already; it is never loaded by this call. */
void* loadedLibrary(const char* name) noexcept
{
#if defined(RTLD_NOLOAD)
	return ::dlopen(name, RTLD_LAZY | RTLD_LOCAL | RTLD_NOLOAD);
#else
	(void)name;
	return nullptr;
#endif
}

template <typename F>
void resolve(void* library, const char* name, F& out) noexcept
{
	out = reinterpret_cast<F>(::dlsym(library, name));
}

bool fill(GlFunctions& table) noexcept
{
	// libGL.so.1 is the classic library and carries GLX. libOpenGL.so.0 is the vendor-neutral
	// dispatch library without a window system.
	static constexpr const char* kNames[] = {"libGL.so.1", "libOpenGL.so.0"};
	void* library = nullptr;
	for (const char* name : kNames) {
		library = loadedLibrary(name);
		if (library != nullptr) {
			break;
		}
	}
	for (const char* name : kNames) {
		if (library != nullptr) {
			break;
		}
		library = ::dlopen(name, RTLD_LAZY | RTLD_LOCAL);
	}
	if (library == nullptr) {
		return false;
	}
	resolve(library, "glBegin", table.begin);
	resolve(library, "glEnd", table.end);
	resolve(library, "glVertex2d", table.vertex2d);
	resolve(library, "glColor4f", table.color4f);
	resolve(library, "glLineWidth", table.lineWidth);
	resolve(library, "glEnable", table.enable);
	resolve(library, "glDisable", table.disable);
	resolve(library, "glLineStipple", table.lineStipple);
	resolve(library, "glMatrixMode", table.matrixMode);

	resolve(library, "glXGetCurrentContext", glxCurrentContext);
	if (glxCurrentContext == nullptr) {
		if (void* glx = loadedLibrary("libGLX.so.0"); glx != nullptr) {
			resolve(glx, "glXGetCurrentContext", glxCurrentContext);
		}
	}
	if (void* egl = loadedLibrary("libEGL.so.1"); egl != nullptr) {
		resolve(egl, "eglGetCurrentContext", eglCurrentContext);
	}
	table.hasCurrentContext = []() {
		return (glxCurrentContext != nullptr && glxCurrentContext() != nullptr) || (eglCurrentContext != nullptr && eglCurrentContext() != nullptr);
	};
	return table.complete();
}

#endif

}  // namespace

const GlFunctions* GlFunctions::system() noexcept
{
	static const GlFunctions* shared = []() -> const GlFunctions* {
		static GlFunctions table;
		return fill(table) ? &table : nullptr;
	}();
	return shared;
}

OFXGRIP_NAMESPACE_END
