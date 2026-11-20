//! The functions the C++ that remains calls into Rust.
//!
//! Every ported file keeps its C++ callers working through this one
//! `#[cxx::bridge]`, whose generated header is `next-layer-app/src/bridge.rs.h`.
//! It is the reverse of `hfw`: that boundary is Rust asking a C library, this one
//! is C++ asking this crate, and both disappear as `src/qt/src` empties.

#[cxx::bridge(namespace = "next_layer")]
mod ffi {
    extern "Rust" {
        /// The engine's resources tree: presets, bed meshes, icons, fonts.
        fn resources_dir() -> String;

        /// The version this build stamps into G-code and shows in the window.
        fn application_version() -> String;

        /// Rewrites the branding in an exported G-code file; returns the number of
        /// lines rewritten, or -1 when the file cannot be read or written.
        fn brand_gcode(path: &str, configuration_json: &str) -> i32;

        /// sha256 of a toolpath's move and command lines, comments left out.
        fn toolpath_digest(path: &str) -> String;

        /// The `QTemporaryDir` template a scratch directory is made from.
        fn scratch_template() -> String;

        /// Removes scratch directories whose owning process is gone.
        fn sweep_scratch_dirs();
    }
}

fn resources_dir() -> String {
    crate::Resources::resources_dir()
}

fn application_version() -> String {
    crate::GcodeBrand::application_version()
}

fn brand_gcode(path: &str, configuration_json: &str) -> i32 {
    crate::GcodeBrand::brand_gcode(path, configuration_json)
}

fn toolpath_digest(path: &str) -> String {
    crate::GcodeBrand::toolpath_digest(path)
}

fn scratch_template() -> String {
    crate::Scratch::scratch_template()
}

fn sweep_scratch_dirs() {
    crate::Scratch::sweep_scratch_dirs();
}
