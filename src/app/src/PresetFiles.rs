//! Where presets live on disk and how one preset file is written.
//!
//! Ported from `src/qt/src/PresetFiles.hpp`, which is header-only: `namespace
//! next_layer::presets`, fourteen `inline` free functions and one `struct`, shared
//! by the store that resolves presets and the importer that creates them, so a
//! preset written by an import is byte for byte the shape the store reads.
//!
//! Every string in this module is a wire format rather than a value. `struct
//! CustomPrinter` and the `printers.json` / `vendor.yaml` layout are where
//! `PresetStore`, `PresetImport` and the engine's preset resolver meet, which is
//! why `manifest_json` and `vendor_yaml` are literal concatenations and not a
//! serialiser: the engine re-reads those files, and `--dump-presets` lists what it
//! found in them.
//!
//! Three answers here are Qt's rather than the obvious ones. `root` is
//! `QStandardPaths::writableLocation(GenericDataLocation)` — `$XDG_DATA_HOME` when
//! it is absolute, `$HOME/.local/share` otherwise — and the user-preset gate in
//! `tools/check` redirects that variable, so a `root` that reads `$HOME` directly
//! writes into the real preset directory while a gate believes it is isolated.
//! `slug_of` walks UTF-16 the way `QString` does and tests `QChar::isLetterOrNumber`,
//! which is every Unicode letter and digit and not ASCII. `fresh_id` is
//! `QUuid::createUuid().toString(WithoutBraces)`: thirty-six characters, lowercase,
//! canonical hyphens.

use std::io::Read;
use std::sync::atomic::{AtomicU64, Ordering};
use std::time::{SystemTime, UNIX_EPOCH};

use serde_json::Value;

use crate::PresetYaml;

const URANDOM: &str = "/dev/urandom";
const HEX: &[u8; 16] = b"0123456789abcdef";
const DEFAULT_TOOLS: i32 = 1;
const NAME_NEEDS_A_CHARACTER: &str = "The name needs at least one letter or digit";
const MANIFEST_UNWRITTEN: &str = "Could not write the printer manifest";
const BUNDLE_UNWRITTEN: &str = "Could not write the vendor bundle";
const TOOL_PRESET_UNWRITTEN: &str = "Could not write the tool preset";

const VENDOR_HEAD: &str = concat!(
    "kind: vendor\nid: Custom\nname: Custom\nversion: 1.0.0\n",
    "features:\n  printer: {}\n  tool: {}\n  feeder: {}\n  sheet: {}\n",
    "printer_families:\n",
);

const VENDOR_TOOL_AND_SHEET: &str = concat!(
    "---\nkind: tool\ntechnology: FFF\nid: '0.4'\nname: '0.4'\n",
    "features:\n  nozzle_diameter:\n    default: 0.4\n",
    "---\nkind: sheet\nid: pei_smooth\nname: PEI Smooth\ntype: pei_smooth\n",
);

/// One line per printer in the user's own bundle: the name the person typed, the
/// slug every id in the bundle is built from, and how many tools it carries.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct CustomPrinter {
    pub name: String,
    pub slug: String,
    pub tools: i32,
}

/// The directory every preset lives under: `$XDG_DATA_HOME/next-layer/presets`,
/// falling back to `$HOME/.local/share/next-layer/presets` exactly as
/// `QStandardPaths` does.
pub fn root() -> String {
    data_home() + "/next-layer/presets"
}

/// The bundle a user's own printers live in. Its name sorts after every vendor
/// bundle, so a machine a person made is never resolved before a real one.
pub fn custom_vendor_directory() -> String {
    root() + "/local/next-layer-custom/Custom"
}

/// Where the user bundle's printer manifest is written and read.
pub fn manifest_path() -> String {
    custom_vendor_directory() + "/printers.json"
}

/// Which of the shipped filament presets a bundle still has to be given: the names
/// whose target file is not there yet, in the order the library listed them.
///
/// The listing itself comes from C++ because `:/next-layer/filaments` is a Qt
/// resource and only `QDir` can enumerate one; the decision to copy is here, so the
/// library is never written twice over a file a person has edited.
pub fn filament_library_plan(directory: &str, library_names: &str) -> Vec<String> {
    let mut plan = Vec::new();
    for name in library_names.split('\n') {
        if name.is_empty() {
            continue;
        }
        if !std::path::Path::new(directory).join(name).exists() {
            plan.push(name.to_owned());
        }
    }
    plan
}

