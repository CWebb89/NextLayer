//! The project file: one JSON document holding the plate, its objects, their
//! geometry, paint and settings, and the print setup they were sliced with.
//!
//! Ported from `src/qt/src/ProjectFile.cpp`. Meshes travel as binary STL bytes and
//! are base64'd into the document, so a project is one file that moves between
//! machines without the models it was built from; the STL encoding itself stays on
//! the C++ side of the bridge, which is where the engine's mesh still lives.
//!
//! Keys are written in alphabetical order rather than the order the C++ wrote them,
//! and the document is indented by two spaces instead of one. Nothing reads this
//! file positionally: the loader is by name, and the version test re-parses the body
//! with its own JSON reader before handing it back.

use crate::bridge::{
    ProjectMarkerRecord, ProjectMetadataRecord, ProjectObjectRecord, ProjectOutcome,
    ProjectPaint, ProjectPaintOutcome, ProjectSetting, ProjectVolumeRecord,
};
use serde_json::{json, Map, Value};

/// What a project file says it is. A file that does not is not one of ours.
const FORMAT: &str = "next-layer project";

/// The project file version this build writes, and the newest it will read.
const VERSION: i64 = 1;

/// Settings values, as the record discriminates them: text, boolean, integer,
/// number, or the `{value, is_percent}` object a percentage is stored as.
const SETTING_TEXT: u8 = 0;
const SETTING_BOOL: u8 = 1;
const SETTING_INT: u8 = 2;
const SETTING_NUMBER: u8 = 3;
const SETTING_PERCENT: u8 = 4;

/// The project file version this build writes, and the newest it will read.
pub fn format_version() -> i32 {
    VERSION as i32
}

/// The document, as the bytes of a project file.
pub fn serialise_project(
    objects: &[ProjectObjectRecord],
    metadata: &ProjectMetadataRecord,
) -> Vec<u8> {
    let mut root = Map::new();
    root.insert("format".to_string(), json!(FORMAT));
    root.insert("version".to_string(), json!(VERSION));
    root.insert(
        "application".to_string(),
        json!(metadata.application.as_str()),
    );
    root.insert(
        "print_setup".to_string(),
        json!({
            "printer": metadata.printer.as_str(),
            "process": metadata.process.as_str(),
            "filament": metadata.filament.as_str(),
        }),
    );
    root.insert("active_plate".to_string(), json!(metadata.active_plate));
    root.insert("plates".to_string(), json!(metadata.plate_count));

    let plate_flags: Vec<Value> = (0..metadata.plate_count.max(0) as usize)
        .map(|index| {
            json!({
                "name": metadata.plate_names.get(index).map_or("", String::as_str),
                "locked": metadata.plate_locked.get(index).copied().unwrap_or(false),
            })
        })
        .collect();
    root.insert("plate_flags".to_string(), Value::Array(plate_flags));

    let tool_colours: Vec<Value> = metadata
        .tool_colours
        .iter()
        .map(|colour| json!(colour.as_str()))
        .collect();
    root.insert("tool_colours".to_string(), Value::Array(tool_colours));

    let markers: Vec<Value> = metadata
        .markers
        .iter()
        .map(marker_json)
        .collect();
    root.insert("layer_gcodes".to_string(), Value::Array(markers));

    let list: Vec<Value> = objects.iter().map(object_json).collect();
    root.insert("objects".to_string(), Value::Array(list));

    serde_json::to_vec_pretty(&Value::Object(root)).unwrap_or_default()
}

fn marker_json(marker: &ProjectMarkerRecord) -> Value {
    json!({
        "z": finite(marker.z),
        "type": marker.kind.as_str(),
        "text": marker.text.as_str(),
    })
}

