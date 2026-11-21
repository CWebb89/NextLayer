//! The records a `.3mf` project carries that the engine's own loader discards.
//!
//! Ported from `src/qt/src/ThreeMfProject.cpp`. A `.3mf` is a zip: the engine reads
//! the geometry out of it and drops the rest, so everything else is read here — the
//! build list that says where each object sits, the plate records that say which bed
//! it belongs to, the object and volume records, the variable layer height bands, the
//! facets painted on each triangle, the settings another slicer wrote under its own
//! names, and the timeline markers.
//!
//! The archive is read here rather than handed over by the engine: `flate2` inflates
//! a deflated entry and the central directory is parsed in this file, so the five
//! small XML records are the only entries ever decompressed and geometry stays with
//! the engine's loader.
//!
//! A volume's triangle range is a number, not a setting. The C++ this replaces
//! injected it into the volume's own settings under `__first_triangle` and read it
//! back out of there; here it is [`VolumeRecord::first_triangle`], the paint mapping
//! reads it as a number, and [`VolumeSettings`] is the only type the overrides live
//! in and it refuses that name as it reads it, so a range cannot reach the settings a
//! caller hands the engine.

use std::collections::BTreeMap;
use std::fs::File;
use std::io::{Read, Seek, SeekFrom};

use flate2::read::DeflateDecoder;

use crate::bridge::{
    ThreeMfFileFacet, ThreeMfFileForeign, ThreeMfFileMarker, ThreeMfFileName, ThreeMfFileOutcome,
    ThreeMfFilePlate, ThreeMfFileRecord, ThreeMfFileSetting, ThreeMfFileVolume,
};

/// The plate and object records, and the settings another slicer keeps there.
const MODEL_SETTINGS: &str = "Metadata/model_settings.config";
/// The classic writer's object and volume records.
const MODEL_CONFIG: &str = "Metadata/Slic3r_PE_model.config";
/// The variable layer height bands.
const LAYER_HEIGHTS: &str = "Metadata/Slic3r_PE_layer_heights_profile.txt";
/// The timeline markers.
const LAYER_GCODES: &str = "Metadata/Prusa_Slicer_custom_gcode_per_print_z.xml";
/// The model itself. Its presence is what separates a project from a sliced print.
const SCENE: &str = "3D/3dmodel.model";

/// The key the C++ stored a volume's first triangle under. Files this application
/// exported before the port carry it as an ordinary metadata record.
const RESERVED_FIRST_TRIANGLE: &str = "__first_triangle";

/// The metadata records that describe where a part sits rather than how it prints.
const LAYOUT_METADATA: [&str; 17] = [
    "name",
    "matrix",
    "source_file",
    "source_object_id",
    "source_volume_id",
    "source_offset_x",
    "source_offset_y",
    "source_offset_z",
    "identify_id",
    "instance_id",
    "object_id",
    "plater_id",
    "plater_name",
    "locked",
    "thumbnail_file",
    "top_file",
    "pick_file",
];

/// A decompressed entry larger than this is refused instead of allocated: the size
/// in the header is the file's own claim, and the largest entry this reads is a model
/// of a full plate. Everything the reader wants is small, so a header promising
/// hundreds of megabytes is a file nobody wrote.
const MAX_ENTRY_BYTES: u64 = 512 * 1024 * 1024;

/// The central directory is names and sizes; a file claiming a larger one than this
/// is refused before it is read.
const MAX_DIRECTORY_BYTES: u64 = 32 * 1024 * 1024;

/// The furthest back a trailing comment can push the end-of-central-directory record.
const EOCD_WINDOW: u64 = 66_000;

const EOCD: &[u8] = b"PK\x05\x06";
const EOCD64: &[u8] = b"PK\x06\x06";
const EOCD64_LOCATOR: &[u8] = b"PK\x06\x07";
const DIRECTORY_ENTRY: &[u8] = b"PK\x01\x02";
const LOCAL_HEADER: &[u8] = b"PK\x03\x04";

/// Reads a project file's own records, or an outcome with `valid` false when the file
/// holds no model.
pub fn read_three_mf_project(path: &str) -> ThreeMfFileOutcome {
    let Some(mut archive) = Archive::open(path) else {
        return empty_outcome();
    };

    let settings = archive.read_named(MODEL_SETTINGS);
    let scene = archive.read_named(SCENE);
    if scene.is_empty() {
        return empty_outcome();
    }

    let mut project = Project::default();
    read_plate_records(&settings, &mut project);
    read_foreign_settings(&settings, &mut project);
    read_build_list(&scene, &mut project);
    read_model_config(&archive.read_named(MODEL_CONFIG), &mut project);
    read_layer_heights(&archive.read_named(LAYER_HEIGHTS), &mut project);
    read_layer_gcodes(&archive.read_named(LAYER_GCODES), &mut project);
    read_paint(&scene, &mut project);
    if project.build_transforms.is_empty() {
        return empty_outcome();
    }

    project.valid = true;
    into_outcome(project)
}

/// Whether the file is a print that has already been sliced rather than a model: a
/// `.gcode`, or a `.3mf` archive that carries a G-code and no mesh.
pub fn is_pre_sliced_file(path: &str) -> bool {
    if ends_with_case_insensitive(path, ".gcode") {
        return std::fs::metadata(path).is_ok_and(|metadata| metadata.is_file());
    }
    if !ends_with_case_insensitive(path, ".3mf") {
        return false;
    }

    let Some(archive) = Archive::open(path) else {
        return false;
    };
    let has_model = archive.index_of(SCENE).is_some();
    let has_gcode = archive.gcode_index().is_some();
    has_gcode && !has_model
}

/// Writes the G-code inside a pre-sliced file to `out_path`. A plain `.gcode` is
/// copied, and an existing destination is refused rather than replaced.
pub fn extract_pre_sliced_gcode(path: &str, out_path: &str) -> bool {
    if ends_with_case_insensitive(path, ".gcode") {
        if std::path::Path::new(out_path).exists() {
            return false;
        }
        return std::fs::copy(path, out_path).is_ok();
    }

    let Some(mut archive) = Archive::open(path) else {
        return false;
    };
    let Some(index) = archive.gcode_index() else {
        return false;
    };
    let Some(gcode) = archive.read(index) else {
        return false;
    };
    if gcode.is_empty() {
        return false;
    }
    std::fs::write(out_path, &gcode).is_ok()
}

