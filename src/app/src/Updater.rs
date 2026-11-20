//! The updater: a static HTTPS channel, a manifest, and an install by rename.
//!
//! Ported from `src/qt/src/Updater.cpp`. `<channel>/releases/latest/download/`
//! holds `update.json` and the artifact the manifest names, which is the layout
//! GitHub Releases already has; any host that can serve those two paths works the
//! same way. Nothing else is trusted: the artifact is verified by its sha256 before
//! a byte of it is installed.
//!
//! The install is a staging directory plus renames, never an in-place write: on
//! Linux the running binary stays mapped under its old inode, so replacing it is
//! safe while the app runs and the new one takes over on restart.

use core::pin::Pin;
use cxx_qt::{CxxQtType, Threading};
use cxx_qt_lib::QString;
use serde_json::{json, Value};
use sha2::{Digest, Sha256};
use std::path::Path;

use crate::qobject;

const CHANNEL_DEFAULT: &str = "https://github.com/CWebb89/NextLayer";
const MANIFEST_PATH: &str = "/releases/latest/download/update.json";
const BINARY_NAME: &str = "next-layer-qt";

pub fn channel() -> String {
    non_empty_env("NEXT_LAYER_UPDATE_URL").unwrap_or_else(|| CHANNEL_DEFAULT.to_owned())
}

/// Where this build looks for releases, for the preferences panel to name instead
/// of a constant nobody can verify.
pub fn channel_display() -> String {
    channel()
}

pub fn manifest_url(channel: &str) -> String {
    format!("{channel}{MANIFEST_PATH}")
}

pub fn install_root() -> String {
    non_empty_env("NEXT_LAYER_UPDATE_ROOT").unwrap_or_else(|| {
        std::env::current_exe()
            .ok()
            .and_then(|exe| exe.parent().map(Path::to_path_buf))
            .unwrap_or_default()
            .to_string_lossy()
            .into_owned()
    })
}

pub fn current_version() -> String {
    crate::BAKED_VERSION.to_owned()
}

pub fn running_from_appimage() -> bool {
    non_empty_env("APPIMAGE").is_some()
}

/// Strictly newer, comparing dotted numbers. A malformed version is never newer.
pub fn version_is_newer(candidate: &str, current: &str) -> bool {
    let left: Vec<&str> = candidate.split('.').collect();
    let right: Vec<&str> = current.split('.').collect();
    for index in 0..left.len().max(right.len()) {
        let a = left.get(index).map(|part| part.parse::<i64>());
        let b = right.get(index).map(|part| part.parse::<i64>());
        let a = match a {
            Some(Ok(number)) => number,
            Some(Err(_)) => return false,
            None => 0,
        };
        let b = match b {
            Some(Ok(number)) => number,
            Some(Err(_)) => return false,
            None => 0,
        };
        if a != b {
            return a > b;
        }
    }
    false
}

/// What a manifest names, and whether it is worth installing.
#[derive(Default)]
pub struct Release {
    pub version: String,
    pub url: String,
    pub sha256: String,
    pub notes: String,
    pub error: String,
    pub newer: bool,
}

impl Release {
    fn from_json(document: &Value, channel: &str) -> Self {
        let mut release = Release {
            version: text(document, "version"),
            url: text(document, "url"),
            sha256: text(document, "sha256").to_lowercase(),
            notes: text(document, "notes"),
            error: String::new(),
            newer: false,
        };
        if release.version.is_empty() {
            release.error = "the update manifest carries no version".to_owned();
            return release;
        }
        if release.url.is_empty() || release.sha256.is_empty() {
            release.error = format!("{} is published without an artifact", release.version);
            return release;
        }
        if !release.url.starts_with("http") {
            release.url = format!("{channel}/{}", release.url);
        }
        release.newer = version_is_newer(&release.version, &current_version());
        release
    }

    pub fn to_json(&self) -> String {
        json!({
            "version": self.version,
            "url": self.url,
            "sha256": self.sha256,
            "notes": self.notes,
            "error": self.error,
            "newer": self.newer,
        })
        .to_string()
    }
}