/// The file-name and id fragment a printer name turns into: lowercased, every run
/// of characters that are neither a Unicode letter nor a Unicode digit collapsed
/// into one `-`, and no trailing `-`.
pub fn slug_of(name: &str) -> String {
    let mut slug = String::with_capacity(name.len());
    for unit in name.to_lowercase().encode_utf16() {
        match char::from_u32(u32::from(unit)) {
            Some(character) if character.is_alphabetic() || character.is_numeric() => {
                slug.push(character);
            }
            _ if !slug.is_empty() && !slug.ends_with('-') => slug.push('-'),
            _ => {}
        }
    }
    while slug.ends_with('-') {
        slug.pop();
    }
    slug
}

/// `QUuid::createUuid().toString(QUuid::WithoutBraces)`: a version 4 UUID in its
/// thirty-six character canonical spelling. Qt draws it from the system random
/// generator and this reads the same source.
pub fn fresh_id() -> String {
    let mut bytes = [0u8; 16];
    random_bytes(&mut bytes);
    bytes[6] = (bytes[6] & 0x0f) | 0x40;
    bytes[8] = (bytes[8] & 0x3f) | 0x80;
    let mut text = String::with_capacity(36);
    for (index, byte) in bytes.iter().enumerate() {
        if matches!(index, 4 | 6 | 8 | 10) {
            text.push('-');
        }
        text.push(char::from(HEX[usize::from(byte >> 4)]));
        text.push(char::from(HEX[usize::from(byte & 0x0f)]));
    }
    text
}

/// Writes a file whole, creating the directory chain above it. False when either
/// step fails or the file could not be given every byte.
pub fn write_file(path: &str, contents: &str) -> bool {
    let target = std::path::Path::new(path);
    if let Some(parent) = target.parent() {
        if !parent.as_os_str().is_empty() && std::fs::create_dir_all(parent).is_err() {
            return false;
        }
    }
    std::fs::write(target, contents.as_bytes()).is_ok()
}

/// One preset document in the engine's own shape: the kind, the id, the name, then
/// the hardware condition, the parent it inherits and the values it overrides —
/// each present only when it was given, in that order, because the engine's reader
/// and this application's own lookups both take the file as written.
pub fn document(
    kind: &str,
    name: &str,
    parent: &str,
    condition: &str,
    values_yaml: &str,
    id: &str,
) -> String {
    let identity = if id.is_empty() { fresh_id() } else { id.to_owned() };
    let mut text = format!(
        "kind: {kind}\nid: {identity}\nname: {}\n",
        PresetYaml::quoted(name)
    );
    if !condition.is_empty() {
        text.push_str("condition: ");
        text.push_str(condition);
        text.push('\n');
    }
    if !parent.is_empty() {
        text.push_str("inherits:\n- ");
        text.push_str(&PresetYaml::quoted(parent));
        text.push('\n');
    }
    if !values_yaml.trim().is_empty() {
        text.push_str("values:\n");
        text.push_str(values_yaml);
    }
    text
}

/// The printers the user's own bundle lists, in manifest order. A manifest that
/// cannot be read, cannot be parsed, or holds no `printers` array reads as none:
/// the bundle then has no machines of its own rather than an error to report.
pub fn custom_printers() -> Vec<CustomPrinter> {
    match std::fs::read(manifest_path()) {
        Ok(body) => printers_from(&body),
        Err(_) => Vec::new(),
    }
}

/// The bundle's vendor description: one `printer_families` entry per machine, then
/// the tool and the sheet every machine here shares, then a `printer` and a
/// `printer_config` record each. Written whole every time, because the engine reads
/// it as one multi-document file.
pub fn vendor_yaml(printers: &[CustomPrinter]) -> String {
    let mut text = String::from(VENDOR_HEAD);
    for printer in printers {
        text.push_str("- label: ");
        text.push_str(&PresetYaml::quoted(&printer.name));
        text.push_str("\n  base_model: NL_");
        text.push_str(&printer.slug);
        text.push('\n');
    }
    text.push_str(VENDOR_TOOL_AND_SHEET);
    for printer in printers {
        let name = PresetYaml::quoted(&printer.name);
        let slug = printer.slug.as_str();
        let tools = printer.tools.max(1);
        text.push_str(&format!(
            "---\nkind: printer\ntechnology: FFF\nname: {name}\nid: NL_{slug}\n\
             model:\n  base_model: NL_{slug}\n  model: NL_{slug}_MODEL\n\
             tool_count: {tools}\nfeatures: {{}}\n"
        ));
        text.push_str(&format!(
            "---\nkind: printer_config\nid: nl-{slug}\nname: {name}\nprinter: NL_{slug}\n\
             legacy_printer_model:\n- {name}\ntool_count: {tools}\n\
             tools:\n- tool: '0.4'\nsheet: pei_smooth\n"
        ));
    }
    text
}

