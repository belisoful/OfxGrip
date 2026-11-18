// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
/*!
	@file  GlFunctions.hpp
	@brief The OpenGL functions the overlay fallback calls, as a table.

	Introduced in OfxGrip 0.5.0. `GlDrawKit` draws through this table. `system()` fills it from
	the platform's OpenGL. macOS and Windows ship OpenGL with the system, so the adapter links
	it there. Linux has no such guarantee, so the library is opened at run time (`libGL.so.1`,
	then `libOpenGL.so.0`) and a plug-in loads on a machine without OpenGL. A host that draws
	overlays with OpenGL has the library loaded, and the open finds that copy. A test passes its
	own table.

	The parameter types are the C types OpenGL's interface fixes (`GLenum` is `unsigned`,
	`GLushort` is `unsigned short`); no OpenGL header is included.
*/
#pragma once

#include <ofxgrip/core/Types.hpp>

OFXGRIP_NAMESPACE_BEGIN

/*! The OpenGL enumerants the overlay fallback passes. */
namespace gl {
inline constexpr unsigned kLines = 0x0001;
inline constexpr unsigned kLineLoop = 0x0002;
inline constexpr unsigned kLineStrip = 0x0003;
inline constexpr unsigned kQuads = 0x0007;
inline constexpr unsigned kPolygon = 0x0009;
inline constexpr unsigned kLineStipple = 0x0B24;
inline constexpr unsigned kModelView = 0x1700;
}  // namespace gl

/*! The OpenGL 2.1 fixed-function calls of the overlay fallback. */
struct GlFunctions {
	void (*begin)(unsigned mode){nullptr};
	void (*end)(){nullptr};
	void (*vertex2d)(double x, double y){nullptr};
	void (*color4f)(float red, float green, float blue, float alpha){nullptr};
	void (*lineWidth)(float width){nullptr};
	void (*enable)(unsigned capability){nullptr};
	void (*disable)(unsigned capability){nullptr};
	void (*lineStipple)(int factor, unsigned short pattern){nullptr};
	void (*matrixMode)(unsigned mode){nullptr};
	/*! Whether an OpenGL context is current on the calling thread. */
	bool (*hasCurrentContext)(){nullptr};

	/*! Whether every function is set. */
	bool complete() const noexcept;
	/*! The platform's OpenGL, or null when the process cannot reach one. The outcome of the
	    first call is kept. */
	static const GlFunctions* system() noexcept;
};

OFXGRIP_NAMESPACE_END