fn object_json(object: &ProjectObjectRecord) -> Value {
    let volumes: Vec<Value> = object.volumes.iter().map(volume_json).collect();

    let mut settings = Map::new();
    for setting in &object.settings {
        settings.insert(setting.name.clone(), setting_json(setting));
    }

    let profile: Vec<Value> = object
        .layer_heights
        .chunks_exact(2)
        .map(|pair| json!([finite(pair[0]), finite(pair[1])]))
        .collect();

    json!({
        "name": object.name.as_str(),
        "source": object.source.as_str(),
        "plate": object.plate,
        "visible": object.visible,
        "matrix": matrix_json(&object.matrix),
        "mesh": crate::base64::encode(&object.mesh),
        "volumes": volumes,
        "settings": Value::Object(settings),
        "layer_heights": profile,
        "support_paint": paint_json(&object.support_paint),
        "seam_paint": paint_json(&object.seam_paint),
        "tool_paint": paint_json(&object.tool_paint),
    })
}

fn volume_json(volume: &ProjectVolumeRecord) -> Value {
    json!({
        "name": volume.name.as_str(),
        "type": volume.kind.as_str(),
        "visible": volume.visible,
        "matrix": matrix_json(&volume.matrix),
        "mesh": crate::base64::encode(&volume.mesh),
    })
}

fn matrix_json(matrix: &[f64]) -> Value {
    let mut json = Vec::with_capacity(16);
    for index in 0..16 {
        json.push(json!(finite(matrix.get(index).copied().unwrap_or(0.0))));
    }
    Value::Array(json)
}

fn setting_json(setting: &ProjectSetting) -> Value {
    match setting.kind {
        SETTING_BOOL => json!(setting.truth),
        SETTING_INT => json!(setting.number as i64),
        SETTING_NUMBER => json!(finite(setting.number)),
        SETTING_PERCENT => json!({
            "value": finite(setting.number),
            "is_percent": setting.truth,
        }),
        _ => json!(setting.text.as_str()),
    }
}

fn paint_json(paint: &ProjectPaint) -> Value {
    if paint.triangles.is_empty() {
        return json!({});
    }

    let splits: Vec<Value> = paint
        .triangles
        .chunks_exact(2)
        .map(|pair| json!([pair[0], pair[1]]))
        .collect();

    json!({
        "triangles": splits,
        "bitstream": paint.bitstream.as_str(),
        "states": paint.states,
    })
}

/// A number JSON can carry. A NaN or an infinity has no spelling in the format, and
/// a project never means one.
fn finite(value: f64) -> f64 {
    if value.is_finite() {
        value
    } else {
        0.0
    }
}

/// The document a project file carries, or why it could not be read.
pub fn parse_project(body: &[u8]) -> ProjectOutcome {
    let Ok(root) = serde_json::from_slice::<Value>(body) else {
        return failure("this is not a next-layer project file");
    };
    if !root.is_object() || root.get("format").and_then(Value::as_str) != Some(FORMAT) {
        return failure("this is not a next-layer project file");
    }
    if root.get("version").and_then(Value::as_i64).unwrap_or(0) > VERSION {
        return failure("this project was written by a newer next-layer");
    }

    let mut metadata = metadata_from(&root);

    let mut objects = Vec::new();
    for entry in array(root.get("objects")) {
        let name = text(entry.get("name"));
        let mesh = match mesh_bytes(entry.get("mesh")) {
            Some(mesh) => mesh,
            None => {
                return failure(&format!(
                    "object \"{name}\" has no readable mesh"
                ))
            }
        };

        let mut volumes = Vec::new();
        for stored in array(entry.get("volumes")) {
            let volume_name = text(stored.get("name"));
            let Some(volume_mesh) = mesh_bytes(stored.get("mesh")) else {
                return failure(&format!(
                    "volume \"{volume_name}\" of \"{name}\" has no readable mesh"
                ));
            };
            volumes.push(ProjectVolumeRecord {
                name: volume_name,
                kind: defaulted(stored.get("type"), "part"),
                visible: stored
                    .get("visible")
                    .and_then(Value::as_bool)
                    .unwrap_or(true),
                matrix: matrix_from(stored.get("matrix")),
                mesh: volume_mesh,
            });
        }

        objects.push(ProjectObjectRecord {
            name,
            source: text(entry.get("source")),
            plate: entry.get("plate").and_then(Value::as_i64).unwrap_or(0) as i32,
            visible: entry
                .get("visible")
                .and_then(Value::as_bool)
                .unwrap_or(true),
            matrix: matrix_from(entry.get("matrix")),
            mesh,
            volumes,
            settings: settings_from(entry.get("settings")),
            layer_heights: profile_from(entry.get("layer_heights")),
            support_paint: paint_from(entry.get("support_paint")),
            seam_paint: paint_from(entry.get("seam_paint")),
            tool_paint: paint_from(entry.get("tool_paint")),
        });
    }

    if objects.is_empty() {
        return failure("this project has nothing on any plate");
    }

    metadata.markers = metadata
        .markers
        .into_iter()
        .filter(|marker| marker.z > 0.0)
        .collect();

    ProjectOutcome {
        ok: true,
        error: String::new(),
        objects,
        metadata,
    }
}

