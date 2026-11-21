//! The plate half of the engine boundary: build the document the engine slices,
//! read it back, save it as the engine's project file, and hand it to the slicer.
//!
//! Ported from `src/qt/src/HfwEngine.cpp`. No engine type is named here: the
//! document's own mesh, transform and paint are translated into the plain structs
//! `src/app/src/bridge.rs` declares, by `HfwEngine.hpp`, which is the only Qt-typed
//! code this module has. Everything below speaks those structs and the `hfw_*`
//! calls `crate::hfw` wraps.
//!
//! The conventions the boundary settled are load-bearing and each is applied where
//! it belongs:
//!
//! - Geometry reaches `hfw_mesh_new` as arrays on every path that carries paint.
//!   Triangle `i` of the handle is triangle `i` of the indices, so the per-triangle
//!   states stay aligned; STL bytes are the project file's transport and are not
//!   used here, because a repair in between may renumber a triangle.
//! - Placement crosses as sixteen `f64`, row-major. A matrix that shears, collapses
//!   an axis or is not affine is refused with a reason rather than decomposed: a
//!   scale triple and an Euler triple cannot carry it.
//! - Paint crosses dense, one byte per triangle per channel: `0` none, `1`
//!   enforcer, `2` blocker. The `{triangles, bitstream, states}` record is the
//!   project file's own encoding and is not what the ABI takes.
//! - Layer heights cross as interleaved `(z, height)` pairs, and zero pairs mean the
//!   plate's uniform height rather than one pair equal to it.
//!
//! Tool paint has no channel and none is expected: this engine drives one extruder,
//! so an object that carries tool paint has that part of it dropped, and the reason
//! travels back to the caller rather than being kept in the hope of a later pass.

use std::collections::BTreeMap;
use std::fs;
use std::path::Path;

use serde_json::Value;

use crate::bridge::{
    EngineMarker, EngineObject, EngineOutcome, EnginePlate, EngineSetting, EngineStroke,
    EngineVolume, PlateOutcome, PlateReadback, ReadbackObject, ReadbackVolume,
};
use crate::hfw;

/// `HFW_PAINT_SUPPORT`: the support enforcer and blocker channel.
const PAINT_SUPPORT: i32 = 0;
/// `HFW_PAINT_SEAM`: the seam enforcer and blocker channel.
const PAINT_SEAM: i32 = 1;
/// `HFW_VOLUME_PART`.
const VOLUME_PART: i32 = 0;
/// `HFW_VOLUME_NEGATIVE`.
const VOLUME_NEGATIVE: i32 = 1;
/// `HFW_VOLUME_SUPPORT_BLOCKER`.
const VOLUME_SUPPORT_BLOCKER: i32 = 2;
/// `HFW_VOLUME_SUPPORT_ENFORCER`.
const VOLUME_SUPPORT_ENFORCER: i32 = 3;
/// `HFW_VOLUME_MODIFIER`.
const VOLUME_MODIFIER: i32 = 4;
/// `HFW_MARKER_CUSTOM`.
const MARKER_CUSTOM: i32 = 0;
/// `HFW_MARKER_PAUSE`.
const MARKER_PAUSE: i32 = 1;
/// `HFW_MARKER_COLOUR`.
const MARKER_COLOUR: i32 = 2;
/// How far a placement may sit from an affine scale-and-rotation before it is refused.
const PLACEMENT_TOLERANCE: f64 = 1e-9;
/// The significant digits `QString::number(value, 'g', 10)` prints.
const SETTING_DIGITS: i32 = 10;
/// Axis names, for the reason a refused placement gives.
const AXES: [&str; 3] = ["X", "Y", "Z"];

/// The engine code for a volume kind, named in the document's vocabulary.
fn volume_code(kind: &str) -> Result<i32, String> {
    match kind {
        "part" => Ok(VOLUME_PART),
        "negative" => Ok(VOLUME_NEGATIVE),
        "blocker" => Ok(VOLUME_SUPPORT_BLOCKER),
        "enforcer" => Ok(VOLUME_SUPPORT_ENFORCER),
        "modifier" => Ok(VOLUME_MODIFIER),
        other => Err(format!(
            "`{other}` is not a volume kind: a volume is a part, a negative, a modifier, a \
             support blocker or a support enforcer"
        )),
    }
}

/// The volume kind a code stands for, the exact inverse of `volume_code`.
fn volume_kind_name(code: i32) -> Result<&'static str, String> {
    match code {
        VOLUME_PART => Ok("part"),
        VOLUME_NEGATIVE => Ok("negative"),
        VOLUME_SUPPORT_BLOCKER => Ok("blocker"),
        VOLUME_SUPPORT_ENFORCER => Ok("enforcer"),
        VOLUME_MODIFIER => Ok("modifier"),
        other => Err(format!(
            "the engine returned volume kind {other}, and this application knows five"
        )),
    }
}

/// The engine code for a marker kind.
///
/// The document's vocabulary is wider than the engine's: `color` and `colour` are
/// one kind, and every other spelling — including `tool` — is a custom marker here.
/// The engine has no tool change because it prints with one extruder.
fn marker_code(kind: &str) -> i32 {
    match kind {
        "pause" => MARKER_PAUSE,
        "color" | "colour" => MARKER_COLOUR,
        _ => MARKER_CUSTOM,
    }
}