/// Everything one pass over the archive learned, in the shape the reads above fill
/// and the bridge structs are built from.
#[derive(Default)]
struct Project {
    valid: bool,
    application: String,
    plate_count: i32,
    build_order: Vec<i32>,
    build_transforms: Vec<[f64; 12]>,
    plate_of_object: BTreeMap<i32, i32>,
    objects: BTreeMap<i32, ObjectRecord>,
    foreign_settings: BTreeMap<i32, Vec<ThreeMfFileSetting>>,
    foreign_names: BTreeMap<i32, String>,
    layer_gcodes: BTreeMap<i32, Vec<ThreeMfFileMarker>>,
}

#[derive(Default)]
struct ObjectRecord {
    name: String,
    settings: Vec<ThreeMfFileSetting>,
    volumes: Vec<VolumeRecord>,
    layer_heights: Vec<f64>,
}

#[derive(Default)]
struct VolumeRecord {
    kind: String,
    /// The index of this volume's first triangle in the object's mesh. The paint is
    /// stored against the object, and this is what splits it per volume.
    first_triangle: i32,
    settings: VolumeSettings,
    supports: Vec<ThreeMfFileFacet>,
    seams: Vec<ThreeMfFileFacet>,
    tools: Vec<ThreeMfFileFacet>,
}

/// A volume's overridden settings, and the one name that is not one of them.
///
/// The only way in is [`VolumeSettings::insert`], so the triangle range the C++
/// carried in this same map cannot be written back out into the settings a caller
/// hands the engine.
#[derive(Default)]
struct VolumeSettings(Vec<ThreeMfFileSetting>);

impl VolumeSettings {
    fn insert(&mut self, name: String, value: String) {
        if name == RESERVED_FIRST_TRIANGLE {
            return;
        }
        insert_setting(&mut self.0, name, value);
    }
}

/// Nothing read: a file that is not a project, or one with no build items, comes back
/// as this.
fn empty_outcome() -> ThreeMfFileOutcome {
    ThreeMfFileOutcome {
        valid: false,
        application: String::new(),
        plate_count: 0,
        build_order: Vec::new(),
        build_transforms: Vec::new(),
        plates: Vec::new(),
        records: Vec::new(),
        foreign_names: Vec::new(),
        foreign_settings: Vec::new(),
        markers: Vec::new(),
    }
}

fn into_outcome(project: Project) -> ThreeMfFileOutcome {
    ThreeMfFileOutcome {
        valid: project.valid,
        application: project.application,
        plate_count: project.plate_count,
        build_order: project.build_order,
        build_transforms: project
            .build_transforms
            .iter()
            .flatten()
            .copied()
            .collect(),
        plates: project
            .plate_of_object
            .iter()
            .map(|(object, plate)| ThreeMfFilePlate {
                object: *object,
                plate: *plate,
            })
            .collect(),
        records: project
            .objects
            .into_iter()
            .map(|(id, record)| record.into_bridge(id))
            .collect(),
        foreign_names: project
            .foreign_names
            .into_iter()
            .map(|(id, name)| ThreeMfFileName { id, name })
            .collect(),
        foreign_settings: project
            .foreign_settings
            .into_iter()
            .map(|(id, settings)| ThreeMfFileForeign { id, settings })
            .collect(),
        markers: project
            .layer_gcodes
            .into_iter()
            .flat_map(|(_, markers)| markers)
            .collect(),
    }
}

impl ObjectRecord {
    fn into_bridge(self, id: i32) -> ThreeMfFileRecord {
        ThreeMfFileRecord {
            id,
            name: self.name,
            settings: self.settings,
            volumes: self
                .volumes
                .into_iter()
                .map(|volume| ThreeMfFileVolume {
                    kind: volume.kind,
                    first_triangle: volume.first_triangle,
                    settings: volume.settings.0,
                    supports: volume.supports,
                    seams: volume.seams,
                    tools: volume.tools,
                })
                .collect(),
            layer_heights: self.layer_heights,
        }
    }
}

/// One setting or metadata record, replaced in place when a file repeats its name,
/// which is what the `QHash` this replaces did.
fn insert_setting(settings: &mut Vec<ThreeMfFileSetting>, name: String, value: String) {
    match settings.iter_mut().find(|setting| setting.name == name) {
        Some(setting) => setting.value = value,
        None => settings.push(ThreeMfFileSetting { name, value }),
    }
}

/// The plate records name a plate per object twice over: an object carries a
/// `plater_id` of its own, and each `<plate>` lists the instances it holds by
/// `object_id`. Both forms appear in the wild, and the plate list is the one that
/// counts the plates, so both are read.
fn read_plate_records(settings: &[u8], project: &mut Project) {
    let mut scanner = Scanner::new(settings);
    let mut object_id = 0;
    let mut plate_id = 0;
    let mut instance_object = 0;
    let mut in_plate = false;

    while let Some(step) = scanner.next() {
        match step {
            Step::Start(element) => match element.name.as_str() {
                "object" => object_id = element.integer("id"),
                "plate" => {
                    in_plate = true;
                    plate_id = 0;
                }
                "model_instance" => instance_object = 0,
                "metadata" => {
                    let key = element.value("key").unwrap_or_default();
                    let value = element.value("value").unwrap_or_default();
                    if key != "plater_id" && key != "object_id" {
                        continue;
                    }
                    if in_plate {
                        if key == "plater_id" {
                            plate_id = integer(value);
                        } else {
                            instance_object = integer(value);
                        }
                    } else if key == "plater_id" && object_id > 0 && integer(value) > 0 {
                        project.plate_of_object.insert(object_id, integer(value));
                    }
                }
                _ => {}
            },
            Step::End(name) => match name.as_str() {
                "model_instance" => {
                    if in_plate && plate_id > 0 && instance_object > 0 {
                        project.plate_of_object.insert(instance_object, plate_id);
                    }
                }
                "plate" => {
                    in_plate = false;
                    project.plate_count = project.plate_count.max(plate_id);
                }
                "object" => object_id = 0,
                _ => {}
            },
            Step::Text(_) => {}
        }
    }

    if scanner.failed() {
        project.plate_of_object.clear();
        project.plate_count = 0;
    }
}