fn metadata_from(root: &Value) -> ProjectMetadataRecord {
    let setup = root.get("print_setup");
    let mut plate_names = Vec::new();
    let mut plate_locked = Vec::new();
    for entry in array(root.get("plate_flags")) {
        plate_names.push(text(entry.get("name")));
        plate_locked.push(entry.get("locked").and_then(Value::as_bool).unwrap_or(false));
    }

    ProjectMetadataRecord {
        application: text(root.get("application")),
        printer: text(setup.and_then(|setup| setup.get("printer"))),
        process: text(setup.and_then(|setup| setup.get("process"))),
        filament: text(setup.and_then(|setup| setup.get("filament"))),
        active_plate: root.get("active_plate").and_then(Value::as_i64).unwrap_or(0) as i32,
        plate_count: root.get("plates").and_then(Value::as_i64).unwrap_or(1) as i32,
        plate_names,
        plate_locked,
        tool_colours: array(root.get("tool_colours"))
            .iter()
            .map(|colour| colour.as_str().unwrap_or_default().to_string())
            .collect(),
        markers: array(root.get("layer_gcodes"))
            .iter()
            .map(|marker| ProjectMarkerRecord {
                z: marker.get("z").and_then(Value::as_f64).unwrap_or(0.0),
                kind: defaulted(marker.get("type"), "gcode"),
                text: text(marker.get("text")),
            })
            .collect(),
    }
}

fn settings_from(settings: Option<&Value>) -> Vec<ProjectSetting> {
    let Some(Value::Object(map)) = settings else {
        return Vec::new();
    };

    map.iter()
        .map(|(name, value)| {
            let mut setting = ProjectSetting {
                name: name.clone(),
                kind: SETTING_TEXT,
                truth: false,
                number: 0.0,
                text: String::new(),
            };
            match value {
                Value::Bool(truth) => {
                    setting.kind = SETTING_BOOL;
                    setting.truth = *truth;
                }
                Value::Number(number) if number.is_i64() || number.is_u64() => {
                    setting.kind = SETTING_INT;
                    setting.number = number.as_f64().unwrap_or(0.0);
                }
                Value::Number(number) => {
                    setting.kind = SETTING_NUMBER;
                    setting.number = number.as_f64().unwrap_or(0.0);
                }
                Value::Object(object) => {
                    setting.kind = SETTING_PERCENT;
                    setting.number = object.get("value").and_then(Value::as_f64).unwrap_or(0.0);
                    setting.truth = object
                        .get("is_percent")
                        .and_then(Value::as_bool)
                        .unwrap_or(false);
                }
                Value::String(text) => setting.text = text.clone(),
                other => setting.text = other.to_string(),
            }
            setting
        })
        .collect()
}

