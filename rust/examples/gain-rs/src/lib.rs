// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
//! The OfxGrip gain effect in Rust: a master gain, an optional per-channel group revealed by a
//! visibility rule, and a clamp choice. The manifest, the snapshot, the visibility rigging, the
//! presets, and the host quirk handling all come from the framework.
#![allow(non_snake_case)]

use ofxgrip::{bundle, Capabilities, Depth, Effect, Kind, Manifest, MessageKind, RenderContext, Result, Services, Snapshot};

const GAIN: u32 = 100;
const PER_CHANNEL: u32 = 101;
const CHANNELS: u32 = 110;
const GAIN_R: u32 = 111;
const GAIN_G: u32 = 112;
const GAIN_B: u32 = 113;
const GAIN_A: u32 = 114;
const CLAMP: u32 = 120;
const ABOUT: u32 = 900;

struct Gain;

fn gains(snapshot: &Snapshot) -> [f32; 4] {
    let master = snapshot.number(GAIN, 1.0) as f32;
    if snapshot.boolean(PER_CHANNEL, false) {
        [
            master * snapshot.number(GAIN_R, 1.0) as f32,
            master * snapshot.number(GAIN_G, 1.0) as f32,
            master * snapshot.number(GAIN_B, 1.0) as f32,
            master * snapshot.number(GAIN_A, 1.0) as f32,
        ]
    } else {
        [master, master, master, 1.0]
    }
}

impl Effect for Gain {
    fn parameter_clicked(&mut self, services: &mut Services, parameter: u32) -> Result<()> {
        if parameter == ABOUT {
            services.message(MessageKind::Message, "about", "OfxGrip Gain in Rust. MIT License.")?;
        }
        Ok(())
    }

    fn is_identity(&mut self, snapshot: &Snapshot, _time: f64) -> Result<bool> {
        Ok(snapshot.int(CLAMP, 0) == 0 && gains(snapshot) == [1.0, 1.0, 1.0, 1.0])
    }

    fn render(&mut self, context: &mut RenderContext) -> Result<()> {
        let output = context.output();
        if output.is_gpu() {
            return Err(ofxgrip::Error::with_status(ofxgrip::sys::OFXGRIP_UNSUPPORTED, "GPU frames are not handled by the Rust sample"));
        }
        let snapshot = context.snapshot();
        let gains = gains(&snapshot);
        let clamp = snapshot.int(CLAMP, 0) == 1;
        let source = context.source().filter(|input| input.connected);
        let args = context.args();
        let (x1, y1, x2, y2) = args.window;
        let components = output.components().count();
        for y in y1..y2 {
            if context.abort_requested() {
                return Err(ofxgrip::Error::cancelled());
            }
            for x in x1..x2 {
                for c in 0..components {
                    let gain_index = if components == 1 { 3 } else { c };
                    let value = match &source {
                        Some(input) => {
                            let v = input.image.read(x, y, c) * gains[gain_index];
                            if clamp {
                                v.clamp(0.0, 1.0)
                            } else {
                                v
                            }
                        }
                        None => 0.0,
                    };
                    output.write(x, y, c, value);
                }
            }
        }
        Ok(())
    }
}

fn register() {
    let manifest = Manifest::new("com.belisoful.ofxgrip.gainrs", "OfxGrip Gain (Rust)")
        .and_then(|m| m.identity("5B7F0E1D-2C3A-4B4C-9D5E-6F7A8B9C0D1E", "OfxGrip", "Multiplies each channel by a gain. The OfxGrip sample effect in Rust.", Kind::Filter))
        .and_then(|m| m.author("Belisoful", "ofxgrip"))
        .and_then(|m| m.version(1, 1, 0))
        .and_then(|m| m.capabilities(Capabilities { no_spatial_awareness: true, depths: &[Depth::UInt8, Depth::UInt16, Depth::Half, Depth::Float], ..Capabilities::default() }))
        .and_then(|m| m.float(GAIN, "gain", "Gain", 1.0, 0.0, 10.0))
        .and_then(|m| m.display_range(GAIN, 0.0, 4.0))
        .and_then(|m| m.hint(GAIN, "Multiplies every channel."))
        .and_then(|m| m.tags(GAIN, "gain"))
        .and_then(|m| m.boolean(PER_CHANNEL, "perChannel", "Per-channel gains", false))
        .and_then(|m| m.hint(PER_CHANNEL, "Reveals one gain per channel."))
        .and_then(|m| m.group(CHANNELS, "channels", "Channels", false))
        .and_then(|m| m.flags(CHANNELS, "hidden"))
        .and_then(|m| m.visible_when(CHANNELS, "perChannel"))
        .and_then(|m| m.float(GAIN_R, "gainR", "Red", 1.0, 0.0, 10.0))
        .and_then(|m| m.parent(GAIN_R, CHANNELS))
        .and_then(|m| m.float(GAIN_G, "gainG", "Green", 1.0, 0.0, 10.0))
        .and_then(|m| m.parent(GAIN_G, CHANNELS))
        .and_then(|m| m.float(GAIN_B, "gainB", "Blue", 1.0, 0.0, 10.0))
        .and_then(|m| m.parent(GAIN_B, CHANNELS))
        .and_then(|m| m.float(GAIN_A, "gainA", "Alpha", 1.0, 0.0, 10.0))
        .and_then(|m| m.parent(GAIN_A, CHANNELS))
        .and_then(|m| m.choice(CLAMP, "clamp", "Clamp", &[("none", "None"), ("unit", "0 to 1")], 0))
        .and_then(|m| m.hint(CLAMP, "Limits the result."))
        .and_then(|m| m.button(ABOUT, "about", "About OfxGrip Gain (Rust)"));
    match manifest {
        Ok(manifest) => {
            let _ = ofxgrip::register(manifest, |_services| Gain);
        }
        Err(_) => {}
    }
}

bundle!(register);

// `Manifest::new` is infallible; the chain above starts from a `Result` for uniformity.
trait IntoResult {
    fn and_then<F: FnOnce(Manifest) -> Result<Manifest>>(self, f: F) -> Result<Manifest>;
}

impl IntoResult for Manifest {
    fn and_then<F: FnOnce(Manifest) -> Result<Manifest>>(self, f: F) -> Result<Manifest> {
        f(self)
    }
}