/// The object and volume records: what type each volume is, the overrides the file
/// set on it, and the range of triangles its mesh occupies.
fn read_model_config(config: &[u8], project: &mut Project) {
    let mut scanner = Scanner::new(config);
    let mut object_id = 0;
    let mut record = ObjectRecord::default();
    let mut volume = VolumeRecord::default();
    let mut in_volume = false;

    while let Some(step) = scanner.next() {
        match step {
            Step::Start(element) => match element.name.as_str() {
                "object" => {
                    object_id = element.integer("id");
                    record = ObjectRecord::default();
                }
                "volume" => {
                    in_volume = true;
                    volume = VolumeRecord::default();
                    volume.first_triangle = element.integer("firstid");
                }
                "metadata" => {
                    let key = element.value("key").unwrap_or_default().to_owned();
                    let value = element.value("value").unwrap_or_default().to_owned();
                    if in_volume {
                        if key == "volume_type" {
                            volume.kind = value;
                        } else if key != "matrix" {
                            volume.settings.insert(key, value);
                        }
                    } else if object_id > 0 {
                        if key == "name" {
                            record.name = value;
                        } else {
                            insert_setting(&mut record.settings, key, value);
                        }
                    }
                }
                _ => {}
            },
            Step::End(name) => match name.as_str() {
                "volume" if in_volume => {
                    record.volumes.push(std::mem::take(&mut volume));
                    in_volume = false;
                }
                "object" if object_id > 0 => {
                    project.objects.insert(object_id, std::mem::take(&mut record));
                    object_id = 0;
                }
                _ => {}
            },
            Step::Text(_) => {}
        }
    }

    if scanner.failed() {
        project.objects.clear();
    }
}

/// The build list: one `<item>` per placed object, in the order the model file lists
/// them. That order is how an object is matched to the records above. The same pass
/// records who wrote the file, because a project written for one slicer is written
/// back in that slicer's dialect rather than in this engine's names.
///
/// `<metadata name="Application">` carries its value in the element body rather than
/// in an attribute, which is what the authoring slicer writes; the attribute is what
/// a file that puts it there instead is read from.
fn read_build_list(scene: &[u8], project: &mut Project) {
    let mut scanner = Scanner::new(scene);
    let mut in_build = false;

    while let Some(step) = scanner.next() {
        match step {
            Step::Start(element) => match element.name.as_str() {
                "metadata" => {
                    if element.value("name") != Some("Application") || !project.application.is_empty()
                    {
                        continue;
                    }
                    let attribute = element.value("value").unwrap_or_default().to_owned();
                    let body = scanner.read_text();
                    project.application = if body.is_empty() { attribute } else { body };
                }
                "build" => in_build = true,
                "item" if in_build => {
                    let object_id = element.integer("objectid");
                    if object_id <= 0 {
                        continue;
                    }

                    let mut placement = [1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0];
                    let numbers: Vec<&str> = element
                        .value("transform")
                        .unwrap_or_default()
                        .split(' ')
                        .filter(|number| !number.is_empty())
                        .collect();
                    if numbers.len() == placement.len() {
                        for (slot, text) in placement.iter_mut().zip(numbers) {
                            *slot = number(text);
                        }
                    } else if !numbers.is_empty() {
                        continue;
                    }

                    project.build_order.push(object_id);
                    project.build_transforms.push(placement);
                }
                _ => {}
            },
            Step::End(name) => {
                if name == "build" {
                    in_build = false;
                }
            }
            Step::Text(_) => {}
        }
    }

    if scanner.failed() {
        project.build_order.clear();
        project.build_transforms.clear();
    }
}

/// The variable layer height bands, one `object_id=N|z;height;z;height;...` line per
/// object.
fn read_layer_heights(body: &[u8], project: &mut Project) {
    let text = String::from_utf8_lossy(body);
    for line in text.split('\n').filter(|line| !line.is_empty()) {
        let Some(marker) = line.find("object_id=") else {
            continue;
        };
        let Some(bar) = line.find('|') else {
            continue;
        };
        if bar <= marker + 10 {
            continue;
        }
        let id = integer(&line[marker + 10..bar]);
        if id <= 0 {
            continue;
        }
        let record = project.objects.entry(id).or_default();
        for band in line[bar + 1..].split(';').filter(|band| !band.is_empty()) {
            record.layer_heights.push(number(band));
        }
    }
}

/// The painted facets, written as attributes on the model's own triangles. A
/// triangle's index inside its volume is what the engine's selector is keyed by, so
/// the global index is collected here and mapped onto the volume ranges afterwards.
///
/// The attributes are matched by local name, because the reference slicer writes them
/// in its own namespace: they arrive as `slic3rpe:custom_supports`.
fn read_paint(scene: &[u8], project: &mut Project) {
    let mut scanner = Scanner::new(scene);
    let mut supports: BTreeMap<i32, BTreeMap<i32, String>> = BTreeMap::new();
    let mut seams: BTreeMap<i32, BTreeMap<i32, String>> = BTreeMap::new();
    let mut tools: BTreeMap<i32, BTreeMap<i32, String>> = BTreeMap::new();
    let mut object_id = 0;
    let mut triangle = 0;
    let mut in_mesh = false;

    while let Some(step) = scanner.next() {
        match step {
            Step::Start(element) => match element.name.as_str() {
                "object" => object_id = element.integer("id"),
                "mesh" => {
                    in_mesh = true;
                    triangle = 0;
                }
                "triangle" if in_mesh && object_id > 0 => {
                    for (name, value) in &element.attributes {
                        let painted = match name.as_str() {
                            "custom_supports" => &mut supports,
                            "custom_seam" => &mut seams,
                            "paint_color" => &mut tools,
                            _ => continue,
                        };
                        painted
                            .entry(object_id)
                            .or_default()
                            .insert(triangle, value.clone());
                    }
                    triangle += 1;
                }
                _ => {}
            },
            Step::End(name) => {
                if name == "mesh" {
                    in_mesh = false;
                }
            }
            Step::Text(_) => {}
        }
    }

    if scanner.failed() {
        return;
    }

    for (id, object) in project.objects.iter_mut() {
        let object_supports = supports.remove(id).unwrap_or_default();
        let object_seams = seams.remove(id).unwrap_or_default();
        let object_tools = tools.remove(id).unwrap_or_default();
        if object_supports.is_empty() && object_seams.is_empty() && object_tools.is_empty() {
            continue;
        }
        if object.volumes.is_empty() {
            continue;
        }
        for index in 0..object.volumes.len() {
            let first = object.volumes[index].first_triangle;
            let last = match object.volumes.get(index + 1) {
                Some(next) => next.first_triangle - 1,
                None => i32::MAX,
            };
            object.volumes[index].supports = range(&object_supports, first, last);
            object.volumes[index].seams = range(&object_seams, first, last);
            object.volumes[index].tools = range(&object_tools, first, last);
        }
    }
}