/// The document's spelling for a marker kind the engine returned.
fn marker_kind_name(code: i32) -> Result<&'static str, String> {
    match code {
        MARKER_CUSTOM => Ok("gcode"),
        MARKER_PAUSE => Ok("pause"),
        MARKER_COLOUR => Ok("color"),
        other => Err(format!(
            "the engine returned marker kind {other}, and this application knows three"
        )),
    }
}

/// One setting value as the text the engine's preset reader takes it as.
///
/// `50%` for a percentage, `1`/`0` for a flag, a comma-joined list for a vector, and
/// the plain value for anything else. A value this shape cannot carry is empty, and
/// an empty value is a setting the document did not really hold.
fn setting_text(value: &Value) -> String {
    match value {
        Value::Bool(flag) => {
            if *flag {
                "1".to_owned()
            } else {
                "0".to_owned()
            }
        }
        Value::Number(number) => setting_number(number.as_f64().unwrap_or_default()),
        Value::String(text) => text.clone(),
        Value::Object(fields) => {
            let Some(value) = fields.get("value") else {
                return String::new();
            };
            let number = setting_text(value);
            if fields
                .get("is_percent")
                .and_then(Value::as_bool)
                .unwrap_or(false)
            {
                format!("{number}%")
            } else {
                number
            }
        }
        Value::Array(items) => items
            .iter()
            .map(setting_text)
            .collect::<Vec<String>>()
            .join(","),
        Value::Null => String::new(),
    }
}

/// One number as `QString::number(value, 'g', 10)` spells it.
///
/// Ten significant digits, trailing zeros removed, and the exponent form outside
/// `1e-4 <= |value| < 1e10` with a signed two-digit exponent. Rust's `Display` and
/// `ryu` write the same number differently (`0.00001` against `1e-05`), and the
/// text is what the engine's reader sees, so this is not a formatting preference.
fn setting_number(value: f64) -> String {
    if value.is_nan() {
        return "nan".to_owned();
    }
    if value.is_infinite() {
        return if value.is_sign_negative() {
            "-inf".to_owned()
        } else {
            "inf".to_owned()
        };
    }
    if value == 0.0 {
        return if value.is_sign_negative() {
            "-0".to_owned()
        } else {
            "0".to_owned()
        };
    }

    let mut exponent = decimal_exponent(value);
    if exponent < -4 || exponent >= SETTING_DIGITS {
        let mut mantissa = fixed_digits(value / 10f64.powi(exponent), (SETTING_DIGITS - 1) as usize);
        if mantissa == "10" || mantissa == "-10" {
            mantissa.pop();
            exponent += 1;
        }
        format!(
            "{mantissa}e{}{:02}",
            if exponent < 0 { '-' } else { '+' },
            exponent.unsigned_abs()
        )
    } else {
        fixed_digits(value, (SETTING_DIGITS - 1 - exponent) as usize)
    }
}

/// The power of ten the leading digit of `value` sits at.
fn decimal_exponent(value: f64) -> i32 {
    let shortest = format!("{value:e}");
    let at = shortest.find('e').unwrap_or(shortest.len());
    shortest[at + 1..].parse().unwrap_or_default()
}

/// `value` to `decimals` places with the trailing zeros and the point taken off.
fn fixed_digits(value: f64, decimals: usize) -> String {
    let mut text = format!("{value:.decimals$}");
    if text.contains('.') {
        while text.ends_with('0') {
            text.pop();
        }
        if text.ends_with('.') {
            text.pop();
        }
    }
    text
}

/// One settings document as its fields, refusing text that is not a JSON object.
fn settings_object(json: &str, name: &str) -> Result<serde_json::Map<String, Value>, String> {
    if json.trim().is_empty() {
        return Ok(serde_json::Map::new());
    }
    match serde_json::from_str::<Value>(json) {
        Ok(Value::Object(fields)) => Ok(fields),
        Ok(_) => Err(format!("{name}: the settings are not a JSON object")),
        Err(error) => Err(format!("{name}: the settings are not JSON: {error}")),
    }
}

/// An object's settings as the rows the engine reads: its own overrides first, then
/// the ones its file carried in another slicer's dialect, which cross unchanged.
///
/// `layer_height` is taken out: it is not an object setting on this engine but a
/// channel of its own, so it reaches the plate as a `(z, height)` pair instead. The
/// rows come out in name order, which is the order the engine's first refusal names.
fn object_overrides(object: &EngineObject) -> Result<BTreeMap<String, String>, String> {
    let mut rows = BTreeMap::new();
    for (name, value) in settings_object(&object.settings_json, &object.name)? {
        let text = setting_text(&value);
        if !text.is_empty() {
            rows.insert(name, text);
        }
    }
    for setting in &object.foreign_settings {
        rows.insert(setting.name.clone(), setting.value.clone());
    }
    rows.remove("layer_height");
    Ok(rows)
}

/// The object's uniform layer height, when it carries one as an override.
///
/// A layer height is not an object setting on this engine: it decides which planes
/// the object is sliced on, so it is folded into the object's layer height pairs and
/// a uniform override and a variable profile reach the engine the same way. Zero
/// means the object carries none.
fn layer_height_override(object: &EngineObject) -> Result<f64, String> {
    let settings = settings_object(&object.settings_json, &object.name)?;
    if let Some(value) = settings.get("layer_height") {
        let height = setting_text(value).parse::<f64>().unwrap_or_default();
        if height > 0.0 {
            return Ok(height);
        }
    }
    if let Some(setting) = object
        .foreign_settings
        .iter()
        .find(|setting| setting.name == "layer_height")
    {
        let height = setting.value.parse::<f64>().unwrap_or_default();
        if height > 0.0 {
            return Ok(height);
        }
    }
    Ok(0.0)
}

