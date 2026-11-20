use cxx_qt_build::{CxxQtBuilder, QmlModule};
use std::path::{Path, PathBuf};

/// Where HFW's static library is, for a build that did not name one.
const DEVELOPMENT_LIBRARY_DIR: &str = "/home/cwebb/dev/HFW/target/release";

fn engine_library_dir() -> PathBuf {
    match std::env::var_os("NEXT_LAYER_HFW_LIB") {
        Some(named) => PathBuf::from(named),
        None => PathBuf::from(DEVELOPMENT_LIBRARY_DIR),
    }
}

fn main() {
    let library_dir = engine_library_dir();
    let archive = library_dir.join("libhfw_capi.a");
    if !archive.is_file() {
        panic!(
            "the engine's static library is missing: {} does not exist. Build it with \
             `cargo build --release -p hfw-capi` in the HFW repository, or point \
             NEXT_LAYER_HFW_LIB at the directory holding libhfw_capi.a.",
            archive.display()
        );
    }
    let library_dir: &Path = &library_dir;
    println!("cargo:rustc-link-search=native={}", library_dir.display());
    println!("cargo:rustc-link-lib=static=hfw_capi");
    println!("cargo:rerun-if-env-changed=NEXT_LAYER_HFW_LIB");
    println!("cargo:rerun-if-changed={}", archive.display());

    CxxQtBuilder::new_qml_module(QmlModule::new("NextLayer"))
        .qt_module("Quick")
        .file("src/lib.rs")
        .build();
}