/// The facets of one volume, renumbered onto the volume's own triangles.
fn range(painted: &BTreeMap<i32, String>, first: i32, last: i32) -> Vec<ThreeMfFileFacet> {
    if first > last {
        return Vec::new();
    }
    painted
        .range(first..=last)
        .map(|(triangle, text)| ThreeMfFileFacet {
            triangle: triangle - first,
            text: text.clone(),
        })
        .collect()
}

/// A marker's type, as the integer this format standardised on, or as the name
/// another writer may have used instead.
fn marker_type(value: &str) -> i32 {
    if let Ok(number) = value.trim().parse::<i32>() {
        return number;
    }
    match value.to_ascii_lowercase().as_str() {
        "colorchange" => 0,
        "pauseprint" => 1,
        "toolchange" => 2,
        "template" => 3,
        _ => 4,
    }
}

/// The text a marker carries.
///
/// The colour field holds a colour, not a note. A file written before that was
/// enforced -- this application's own exports among them -- carries free text there,
/// so the text is read from wherever it actually is rather than from the field's
/// name.
fn marker_text<'a>(extra: &'a str, colour: &'a str) -> &'a str {
    if extra.is_empty() {
        colour
    } else {
        extra
    }
}

/// Whether the field holds a colour rather than a note.
fn is_colour(text: &str) -> bool {
    text.len() == 7
        && text.starts_with('#')
        && text.as_bytes()[1..].iter().all(u8::is_ascii_hexdigit)
}

/// The timeline markers, per bed: pauses, colour and tool changes and custom G-code,
/// each at its own Z.
fn read_layer_gcodes(body: &[u8], project: &mut Project) {
    let mut scanner = Scanner::new(body);
    let mut bed = 0;

    while let Some(step) = scanner.next() {
        let Step::Start(element) = step else {
            continue;
        };
        match element.name.as_str() {
            "custom_gcodes_per_print_z" => bed = element.integer("bed_idx"),
            "code" => {
                let extra = element.value("extra").unwrap_or_default();
                let colour = element.value("color").unwrap_or_default();
                let real_colour = is_colour(colour);
                let mut marker = ThreeMfFileMarker {
                    bed,
                    z: number(element.value("print_z").unwrap_or_default()),
                    kind: marker_type(element.value("type").unwrap_or_default()),
                    text: marker_text(extra, colour).to_owned(),
                };
                if marker.kind == 0 && !real_colour {
                    marker.kind = 4;
                }
                if marker.z > 0.0 {
                    project.layer_gcodes.entry(bed).or_default().push(marker);
                }
            }
            _ => {}
        }
    }
}

/// The object settings another slicer keeps in `model_settings.config`: Bambu and
/// Orca write them as plain `<metadata key value>` under the object, where PrusaSlicer
/// writes a model config file of its own. Their names are that slicer's, so what comes
/// back is filtered against this engine's definitions before it is applied; here only
/// the layout keys are dropped.
fn read_foreign_settings(settings: &[u8], project: &mut Project) {
    let mut scanner = Scanner::new(settings);
    let mut object_id = 0;
    let mut in_part = false;
    let mut record: Vec<ThreeMfFileSetting> = Vec::new();

    while let Some(step) = scanner.next() {
        match step {
            Step::Start(element) => match element.name.as_str() {
                "object" => {
                    object_id = element.integer("id");
                    record.clear();
                }
                "part" => in_part = true,
                "metadata" if object_id > 0 && !in_part => {
                    let key = element.value("key").unwrap_or_default().to_owned();
                    let value = element.value("value").unwrap_or_default().to_owned();
                    if key.is_empty() {
                        continue;
                    }
                    if key == "name" {
                        project.foreign_names.insert(object_id, value);
                        continue;
                    }
                    if LAYOUT_METADATA.contains(&key.as_str()) {
                        continue;
                    }
                    insert_setting(&mut record, key, value);
                }
                _ => {}
            },
            Step::End(name) => match name.as_str() {
                "part" => in_part = false,
                "object" if object_id > 0 => {
                    if !record.is_empty() {
                        project
                            .foreign_settings
                            .insert(object_id, std::mem::take(&mut record));
                    }
                    object_id = 0;
                    record.clear();
                }
                _ => {}
            },
            Step::Text(_) => {}
        }
    }

    if scanner.failed() {
        project.foreign_settings.clear();
    }
}

/// Text as a number, zero when it is not one: every attribute here is written by
/// another program and a field that holds nothing usable is worth no more than its
/// absence.
fn number(text: &str) -> f64 {
    text.trim().parse().unwrap_or(0.0)
}

/// Text as a whole number, zero when it is not one.
fn integer(text: &str) -> i32 {
    text.trim().parse().unwrap_or(0)
}

fn ends_with_case_insensitive(text: &str, suffix: &str) -> bool {
    let (text, suffix) = (text.as_bytes(), suffix.as_bytes());
    text.len() >= suffix.len() && text[text.len() - suffix.len()..].eq_ignore_ascii_case(suffix)
}

/// One entry of an archive's central directory.
struct Entry {
    name: String,
    flags: u16,
    method: u16,
    crc: u32,
    compressed: u64,
    uncompressed: u64,
    header_offset: u64,
}

/// A zip archive read from a file, one entry at a time.
///
/// The central directory gives every name and size, and an entry is then read by
/// seeking to its own local header: nothing is unpacked to disk and no entry this
/// file does not ask for is touched.
struct Archive {
    file: File,
    entries: Vec<Entry>,
}

impl Archive {
    fn open(path: &str) -> Option<Archive> {
        let mut file = File::open(path).ok()?;
        let length = file.metadata().ok()?.len();
        let entries = read_directory(&mut file, length)?;
        Some(Archive { file, entries })
    }

    fn index_of(&self, name: &str) -> Option<usize> {
        self.entries.iter().position(|entry| entry.name == name)
    }

