//! Scratch directories, named for the process that owns them.
//!
//! Ported from `src/qt/src/Scratch.cpp`. A plate is staged as a multi-megabyte
//! STL, and on a machine whose `/tmp` is a small tmpfs the directories a crash or
//! a killed session leaves behind are enough to fill it and break every other
//! program that writes there: the process id in each name is what tells a leftover
//! from one still in use.

use std::path::PathBuf;
use std::time::{Duration, SystemTime};

const PREFIX: &str = "next-layer-";

/// The `QTemporaryDir` template a caller in C++ hands to Qt: the process id, then
/// the six placeholders Qt replaces with a unique suffix.
pub fn scratch_template() -> String {
    let directory = std::env::temp_dir();
    format!("{}/{PREFIX}{}-XXXXXX", directory.display(), std::process::id())
}

/// Removes the scratch directories of processes that no longer exist, and any
/// older than a day whose owner cannot be read.
pub fn sweep_scratch_dirs() {
    let Ok(entries) = std::fs::read_dir(std::env::temp_dir()) else {
        return;
    };
    let cutoff = SystemTime::now()
        .checked_sub(Duration::from_secs(86_400))
        .unwrap_or(SystemTime::UNIX_EPOCH);

    for entry in entries.flatten() {
        let path = entry.path();
        let Some(name) = entry.file_name().to_str().map(str::to_owned) else {
            continue;
        };
        let Some(rest) = name.strip_prefix(PREFIX) else {
            continue;
        };
        if !path.is_dir() {
            continue;
        }

        let owner = rest
            .split('-')
            .next()
            .and_then(|pid| pid.parse::<i32>().ok())
            .filter(|pid| *pid > 0);
        match owner {
            Some(pid) if process_alive(pid) => continue,
            Some(_) => {}
            None if last_modified(&path).is_some_and(|modified| modified >= cutoff) => continue,
            None => {}
        }
        let _ = std::fs::remove_dir_all(&path);
    }
}

fn process_alive(pid: i32) -> bool {
    if pid <= 0 {
        return false;
    }
    let alive = unsafe { libc::kill(pid, 0) } == 0;
    alive || std::io::Error::last_os_error().raw_os_error() == Some(libc::EPERM)
}

fn last_modified(path: &PathBuf) -> Option<SystemTime> {
    std::fs::metadata(path).ok()?.modified().ok()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn a_template_names_the_process_and_leaves_room_for_qt() {
        let template = scratch_template();
        assert!(template.ends_with(&format!(
            "next-layer-{}-XXXXXX",
            std::process::id()
        )));
        assert!(std::path::Path::new(&template)
            .parent()
            .is_some_and(|parent| parent.is_dir()));    }

    #[test]
    fn this_process_is_alive_and_a_dead_owner_is_not() {
        assert!(process_alive(std::process::id() as i32));
        assert!(!process_alive(0));
    }

    #[test]
    fn a_sweep_removes_an_orphan_and_keeps_a_live_owner() {
        let orphan = std::env::temp_dir().join(format!("{PREFIX}999999999-orphan"));
        let mine = std::env::temp_dir().join(format!("{PREFIX}{}-live", std::process::id()));
        for path in [&orphan, &mine] {
            std::fs::create_dir_all(path).expect("a scratch directory");
        }

        sweep_scratch_dirs();

        assert!(!orphan.exists(), "an orphan should be swept");
        assert!(mine.exists(), "a live owner should be kept");
        let _ = std::fs::remove_dir_all(&mine);
    }

    #[test]
    fn a_sweep_keeps_recent_directories_it_cannot_attribute() {
        let unowned = std::env::temp_dir().join(format!("{PREFIX}unowned-recent"));
        std::fs::create_dir_all(&unowned).expect("a scratch directory");

        sweep_scratch_dirs();

        assert!(unowned.exists(), "a recent directory with no owner stays");
        let _ = std::fs::remove_dir_all(&unowned);
    }
}