/// The engine's handle for one mesh, built from the arrays so its triangle order is
/// the order the caller holds.
fn mesh_handle(vertices: &[f64], indices: &[i64], name: &str) -> Result<hfw::Mesh, String> {
    if vertices.is_empty() || indices.is_empty() {
        return Err(format!("{name} has no triangles"));
    }
    if !vertices.len().is_multiple_of(3) || !indices.len().is_multiple_of(3) {
        return Err(format!("{name}: the geometry is not whole triangles"));
    }
    let mut corners = Vec::with_capacity(indices.len());
    for index in indices {
        let corner = u32::try_from(*index)
            .map_err(|_| format!("{name}: triangle corner {index} is not a vertex number"))?;
        corners.push(corner);
    }
    hfw::Mesh::from_arrays(vertices, &corners).map_err(|error| format!("{name}: {error}"))
}

/// A placement as the engine records it, or the reason it cannot record this one.
///
/// Sixteen numbers, row-major. The linear block is `R * S`, so its three columns are
/// the scaled axes: a column of zero length collapses an axis, two columns that are
/// not perpendicular shear, and a bottom row other than `0 0 0 1` is not affine. A
/// scale triple and an Euler triple cannot carry any of those, and a placement that
/// is quietly approximated is a part printed in the wrong place, so each is refused
/// by name instead of being decomposed.
fn placement_matrix(matrix: &[f64], name: &str) -> Result<[f64; 16], String> {
    let Ok(values) = <[f64; 16]>::try_from(matrix) else {
        return Err(format!(
            "{name}: the placement is {} number(s), and a matrix is sixteen",
            matrix.len()
        ));
    };
    if values[12].abs() > PLACEMENT_TOLERANCE
        || values[13].abs() > PLACEMENT_TOLERANCE
        || values[14].abs() > PLACEMENT_TOLERANCE
        || (values[15] - 1.0).abs() > PLACEMENT_TOLERANCE
    {
        return Err(format!(
            "{name}: the placement is not an affine matrix, because its bottom row is \
             {} {} {} {}",
            values[12], values[13], values[14], values[15]
        ));
    }

    let columns = [
        [values[0], values[4], values[8]],
        [values[1], values[5], values[9]],
        [values[2], values[6], values[10]],
    ];
    let mut lengths = [0.0f64; 3];
    for (at, column) in columns.iter().enumerate() {
        lengths[at] = (column[0] * column[0] + column[1] * column[1] + column[2] * column[2]).sqrt();
        if lengths[at] <= PLACEMENT_TOLERANCE {
            return Err(format!("{name}: the placement collapses the {} axis", AXES[at]));
        }
    }
    for (first, second) in [(0usize, 1usize), (0, 2), (1, 2)] {
        let (a, b) = (columns[first], columns[second]);
        let dot = a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
        if dot.abs() > PLACEMENT_TOLERANCE * lengths[first] * lengths[second] {
            return Err(format!(
                "{name}: the placement shears the {} and {} axes, and this engine records a \
                 scale and a rotation",
                AXES[first], AXES[second]
            ));
        }
    }
    Ok(values)
}

/// Writes one object's overrides onto the plate, in name order.
fn apply_settings(plate: &mut hfw::Plate, id: u64, object: &EngineObject) -> Result<(), String> {
    for (name, value) in object_overrides(object)? {
        plate
            .set_object_setting(id, &name, &value)
            .map_err(|error| format!("{}: setting {name} = {value}: {error}", object.name))?;
    }
    Ok(())
}

/// Attaches one volume to an object, then the settings it prints with.
///
/// Only a modifier has a region of its own to print differently, so a key set on any
/// other kind would be stored and ignored; the engine refuses it, and this only asks
/// where it makes sense.
fn add_volume(
    plate: &mut hfw::Plate,
    id: u64,
    object: &EngineObject,
    volume: &EngineVolume,
) -> Result<(), String> {
    let mesh = mesh_handle(&volume.vertices, &volume.indices, &object.name)?;
    let kind = volume_code(&volume.kind)
        .map_err(|error| format!("{}: volume: {error}", object.name))?;
    let volume_id = plate
        .add_volume(id, kind, &mesh)
        .map_err(|error| format!("{}: volume: {error}", object.name))?;
    if kind != VOLUME_MODIFIER {
        return Ok(());
    }

    for (name, value) in settings_object(&volume.settings_json, &object.name)? {
        let text = setting_text(&value);
        if text.is_empty() {
            continue;
        }
        plate
            .set_volume_setting(id, volume_id, &name, &text)
            .map_err(|error| format!("{}: volume setting {name} = {text}: {error}", object.name))?;
    }
    Ok(())
}

/// Writes one paint channel onto an object.
///
/// One byte per triangle, `0` none, `1` enforcer, `2` blocker. A length that is not
/// the mesh's triangle count is refused rather than truncated, because a shorter
/// channel would paint the wrong facets — the states are addressed by triangle index
/// and nothing else. Empty means the channel was never painted.
fn set_paint(
    plate: &mut hfw::Plate,
    id: u64,
    name: &str,
    channel: i32,
    label: &str,
    states: &[u8],
    triangles: usize,
) -> Result<(), String> {
    if states.is_empty() {
        return Ok(());
    }
    if states.len() != triangles {
        return Err(format!(
            "{name}: the {label} paint has {} state(s) and the mesh has {triangles} triangle(s)",
            states.len()
        ));
    }
    if let Some(state) = states.iter().find(|state| **state > 2) {
        return Err(format!(
            "{name}: the {label} paint carries state {state}, and a facet is 0, 1 or 2"
        ));
    }
    plate
        .set_paint(id, channel, states)
        .map_err(|error| format!("{name}: paint: {error}"))
}