/// One manifest fetch. A missing manifest is an ordinary answer — "this channel
/// publishes nothing newer" — and is not reported as a failure.
pub fn check_manifest(channel: &str) -> Release {
    let body = match fetch(&manifest_url(channel)) {
        Ok(body) => body,
        Err(FetchError::NotFound) => {
            return Release {
                version: String::new(),
                url: String::new(),
                sha256: String::new(),
                notes: String::new(),
                error: String::new(),
                newer: false,
            }
        }
        Err(FetchError::Failed(message)) => {
            return Release {
                version: String::new(),
                url: String::new(),
                sha256: String::new(),
                notes: String::new(),
                error: message,
                newer: false,
            }
        }
    };

    match serde_json::from_slice::<Value>(&body) {
        Ok(document) if document.is_object() => Release::from_json(&document, channel),
        _ => Release {
            version: String::new(),
            url: String::new(),
            sha256: String::new(),
            notes: String::new(),
            error: "the update manifest is not JSON".to_owned(),
            newer: false,
        },
    }
}

/// Downloads `release` into a staging directory under `root` and verifies it.
/// Returns the staging directory, or the reason it failed.
pub fn download_artifact(url: &str, sha256: &str, version: &str, root: &str) -> Result<String, String> {
    if version.is_empty() {
        return Err("there is no release to download".to_owned());
    }

    let body = match fetch(url) {
        Ok(body) => body,
        Err(FetchError::NotFound) => return Err("the artifact is not on the channel".to_owned()),
        Err(FetchError::Failed(message)) => return Err(message),
    };

    let staging = Path::new(root).join(format!(".update-{version}"));
    let _ = std::fs::remove_dir_all(&staging);
    std::fs::create_dir_all(&staging)
        .map_err(|_| format!("could not create {}", staging.display()))?;

    let archive = staging.join("release.tar.gz");
    std::fs::write(&archive, &body)
        .map_err(|_| format!("could not write {}", archive.display()))?;

    let digest = sha256_of(&archive)?;
    if digest != sha256 {
        return Err(format!(
            "the artifact's sha256 is {digest}, the manifest says {sha256}"
        ));
    }

    let unpacked = std::process::Command::new("tar")
        .current_dir(&staging)
        .args(["-xzf", &archive.to_string_lossy(), "--strip-components=0"])
        .output()
        .map_err(|error| format!("could not unpack the artifact: {error}"))?;
    if !unpacked.status.success() {
        let reason = String::from_utf8_lossy(&unpacked.stderr).trim().to_owned();
        return Err(format!("could not unpack the artifact: {reason}"));
    }

    if !staging.join(BINARY_NAME).is_file() {
        return Err(format!("the artifact carries no {BINARY_NAME}"));
    }

    Ok(staging.to_string_lossy().into_owned())
}

/// Installs the staged artifact over `root`, keeping the binary it replaces as
/// `.previous`.
pub fn install_artifact(staged: &str, root: &str) -> Result<(), String> {
    let staged = Path::new(staged);
    if staged.as_os_str().is_empty() {
        return Err("nothing has been downloaded".to_owned());
    }
    if running_from_appimage() {
        return Err("this build is an AppImage, so it cannot be replaced in place".to_owned());
    }

    let staged_binary = staged.join(BINARY_NAME);
    let target_binary = Path::new(root).join(BINARY_NAME);
    let previous = Path::new(root).join(format!("{BINARY_NAME}.previous"));

    let _ = std::fs::remove_file(&previous);
    if target_binary.exists() && std::fs::rename(&target_binary, &previous).is_err() {
        return Err(format!("could not move {} aside", target_binary.display()));
    }
    let _ = std::fs::remove_file(&target_binary);
    if std::fs::rename(&staged_binary, &target_binary).is_err() {
        let _ = std::fs::rename(&previous, &target_binary);
        return Err(format!("could not install {}", target_binary.display()));
    }

    for directory in ["qml", "resources"] {
        let from = staged.join(directory);
        if !from.is_dir() {
            continue;
        }
        if let Err(error) = copy_tree(&from, &Path::new(root).join(directory)) {
            let _ = std::fs::rename(&previous, &target_binary);
            return Err(error);
        }
    }

    let _ = std::fs::remove_dir_all(staged);
    Ok(())
}

/// Starts the installed binary again with this process's arguments and quits.
pub fn relaunch() -> Result<(), String> {
    let binary = Path::new(&install_root()).join(BINARY_NAME);
    let arguments: Vec<String> = std::env::args().skip(1).collect();
    std::process::Command::new(&binary)
        .args(&arguments)
        .spawn()
        .map_err(|_| format!("could not start {}", binary.display()))?;
    crate::bridge::quit_application();
    Ok(())
}