/// Registers a printer under the user's own bundle and rewrites the manifest and
/// the vendor description. An existing name keeps its slug, so a printer can be
/// rewritten without becoming a second machine, and keeps the tool count it was
/// given unless this call says otherwise. Empty on success, else why nothing was
/// written.
pub fn add_custom_printer(name: &str, tools: i32) -> String {
    let mut printers = custom_printers();
    let mut slug = String::new();
    let mut existing_tools = 0;
    for printer in &printers {
        if printer.name != name {
            continue;
        }
        slug = printer.slug.clone();
        existing_tools = printer.tools;
    }

    if slug.is_empty() {
        slug = slug_of(name);
        if slug.is_empty() {
            return NAME_NEEDS_A_CHARACTER.to_owned();
        }
        let base = slug.clone();
        let mut suffix = 2;
        while printers.iter().any(|printer| printer.slug == slug) {
            slug = format!("{base}-{suffix}");
            suffix += 1;
        }
        printers.push(CustomPrinter {
            name: name.to_owned(),
            slug: slug.clone(),
            tools: 1,
        });
    }

    let count = if tools > 0 {
        tools
    } else if existing_tools > 0 {
        existing_tools
    } else {
        DEFAULT_TOOLS
    }
    .max(1);
    for printer in &mut printers {
        if printer.name == name {
            printer.tools = count;
        }
    }

    if !write_file(&manifest_path(), &manifest_json(&printers)) {
        return MANIFEST_UNWRITTEN.to_owned();
    }
    if !write_file(
        &(custom_vendor_directory() + "/vendor.yaml"),
        &vendor_yaml(&printers),
    ) {
        return BUNDLE_UNWRITTEN.to_owned();
    }
    String::new()
}

/// The preset file a registered printer's settings are written to, or empty when
/// the bundle does not list that name.
pub fn custom_printer_path(name: &str) -> String {
    for printer in custom_printers() {
        if printer.name == name {
            return format!(
                "{}/preset-printer-{}.yaml",
                custom_vendor_directory(),
                printer.slug
            );
        }
    }
    String::new()
}

/// A bundle's printers cannot be evaluated without at least one tool-print preset:
/// the engine looks one up per hardware configuration and asserts when the
/// collection is empty. One empty `Standard` is what a bundle without a tool of its
/// own needs. Empty on success, else why nothing was written.
pub fn ensure_tool_print_preset() -> String {
    let path = custom_vendor_directory() + "/preset-tool-standard.yaml";
    if std::path::Path::new(&path).exists() {
        return String::new();
    }
    if write_file(&path, &document("tool_print", "Standard", "", "", "", "")) {
        String::new()
    } else {
        TOOL_PRESET_UNWRITTEN.to_owned()
    }
}

fn data_home() -> String {
    match std::env::var_os("XDG_DATA_HOME").map(|value| value.to_string_lossy().into_owned()) {
        Some(value) if value.starts_with('/') => value,
        _ => home_path() + "/.local/share",
    }
}

fn home_path() -> String {
    match std::env::var_os("HOME").map(|value| value.to_string_lossy().into_owned()) {
        Some(value) if !value.is_empty() => value,
        _ => "/".to_owned(),
    }
}

fn random_bytes(bytes: &mut [u8; 16]) {
    let mut filled = false;
    if let Ok(mut source) = std::fs::File::open(URANDOM) {
        filled = source.read_exact(&mut bytes[..]).is_ok();
    }
    if !filled {
        seeded(bytes);
    }
}