    /// The G-code entry inside a sliced 3MF, whatever the slicer named it. Prusa
    /// writes `Metadata/Slicer PE_...gcode`, Bambu and Orca write
    /// `Metadata/plate_1.gcode`, and a renamed archive may carry anything ending in
    /// `.gcode`.
    fn gcode_index(&self) -> Option<usize> {
        self.entries
            .iter()
            .position(|entry| ends_with_case_insensitive(&entry.name, ".gcode"))
    }

    fn read_named(&mut self, name: &str) -> Vec<u8> {
        match self.index_of(name) {
            Some(index) => self.read(index).unwrap_or_default(),
            None => Vec::new(),
        }
    }

    fn read(&mut self, index: usize) -> Option<Vec<u8>> {
        let entry = self.entries.get(index)?;
        if entry.flags & 1 != 0
            || entry.compressed > MAX_ENTRY_BYTES
            || entry.uncompressed > MAX_ENTRY_BYTES
        {
            return None;
        }

        self.file.seek(SeekFrom::Start(entry.header_offset)).ok()?;
        let mut local = [0u8; 30];
        self.file.read_exact(&mut local).ok()?;
        if &local[0..4] != LOCAL_HEADER {
            return None;
        }
        let name_length = u64::from(offset_u16(&local, 26)?);
        let extra_length = u64::from(offset_u16(&local, 28)?);
        self.file
            .seek(SeekFrom::Current((name_length + extra_length) as i64))
            .ok()?;

        let mut compressed = Vec::new();
        self.file
            .by_ref()
            .take(entry.compressed)
            .read_to_end(&mut compressed)
            .ok()?;
        if compressed.len() as u64 != entry.compressed {
            return None;
        }

        let contents = match entry.method {
            0 => compressed,
            8 => {
                let mut plain = Vec::new();
                DeflateDecoder::new(compressed.as_slice())
                    .take(MAX_ENTRY_BYTES)
                    .read_to_end(&mut plain)
                    .ok()?;
                plain
            }
            _ => return None,
        };
        if contents.len() as u64 != entry.uncompressed || crc32(&contents) != entry.crc {
            return None;
        }
        Some(contents)
    }
}

fn read_directory(file: &mut File, length: u64) -> Option<Vec<Entry>> {
    let window = length.min(EOCD_WINDOW);
    let mut tail = vec![0u8; window as usize];
    file.seek(SeekFrom::End(-(window as i64))).ok()?;
    file.read_exact(&mut tail).ok()?;

    let eocd = tail
        .windows(EOCD.len())
        .rposition(|candidate| candidate == EOCD)?;
    let (offset, size, count) = directory_location(file, &tail, eocd, length)?;

    if size > MAX_DIRECTORY_BYTES {
        return None;
    }
    let mut directory = vec![0u8; size as usize];
    file.seek(SeekFrom::Start(offset)).ok()?;
    file.read_exact(&mut directory).ok()?;
    read_entries(&directory, count)
}

/// Where the central directory starts, how long it is and how many entries it holds,
/// from the end record or the zip64 pair that supersedes it.
fn directory_location(
    file: &mut File,
    tail: &[u8],
    eocd: usize,
    length: u64,
) -> Option<(u64, u64, usize)> {
    let record = &tail[eocd..];
    let mut count = u64::from(offset_u16(record, 10)?);
    let mut size = u64::from(offset_u32(record, 12)?);
    let mut offset = u64::from(offset_u32(record, 16)?);

    let oversized = offset == u64::from(u32::MAX)
        || size == u64::from(u32::MAX)
        || count == u64::from(u16::MAX);
    if oversized {
        let locator = tail[..eocd]
            .windows(EOCD64_LOCATOR.len())
            .rposition(|candidate| candidate == EOCD64_LOCATOR)?;
        let at = offset_u64(tail, locator + 8)?;
        if at.checked_add(56)? > length {
            return None;
        }
        let mut body = [0u8; 56];
        file.seek(SeekFrom::Start(at)).ok()?;
        file.read_exact(&mut body).ok()?;
        if &body[0..4] != EOCD64 {
            return None;
        }
        count = offset_u64(&body, 32)?;
        size = offset_u64(&body, 40)?;
        offset = offset_u64(&body, 48)?;
    }

    Some((offset, size, usize::try_from(count).ok()?))
}

fn read_entries(directory: &[u8], count: usize) -> Option<Vec<Entry>> {
    let mut entries = Vec::new();
    let mut at = 0;
    for _ in 0..count {
        if directory.get(at..at + 4)? != DIRECTORY_ENTRY {
            return None;
        }
        let flags = offset_u16(directory, at + 8)?;
        let method = offset_u16(directory, at + 10)?;
        let crc = offset_u32(directory, at + 16)?;
        let mut compressed = u64::from(offset_u32(directory, at + 20)?);
        let mut uncompressed = u64::from(offset_u32(directory, at + 24)?);
        let name_length = usize::from(offset_u16(directory, at + 28)?);
        let extra_length = usize::from(offset_u16(directory, at + 30)?);
        let comment_length = usize::from(offset_u16(directory, at + 32)?);
        let mut header_offset = u64::from(offset_u32(directory, at + 42)?);

        let name_at = at + 46;
        let name = String::from_utf8_lossy(directory.get(name_at..name_at + name_length)?)
            .into_owned();
        let extra = directory.get(name_at + name_length..name_at + name_length + extra_length)?;
        read_zip64_extra(extra, &mut uncompressed, &mut compressed, &mut header_offset)?;

        at = name_at + name_length + extra_length + comment_length;
        entries.push(Entry {
            name,
            flags,
            method,
            crc,
            compressed,
            uncompressed,
            header_offset,
        });
    }
    Some(entries)
}

/// The sizes and offset zip64 moves out of the fixed fields into an extra block, in
/// the order the format lists them.
fn read_zip64_extra(
    extra: &[u8],
    uncompressed: &mut u64,
    compressed: &mut u64,
    header_offset: &mut u64,
) -> Option<()> {
    let mut at = 0;
    while at + 4 <= extra.len() {
        let identifier = offset_u16(extra, at)?;
        let length = usize::from(offset_u16(extra, at + 2)?);
        if identifier == 0x0001 {
            let body = extra.get(at + 4..at + 4 + length)?;
            let mut cursor = 0;
            if *uncompressed == u64::from(u32::MAX) {
                *uncompressed = offset_u64(body, cursor)?;
                cursor += 8;
            }
            if *compressed == u64::from(u32::MAX) {
                *compressed = offset_u64(body, cursor)?;
                cursor += 8;
            }
            if *header_offset == u64::from(u32::MAX) {
                *header_offset = offset_u64(body, cursor)?;
            }
            return Some(());
        }
        at += 4 + length;
    }
    Some(())
}