/// An object's own layer heights, as interleaved `(z, height)` pairs.
///
/// Zero pairs mean the plate's uniform height, so a uniform override is sent as one
/// pair starting at zero rather than as a pair equal to the plate's height, which is
/// what a variable profile would arrive as. The engine takes a layer whose bottom
/// sits at or above a `z` at that height until the next pair, and the last pair stays
/// in force to the top.
fn set_layer_heights(plate: &mut hfw::Plate, id: u64, object: &EngineObject) -> Result<(), String> {
    let pairs = if object.layer_heights.is_empty() {
        let uniform = layer_height_override(object)?;
        if uniform <= 0.0 {
            return Ok(());
        }
        vec![0.0, uniform]
    } else {
        if !object.layer_heights.len().is_multiple_of(2) {
            return Err(format!(
                "{}: the layer heights are not (z, height) pairs",
                object.name
            ));
        }
        object.layer_heights.clone()
    };
    plate
        .set_layer_heights(id, &pairs)
        .map_err(|error| format!("{}: layer heights: {error}", object.name))
}

/// The plate's timeline: one command per print height.
fn set_markers(plate: &mut hfw::Plate, markers: &[EngineMarker]) -> Result<(), String> {
    if markers.is_empty() {
        return Ok(());
    }
    let mut kinds = Vec::with_capacity(markers.len());
    let mut heights = Vec::with_capacity(markers.len());
    let mut texts = Vec::with_capacity(markers.len());
    for marker in markers {
        kinds.push(marker_code(&marker.kind));
        heights.push(marker.z);
        texts.push(marker.text.as_str());
    }
    plate
        .set_markers(&kinds, &heights, &texts)
        .map_err(|error| format!("the plate's timeline: {error}"))
}

/// The beads the plate prints inside a layer that its model does not describe.
fn set_in_layer(plate: &mut hfw::Plate, strokes: &[EngineStroke]) -> Result<(), String> {
    if strokes.is_empty() {
        return Ok(());
    }
    let mut entries = Vec::with_capacity(strokes.len());
    for stroke in strokes {
        if !stroke.points.len().is_multiple_of(2) {
            return Err(format!(
                "the plate's in-layer strokes: the stroke at Z{} has {} coordinate(s), which is \
                 not whole points",
                stroke.z,
                stroke.points.len()
            ));
        }
        entries.push(hfw::InLayerStroke {
            z: stroke.z,
            points: stroke.points.clone(),
            width: stroke.line_width,
            height: stroke.layer_height,
            speed: stroke.speed,
            prefix: stroke.prefix.clone(),
        });
    }
    plate
        .set_in_layer(&entries)
        .map_err(|error| format!("the plate's in-layer strokes: {error}"))
}

/// Builds the document the engine slices: one object per plate entry, with its
/// placement, its overrides, its volumes and their kinds, its layer heights, and the
/// plate's timeline and strokes.
///
/// The object's own triangle order is handed over unchanged, so the paint the caller
/// addressed by triangle index still lands on the facets it named.
fn fill_plate(
    plate: &mut hfw::Plate,
    document: &EnginePlate,
    dropped: &mut Vec<String>,
) -> Result<(), String> {
    for object in &document.objects {
        let mesh = mesh_handle(&object.vertices, &object.indices, &object.name)?;
        let id = plate
            .add_positioned(&mesh)
            .map_err(|error| format!("{}: {error}", object.name))?;
        plate
            .set_object_name(id, &object.name)
            .map_err(|error| format!("{}: {error}", object.name))?;

        let matrix = placement_matrix(&object.matrix, &object.name)?;
        plate
            .place_matrix(id, &matrix)
            .map_err(|error| format!("{}: placement: {error}", object.name))?;

        apply_settings(plate, id, object)?;
        for volume in &object.volumes {
            add_volume(plate, id, object, volume)?;
        }
        set_layer_heights(plate, id, object)?;

        let triangles = object.indices.len() / 3;
        set_paint(
            plate,
            id,
            &object.name,
            PAINT_SUPPORT,
            "support",
            &object.support_paint,
            triangles,
        )?;
        set_paint(
            plate,
            id,
            &object.name,
            PAINT_SEAM,
            "seam",
            &object.seam_paint,
            triangles,
        )?;

        if object.tool_paint_facets > 0 {
            let reason = format!(
                "{}: {} facet(s) of tool paint have no channel, because this engine prints with \
                 one extruder",
                object.name, object.tool_paint_facets
            );
            if !dropped.contains(&reason) {
                dropped.push(reason);
            }
        }
    }
    set_markers(plate, &document.markers)?;
    set_in_layer(plate, &document.strokes)
}

/// One volume of a plate, as the ABI hands it back.
struct ReadVolume {
    kind: i32,
    settings_json: String,
    vertices: Vec<f64>,
    indices: Vec<i64>,
}

/// One object of a plate, as the ABI hands it back.
struct ReadObject {
    name: String,
    matrix: [f64; 16],
    enabled: bool,
    settings_json: String,
    vertices: Vec<f64>,
    indices: Vec<i64>,
    support_paint: Vec<u8>,
    seam_paint: Vec<u8>,
    layer_heights: Vec<f64>,
    volumes: Vec<ReadVolume>,
}

