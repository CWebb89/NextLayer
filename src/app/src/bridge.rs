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
    }
}

fn resources_dir() -> String {
    crate::Resources::resources_dir()
}