/// The id still has to be unique when there is no entropy source to draw on: two
/// presets sharing an id orphan the child that inherits one of them.
fn seeded(bytes: &mut [u8; 16]) {
    static COUNTER: AtomicU64 = AtomicU64::new(0);
    let nanos = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map_or(0, |since| since.as_nanos() as u64);
    let mut state = nanos
        ^ (u64::from(std::process::id()) << 32)
        ^ COUNTER.fetch_add(1, Ordering::Relaxed);
    for chunk in bytes.chunks_mut(8) {
        state = state.wrapping_add(0x9e37_79b9_7f4a_7c15);
        let mut mixed = state;
        mixed = (mixed ^ (mixed >> 30)).wrapping_mul(0xbf58_476d_1ce4_e5b9);
        mixed = (mixed ^ (mixed >> 27)).wrapping_mul(0x94d0_49bb_1331_11eb);
        mixed ^= mixed >> 31;
        chunk.copy_from_slice(&mixed.to_le_bytes()[..chunk.len()]);
    }
}

fn printers_from(body: &[u8]) -> Vec<CustomPrinter> {
    let body = body.strip_prefix(b"\xef\xbb\xbf".as_slice()).unwrap_or(body);
    let Ok(document) = serde_json::from_slice::<Value>(body) else {
        return Vec::new();
    };
    let Some(entries) = document.get("printers").and_then(Value::as_array) else {
        return Vec::new();
    };
    entries.iter().map(printer_of).collect()
}

fn printer_of(entry: &Value) -> CustomPrinter {
    CustomPrinter {
        name: text_field(entry, "name"),
        slug: text_field(entry, "slug"),
        tools: manifest_tools(entry.get("tools")).max(1),
    }
}

fn text_field(entry: &Value, key: &str) -> String {
    entry
        .get(key)
        .and_then(Value::as_str)
        .unwrap_or_default()
        .to_owned()
}

/// `QJsonValue::toInt(1)` on the manifest's `tools`: a number truncated toward zero
/// when it fits an `int`, and the default 1 for anything else — a string, a
/// boolean, a value out of range, or no value at all.
fn manifest_tools(value: Option<&Value>) -> i32 {
    let Some(Value::Number(number)) = value else {
        return DEFAULT_TOOLS;
    };
    let Some(double) = number.as_f64() else {
        return DEFAULT_TOOLS;
    };
    let truncated = double.trunc();
    if truncated < f64::from(i32::MIN) || truncated > f64::from(i32::MAX) {
        return DEFAULT_TOOLS;
    }
    truncated as i32
}

/// The manifest as `QJsonDocument::toJson(QJsonDocument::Indented)` writes it: four
/// spaces a level, one printer per object, a trailing newline, and an empty list
/// closed on its own line. The keys are written name, slug, tools because
/// `QJsonObject` sorts them and that is the sorted order.
fn manifest_json(printers: &[CustomPrinter]) -> String {
    let mut text = String::from("{\n    \"printers\": [\n");
    for (index, printer) in printers.iter().enumerate() {
        if index > 0 {
            text.push_str(",\n");
        }
        text.push_str("        {\n");
        text.push_str("            \"name\": ");
        text.push_str(&json_string(&printer.name));
        text.push_str(",\n            \"slug\": ");
        text.push_str(&json_string(&printer.slug));
        text.push_str(",\n            \"tools\": ");
        text.push_str(&printer.tools.to_string());
        text.push_str("\n        }");
    }
    if !printers.is_empty() {
        text.push('\n');
    }
    text.push_str("    ]\n}\n");
    text
}