/// One paint channel, read through the ABI.
///
/// Empty when the mesh carries none for that channel, which is the state an
/// unpainted object is in. Any other length than the triangle count is refused: the
/// engine either replies with one state per triangle or with none at all, and a
/// partial channel would be silently misaligned.
fn read_paint(
    plate: &hfw::Plate,
    id: u64,
    channel: i32,
    triangles: usize,
    name: &str,
    label: &str,
) -> Result<Vec<u8>, String> {
    let states = plate
        .paint(id, channel, triangles)
        .map_err(|error| format!("{name}: reading {label} paint: {error}"))?;
    if states.is_empty() || states.len() == triangles {
        return Ok(states);
    }
    Err(format!(
        "{name}: the engine returned {} {label} state(s) for {triangles} triangle(s)",
        states.len()
    ))
}

/// Reads every object of a plate back through the ABI, in the order it lists them.
fn read_plate(plate: &hfw::Plate) -> Result<Vec<ReadObject>, String> {
    let ids = plate
        .object_ids()
        .map_err(|_| "the plate would not list its objects".to_owned())?;
    let mut objects = Vec::with_capacity(ids.len());
    for id in ids {
        let name = plate
            .object_name(id)
            .map_err(|error| format!("object {id}: {error}"))?;
        let matrix = plate
            .object_placement_matrix(id)
            .map_err(|error| format!("{name}: {error}"))?;
        let enabled = plate
            .object_enabled(id)
            .map_err(|error| format!("{name}: {error}"))?;
        let settings_json = plate
            .object_settings_json(id)
            .map_err(|error| format!("{name}: {error}"))?;

        let mesh = plate
            .object_mesh(id)
            .map_err(|error| format!("{name}: {error}"))?;
        let triangles = mesh.index_count();
        let vertices = mesh
            .vertices_mm()
            .map_err(|error| format!("{name}: reading geometry: {error}"))?;
        let indices = mesh
            .indices()
            .map_err(|error| format!("{name}: reading geometry: {error}"))?
            .into_iter()
            .map(i64::from)
            .collect();
        let support_paint = read_paint(plate, id, PAINT_SUPPORT, triangles, &name, "support")?;
        let seam_paint = read_paint(plate, id, PAINT_SEAM, triangles, &name, "seam")?;

        let layer_heights = plate
            .layer_heights(id)
            .map_err(|error| format!("{name}: {error}"))?;
        if !layer_heights.len().is_multiple_of(2) {
            return Err(format!(
                "{name}: the engine returned {} layer height(s), which is not whole pairs",
                layer_heights.len()
            ));
        }

        let volume_ids = plate
            .volume_ids(id)
            .map_err(|error| format!("{name}: {error}"))?;
        let mut volumes = Vec::with_capacity(volume_ids.len());
        for volume in volume_ids {
            let kind = plate
                .volume_kind(id, volume)
                .map_err(|error| format!("{name}: {error}"))?;
            let settings_json = plate
                .volume_settings_json(id, volume)
                .map_err(|error| format!("{name} volume {volume}: {error}"))?;
            let mesh = plate
                .volume_mesh(id, volume)
                .map_err(|error| format!("{name} volume {volume}: {error}"))?;
            let vertices = mesh
                .vertices_mm()
                .map_err(|error| format!("{name} volume {volume}: reading geometry: {error}"))?;
            let indices = mesh
                .indices()
                .map_err(|error| format!("{name} volume {volume}: reading geometry: {error}"))?
                .into_iter()
                .map(i64::from)
                .collect();
            volumes.push(ReadVolume {
                kind,
                settings_json,
                vertices,
                indices,
            });
        }

        objects.push(ReadObject {
            name,
            matrix,
            enabled,
            settings_json,
            vertices,
            indices,
            support_paint,
            seam_paint,
            layer_heights,
            volumes,
        });
    }
    Ok(objects)
}

/// Builds a second plate out of nothing but what `read_plate` handed back, which is
/// the document the engine has to be able to reconstruct.
///
/// Geometry goes in through `hfw_mesh_new`, never as STL bytes: the paint read back
/// with the mesh is addressed by triangle index, and a trip through STL can renumber
/// the triangles it carries.
fn rebuild_plate(read: &[ReadObject], plate: &mut hfw::Plate) -> Result<(), String> {
    for object in read {
        let mesh = mesh_handle(&object.vertices, &object.indices, &object.name)?;
        let id = plate
            .add(&mesh)
            .map_err(|error| format!("{}: {error}", object.name))?;
        plate
            .set_object_name(id, &object.name)
            .map_err(|error| format!("{}: {error}", object.name))?;
        plate
            .place_matrix(id, &object.matrix)
            .map_err(|error| format!("{}: {error}", object.name))?;

        for row in settings_rows(&object.settings_json, &object.name)? {
            plate
                .set_object_setting(id, &row.name, &row.value)
                .map_err(|error| format!("{}: {error}", object.name))?;
        }

        let triangles = object.indices.len() / 3;
        set_paint(
            plate,
            id,
            &object.name,
            PAINT_SUPPORT,
            "support",
            &object.support_paint,
            triangles,
        )?;
        set_paint(
            plate,
            id,
            &object.name,
            PAINT_SEAM,
            "seam",
            &object.seam_paint,
            triangles,
        )?;
        if !object.layer_heights.is_empty() {
            plate
                .set_layer_heights(id, &object.layer_heights)
                .map_err(|error| format!("{}: {error}", object.name))?;
        }

        for volume in &object.volumes {
            let mesh = mesh_handle(&volume.vertices, &volume.indices, &object.name)?;
            let volume_id = plate
                .add_volume(id, volume.kind, &mesh)
                .map_err(|error| format!("{} volume: {error}", object.name))?;
            for row in settings_rows(&volume.settings_json, &object.name)? {
                plate
                    .set_volume_setting(id, volume_id, &row.name, &row.value)
                    .map_err(|error| format!("{} volume {volume_id}: {error}", object.name))?;
            }
        }
    }
    Ok(())
}

