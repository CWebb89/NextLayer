//! The functions the C++ that remains calls into Rust.
//!
//! Every ported file keeps its C++ callers working through this one
//! `#[cxx::bridge]`, whose generated header is `next-layer-app/src/bridge.rs.h`.
//! It is the reverse of `hfw`: that boundary is Rust asking a C library, this one
//! is C++ asking this crate, and both disappear as `src/qt/src` empties.

#[cxx::bridge(namespace = "next_layer")]
mod ffi {
    unsafe extern "C++" {
        include!("next-layer-app/include/next-layer-qt.hpp");

        /// Leaves Qt's event loop, so the registered teardown runs.
        fn quit_application();
    }

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

        /// The resolved matugen palette, as one JSON object.
        fn palette_json() -> String;

        /// One manifest fetch, as JSON: version, url, sha256, notes, error, newer.
        fn update_check(channel: &str) -> String;

        /// Downloads and verifies an artifact; JSON with `ok`, `error`, `staged`.
        fn update_download(url: &str, sha256: &str, version: &str, root: &str) -> String;

        /// Installs a staged artifact; JSON with `ok` and `error`.
        fn update_install(staged: &str, root: &str) -> String;

        /// The install prefix this build replaces.
        fn update_install_root() -> String;

        /// The version this build reports.
        fn update_current_version() -> String;
    }
}

pub fn quit_application() {
    ffi::quit_application();
}

fn palette_json() -> String {
    crate::Palette::palette_json()
}

fn update_check(channel: &str) -> String {
    crate::Updater::check_manifest(channel).to_json()
}

fn update_download(url: &str, sha256: &str, version: &str, root: &str) -> String {
    match crate::Updater::download_artifact(url, sha256, version, root) {
        Ok(staged) => serde_json::json!({"ok": true, "error": "", "staged": staged}).to_string(),
        Err(error) => serde_json::json!({"ok": false, "error": error, "staged": ""}).to_string(),
    }
}

fn update_install(staged: &str, root: &str) -> String {
    match crate::Updater::install_artifact(staged, root) {
        Ok(()) => serde_json::json!({"ok": true, "error": ""}).to_string(),
        Err(error) => serde_json::json!({"ok": false, "error": error}).to_string(),
    }
}

fn update_install_root() -> String {
    crate::Updater::install_root()
}

fn update_current_version() -> String {
    crate::Updater::current_version()
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
