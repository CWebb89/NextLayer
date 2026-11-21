//! What QML needs that only the application can determine, and the two pieces of it
//! that are logic rather than storage.
//!
//! Ported from `src/qt/src/Host.cpp`. The resolved palette, the document's framing,
//! the printer's bed polygon and its Z limit are the QObject's own property
//! storage: QML binds all fourteen of them, so the notify comes from the shell, and
//! nothing in this module computes them. What is here is which directory a person's
//! models are in — the one place a print-host decision is made by looking at the
//! filesystem — and putting an exported file on disk.
//!
//! Both are called on the GUI thread, from a property read and a `Q_INVOKABLE`,
//! which is where the C++ called them: there is no lock here and adding one would
//! be a claim that something else can reach this state.
//!
//! **The extension list is the C++ importer's, six of them, and deliberately not
//! HFW's.** `Slic3r::Biz::FileLoadingLogic::get_import_extensions()` answers
//! `.3mf .stl .obj .svg .step .stp`
//! (`upstream/src/slic3r-biz/src/Slic3r/Biz/FileLoadingLogic.cpp:1234-1244`) while
//! `hfw_mesh_load_extensions()` answers `.3mf,.stl,.obj`
//! (`crates/capi/include/hfw.h:81`) because that is what the engine's own mesh
//! reader opens. Counting against the ABI's three would change which directory
//! wins for anyone whose downloads are STEP files, so the six are here.

use std::path::Path;

/// The extensions the folder count is made against, in the loader's order. Each is
/// matched in lower and upper case, which is what the C++ did by building a second
/// `*.STL` pattern beside `*.stl`.
const COUNTED_EXTENSIONS: [&str; 6] = [".3mf", ".stl", ".obj", ".svg", ".step", ".stp"];

/// Where the system says downloads live.
///
/// `system_dir` is the system's own answer and `home_dir` is the home directory,
/// both resolved by the caller through Qt: `QStandardPaths` reads the XDG
/// user-dirs file, which is not worth reimplementing against a directory whose
/// whole purpose here is a starting folder. `Downloads` and `downloads` are
/// different directories on a case-sensitive filesystem and only one of them holds
/// the files, so the candidate with the most models wins it; a tie keeps the
/// earlier candidate, which makes the system answer the tie-breaker it is meant to
/// be. A directory that cannot be read counts zero rather than dropping out of the
/// choice, so an existing candidate always wins over nothing: the home directory is
/// the answer only when no candidate directory exists at all.
pub fn download_folder(system_dir: &str, home_dir: &str) -> String {
    let mut candidates: Vec<String> = Vec::new();
    for candidate in [
        system_dir.to_owned(),
        format!("{home_dir}/Downloads"),
        format!("{home_dir}/downloads"),
    ] {
        if candidate.is_empty() || candidates.contains(&candidate) {
            continue;
        }
        if Path::new(&candidate).is_dir() {
            candidates.push(candidate);
        }
    }

    let mut best = String::new();
    let mut best_count: i64 = -1;
    for candidate in &candidates {
        let count = models_in(candidate);
        if count > best_count {
            best_count = count;
            best = candidate.clone();
        }
    }
    if best.is_empty() {
        home_dir.to_owned()
    } else {
        best
    }
}

/// Puts a copy of `source` at `target`.
///
/// `QFile::copy` refuses to overwrite an existing target, and so does this: the
/// export dialog has already asked once, and a false answer is what puts "export
/// failed" on screen instead of replacing a file the person had.
pub fn copy_file(source: &str, target: &str) -> bool {
    let target = Path::new(target);
    if target.exists() {
        return false;
    }
    std::fs::copy(source, target).is_ok()
}

fn models_in(directory: &str) -> i64 {
    let Ok(entries) = std::fs::read_dir(directory) else {
        return 0;
    };
    entries
        .filter_map(|entry| entry.ok())
        .filter(|entry| is_model(&entry.path()))
        .count() as i64
}