/// One settings JSON object as the key and value rows it was built from.
fn settings_rows(json: &str, name: &str) -> Result<Vec<EngineSetting>, String> {
    let mut rows = Vec::new();
    for (key, value) in settings_object(json, name)? {
        let Some(text) = value.as_str() else {
            return Err(format!("{name}: the setting {key} is not text"));
        };
        rows.push(EngineSetting {
            name: key,
            value: text.to_owned(),
        });
    }
    Ok(rows)
}

/// One object read back, in the terms the document uses.
fn readback_object(object: &ReadObject) -> Result<ReadbackObject, String> {
    let mut volumes = Vec::with_capacity(object.volumes.len());
    for volume in &object.volumes {
        volumes.push(ReadbackVolume {
            kind: volume_kind_name(volume.kind)?.to_owned(),
            settings: settings_rows(&volume.settings_json, &object.name)?,
            vertices: volume.vertices.clone(),
            indices: volume.indices.clone(),
        });
    }
    Ok(ReadbackObject {
        name: object.name.clone(),
        vertices: object.vertices.clone(),
        indices: object.indices.clone(),
        matrix: object.matrix.to_vec(),
        enabled: object.enabled,
        settings: settings_rows(&object.settings_json, &object.name)?,
        layer_heights: object.layer_heights.clone(),
        support_paint: object.support_paint.clone(),
        seam_paint: object.seam_paint.clone(),
        volumes,
    })
}

/// The plate's timeline, read back.
fn read_markers(plate: &hfw::Plate) -> Result<Vec<EngineMarker>, String> {
    let count = plate
        .marker_count()
        .map_err(|error| format!("the plate's timeline: {error}"))?;
    let mut markers = Vec::with_capacity(count);
    for index in 0..count {
        let marker = plate
            .marker(index)
            .map_err(|error| format!("the plate's timeline: {error}"))?;
        markers.push(EngineMarker {
            z: marker.z,
            kind: marker_kind_name(marker.kind)?.to_owned(),
            text: marker.text,
        });
    }
    Ok(markers)
}

/// The plate's in-layer strokes, read back.
fn read_strokes(plate: &hfw::Plate) -> Result<Vec<EngineStroke>, String> {
    let count = plate
        .in_layer_count()
        .map_err(|error| format!("the plate's in-layer strokes: {error}"))?;
    let mut strokes = Vec::with_capacity(count);
    for index in 0..count {
        let stroke = plate
            .in_layer(index)
            .map_err(|error| format!("the plate's in-layer strokes: {error}"))?;
        if stroke.points.is_empty() {
            continue;
        }
        strokes.push(EngineStroke {
            z: stroke.z,
            points: stroke.points,
            line_width: stroke.width,
            layer_height: stroke.height,
            speed: stroke.speed,
            prefix: stroke.prefix,
        });
    }
    Ok(strokes)
}

/// The plate as the engine's project file, which is also how two documents are
/// compared.
fn project_bytes(plate: &hfw::Plate) -> Result<Vec<u8>, String> {
    plate
        .project_bytes()
        .map_err(|error| format!("the engine would not write a document: {error}"))
}

/// A failed read, with an empty snapshot.
fn failed_plate(error: String) -> PlateOutcome {
    PlateOutcome {
        ok: false,
        error,
        snapshot: PlateReadback::default(),
    }
}

/// A successful read.
fn read_plate_outcome(snapshot: PlateReadback) -> PlateOutcome {
    PlateOutcome {
        ok: true,
        error: String::new(),
        snapshot,
    }
}

/// Reads a plate back through the ABI into the shape a document is built from.
///
/// The inverse of what `fill_plate` hands the slicer: the objects, their geometry,
/// placement, paint, layer heights, volumes and settings all come from the engine.
/// The timeline and the strokes are the caller's, because this is the plate the
/// caller just described rather than one read out of a file.
pub fn read_plate_snapshot(document: &EnginePlate) -> PlateOutcome {
    if document.objects.is_empty() {
        return failed_plate("no object on the plate".to_owned());
    }
    let mut dropped = Vec::new();
    let outcome = (|| -> Result<Vec<ReadbackObject>, String> {
        let mut plate = hfw::Plate::create()
            .map_err(|error| format!("the engine could not create a plate: {error}"))?;
        fill_plate(&mut plate, document, &mut dropped)?;
        let read = read_plate(&plate)?;
        let mut objects = Vec::with_capacity(read.len());
        for object in &read {
            objects.push(readback_object(object)?);
        }
        Ok(objects)
    })();
    match outcome {
        Ok(objects) => read_plate_outcome(PlateReadback {
            objects,
            markers: document.markers.clone(),
            strokes: document.strokes.clone(),
            metadata: Vec::new(),
            dropped,
        }),
        Err(error) => failed_plate(error),
    }
}