enum FetchError {
    NotFound,
    Failed(String),
}

/// The artifact is a Qt application with its QML beside it, so the default 10 MB
/// body limit a manifest-sized GET would use is not enough here.
const MAX_ARTIFACT_BYTES: u64 = 512 * 1024 * 1024;

fn fetch(url: &str) -> Result<Vec<u8>, FetchError> {
    let mut response = ureq::get(url).call().map_err(|error| match error {
        ureq::Error::StatusCode(404) => FetchError::NotFound,
        other => FetchError::Failed(other.to_string()),
    })?;
    response
        .body_mut()
        .with_config()
        .limit(MAX_ARTIFACT_BYTES)
        .read_to_vec()
        .map_err(|error| FetchError::Failed(error.to_string()))
}

fn sha256_of(path: &Path) -> Result<String, String> {
    let bytes = std::fs::read(path).map_err(|_| format!("could not read {}", path.display()))?;
    let mut hash = Sha256::new();
    hash.update(&bytes);
    Ok(hash
        .finalize()
        .iter()
        .map(|byte| format!("{byte:02x}"))
        .collect())
}

fn copy_tree(from: &Path, to: &Path) -> Result<(), String> {
    let entries = std::fs::read_dir(from).map_err(|_| format!("could not read {}", from.display()))?;
    for entry in entries.flatten() {
        let source = entry.path();
        let target = to.join(entry.file_name());
        if source.is_dir() {
            std::fs::create_dir_all(&target)
                .map_err(|_| format!("could not create {}", target.display()))?;
            copy_tree(&source, &target)?;
            continue;
        }
        if let Some(parent) = target.parent() {
            std::fs::create_dir_all(parent).ok();
        }
        let _ = std::fs::remove_file(&target);
        std::fs::copy(&source, &target)
            .map_err(|_| format!("could not write {}", target.display()))?;
    }
    Ok(())
}

fn text(document: &Value, key: &str) -> String {
    document
        .get(key)
        .and_then(Value::as_str)
        .unwrap_or_default()
        .to_owned()
}

fn non_empty_env(key: &str) -> Option<String> {
    std::env::var(key).ok().filter(|value| !value.is_empty())
}

#[derive(Default)]
#[allow(non_snake_case)]
pub struct UpdaterRust {
    pub status: QString,
    pub availableVersion: QString,
    pub notes: QString,
    pub busy: bool,
    pub downloaded: bool,
    pub canInstall: bool,
    pub channel: QString,
    pub(crate) release: Release,
    pub(crate) staged: String,
}

impl qobject::Updater {
    pub fn check(mut self: Pin<&mut Self>) {
        let channel = channel_display();
        self.as_mut().set_busy(true);
        self.as_mut()
            .set_status(QString::from(format!("checking {channel}")));

        let thread = self.qt_thread();
        std::thread::spawn(move || {
            let release = check_manifest(&channel);
            let _ = thread.queue(move |mut object| {
                object.as_mut().set_busy(false);
                if !release.error.is_empty() {
                    let message = format!("update check failed: {}", release.error);
                    object.as_mut().set_status(QString::from(message));
                    return;
                }
                if !release.newer {
                    let message = format!("version {} is the newest", current_version());
                    object.as_mut().set_status(QString::from(message));
                    return;
                }
                let status = if running_from_appimage() {
                    format!(
                        "{} is available (this build is {}) -- this is an AppImage, so \
                         download the new one from the releases page and replace this file",
                        release.version,
                        current_version()
                    )
                } else {
                    format!(
                        "{} is available (this build is {})",
                        release.version,
                        current_version()
                    )
                };
                object.as_mut().set_status(QString::from(status));
                object
                    .as_mut()
                    .set_availableVersion(QString::from(release.version.clone()));
                object.as_mut().set_notes(QString::from(release.notes.clone()));
                object.as_mut().rust_mut().get_mut().release = release;
            });
        });
    }