fn offset_u16(body: &[u8], at: usize) -> Option<u16> {
    let bytes: [u8; 2] = body.get(at..at + 2)?.try_into().ok()?;
    Some(u16::from_le_bytes(bytes))
}

fn offset_u32(body: &[u8], at: usize) -> Option<u32> {
    let bytes: [u8; 4] = body.get(at..at + 4)?.try_into().ok()?;
    Some(u32::from_le_bytes(bytes))
}

fn offset_u64(body: &[u8], at: usize) -> Option<u64> {
    let bytes: [u8; 8] = body.get(at..at + 8)?.try_into().ok()?;
    Some(u64::from_le_bytes(bytes))
}

/// CRC-32 as zip stores it: the reflected polynomial, seeded and finished the way
/// zlib does it, which is what `mz_zip_reader_extract_to_mem` checks an entry
/// against and what refuses a corrupt record instead of parsing it as XML.
fn crc32(body: &[u8]) -> u32 {
    let mut crc = 0xFFFF_FFFFu32;
    for byte in body {
        crc = CRC_TABLE[((crc ^ u32::from(*byte)) & 0xFF) as usize] ^ (crc >> 8);
    }
    crc ^ 0xFFFF_FFFF
}

const CRC_TABLE: [u32; 256] = crc_table();

const fn crc_table() -> [u32; 256] {
    let mut table = [0u32; 256];
    let mut index = 0;
    while index < 256 {
        let mut value = index as u32;
        let mut bit = 0;
        while bit < 8 {
            value = if value & 1 == 1 {
                0xEDB8_8320 ^ (value >> 1)
            } else {
                value >> 1
            };
            bit += 1;
        }
        table[index] = value;
        index += 1;
    }
    table
}

enum Step {
    Start(Element),
    End(String),
    Text(String),
}

/// One element as the readers need it: its local name and its attributes, the prefix
/// stripped from both because the reference slicers write parts of these documents in
/// their own namespace.
struct Element {
    name: String,
    attributes: Vec<(String, String)>,
}

impl Element {
    fn value(&self, name: &str) -> Option<&str> {
        self.attributes
            .iter()
            .find(|(key, _)| key == name)
            .map(|(_, value)| value.as_str())
    }

    fn integer(&self, name: &str) -> i32 {
        self.value(name).map_or(0, integer)
    }
}

/// A pull scanner over one of the small XML documents inside a project: elements,
/// their attributes and their text, which is the whole of what these four formats use.
struct Scanner<'a> {
    body: &'a [u8],
    at: usize,
    depth: usize,
    pending_end: Option<String>,
    failed: bool,
}

impl<'a> Scanner<'a> {
    fn new(body: &'a [u8]) -> Scanner<'a> {
        Scanner {
            body,
            at: 0,
            depth: 0,
            pending_end: None,
            failed: false,
        }
    }

    /// Whether the document ended in the middle of an element, which is a truncated
    /// file rather than a short one.
    fn failed(&self) -> bool {
        self.failed
    }

    /// The text of the element that just started, its children included, up to and
    /// including its matching end tag.
    fn read_text(&mut self) -> String {
        let mut text = String::new();
        let mut depth = 1;
        while let Some(step) = self.next() {
            match step {
                Step::Start(_) => depth += 1,
                Step::End(_) => {
                    depth -= 1;
                    if depth == 0 {
                        return text;
                    }
                }
                Step::Text(body) => text.push_str(&body),
            }
        }
        text
    }

    fn next(&mut self) -> Option<Step> {
        if let Some(name) = self.pending_end.take() {
            self.depth = self.depth.saturating_sub(1);
            return Some(Step::End(name));
        }

        loop {
            let Some(open) = self.body[self.at..]
                .iter()
                .position(|byte| *byte == b'<')
                .map(|at| at + self.at)
            else {
                return self.text_to_end();
            };

            if open > self.at {
                let text = decode(&self.body[self.at..open]);
                self.at = open;
                return Some(Step::Text(text));
            }

            if self.body[open..].starts_with(b"<?") {
                self.at = self.past(open, b"?>")?;
                continue;
            }
            if self.body[open..].starts_with(b"<!--") {
                self.at = self.past(open, b"-->")?;
                continue;
            }
            if self.body[open..].starts_with(b"<![CDATA[") {
                let start = open + 9;
                let Some(end) = find(self.body, start, b"]]>") else {
                    return self.fail();
                };
                self.at = end + 3;
                return Some(Step::Text(
                    String::from_utf8_lossy(&self.body[start..end]).into_owned(),
                ));
            }
            if self.body[open..].starts_with(b"<!") {
                self.at = self.past(open, b">")?;
                continue;
            }
            if self.body[open..].starts_with(b"</") {
                let Some(end) = find(self.body, open, b">") else {
                    return self.fail();
                };
                let name = local_name(&self.body[open + 2..end]);
                self.at = end + 1;
                self.depth = self.depth.saturating_sub(1);
                return Some(Step::End(name));
            }

            self.at = open;
            let element = match self.start_element() {
                Some(element) => element,
                None => return self.fail(),
            };
            self.depth += 1;
            return Some(Step::Start(element));
        }
    }

    fn text_to_end(&mut self) -> Option<Step> {
        if self.at >= self.body.len() {
            self.failed |= self.depth != 0;
            return None;
        }
        let text = decode(&self.body[self.at..]);
        self.at = self.body.len();
        Some(Step::Text(text))
    }

    fn fail(&mut self) -> Option<Step> {
        self.failed = true;
        None
    }

    fn past(&mut self, from: usize, needle: &[u8]) -> Option<usize> {
        let Some(at) = find(self.body, from, needle) else {
            self.failed = true;
            return None;
        };
        Some(at + needle.len())
    }

    fn start_element(&mut self) -> Option<Element> {
        let mut at = self.at + 1;
        let name_start = at;
        while at < self.body.len()
            && !is_space(self.body[at])
            && self.body[at] != b'/'
            && self.body[at] != b'>'
        {
            at += 1;
        }
        let name = local_name(&self.body[name_start..at]);
        let mut attributes = Vec::new();

        loop {
            while at < self.body.len() && is_space(self.body[at]) {
                at += 1;
            }
            match self.body.get(at) {
                Some(b'>') => {
                    self.at = at + 1;
                    return Some(Element { name, attributes });
                }
                Some(b'/') => {
                    if self.body.get(at + 1) != Some(&b'>') {
                        return None;
                    }
                    self.at = at + 2;
                    self.pending_end = Some(name.clone());
                    return Some(Element { name, attributes });
                }
                Some(_) => {}
                None => return None,
            }

            let key_start = at;
            while at < self.body.len()
                && !is_space(self.body[at])
                && self.body[at] != b'='
                && self.body[at] != b'>'
            {
                at += 1;
            }
            let key = local_name(&self.body[key_start..at]);
            while at < self.body.len() && is_space(self.body[at]) {
                at += 1;
            }
            if self.body.get(at) != Some(&b'=') {
                return None;
            }
            at += 1;
            while at < self.body.len() && is_space(self.body[at]) {
                at += 1;
            }
            let quote = *self.body.get(at)?;
            if quote != b'"' && quote != b'\'' {
                return None;
            }
            at += 1;
            let value_start = at;
            while at < self.body.len() && self.body[at] != quote {
                at += 1;
            }
            if at >= self.body.len() {
                return None;
            }
            attributes.push((key, decode(&self.body[value_start..at])));
            at += 1;
        }
    }
}

fn find(body: &[u8], from: usize, needle: &[u8]) -> Option<usize> {
    body.get(from..)?
        .windows(needle.len())
        .position(|candidate| candidate == needle)
        .map(|at| at + from)
}

fn is_space(byte: u8) -> bool {
    matches!(byte, b' ' | b'\t' | b'\r' | b'\n')
}

fn local_name(name: &[u8]) -> String {
    let start = name
        .iter()
        .rposition(|byte| *byte == b':')
        .map_or(0, |colon| colon + 1);
    String::from_utf8_lossy(&name[start..]).into_owned()
}

/// Text with the XML entities a metadata value can carry replaced, so a custom
/// G-code record arrives with the characters the author wrote.
fn decode(bytes: &[u8]) -> String {
    let raw = String::from_utf8_lossy(bytes);
    if !raw.contains('&') {
        return raw.into_owned();
    }

    let mut out = String::with_capacity(raw.len());
    let mut rest = raw.as_ref();
    while let Some(ampersand) = rest.find('&') {
        out.push_str(&rest[..ampersand]);
        let tail = &rest[ampersand..];
        let Some(semicolon) = tail.find(';') else {
            out.push_str(tail);
            return out;
        };
        let entity = &tail[1..semicolon];
        let replacement = match entity {
            "amp" => Some('&'),
            "lt" => Some('<'),
            "gt" => Some('>'),
            "quot" => Some('"'),
            "apos" => Some('\''),
            _ => reference(entity),
        };
        match replacement {
            Some(character) => out.push(character),
            None => out.push_str(&tail[..=semicolon]),
        }
        rest = &tail[semicolon + 1..];
    }
    out.push_str(rest);
    out
}

fn reference(entity: &str) -> Option<char> {
    let digits = entity.strip_prefix('#')?;
    let code = match digits
        .strip_prefix('x')
        .or_else(|| digits.strip_prefix('X'))
    {
        Some(hexadecimal) => u32::from_str_radix(hexadecimal, 16).ok()?,
        None => digits.parse().ok()?,
    };
    char::from_u32(code)
}

#[cfg(test)]
mod tests {
    use super::*;