/// Reads a saved document — the engine's own project file — back into a snapshot.
///
/// This is how a project opens: the file is the engine's document, so a saved plate
/// reopens as the plate that was saved rather than as a second format kept in step
/// with it. The caller's own bytes come back in `snapshot.metadata`.
pub fn read_plate_document(bytes: &[u8]) -> PlateOutcome {
    if bytes.is_empty() {
        return failed_plate("the file is empty".to_owned());
    }
    let outcome = (|| -> Result<PlateReadback, String> {
        let plate = hfw::Plate::open_project(bytes)
            .map_err(|error| format!("the engine could not read the project: {error}"))?;
        let read = read_plate(&plate)?;
        let mut objects = Vec::with_capacity(read.len());
        for object in &read {
            objects.push(readback_object(object)?);
        }
        Ok(PlateReadback {
            objects,
            markers: read_markers(&plate)?,
            strokes: read_strokes(&plate)?,
            metadata: plate
                .metadata()
                .map_err(|error| format!("the document's own bytes: {error}"))?,
            dropped: Vec::new(),
        })
    })();
    match outcome {
        Ok(snapshot) => read_plate_outcome(snapshot),
        Err(error) => failed_plate(error),
    }
}

/// Saves the plate as the engine's project file, with `metadata` inside it.
///
/// One file: the document is the engine's, and the application's own state travels in
/// the engine's opaque bytes rather than beside it in a second file that can be lost.
pub fn save_plate_document(document: &EnginePlate, metadata: &[u8], path: &str) -> EngineOutcome {
    if document.objects.is_empty() {
        return EngineOutcome {
            ok: false,
            error: "no object on the plate".to_owned(),
            report: String::new(),
            dropped: Vec::new(),
        };
    }
    let mut dropped = Vec::new();
    let outcome = (|| -> Result<(), String> {
        let mut plate = hfw::Plate::create()
            .map_err(|error| format!("the engine could not create a plate: {error}"))?;
        fill_plate(&mut plate, document, &mut dropped)?;
        if !metadata.is_empty() {
            plate
                .set_metadata(metadata)
                .map_err(|error| format!("the document's own bytes were refused: {error}"))?;
        }
        let bytes = plate
            .project_bytes()
            .map_err(|error| format!("the engine could not write the plate: {error}"))?;
        fs::write(path, &bytes).map_err(|_| format!("could not write {path}"))?;
        Ok(())
    })();
    match outcome {
        Ok(()) => EngineOutcome {
            ok: true,
            error: String::new(),
            report: String::new(),
            dropped,
        },
        Err(error) => EngineOutcome {
            ok: false,
            error,
            report: String::new(),
            dropped,
        },
    }
}

/// Reads a plate back and rebuilds it, to prove a document survives being handed to
/// the engine.
///
/// Every part of the plate is read through the ABI — each object's name, placement,
/// settings, geometry, paint, layer heights and volumes with their kinds and settings
/// — and a second plate is built from nothing but what was read. The two project
/// files must be identical byte for byte, or something the engine holds is not
/// something it can give back.
///
/// The report is what came back, so a gate reads a count rather than an exit code.
pub fn round_trip_plate(document: &EnginePlate) -> EngineOutcome {
    if document.objects.is_empty() {
        return EngineOutcome {
            ok: false,
            error: "no object on the plate".to_owned(),
            report: String::new(),
            dropped: Vec::new(),
        };
    }
    let mut dropped = Vec::new();
    let outcome = (|| -> Result<String, String> {
        let mut original = hfw::Plate::create()
            .map_err(|error| format!("the engine could not create a plate: {error}"))?;
        fill_plate(&mut original, document, &mut dropped)?;

        let read = read_plate(&original)?;
        let mut rebuilt = hfw::Plate::create()
            .map_err(|error| format!("the engine could not create a plate: {error}"))?;
        rebuild_plate(&read, &mut rebuilt)?;
        set_markers(&mut rebuilt, &document.markers)?;
        set_in_layer(&mut rebuilt, &document.strokes)?;

        let mut volumes = 0;
        let mut painted = 0;
        let mut settings = 0;
        for object in &read {
            volumes += object.volumes.len();
            painted += object
                .support_paint
                .iter()
                .chain(object.seam_paint.iter())
                .filter(|state| **state == 1)
                .count();
            settings += settings_object(&object.settings_json, &object.name)?.len();
        }

        let first = project_bytes(&original)?;
        let second = project_bytes(&rebuilt)?;
        if first != second {
            return Err(format!(
                "the rebuilt plate is not the one it was read from: {} bytes against {}",
                first.len(),
                second.len()
            ));
        }
        Ok(format!(
            "{} object(s), {} volume(s), {} setting(s), {} painted facet(s), {} marker(s), {} \
             stroke(s), documents identical at {} bytes",
            read.len(),
            volumes,
            settings,
            painted,
            document.markers.len(),
            document.strokes.len(),
            first.len()
        ))
    })();
    match outcome {
        Ok(report) => EngineOutcome {
            ok: true,
            error: String::new(),
            report,
            dropped,
        },
        Err(error) => EngineOutcome {
            ok: false,
            error,
            report: String::new(),
            dropped,
        },
    }
}