    pub fn download(mut self: Pin<&mut Self>) {
        let release = Release {
            version: self.rust().release.version.clone(),
            url: self.rust().release.url.clone(),
            sha256: self.rust().release.sha256.clone(),
            notes: self.rust().release.notes.clone(),
            error: self.rust().release.error.clone(),
            newer: self.rust().release.newer,
        };
        if release.version.is_empty() {
            self.as_mut()
                .set_status(QString::from("check for updates first"));
            return;
        }

        self.as_mut().set_busy(true);
        self.as_mut()
            .set_status(QString::from(format!("downloading {}", release.version)));

        let root = install_root();
        let thread = self.qt_thread();
        std::thread::spawn(move || {
            let outcome = download_artifact(&release.url, &release.sha256, &release.version, &root);
            let _ = thread.queue(move |mut object| {
                object.as_mut().set_busy(false);
                match outcome {
                    Ok(staged) => {
                        let installable = !running_from_appimage();
                        let message = format!("{} downloaded and verified", release.version);
                        object.as_mut().set_status(QString::from(message));
                        object.as_mut().set_downloaded(true);
                        object.as_mut().set_canInstall(installable);
                        let state = object.as_mut().rust_mut().get_mut();
                        state.staged = staged;
                        state.release = release;
                    }
                    Err(error) => {
                        let message = format!("download failed: {error}");
                        object.as_mut().set_status(QString::from(message));
                    }
                }
            });
        });
    }

    pub fn install(mut self: Pin<&mut Self>) {
        let staged = self.rust().staged.clone();
        let version = self.rust().release.version.clone();
        self.as_mut().set_busy(true);

        let outcome = install_artifact(&staged, &install_root());
        self.as_mut().set_busy(false);
        match outcome {
            Ok(()) => {
                self.as_mut().set_downloaded(false);
                self.as_mut().set_canInstall(false);
                self.as_mut()
                    .set_status(QString::from(format!("{version} installed -- restart to run it")));
                self.as_mut().rust_mut().get_mut().staged.clear();
            }
            Err(error) => {
                let message = format!("install failed: {error}");
                self.as_mut().set_status(QString::from(message));
            }
        }
    }

    pub fn relaunch(mut self: Pin<&mut Self>) {
        if let Err(error) = relaunch() {
            self.as_mut().set_status(QString::from(error));
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn versions_compare_by_dotted_number() {
        assert!(version_is_newer("0.1.3", "0.1.2"));
        assert!(version_is_newer("1.0.0", "0.9.9"));
        assert!(version_is_newer("0.2", "0.1.9"));
        assert!(!version_is_newer("0.1.2", "0.1.2"));
        assert!(!version_is_newer("0.1.2", "0.1.3"));
        assert!(!version_is_newer("0.2-beta", "0.1.9"));
        assert!(version_is_newer("1.0.0-beta", "0.9.9"));
        assert!(!version_is_newer("", "0.1.2"));
    }

    #[test]
    fn a_manifest_names_its_artifact_and_a_relative_url_resolves_against_the_channel() {
        let release = Release::from_json(
            &json!({"version": "9.9.9", "url": "release.tar.gz", "sha256": "AB", "notes": "n"}),
            "https://example.test/channel",
        );
        assert_eq!(release.url, "https://example.test/channel/release.tar.gz");
        assert_eq!(release.sha256, "ab");
        assert!(release.newer);
        assert!(release.error.is_empty());
    }

    #[test]
    fn a_manifest_without_an_artifact_is_an_error_not_an_update() {
        let release = Release::from_json(&json!({"version": "9.9.9"}), "https://example.test");
        assert!(release.error.contains("without an artifact"));
        assert!(!release.newer);
    }

    #[test]
    fn an_install_swaps_the_binary_and_keeps_the_old_one() {
        let root = std::env::temp_dir().join(format!("next-layer-update-{}", std::process::id()));
        let staged = root.join(".update-9.9.9");
        std::fs::create_dir_all(staged.join("qml")).expect("a staging directory");
        std::fs::write(staged.join(BINARY_NAME), b"new").expect("a staged binary");
        std::fs::write(staged.join("qml/Main.qml"), b"import QtQuick").expect("a staged qml file");
        std::fs::create_dir_all(&root).expect("an install root");
        std::fs::write(root.join(BINARY_NAME), b"old").expect("an installed binary");

        install_artifact(&staged.to_string_lossy(), &root.to_string_lossy()).expect("an install");

        assert_eq!(std::fs::read(root.join(BINARY_NAME)).unwrap(), b"new");
        assert_eq!(std::fs::read(root.join(format!("{BINARY_NAME}.previous"))).unwrap(), b"old");
        assert!(root.join("qml/Main.qml").is_file());
        assert!(!staged.exists(), "the staging directory is cleaned up");
        let _ = std::fs::remove_dir_all(&root);
    }
}