    fn scratch(name: &str) -> std::path::PathBuf {
        std::env::temp_dir().join(format!("next-layer-3mf-{}-{name}", std::process::id()))
    }

    /// A zip of stored entries, written by hand: the reader is what is under test, so
    /// nothing here goes through it.
    fn stored_zip(entries: &[(&str, &[u8])]) -> Vec<u8> {
        let mut body = Vec::new();
        let mut directory = Vec::new();
        for (name, contents) in entries {
            let offset = body.len() as u32;
            let crc = crc32(contents);

            body.extend_from_slice(LOCAL_HEADER);
            body.extend_from_slice(&[20, 0, 0, 0, 0, 0, 0, 0, 0, 0]);
            body.extend_from_slice(&crc.to_le_bytes());
            body.extend_from_slice(&(contents.len() as u32).to_le_bytes());
            body.extend_from_slice(&(contents.len() as u32).to_le_bytes());
            body.extend_from_slice(&(name.len() as u16).to_le_bytes());
            body.extend_from_slice(&0u16.to_le_bytes());
            body.extend_from_slice(name.as_bytes());
            body.extend_from_slice(contents);

            directory.extend_from_slice(DIRECTORY_ENTRY);
            directory.extend_from_slice(&[20, 0, 20, 0, 0, 0, 0, 0, 0, 0, 0, 0]);
            directory.extend_from_slice(&crc.to_le_bytes());
            directory.extend_from_slice(&(contents.len() as u32).to_le_bytes());
            directory.extend_from_slice(&(contents.len() as u32).to_le_bytes());
            directory.extend_from_slice(&(name.len() as u16).to_le_bytes());
            directory.extend_from_slice(&0u16.to_le_bytes());
            directory.extend_from_slice(&0u16.to_le_bytes());
            directory.extend_from_slice(&0u16.to_le_bytes());
            directory.extend_from_slice(&0u16.to_le_bytes());
            directory.extend_from_slice(&0u32.to_le_bytes());
            directory.extend_from_slice(&offset.to_le_bytes());
            directory.extend_from_slice(name.as_bytes());
        }

        let offset = body.len() as u32;
        let size = directory.len() as u32;
        body.extend_from_slice(&directory);
        body.extend_from_slice(EOCD);
        body.extend_from_slice(&0u16.to_le_bytes());
        body.extend_from_slice(&0u16.to_le_bytes());
        body.extend_from_slice(&(entries.len() as u16).to_le_bytes());
        body.extend_from_slice(&(entries.len() as u16).to_le_bytes());
        body.extend_from_slice(&size.to_le_bytes());
        body.extend_from_slice(&offset.to_le_bytes());
        body.extend_from_slice(&0u16.to_le_bytes());
        body
    }

    #[test]
    fn a_crc_is_the_one_zlib_computes() {
        assert_eq!(crc32(b"123456789"), 0xCBF4_3926);
        assert_eq!(crc32(b""), 0);
    }

