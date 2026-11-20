//! Where this build keeps the engine's resources tree.
//!
//! Ported from `src/qt/src/Resources.cpp`. The rule is one Rust file per C++ file,
//! named after it, so this file is what `tools/rust-port` counts as the port.

/// The resources tree: presets, bed meshes, icons, fonts.
///
/// `NEXT_LAYER_RESOURCES` names it at runtime for a packaged build. A build-tree
/// run finds it beside the executable or in the install prefix, and the path the
/// build was configured with is the last resort — the same order the C++ used.
pub fn resources_dir() -> String {
    if let Some(named) = std::env::var_os("NEXT_LAYER_RESOURCES") {
        let path = std::path::PathBuf::from(named);
        if path.is_dir() {
            return path.to_string_lossy().into_owned();
        }
    }

    let base = std::env::current_exe()
        .ok()
        .and_then(|exe| exe.parent().map(std::path::Path::to_path_buf))
        .unwrap_or_default();
    for candidate in [
        base.join("resources"),
        base.join("../share/next-layer/resources"),
    ] {
        if candidate.is_dir() {
            let resolved = std::fs::canonicalize(&candidate).unwrap_or(candidate);
            return resolved.to_string_lossy().into_owned();
        }
    }

    crate::BAKED_RESOURCES_DIR.to_owned()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn a_named_tree_wins_and_a_missing_one_is_ignored() {
        std::env::set_var("NEXT_LAYER_RESOURCES", "/nonexistent/next-layer");
        assert_eq!(resources_dir(), crate::BAKED_RESOURCES_DIR);
        std::env::set_var("NEXT_LAYER_RESOURCES", env!("CARGO_MANIFEST_DIR"));
        let named = resources_dir();
        std::env::remove_var("NEXT_LAYER_RESOURCES");
        assert_eq!(named, env!("CARGO_MANIFEST_DIR"));
    }

    #[test]
    fn the_baked_tree_itself_is_a_directory_in_a_build_tree() {
        let baked = std::path::Path::new(crate::BAKED_RESOURCES_DIR);
        assert!(baked.is_dir(), "{} is not a directory", baked.display());
    }
}
