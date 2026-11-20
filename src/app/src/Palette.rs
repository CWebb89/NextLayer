//! The 36-key matugen MD3 palette the whole interface is coloured from.
//!
//! Ported from `src/qt/src/Palette.cpp`. Candidates are tried in order and the
//! first one that resolves wins: `$NEXT_LAYER_COLORS`,
//! `~/.cache/matugen/next-layer-colors.json`, then `~/.cache/matugen/layer-colors.json`.
//! An unreadable, malformed or non-string-valued entry falls through to the next
//! candidate; when none resolves the roles are empty and `Colors.qml` keeps its
//! compiled-in defaults.

use std::path::PathBuf;

/// The resolved palette as one JSON object: `source` names the file that answered
/// and is empty when none did, `roles` holds only the string-valued entries.
pub fn palette_json() -> String {
    let (source, roles) = resolve();
    let mut document = serde_json::Map::new();
    document.insert("source".to_owned(), source.into());
    document.insert("roles".to_owned(), roles.into());
    serde_json::Value::Object(document).to_string()
}

fn resolve() -> (String, serde_json::Map<String, serde_json::Value>) {
    for candidate in candidates() {
        let Some(roles) = read_roles(&candidate) else {
            continue;
        };
        if roles.is_empty() {
            continue;
        }
        return (candidate, roles);
    }
    (String::new(), serde_json::Map::new())
}

fn candidates() -> Vec<String> {
    let cache = std::env::var_os("HOME")
        .map(PathBuf::from)
        .unwrap_or_default()
        .join(".cache/matugen");
    [
        std::env::var("NEXT_LAYER_COLORS").unwrap_or_default(),
        cache.join("next-layer-colors.json").to_string_lossy().into_owned(),
        cache.join("layer-colors.json").to_string_lossy().into_owned(),
    ]
    .into_iter()
    .filter(|candidate| !candidate.is_empty())
    .collect()
}

fn read_roles(path: &str) -> Option<serde_json::Map<String, serde_json::Value>> {
    let body = std::fs::read_to_string(path).ok()?;
    let parsed: serde_json::Value = serde_json::from_str(&body).ok()?;
    let object = parsed.as_object()?;
    Some(
        object
            .iter()
            .filter(|(_, value)| value.is_string())
            .map(|(key, value)| (key.clone(), value.clone()))
            .collect(),
    )
}

#[cfg(test)]
mod tests {
    use super::*;

    fn written(name: &str, body: &str) -> String {
        let path = std::env::temp_dir().join(format!(
            "next-layer-palette-{}-{name}.json",
            std::process::id()
        ));
        std::fs::write(&path, body).expect("a palette file");
        path.to_string_lossy().into_owned()
    }

    #[test]
    fn only_string_roles_survive_and_the_source_is_named() {
        let path = written(
            "mixed",
            r##"{"primary": "#aabbcc", "count": 4, "flag": true}"##,
        );
        std::env::set_var("NEXT_LAYER_COLORS", &path);
        let palette = palette_json();
        std::env::remove_var("NEXT_LAYER_COLORS");
        let _ = std::fs::remove_file(&path);

        let parsed: serde_json::Value = serde_json::from_str(&palette).expect("valid json");
        assert_eq!(parsed["source"], path.as_str());
        assert_eq!(parsed["roles"]["primary"], "#aabbcc");
        assert!(parsed["roles"].get("count").is_none());
        assert!(parsed["roles"].get("flag").is_none());
    }

    #[test]
    fn a_malformed_candidate_falls_through_to_the_next() {
        let broken = written("broken", "{not json");
        std::env::set_var("NEXT_LAYER_COLORS", &broken);
        let palette = palette_json();
        std::env::remove_var("NEXT_LAYER_COLORS");
        let _ = std::fs::remove_file(&broken);

        let parsed: serde_json::Value = serde_json::from_str(&palette).expect("valid json");
        assert_ne!(parsed["source"], broken.as_str());
    }
}