    #[test]
    fn an_entry_is_read_by_its_name_and_a_corrupt_one_is_refused() {
        let path = scratch("archive.zip");
        let zipped = stored_zip(&[
            ("Metadata/model_settings.config", b"<config/>"),
            ("3D/3dmodel.model", b"<model/>"),
        ]);
        std::fs::write(&path, &zipped).expect("a scratch archive");

        let mut archive = Archive::open(path.to_str().expect("a path")).expect("an archive");
        assert_eq!(
            archive.read_named("3D/3dmodel.model"),
            b"<model/>".to_vec()
        );
        assert!(archive.read_named("nothing/here.config").is_empty());
        assert_eq!(archive.gcode_index(), None);

        let mut damaged = zipped;
        let stored_at = LOCAL_HEADER.len() + 26 + "Metadata/model_settings.config".len();
        damaged[stored_at] ^= 0xFF;
        std::fs::write(&path, &damaged).expect("a scratch archive");
        let mut archive = Archive::open(path.to_str().expect("a path")).expect("an archive");
        assert!(archive.read(0).is_none());
        assert!(archive.read(1).is_some());
        let _ = std::fs::remove_file(&path);
    }

    #[test]
    fn a_project_reads_its_plates_its_build_list_and_its_records() {
        let path = scratch("project.3mf");
        let scene = "<model><resources/>\
                     <build>\
                     <item objectid=\"4\" transform=\"1 0 0 0 1 0 0 0 1 60 60 0\"/>\
                     <item objectid=\"5\" transform=\"1 0 0 0 1 0 0 0 1 100 60 0\"/>\
                     </build></model>";
        let settings = "<config>\
                        <object id=\"4\"><metadata key=\"plater_id\" value=\"1\"/></object>\
                        <plate><metadata key=\"plater_id\" value=\"2\"/>\
                        <model_instance><metadata key=\"object_id\" value=\"4\"/></model_instance>\
                        </plate>\
                        </config>";
        std::fs::write(
            &path,
            stored_zip(&[
                ("3D/3dmodel.model", scene.as_bytes()),
                ("Metadata/model_settings.config", settings.as_bytes()),
            ]),
        )
        .expect("a scratch project");

        let project = read_three_mf_project(path.to_str().expect("a path"));
        let _ = std::fs::remove_file(&path);

        assert!(project.valid);
        assert_eq!(project.build_order, vec![4, 5]);
        assert_eq!(project.build_transforms.len(), 24);
        assert_eq!(project.build_transforms[9], 60.0);
        assert_eq!(project.build_transforms[21], 100.0);
        assert_eq!(project.plate_count, 2);
        assert_eq!(project.plates.len(), 1);
        assert_eq!(project.plates[0].object, 4);
        assert_eq!(project.plates[0].plate, 2);
    }

    #[test]
    fn a_file_with_no_model_is_not_a_project() {
        let path = scratch("sliced.3mf");
        std::fs::write(
            &path,
            stored_zip(&[("Metadata/plate_1.gcode", b"G1 X1 Y1\n")]),
        )
        .expect("a scratch archive");

        assert!(read_three_mf_project(path.to_str().expect("a path")).build_order.is_empty());
        assert!(is_pre_sliced_file(path.to_str().expect("a path")));
        let _ = std::fs::remove_file(&path);
    }

    #[test]
    fn the_triangle_range_is_a_field_and_never_a_setting() {
        let config = "<config><object id=\"2\">\
                      <volume firstid=\"12\"><metadata key=\"volume_type\" value=\"ModelPart\"/>\
                      <metadata key=\"__first_triangle\" value=\"99\"/>\
                      <metadata key=\"perimeters\" value=\"3\"/>\
                      </volume></object></config>";
        let mut project = Project::default();
        read_model_config(config.as_bytes(), &mut project);

        let record = project.objects.get(&2).expect("the object record");
        let volume = record.volumes.first().expect("the volume record");
        assert_eq!(volume.first_triangle, 12);
        assert_eq!(volume.kind, "ModelPart");
        assert_eq!(volume.settings.0.len(), 1);
        assert_eq!(volume.settings.0[0].name, "perimeters");
    }

    #[test]
    fn paint_lands_on_the_volume_that_owns_the_triangle() {
        let mut project = Project::default();
        project.objects.insert(
            7,
            ObjectRecord {
                volumes: vec![
                    VolumeRecord {
                        first_triangle: 0,
                        ..VolumeRecord::default()
                    },
                    VolumeRecord {
                        first_triangle: 4,
                        ..VolumeRecord::default()
                    },
                ],
                ..ObjectRecord::default()
            },
        );
        let scene = "<model><resources><object id=\"7\"><mesh>\
                     <triangle v1=\"0\" v2=\"1\" v3=\"2\" slic3rpe:custom_supports=\"1\"/>\
                     <triangle v1=\"0\" v2=\"1\" v3=\"2\"/>\
                     <triangle v1=\"0\" v2=\"1\" v3=\"2\"/>\
                     <triangle v1=\"0\" v2=\"1\" v3=\"2\" slic3rpe:custom_supports=\"2\"/>\
                     <triangle v1=\"0\" v2=\"1\" v3=\"2\" slic3rpe:custom_seam=\"3\"/>\
                     </mesh></object></resources></model>";

        read_paint(scene.as_bytes(), &mut project);

        let record = project.objects.get(&7).expect("the object record");
        assert_eq!(record.volumes[0].supports.len(), 2);
        assert_eq!(record.volumes[0].supports[0].triangle, 0);
        assert_eq!(record.volumes[0].supports[1].triangle, 3);
        assert!(record.volumes[1].supports.is_empty());
        assert_eq!(record.volumes[1].seams.len(), 1);
        assert_eq!(record.volumes[1].seams[0].triangle, 0);
        assert_eq!(record.volumes[1].seams[0].text, "3");
    }

    #[test]
    fn a_marker_type_is_the_number_or_the_name_a_writer_used() {
        assert_eq!(marker_type("0"), 0);
        assert_eq!(marker_type("PausePrint"), 1);
        assert_eq!(marker_type("colorchange"), 0);
        assert_eq!(marker_type("ToolChange"), 2);
        assert_eq!(marker_type("Template"), 3);
        assert_eq!(marker_type(""), 4);
        assert_eq!(marker_type("something else"), 4);
        assert!(is_colour("#FF8000"));
        assert!(!is_colour("check the first layer"));
    }
}