fn profile_from(profile: Option<&Value>) -> Vec<f64> {
    let mut heights = Vec::new();
    for pair in array(profile) {
        let Some(entry) = pair.as_array() else {
            continue;
        };
        let (Some(z), Some(height)) = (
            entry.first().and_then(Value::as_f64),
            entry.get(1).and_then(Value::as_f64),
        ) else {
            continue;
        };
        heights.push(z);
        heights.push(height);
    }
    heights
}

fn paint_from(json: Option<&Value>) -> ProjectPaint {
    let empty = || ProjectPaint {
        triangles: Vec::new(),
        bitstream: String::new(),
        states: Vec::new(),
    };
    let Some(Value::Object(object)) = json else {
        return empty();
    };
    if !object.contains_key("triangles") {
        return empty();
    }

    let mut triangles = Vec::new();
    for pair in array(object.get("triangles")) {
        let Some(entry) = pair.as_array() else {
            continue;
        };
        triangles.push(entry.first().and_then(Value::as_i64).unwrap_or(0));
        triangles.push(entry.get(1).and_then(Value::as_i64).unwrap_or(0));
    }

    ProjectPaint {
        triangles,
        bitstream: text(object.get("bitstream")),
        states: array(object.get("states"))
            .iter()
            .filter_map(|state| state.as_u64())
            .map(|state| state as u32)
            .collect(),
    }
}

fn mesh_bytes(mesh: Option<&Value>) -> Option<Vec<u8>> {
    let bytes = crate::base64::decode(mesh?.as_str()?)?;
    if bytes.is_empty() {
        return None;
    }
    Some(bytes)
}

fn matrix_from(matrix: Option<&Value>) -> Vec<f64> {
    let mut values = Vec::with_capacity(16);
    for index in 0..16 {
        values.push(
            array(matrix)
                .get(index)
                .and_then(Value::as_f64)
                .unwrap_or(if index % 5 == 0 { 1.0 } else { 0.0 }),
        );
    }
    values
}

fn array(value: Option<&Value>) -> &[Value] {
    match value {
        Some(Value::Array(entries)) => entries,
        _ => &[],
    }
}

fn text(value: Option<&Value>) -> String {
    value
        .and_then(Value::as_str)
        .unwrap_or_default()
        .to_string()
}

fn defaulted(value: Option<&Value>, fallback: &str) -> String {
    match value.and_then(Value::as_str) {
        Some(text) if !text.is_empty() => text.to_string(),
        _ => fallback.to_string(),
    }
}

fn failure(error: &str) -> ProjectOutcome {
    ProjectOutcome {
        ok: false,
        error: error.to_string(),
        objects: Vec::new(),
        metadata: empty_metadata(),
    }
}

fn empty_metadata() -> ProjectMetadataRecord {
    ProjectMetadataRecord {
        application: String::new(),
        printer: String::new(),
        process: String::new(),
        filament: String::new(),
        active_plate: 0,
        plate_count: 1,
        plate_names: Vec::new(),
        plate_locked: Vec::new(),
        tool_colours: Vec::new(),
        markers: Vec::new(),
    }
}

/// One paint channel as text, and back. The encoding is the project file's own, so
/// a value written years ago still reads.
pub fn paint_to_text(paint: &ProjectPaint) -> Vec<u8> {
    serde_json::to_vec(&paint_json(paint)).unwrap_or_default()
}

