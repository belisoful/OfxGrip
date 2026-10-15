// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
//! Links the OfxGrip static libraries into the crate.
//!
//! Two sources, in order:
//! 1. `OFXGRIP_BUILD_DIR`: a configured and built CMake build directory of this repository
//!    (`cmake --preset debug && cmake --build --preset debug`). The libraries are taken from its
//!    `src/` folder. CMake's own Rust integration sets this.
//! 2. Otherwise the crate builds the C++ libraries itself with CMake from the repository root
//!    (`../..`), tests, plug-ins, and the mini host switched off.

use std::env;
use std::path::{Path, PathBuf};

fn library_dir() -> PathBuf {
    if let Ok(dir) = env::var("OFXGRIP_BUILD_DIR") {
        let src = Path::new(&dir).join("src");
        println!("cargo:rerun-if-env-changed=OFXGRIP_BUILD_DIR");
        println!("cargo:rerun-if-changed={}", src.display());
        return src;
    }
    let root = Path::new(env!("CARGO_MANIFEST_DIR")).join("..").join("..");
    let mut config = cmake::Config::new(&root);
    config
        .define("OFXGRIP_BUILD_TESTS", "OFF")
        .define("OFXGRIP_BUILD_PLUGINS", "OFF")
        .define("OFXGRIP_BUILD_MINIHOST", "OFF")
        .define("OFXGRIP_WARNINGS_AS_ERRORS", "OFF")
        .profile("Release")
        .build_target("ofxgrip_c");
    if env::var_os("CMAKE_GENERATOR").is_none() && which("ninja") {
        config.generator("Ninja");
    }
    let dst = config.build();
    dst.join("build").join("src")
}

fn which(program: &str) -> bool {
    env::var_os("PATH")
        .map(|paths| env::split_paths(&paths).any(|p| p.join(program).is_file()))
        .unwrap_or(false)
}

fn main() {
    let dir = library_dir();
    println!("cargo:rustc-link-search=native={}", dir.display());
    println!("cargo:rustc-link-lib=static=ofxgrip_c");
    println!("cargo:rustc-link-lib=static=ofxgrip");
    println!("cargo:rustc-link-lib=static=ofxgrip_kernels");
    println!("cargo:rustc-link-lib=static=ofxgrip_core");
    // The optional backends the adapter was built with. CMake's Rust integration sets these;
    // a crate-driven build reads what its own CMake run produced.
    println!("cargo:rerun-if-env-changed=OFXGRIP_LINK_OPENCL");
    println!("cargo:rerun-if-env-changed=OFXGRIP_LINK_OPENGL");
    let target = env::var("TARGET").unwrap_or_default();
    let has_opencl = env::var("OFXGRIP_LINK_OPENCL").map(|v| v == "1").unwrap_or(target.contains("apple")) || dir.join("libofxgrip_gpu_opencl.a").exists();
    let has_opengl = env::var("OFXGRIP_LINK_OPENGL").map(|v| v == "1").unwrap_or(target.contains("apple"));
    if has_opencl {
        println!("cargo:rustc-link-lib=static=ofxgrip_gpu_opencl");
    }
    if target.contains("apple") {
        println!("cargo:rustc-link-lib=static=ofxgrip_gpu_metal");
        println!("cargo:rustc-link-lib=framework=Metal");
        println!("cargo:rustc-link-lib=framework=Foundation");
        println!("cargo:rustc-link-lib=framework=CoreFoundation");
        println!("cargo:rustc-link-lib=framework=OpenGL");
        if has_opencl {
            println!("cargo:rustc-link-lib=framework=OpenCL");
        }
        println!("cargo:rustc-link-lib=dylib=c++");
    } else if target.contains("windows") {
        // The MSVC runtime is linked by the static libraries' object files.
        if has_opengl {
            println!("cargo:rustc-link-lib=dylib=opengl32");
        }
        if has_opencl {
            println!("cargo:rustc-link-lib=dylib=OpenCL");
        }
    } else {
        if has_opengl {
            println!("cargo:rustc-link-lib=dylib=GL");
        }
        if has_opencl {
            println!("cargo:rustc-link-lib=dylib=OpenCL");
        }
        println!("cargo:rustc-link-lib=dylib=stdc++");
        println!("cargo:rustc-link-lib=dylib=pthread");
        println!("cargo:rustc-link-lib=dylib=dl");
    }
}