/// One JSON string scalar as Qt's writer escapes it: the double quote, the
/// backslash and the five short control escapes, `\u00xy` for the rest below space,
/// and every other character — including 0x7f and anything above 0x80 — raw.
fn json_string(text: &str) -> String {
    let mut escaped = String::with_capacity(text.len() + 2);
    escaped.push('"');
    for character in text.chars() {
        match character {
            '"' => escaped.push_str("\\\""),
            '\\' => escaped.push_str("\\\\"),
            '\u{8}' => escaped.push_str("\\b"),
            '\u{c}' => escaped.push_str("\\f"),
            '\n' => escaped.push_str("\\n"),
            '\r' => escaped.push_str("\\r"),
            '\t' => escaped.push_str("\\t"),
            control if control < ' ' => {
                escaped.push_str(&format!("\\u00{:02x}", u32::from(control)));
            }
            other => escaped.push(other),
        }
    }
    escaped.push('"');
    escaped
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::path::PathBuf;

    const CUSTOM: &str = "oracle/fixtures/regression/presets/local/next-layer-custom/Custom";

    fn repository_root() -> PathBuf {
        PathBuf::from(env!("CARGO_MANIFEST_DIR"))
            .join("../..")
            .canonicalize()
            .expect("the repository root")
    }

    fn captured(name: &str) -> String {
        std::fs::read_to_string(repository_root().join(CUSTOM).join(name))
            .unwrap_or_else(|_| panic!("the captured bundle holds {name}"))
    }

    #[test]
    fn a_slug_lowercases_and_collapses_every_other_run() {
        assert_eq!(slug_of("Voron 2.4 350"), "voron-2-4-350");
        assert_eq!(slug_of("Voron 2.4 350 (0.6 nozzle)"), "voron-2-4-350-0-6-nozzle");
        assert_eq!(slug_of("  --Voron--2.4--  "), "voron-2-4");
        assert_eq!(slug_of("Voron 2"), slug_of("Voron-2"));
        assert_eq!(slug_of("Voron 2"), slug_of("VORON_2"));
        assert_eq!(slug_of(""), "");
        assert_eq!(slug_of("!!!"), "");
        assert_eq!(slug_of("-1"), "1");
    }

    #[test]
    fn a_slug_keeps_the_letters_of_every_script() {
        assert_eq!(slug_of("Принтер"), "принтер");
        assert_eq!(slug_of("打印机"), "打印机");
        assert_eq!(slug_of("Πρίντερ ٣"), "πρίντερ-٣");
    }

    #[test]
    fn a_fresh_id_is_the_uuid_the_store_writes() {
        let id = fresh_id();
        assert_eq!(id.len(), 36);
        let hyphens: Vec<usize> = id.match_indices('-').map(|(at, _)| at).collect();
        assert_eq!(hyphens, vec![8, 13, 18, 23]);
        assert!(id
            .chars()
            .all(|character| character == '-' || character.is_ascii_hexdigit()));
        assert!(!id.chars().any(|character| character.is_ascii_uppercase()));
        assert_eq!(&id[14..15], "4");
        assert!(matches!(&id[19..20], "8" | "9" | "a" | "b"));
        assert_ne!(id, fresh_id());
    }

    #[test]
    fn a_document_writes_kind_id_name_then_the_optional_parts() {
        assert_eq!(
            document("tool_print", "Standard", "", "", "", "f4ade5ee-080f-4bdb-8512-5a248fef9019"),
            "kind: tool_print\nid: f4ade5ee-080f-4bdb-8512-5a248fef9019\nname: 'Standard'\n"
        );
        assert_eq!(
            document(
                "printer",
                "Voron 2.4 350 (0.6 nozzle)",
                "Voron 2.4 350",
                "printer.model == \"NL_voron-2-4-350-0-6_MODEL\"",
                "  layer_height: 0.25\n",
                "nl-voron-2-4-350-0-6",
            ),
            concat!(
                "kind: printer\n",
                "id: nl-voron-2-4-350-0-6\n",
                "name: 'Voron 2.4 350 (0.6 nozzle)'\n",
                "condition: printer.model == \"NL_voron-2-4-350-0-6_MODEL\"\n",
                "inherits:\n- 'Voron 2.4 350'\n",
                "values:\n",
                "  layer_height: 0.25\n",
            )
        );
        assert_eq!(
            document("print", "Check Profile", "", "", "   ", "id"),
            "kind: print\nid: id\nname: 'Check Profile'\n"
        );
    }

    #[test]
    fn the_manifest_is_the_json_the_store_rereads() {
        let manifest = captured("printers.json");
        let printers = printers_from(manifest.as_bytes());
        assert_eq!(printers.len(), 4);
        assert_eq!(
            printers[1],
            CustomPrinter {
                name: "Voron 2.4 350 (0.6 nozzle)".to_owned(),
                slug: "voron-2-4-350-0-6".to_owned(),
                tools: 1,
            }
        );
        assert_eq!(manifest_json(&printers), manifest);
        assert_eq!(manifest_json(&[]), "{\n    \"printers\": [\n    ]\n}\n");
    }

    #[test]
    fn the_vendor_bundle_is_the_captured_one() {
        let printers = printers_from(captured("printers.json").as_bytes());
        assert_eq!(vendor_yaml(&printers), captured("vendor.yaml"));
    }

    #[test]
    fn a_malformed_manifest_reads_as_no_printers() {
        assert!(printers_from(b"").is_empty());
        assert!(printers_from(b"not json").is_empty());
        assert!(printers_from(b"{\"printers\":[],}").is_empty());
        assert!(printers_from(b"[1,2]").is_empty());
        assert!(printers_from(b"{\"printers\":{\"name\":\"x\"}}").is_empty());
        let bom = b"\xef\xbb\xbf{\"printers\":[]}";
        assert!(printers_from(bom).is_empty());
    }

    #[test]
    fn an_entry_that_is_not_an_object_is_still_a_printer() {
        assert_eq!(
            printers_from(b"{\"printers\":[null, 3, {\"name\":\"Voron 0.2\"}]}"),
            vec![
                CustomPrinter { name: String::new(), slug: String::new(), tools: 1 },
                CustomPrinter { name: String::new(), slug: String::new(), tools: 1 },
                CustomPrinter { name: "Voron 0.2".to_owned(), slug: String::new(), tools: 1 },
            ]
        );
    }

    #[test]
    fn a_tool_count_falls_back_the_way_the_json_reader_does() {
        let read = |body: &str| printers_from(body.as_bytes())[0].tools;
        assert_eq!(read("{\"printers\":[{\"tools\":3}]}"), 3);
        assert_eq!(read("{\"printers\":[{\"tools\":2.7}]}"), 2);
        assert_eq!(read("{\"printers\":[{\"tools\":-5}]}"), 1);
        assert_eq!(read("{\"printers\":[{\"tools\":0}]}"), 1);
        assert_eq!(read("{\"printers\":[{\"tools\":\"3\"}]}"), 1);
        assert_eq!(read("{\"printers\":[{\"tools\":true}]}"), 1);
        assert_eq!(read("{\"printers\":[{\"tools\":5000000000}]}"), 1);
        assert_eq!(read("{\"printers\":[{\"tools\":null}]}"), 1);
        assert_eq!(read("{\"printers\":[{}]}"), 1);
    }

    #[test]
    fn a_json_string_is_escaped_the_way_qt_escapes_one() {
        assert_eq!(json_string("Voron 2.4 350"), "\"Voron 2.4 350\"");
        assert_eq!(json_string("say \"hi\""), "\"say \\\"hi\\\"\"");
        assert_eq!(json_string("back\\slash"), "\"back\\\\slash\"");
        assert_eq!(json_string("tab\there"), "\"tab\\there\"");
        assert_eq!(json_string("a\u{8}b\u{c}c"), "\"a\\bb\\fc\"");
        assert_eq!(json_string("a\u{1}b\u{1f}c"), "\"a\\u0001b\\u001fc\"");
        assert_eq!(json_string("a\u{7f}b"), "\"a\u{7f}b\"");
        assert_eq!(json_string("Ünïcode ☃"), "\"Ünïcode ☃\"");
    }

    #[test]
    fn a_filament_plan_skips_what_is_already_there() {
        let directory = std::env::temp_dir().join(format!(
            "next-layer-filaments-{}",
            std::process::id()
        ));
        let _ = std::fs::remove_dir_all(&directory);
        std::fs::create_dir_all(&directory).expect("a bundle directory");
        std::fs::write(directory.join("generic-abs.yaml"), "").expect("an existing material");
        let directory = directory.to_string_lossy().into_owned();

        assert_eq!(
            filament_library_plan(&directory, "generic-abs.yaml\ngeneric-pla.yaml"),
            vec!["generic-pla.yaml".to_owned()]
        );
        assert!(filament_library_plan(&directory, "").is_empty());
        let _ = std::fs::remove_dir_all(&directory);
    }

    #[test]
    fn a_written_file_creates_its_directory_chain_and_truncates() {
        let directory = std::env::temp_dir().join(format!(
            "next-layer-write-{}",
            std::process::id()
        ));
        let _ = std::fs::remove_dir_all(&directory);
        let path = directory.join("local/next-layer-custom/Custom/preset-print-x.yaml");
        let path = path.to_string_lossy().into_owned();

        assert!(write_file(&path, "kind: print\nid: x\nname: 'X'\n"));
        assert_eq!(
            std::fs::read_to_string(&path).expect("the written preset"),
            "kind: print\nid: x\nname: 'X'\n"
        );
        assert!(write_file(&path, "short\n"));
        assert_eq!(
            std::fs::read_to_string(&path).expect("the rewritten preset"),
            "short\n"
        );
        let _ = std::fs::remove_dir_all(&directory);
    }
}