pub fn paint_from_text(text: &[u8]) -> ProjectPaintOutcome {
    match serde_json::from_slice::<Value>(text) {
        Ok(json) => ProjectPaintOutcome {
            ok: true,
            paint: paint_from(Some(&json)),
        },
        Err(_) => ProjectPaintOutcome {
            ok: false,
            paint: paint_from(None),
        },
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn identity() -> Vec<f64> {
        matrix_from(None)
    }

    fn paint(triangle: i64, start: i64, bits: &str, states: Vec<u32>) -> ProjectPaint {
        ProjectPaint {
            triangles: vec![triangle, start],
            bitstream: bits.to_string(),
            states,
        }
    }

    fn object(name: &str) -> ProjectObjectRecord {
        ProjectObjectRecord {
            name: name.to_string(),
            source: "/models/cube.stl".to_string(),
            plate: 1,
            visible: true,
            matrix: identity(),
            mesh: b"solid mesh bytes".to_vec(),
            volumes: Vec::new(),
            settings: Vec::new(),
            layer_heights: Vec::new(),
            support_paint: paint(7, 0, "0110", vec![1, 3]),
            seam_paint: paint(7, 0, "1010", vec![5]),
            tool_paint: ProjectPaint {
                triangles: Vec::new(),
                bitstream: String::new(),
                states: Vec::new(),
            },
        }
    }

    fn metadata() -> ProjectMetadataRecord {
        ProjectMetadataRecord {
            application: "0.1.2".to_string(),
            printer: "Voron 2.4 350".to_string(),
            process: "0.2 standard".to_string(),
            filament: "ABS".to_string(),
            active_plate: 1,
            plate_count: 2,
            plate_names: vec!["one".to_string(), "two".to_string()],
            plate_locked: vec![false, true],
            tool_colours: vec!["#ff0000".to_string()],
            markers: vec![ProjectMarkerRecord {
                z: 4.2,
                kind: "pause".to_string(),
                text: "insert magnets".to_string(),
            }],
        }
    }

    #[test]
    fn a_document_survives_a_round_trip() {
        let mut entry = object("cube");
        entry.settings = vec![
            ProjectSetting {
                name: "brim_width".to_string(),
                kind: SETTING_NUMBER,
                truth: false,
                number: 5.5,
                text: String::new(),
            },
            ProjectSetting {
                name: "perimeters".to_string(),
                kind: SETTING_INT,
                truth: false,
                number: 4.0,
                text: String::new(),
            },
            ProjectSetting {
                name: "support_material".to_string(),
                kind: SETTING_TEXT,
                truth: false,
                number: 0.0,
                text: "everywhere".to_string(),
            },
            ProjectSetting {
                name: "fill_density".to_string(),
                kind: SETTING_PERCENT,
                truth: true,
                number: 15.0,
                text: String::new(),
            },
            ProjectSetting {
                name: "spiral_vase".to_string(),
                kind: SETTING_BOOL,
                truth: true,
                number: 0.0,
                text: String::new(),
            },
        ];
        entry.layer_heights = vec![1.0, 0.2, 5.0, 0.1];
        entry.volumes = vec![ProjectVolumeRecord {
            name: "hole".to_string(),
            kind: "negative".to_string(),
            visible: false,
            matrix: identity(),
            mesh: b"volume mesh".to_vec(),
        }];

        let body = serialise_project(std::slice::from_ref(&entry), &metadata());
        let parsed = parse_project(&body);
        assert!(parsed.ok, "{}", parsed.error);

        assert_eq!(parsed.objects.len(), 1);
        let read = &parsed.objects[0];
        assert_eq!(read.name, "cube");
        assert_eq!(read.mesh, b"solid mesh bytes");
        assert_eq!(read.settings.len(), 5);
        assert_eq!(read.layer_heights, vec![1.0, 0.2, 5.0, 0.1]);
        assert_eq!(read.volumes.len(), 1);
        assert_eq!(read.volumes[0].kind, "negative");
        assert!(!read.volumes[0].visible);
        assert_eq!(read.support_paint.triangles, vec![7, 0]);
        assert_eq!(read.support_paint.bitstream, "0110");
        assert_eq!(read.support_paint.states, vec![1, 3]);
        assert_eq!(read.tool_paint.bitstream, "");

        assert_eq!(parsed.metadata.printer, "Voron 2.4 350");
        assert_eq!(parsed.metadata.process, "0.2 standard");
        assert_eq!(parsed.metadata.filament, "ABS");
        assert_eq!(parsed.metadata.application, "0.1.2");
        assert_eq!(parsed.metadata.plate_count, 2);
        assert_eq!(parsed.metadata.plate_names, vec!["one", "two"]);
        assert_eq!(parsed.metadata.plate_locked, vec![false, true]);
        assert_eq!(parsed.metadata.markers.len(), 1);
        assert_eq!(parsed.metadata.markers[0].text, "insert magnets");
    }

    #[test]
    fn a_setting_keeps_its_kind() {
        let mut entry = object("cube");
        entry.settings = vec![
            ProjectSetting {
                name: "text".to_string(),
                kind: SETTING_TEXT,
                truth: false,
                number: 0.0,
                text: "everywhere".to_string(),
            },
            ProjectSetting {
                name: "percent".to_string(),
                kind: SETTING_PERCENT,
                truth: false,
                number: 15.0,
                text: String::new(),
            },
        ];
        let parsed = parse_project(&serialise_project(&[entry], &metadata()));
        let percent = parsed.objects[0].settings.iter().find(|s| s.name == "percent").unwrap();
        assert_eq!(percent.kind, SETTING_PERCENT);
        assert!(!percent.truth);
        assert_eq!(percent.number, 15.0);
    }

    #[test]
    fn the_version_policy_refuses_only_what_is_newer() {
        let body = serialise_project(&[object("cube")], &metadata());
        let text = String::from_utf8(body).unwrap();

        assert!(parse_project(text.as_bytes()).ok);

        let older = text.replace("\"version\": 1", "\"version\": 0");
        assert!(parse_project(older.as_bytes()).ok);

        let missing = text.replace("\"version\": 1,", "");
        assert!(parse_project(missing.as_bytes()).ok);

        let newer = text.replace("\"version\": 1", "\"version\": 2");
        let refused = parse_project(newer.as_bytes());
        assert!(!refused.ok);
        assert!(refused.error.contains("newer next-layer"));
    }

    #[test]
    fn something_that_is_not_a_project_says_so() {
        assert_eq!(parse_project(b"{}").error, "this is not a next-layer project file");
        assert_eq!(parse_project(b"not json").error, "this is not a next-layer project file");
        assert_eq!(
            parse_project(br#"{"format": "orcaslicer"}"#).error,
            "this is not a next-layer project file"
        );
    }

    #[test]
    fn an_empty_project_says_so() {
        let body = serialise_project(&[], &metadata());
        assert_eq!(parse_project(&body).error, "this project has nothing on any plate");
    }

    #[test]
    fn a_mesh_that_is_not_base64_names_the_object() {
        let body = serialise_project(&[object("cube")], &metadata());
        let text = String::from_utf8(body).unwrap();
        let damaged = text.replace(&crate::base64::encode(b"solid mesh bytes"), "not base64 !");
        assert_eq!(
            parse_project(damaged.as_bytes()).error,
            "object \"cube\" has no readable mesh"
        );
    }

    #[test]
    fn a_marker_below_the_bed_is_dropped() {
        let mut setup = metadata();
        setup.markers.push(ProjectMarkerRecord {
            z: 0.0,
            kind: "pause".to_string(),
            text: "not yet".to_string(),
        });
        let parsed = parse_project(&serialise_project(&[object("cube")], &setup));
        assert_eq!(parsed.metadata.markers.len(), 1);
    }

    #[test]
    fn a_paint_channel_survives_its_text() {
        let channel = paint(11, 4, "1100101", vec![0, 2, 6]);
        let text = paint_to_text(&channel);
        let read = paint_from_text(&text);
        assert!(read.ok);
        assert_eq!(read.paint.triangles, channel.triangles);
        assert_eq!(read.paint.bitstream, channel.bitstream);
        assert_eq!(read.paint.states, channel.states);

        assert_eq!(paint_to_text(&paint_from(None)), b"{}");
        assert!(!paint_from_text(b"{ not json").ok);
    }
}
