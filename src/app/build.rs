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

/// The resources tree this build falls back to when neither the environment nor
/// the layout beside the executable names one.
fn baked_resources_dir() -> PathBuf {
    match std::env::var_os("NEXT_LAYER_RESOURCES_DIR") {
        Some(named) => PathBuf::from(named),
        None => PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../upstream/resources"),
    }
}

/// `<cargo target dir>/<profile>/include`, the one directory CMake puts on the
/// include path for the headers cxx generates.
fn generated_include_dir(out_dir: &Path) -> PathBuf {
    let build = out_dir
        .ancestors()
        .find(|path| path.file_name().is_some_and(|name| name == "build"))
        .expect("OUT_DIR sits under <target dir>/<profile>/build");
    let profile = build.parent().expect("a profile directory above build");
    profile.join("include")
}

fn copy_headers(from: &Path, to: &Path) {
    let Ok(entries) = std::fs::read_dir(from) else {
        return;
    };
    for entry in entries.flatten() {
        let path = entry.path();
        if path.is_dir() {
            copy_headers(&path, &to.join(entry.file_name()));
            continue;
        }
        if path.extension().is_none_or(|extension| extension != "h" && extension != "hpp") {
            continue;
        }
        std::fs::create_dir_all(to).expect("the generated include directory");
        std::fs::copy(&path, to.join(entry.file_name())).expect("a generated header copy");
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
    println!("cargo:rustc-link-search=native={}", library_dir.display());
    println!("cargo:rustc-link-lib=static=hfw_capi");
    println!("cargo:rerun-if-env-changed=NEXT_LAYER_HFW_LIB");
    println!("cargo:rerun-if-changed={}", archive.display());

    println!(
        "cargo:rustc-env=NEXT_LAYER_RESOURCES_DIR={}",
        baked_resources_dir().display()
    );
    println!("cargo:rerun-if-env-changed=NEXT_LAYER_RESOURCES_DIR");

    let version = std::env::var("NEXT_LAYER_VERSION")
        .unwrap_or_else(|_| std::env::var("CARGO_PKG_VERSION").expect("CARGO_PKG_VERSION"));
    println!("cargo:rustc-env=NEXT_LAYER_VERSION={version}");
    println!("cargo:rerun-if-env-changed=NEXT_LAYER_VERSION");

    let out_dir = PathBuf::from(std::env::var_os("OUT_DIR").expect("OUT_DIR"));
    let include_dir = generated_include_dir(&out_dir);
    let qt = qt_build_utils::QtBuild::new(vec!["Core".to_owned()])
        .expect("a Qt installation to compile the interop header against");
    let mut bridge = cxx_build::bridge("src/bridge.rs");
    bridge.std("c++17");
    for include in qt.include_paths() {
        bridge.include(include);
    }
    bridge.compile("next-layer-bridge");
    copy_headers(&out_dir.join("cxxbridge/include"), &include_dir);
    copy_headers(
        &PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("include"),
        &include_dir.join("next-layer-app/include"),
    );
    println!("cargo:rerun-if-changed=src/bridge.rs");
    println!("cargo:rerun-if-changed=src/Resources.rs");
    println!("cargo:rerun-if-changed=src/GcodeBrand.rs");
    println!("cargo:rerun-if-changed=src/Scratch.rs");

    CxxQtBuilder::new_qml_module(QmlModule::new("NextLayer"))
        .qt_module("Quick")
        .file("src/lib.rs")
        .build();
}