/// Whether one directory entry is a model, by the rule
/// `QDir::entryList(patterns, QDir::Files)` applies: a file, not a directory, with
/// one of the counted extensions in lower or upper case, and not hidden — a leading
/// dot excludes it, which the patterns alone would not.
fn is_model(path: &Path) -> bool {
    if !path.is_file() {
        return false;
    }
    let Some(name) = path.file_name().and_then(|name| name.to_str()) else {
        return false;
    };
    if name.starts_with('.') {
        return false;
    }
    COUNTED_EXTENSIONS.iter().copied().any(|extension| {
        name.ends_with(extension) || name.ends_with(&extension.to_ascii_uppercase())
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    fn scratch(name: &str) -> std::path::PathBuf {
        let path = std::env::temp_dir().join(format!(
            "next-layer-host-{}-{name}",
            std::process::id()
        ));
        let _ = std::fs::remove_dir_all(&path);
        std::fs::create_dir_all(&path).expect("a scratch directory");
        path
    }

    fn touch(directory: &Path, name: &str) {
        std::fs::write(directory.join(name), b"").expect("a scratch file");
    }

    #[test]
    fn the_folder_with_more_models_wins_whatever_it_is_called() {
        let root = scratch("count");
        let system = root.join("system");
        let lower = root.join("downloads");
        std::fs::create_dir_all(&system).expect("a scratch directory");
        std::fs::create_dir_all(&lower).expect("a scratch directory");
        touch(&system, "one.stl");
        touch(&lower, "one.stl");
        touch(&lower, "two.STL");
        touch(&lower, "three.STEP");

        let chosen = download_folder(
            system.to_str().expect("a utf-8 path"),
            root.to_str().expect("a utf-8 path"),
        );
        let _ = std::fs::remove_dir_all(&root);
        assert_eq!(chosen, lower.to_str().expect("a utf-8 path"));
    }

    #[test]
    fn a_tie_keeps_the_systems_own_answer_and_a_miss_is_a_home_directory() {
        let root = scratch("tie");
        let system = root.join("system");
        let upper = root.join("Downloads");
        std::fs::create_dir_all(&system).expect("a scratch directory");
        std::fs::create_dir_all(&upper).expect("a scratch directory");
        touch(&system, "one.stl");
        touch(&upper, "two.stl");

        let tied = download_folder(
            system.to_str().expect("a utf-8 path"),
            root.to_str().expect("a utf-8 path"),
        );
        let bare = root.join("bare");
        std::fs::create_dir_all(&bare).expect("a scratch directory");
        let absent = download_folder(
            root.join("absent").to_str().expect("a utf-8 path"),
            bare.to_str().expect("a utf-8 path"),
        );
        let _ = std::fs::remove_dir_all(&root);
        assert_eq!(tied, system.to_str().expect("a utf-8 path"));
        assert_eq!(absent, bare.to_str().expect("a utf-8 path"));
    }

    #[test]
    fn a_hidden_file_and_a_directory_are_not_models() {
        let root = scratch("hidden");
        touch(&root, ".hidden.stl");
        touch(&root, "drawn.svg");
        std::fs::create_dir_all(root.join("nested.stl")).expect("a scratch directory");
        let counted = models_in(root.to_str().expect("a utf-8 path"));
        let _ = std::fs::remove_dir_all(&root);
        assert_eq!(counted, 1);
    }

    #[test]
    fn a_copy_does_not_replace_what_is_already_there() {
        let root = scratch("copy");
        let source = root.join("source.gcode");
        let target = root.join("target.gcode");
        std::fs::write(&source, b"G1 X1\n").expect("a scratch toolpath");

        assert!(copy_file(
            source.to_str().expect("a utf-8 path"),
            target.to_str().expect("a utf-8 path")
        ));
        assert_eq!(
            std::fs::read_to_string(&target).expect("the copy"),
            "G1 X1\n"
        );
        assert!(!copy_file(
            source.to_str().expect("a utf-8 path"),
            target.to_str().expect("a utf-8 path")
        ));
        assert!(!copy_file(
            root.join("absent.gcode").to_str().expect("a utf-8 path"),
            root.join("other.gcode").to_str().expect("a utf-8 path")
        ));
        let _ = std::fs::remove_dir_all(&root);
    }
}