/// Slices a plate with HFW, writing the G-code this application prints.
///
/// The plate is built in this process through the ABI — objects, placement,
/// overrides, volumes, layer heights, paint, markers and strokes — and sliced there,
/// so nothing about the document is reduced to what a file format can carry.
///
/// A timeline that changes tool is refused before anything is built: this engine
/// prints with one extruder, so the change has nowhere to go and a file that silently
/// printed the whole plate on tool 0 would be the wrong print.
pub fn slice_with_hfw(document: &EnginePlate, config_path: &str, gcode_path: &str) -> EngineOutcome {
    if document.objects.is_empty() {
        return EngineOutcome {
            ok: false,
            error: "no object on the plate".to_owned(),
            report: String::new(),
            dropped: Vec::new(),
        };
    }
    for marker in &document.markers {
        if marker.kind == "tool" {
            return EngineOutcome {
                ok: false,
                error: format!(
                    "the plate timeline changes tool at Z{:.2}, and this engine prints with one \
                     extruder; remove the tool change or slice another plate",
                    marker.z
                ),
                report: String::new(),
                dropped: Vec::new(),
            };
        }
    }

    let mut dropped = Vec::new();
    let outcome = (|| -> Result<(), String> {
        let mut plate = hfw::Plate::create()
            .map_err(|error| format!("the engine could not create a plate: {error}"))?;
        fill_plate(&mut plate, document, &mut dropped)?;
        let profile = Path::new(config_path);
        let gcode = if document.place_on_bed {
            plate.slice_placed(profile, 0.0, 0.0)
        } else {
            plate.slice(profile, 0.0, 0.0)
        }
        .map_err(|error| format!("the engine would not slice the plate: {error}"))?;

        let text = gcode.text();
        let written = match text.as_deref() {
            Ok(body) => fs::write(gcode_path, body.as_bytes()).is_ok(),
            Err(_) => false,
        };
        if !written {
            return Err(format!("could not write {gcode_path}"));
        }
        if !gcode.verified() {
            return Err(
                "the engine's verifier refused the G-code it wrote, and the file is there to be \
                 looked at rather than printed"
                    .to_owned(),
            );
        }
        Ok(())
    })();
    match outcome {
        Ok(()) => EngineOutcome {
            ok: true,
            error: String::new(),
            report: String::new(),
            dropped,
        },
        Err(error) => EngineOutcome {
            ok: false,
            error,
            report: String::new(),
            dropped,
        },
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn a_setting_number_is_the_spelling_the_engine_reader_was_given_before() {
        assert_eq!(setting_number(0.2), "0.2");
        assert_eq!(setting_number(0.1), "0.1");
        assert_eq!(setting_number(0.3), "0.3");
        assert_eq!(setting_number(40.0), "40");
        assert_eq!(setting_number(0.0), "0");
        assert_eq!(setting_number(1234.5678), "1234.5678");
        assert_eq!(setting_number(0.00001), "1e-05");
        assert_eq!(setting_number(1e10), "1e+10");
    }

    #[test]
    fn a_setting_value_is_written_the_way_a_preset_writes_it() {
        assert_eq!(setting_text(&Value::Bool(true)), "1");
        assert_eq!(setting_text(&Value::Bool(false)), "0");
        assert_eq!(
            setting_text(&serde_json::json!({ "value": 40.0, "is_percent": true })),
            "40%"
        );
        assert_eq!(setting_text(&serde_json::json!([0.4, 1.2])), "0.4,1.2");
        assert_eq!(setting_text(&serde_json::json!("everywhere")), "everywhere");
        assert_eq!(setting_text(&serde_json::json!(null)), "");
        assert_eq!(setting_text(&serde_json::json!({ "other": 1 })), "");
    }

    #[test]
    fn a_placement_that_is_not_a_scale_and_a_rotation_is_refused_by_name() {
        let identity = [1.0, 0.0, 0.0, 4.0, 0.0, 1.0, 0.0, 5.0, 0.0, 0.0, 1.0, 6.0, 0.0, 0.0, 0.0, 1.0];
        assert!(placement_matrix(&identity, "cube").is_ok());

        let mut sheared = identity;
        sheared[4] = 0.5;
        assert!(placement_matrix(&sheared, "cube")
            .expect_err("a sheared placement is refused")
            .contains("shears"));

        let mut collapsed = identity;
        collapsed[2] = 0.0;
        collapsed[10] = 0.0;
        assert!(placement_matrix(&collapsed, "cube")
            .expect_err("a collapsed axis is refused")
            .contains("collapses"));

        let mut perspective = identity;
        perspective[12] = 0.25;
        assert!(placement_matrix(&perspective, "cube")
            .expect_err("a non-affine placement is refused")
            .contains("not an affine matrix"));

        assert!(placement_matrix(&identity[..15], "cube").is_err());
    }

    #[test]
    fn every_volume_kind_the_engine_names_comes_back_as_itself() {
        for kind in ["part", "negative", "modifier", "blocker", "enforcer"] {
            let code = volume_code(kind).expect("a known volume kind");
            assert_eq!(volume_kind_name(code).expect("a known volume code"), kind);
        }
        assert!(volume_code("hollow").is_err());
        assert!(volume_kind_name(9).is_err());
    }

    #[test]
    fn a_marker_kind_folds_the_way_the_document_does() {
        assert_eq!(marker_kind_name(marker_code("pause")).unwrap(), "pause");
        assert_eq!(marker_kind_name(marker_code("color")).unwrap(), "color");
        assert_eq!(marker_kind_name(marker_code("colour")).unwrap(), "color");
        assert_eq!(marker_kind_name(marker_code("gcode")).unwrap(), "gcode");
        assert_eq!(marker_kind_name(marker_code("tool")).unwrap(), "gcode");
        assert!(marker_kind_name(7).is_err());
    }
}
