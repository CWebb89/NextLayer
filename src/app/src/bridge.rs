//! The functions the C++ that remains calls into Rust.
//!
//! Every ported file keeps its C++ callers working through this one
//! `#[cxx::bridge]`, whose generated header is `next-layer-app/src/bridge.rs.h`.
//! It is the reverse of `hfw`: that boundary is Rust asking a C library, this one
//! is C++ asking this crate, and both disappear as `src/qt/src` empties.

pub use crate::mesh::Mesh;

pub use ffi::{
    MeshGeometryFrame, ObjectSettingInput, ObjectSettingRecord, ObjectSettingsRead,
    PresetPrinterRecord, ProjectMarkerRecord, ProjectMetadataRecord,
    ProjectObjectRecord, ProjectOutcome, ProjectPaint, ProjectPaintOutcome, ProjectSetting,
    ProjectVolumeRecord, SettingChangeRecord, SettingChoiceRecord, SettingDefinitionRecord,
    SettingElementRecord, SettingEnumValueRecord, SettingGroupRecord, SettingInputRecord,
    SettingItemRecord, SettingModifiedRecord, SettingScalarRecord, SettingSubtabRecord,
    SettingTabRecord, SettingValueRecord,
};

pub use crate::SettingsModel::Model as SettingsModelCore;

pub use ffi::{
    ThreeMfFileFacet, ThreeMfFileForeign, ThreeMfFileMarker, ThreeMfFileName, ThreeMfFileOutcome,
    ThreeMfFilePlate, ThreeMfFileRecord, ThreeMfFileSetting, ThreeMfFileVolume,
};

pub use ffi::{
    EngineMarker, EngineObject, EngineOutcome, EnginePlate, EngineSetting, EngineStroke,
    EngineVolume, PlateOutcome, PlateReadback, ReadbackObject, ReadbackVolume,
};

use cxx::CxxString;

#[cxx::bridge(namespace = "next_layer")]
mod ffi {
    /// One paint channel: the triangles the engine split, the state bitstream it
    /// walked, and which states the channel uses.
    struct ProjectPaint {
        /// Flat (triangle index, bitstream start) pairs.
        triangles: Vec<i64>,
        /// The bitstream as text, one `0` or `1` per bit.
        bitstream: String,
        /// The state numbers the channel sets.
        states: Vec<u32>,
    }

    /// One overridden setting, discriminated by `kind` so a `QVariant` never has to
    /// cross the bridge.
    struct ProjectSetting {
        name: String,
        /// 0 text, 1 boolean, 2 integer, 3 number, 4 the `{value, is_percent}` object.
        kind: u8,
        truth: bool,
        number: f64,
        text: String,
    }

    /// A volume attached to an object: geometry in the object's own frame, its own
    /// transform, and the role it plays in the slice.
    struct ProjectVolumeRecord {
        name: String,
        kind: String,
        visible: bool,
        /// Sixteen `f64`, row-major, four rows of four.
        matrix: Vec<f64>,
        /// The mesh as binary STL bytes.
        mesh: Vec<u8>,
    }

    /// One object as a project file stores it.
    struct ProjectObjectRecord {
        name: String,
        source: String,
        plate: i32,
        visible: bool,
        /// Sixteen `f64`, row-major, four rows of four.
        matrix: Vec<f64>,
        /// The mesh as binary STL bytes.
        mesh: Vec<u8>,
        volumes: Vec<ProjectVolumeRecord>,
        settings: Vec<ProjectSetting>,
        /// Flat (z, layer height) pairs.
        layer_heights: Vec<f64>,
        support_paint: ProjectPaint,
        seam_paint: ProjectPaint,
        tool_paint: ProjectPaint,
    }

    /// A marker on the plate's timeline, stored with the plate because the engine
    /// applies it to the bed instance rather than to any one object.
    struct ProjectMarkerRecord {
        z: f64,
        kind: String,
        text: String,
    }

    /// The state the engine has no concept of and the project keeps for itself.
    struct ProjectMetadataRecord {
        application: String,
        printer: String,
        process: String,
        filament: String,
        active_plate: i32,
        plate_count: i32,
        plate_names: Vec<String>,
        plate_locked: Vec<bool>,
        tool_colours: Vec<String>,
        markers: Vec<ProjectMarkerRecord>,
    }

    /// What a project file turned out to hold, or why it held nothing.
    struct ProjectOutcome {
        ok: bool,
        error: String,
        objects: Vec<ProjectObjectRecord>,
        metadata: ProjectMetadataRecord,
    }

    /// One paint channel as text, and back.
    struct ProjectPaintOutcome {
        ok: bool,
        paint: ProjectPaint,
    }

    /// One interleaved vertex buffer as `QQuick3DGeometry` takes it, with the
    /// triangle corners a raycast answers against.
    struct MeshGeometryFrame {
        /// Interleaved vertices as bytes: position, then normal, then the colour
        /// `colour_offset` points at when there is one.
        vertices: Vec<u8>,
        /// The corner positions a raycast runs against, nine `f32` per triangle.
        /// Empty from every builder but the plain mesh one, which is what leaves a
        /// coloured overlay's raycast answering against the previous mesh.
        positions: Vec<f32>,
        low: [f32; 3],
        high: [f32; 3],
        /// Bytes from one vertex to the next: 24, or 40 with a colour.
        stride: u32,
        /// Where the colour starts inside a vertex, or -1 when there is none.
        colour_offset: i32,
    }

    /// One painted facet: the triangle's index inside its volume, and the file's
    /// own paint string for it.
    struct ThreeMfFileFacet {
        triangle: i32,
        text: String,
    }

    /// One metadata record from a project file: an object's or volume's override,
    /// or a record another slicer wrote under its own name.
    struct ThreeMfFileSetting {
        name: String,
        value: String,
    }

    /// One volume of an object: its role, the index of its first triangle in the
    /// object's mesh, its own overrides, and the facets painted on it.
    struct ThreeMfFileVolume {
        kind: String,
        first_triangle: i32,
        settings: Vec<ThreeMfFileSetting>,
        supports: Vec<ThreeMfFileFacet>,
        seams: Vec<ThreeMfFileFacet>,
        tools: Vec<ThreeMfFileFacet>,
    }

    /// One object of a project, keyed out here by `id`.
    struct ThreeMfFileRecord {
        id: i32,
        name: String,
        settings: Vec<ThreeMfFileSetting>,
        volumes: Vec<ThreeMfFileVolume>,
        /// Flat (z, layer height) pairs.
        layer_heights: Vec<f64>,
    }

    /// The plate one object belongs to.
    struct ThreeMfFilePlate {
        object: i32,
        plate: i32,
    }

    /// The label a foreign project gives one object.
    struct ThreeMfFileName {
        id: i32,
        name: String,
    }

    /// The settings a foreign project keeps for one object, under its own names.
    struct ThreeMfFileForeign {
        id: i32,
        settings: Vec<ThreeMfFileSetting>,
    }

    /// One marker on a bed's timeline.
    struct ThreeMfFileMarker {
        bed: i32,
        z: f64,
        kind: i32,
        text: String,
    }

    /// What a project file held, or `valid` false when it held no model.
    struct ThreeMfFileOutcome {
        valid: bool,
        application: String,
        plate_count: i32,
        build_order: Vec<i32>,
        /// Twelve `f64` per build item, row-major, three rows of four.
        build_transforms: Vec<f64>,
        plates: Vec<ThreeMfFilePlate>,
        records: Vec<ThreeMfFileRecord>,
        foreign_names: Vec<ThreeMfFileName>,
        foreign_settings: Vec<ThreeMfFileForeign>,
        markers: Vec<ThreeMfFileMarker>,
    }

    /// One line of the user bundle's manifest: the name a person typed, the slug
    /// every id in the bundle is built from, and how many tools the machine has.
    struct PresetPrinterRecord {
        name: String,
        slug: String,
        tools: i32,
    }

    /// One setting out of a config box as the engine holds it: the name, and the
    /// value in the shapes `Slic3r::Domain::ConfigValue` can take, which is what
    /// `SettingValueRecord` already carries.
    struct ObjectSettingInput {
        name: String,
        value: SettingValueRecord,
    }

    /// One setting as this application's per-object settings map stores it.
    ///
    /// `kind` is 0 text, 1 boolean, 2 integer, 3 number, 4 the `{value, is_percent}`
    /// object a percentage is, 5 the same pair for a float-or-percentage with
    /// `truth` as `is_percent`, 6 a list of names, 7 a list of integers, 8 a list of
    /// numbers, 9 a list of flags.
    ///
    /// Deliberately not `SettingValueRecord`: that record is the settings panel's
    /// vocabulary, where a percentage is a bare number and the editor supplies the
    /// unit. A percentage in an *override* is the pair `load_box` reads back, so the
    /// two vocabularies cannot be one record.
    struct ObjectSettingRecord {
        name: String,
        kind: u8,
        truth: bool,
        integer: i64,
        number: f64,
        text: String,
        integers: Vec<i64>,
        numbers: Vec<f64>,
        texts: Vec<String>,
        flags: Vec<u8>,
    }

    /// What a settings map held, and every name the reader could not represent.
    struct ObjectSettingsRead {
        settings: Vec<ObjectSettingRecord>,
        unreadable: Vec<String>,
    }

    /// One value as the panel reads it. `kind` is 0 invalid, 1 boolean, 2 integer,
    /// 3 number, 4 text, 5 list, 6 percentage, 7 float-or-percentage, 8 enum; a
    /// list carries `items` (0 numbers, 1 texts, 2 points, 3 integers, 4 booleans,
    /// 5 float-or-percentages, 6 optional integers, 7 percentages, 8 enum
    /// spellings) and fills `numbers`, `texts` and `flags` accordingly.
    struct SettingValueRecord {
        kind: u8,
        truth: bool,
        integer: i64,
        number: f64,
        text: String,
        items: u8,
        numbers: Vec<f64>,
        texts: Vec<String>,
        flags: Vec<u8>,
    }

    /// One `QVariant`'s own answers, so no conversion of one is reimplemented in
    /// Rust: `kind` is 0 invalid, 1 boolean, 2 integer, 3 number, 4 text, 5 list,
    /// 6 map, and the `_ok` fields say whether the converter succeeded.
    struct SettingScalarRecord {
        kind: u8,
        truth: bool,
        integer: i64,
        integer_ok: bool,
        number: f64,
        number_ok: bool,
        text: String,
        map_value: f64,
        map_percent: bool,
    }

    /// One element of a list on its way in: `kind` is 0 number, 1 text, 2 point.
    struct SettingElementRecord {
        kind: u8,
        text: String,
        number: f64,
        number_ok: bool,
        pair_first: f64,
        pair_second: f64,
    }

    /// A value on its way in.
    struct SettingInputRecord {
        is_list: bool,
        whole: SettingScalarRecord,
        first: SettingScalarRecord,
        elements: Vec<SettingElementRecord>,
    }

    /// Whether a write was taken, and whether it moved anything.
    struct SettingChangeRecord {
        applied: bool,
        changed: bool,
    }

    /// One value an enum setting accepts: its serialized spelling, its label, and
    /// the number the engine takes.
    struct SettingEnumValueRecord {
        serialized: String,
        label: String,
        number: i64,
    }

    /// One setting as the engine defines it: everything the panel decides from and
    /// nothing it decides.
    struct SettingDefinitionRecord {
        name: String,
        label: String,
        full_label: String,
        tooltip: String,
        location: u8,
        overrides_in: Vec<u8>,
        category: i32,
        option_group: i32,
        category_title: String,
        option_group_title: String,
        order: i32,
        gui_type: String,
        units: Vec<String>,
        has_min: bool,
        min: f64,
        has_max: bool,
        max: f64,
        multiline: bool,
        full_width: bool,
        enum_values: Vec<SettingEnumValueRecord>,
        default_value: SettingValueRecord,
    }

    /// One entry of a combo box.
    struct SettingChoiceRecord {
        value: String,
        label: String,
    }

    /// One row of the settings tree. `complete` false is the seven-key map the
    /// panel draws for a definition it does not hold.
    struct SettingItemRecord {
        name: String,
        label: String,
        full_label: String,
        tooltip: String,
        type_name: String,
        gui_type: String,
        value: SettingValueRecord,
        has_min: bool,
        min: f64,
        has_max: bool,
        max: f64,
        units: Vec<String>,
        choices: Vec<SettingChoiceRecord>,
        modified: bool,
        percent: bool,
        multiline: bool,
        full_width: bool,
        complete: bool,
    }

    /// One sub-tab of the sidebar.
    struct SettingSubtabRecord {
        key: String,
        title: String,
        count: i32,
        modified: i32,
    }

    /// One tab of the sidebar, with its sub-tabs.
    struct SettingTabRecord {
        key: String,
        title: String,
        count: i32,
        modified: i32,
        subtabs: Vec<SettingSubtabRecord>,
    }

    /// One titled group inside a sub-tab.
    struct SettingGroupRecord {
        key: String,
        title: String,
        tab: String,
        tab_title: String,
        subtab: String,
        subtab_title: String,
        items: Vec<SettingItemRecord>,
    }

    /// One row of the preset diff window.
    struct SettingModifiedRecord {
        name: String,
        label: String,
        tab: String,
        subtab: String,
        subtab_key: String,
        before: SettingValueRecord,
        after: SettingValueRecord,
    }

    /// One setting as a name and the text the engine's preset reader accepts.
    struct EngineSetting {
        name: String,
        value: String,
    }

    /// One volume, on its way to the engine.
    struct EngineVolume {
        /// The document's word for the kind: `part`, `negative`, `modifier`,
        /// `blocker` or `enforcer`.
        kind: String,
        /// The volume's own overrides, as the engine-settings JSON the document holds.
        settings_json: String,
        /// Interleaved xyz, three `f64` per vertex.
        vertices: Vec<f64>,
        /// Vertex numbers, three per triangle.
        indices: Vec<i64>,
    }

    /// One object, on its way to the engine.
    struct EngineObject {
        name: String,
        vertices: Vec<f64>,
        indices: Vec<i64>,
        /// Sixteen `f64`, row-major, four rows of four.
        matrix: Vec<f64>,
        /// The object's overrides as a JSON object, as the document holds them.
        settings_json: String,
        /// Settings the object's file carried in another slicer's dialect.
        foreign_settings: Vec<EngineSetting>,
        /// Interleaved `(z, height)` pairs; empty means the plate's uniform height.
        layer_heights: Vec<f64>,
        /// One byte per triangle: 0 none, 1 enforcer, 2 blocker.
        support_paint: Vec<u8>,
        seam_paint: Vec<u8>,
        /// How many facets the document holds on its tool channel. The states
        /// themselves do not cross: the engine has no third channel.
        tool_paint_facets: i64,
        volumes: Vec<EngineVolume>,
    }

    /// One marker on the plate's timeline.
    #[derive(Clone)]
    struct EngineMarker {
        z: f64,
        kind: String,
        text: String,
    }

    /// One bead printed inside a layer, on paths as interleaved x, y `f64`.
    #[derive(Clone)]
    struct EngineStroke {
        z: f64,
        points: Vec<f64>,
        line_width: f64,
        layer_height: f64,
        speed: f64,
        prefix: String,
    }

    /// A plate, on its way to the engine.
    struct EnginePlate {
        objects: Vec<EngineObject>,
        markers: Vec<EngineMarker>,
        strokes: Vec<EngineStroke>,
        /// True for a plate the writer must centre on the profile's bed: a model named
        /// on the command line, not a plate the window laid out.
        place_on_bed: bool,
    }

    /// One volume, read back.
    struct ReadbackVolume {
        kind: String,
        settings: Vec<EngineSetting>,
        vertices: Vec<f64>,
        indices: Vec<i64>,
    }

    /// One object, read back.
    struct ReadbackObject {
        name: String,
        vertices: Vec<f64>,
        indices: Vec<i64>,
        matrix: Vec<f64>,
        enabled: bool,
        settings: Vec<EngineSetting>,
        layer_heights: Vec<f64>,
        support_paint: Vec<u8>,
        seam_paint: Vec<u8>,
        volumes: Vec<ReadbackVolume>,
    }

    /// A plate read back through the ABI.
    #[derive(Default)]
    struct PlateReadback {
        objects: Vec<ReadbackObject>,
        markers: Vec<EngineMarker>,
        strokes: Vec<EngineStroke>,
        metadata: Vec<u8>,
        /// What the exchange dropped and why, for the caller to show.
        dropped: Vec<String>,
    }

    /// A read, or the reason it failed.
    struct PlateOutcome {
        ok: bool,
        error: String,
        snapshot: PlateReadback,
    }

    /// A write or a slice, or the reason it failed. `report` is `round_trip_plate`'s
    /// count line and empty everywhere else; `dropped` is the same reason list as
    /// `PlateReadback::dropped`.
    struct EngineOutcome {
        ok: bool,
        error: String,
        report: String,
        dropped: Vec<String>,
    }

    unsafe extern "C++" {
        include!("next-layer-app/include/next-layer-qt.hpp");

        /// Leaves Qt's event loop, so the registered teardown runs.
        fn quit_application();
    }

    extern "Rust" {
        /// The settings a config box overrides, in the shapes the settings map
        /// stores.
        fn overridden_settings(items: &[ObjectSettingInput]) -> ObjectSettingsRead;

        /// The same, for the key/value lines a project file writes; the type of
        /// each value comes from the setting's own definition. `names` and `values`
        /// are parallel and in the caller's iteration order.
        fn settings_from_text(
            names: &[String],
            values: &[String],
            definitions: &[SettingDefinitionRecord],
        ) -> ObjectSettingsRead;

        /// Whether this engine lets a setting be overridden per object.
        fn overrides_at_object(name: &str, definitions: &[SettingDefinitionRecord]) -> bool;

        /// The pose that lays one facet of the mesh face down: sixteen `f64`,
        /// row-major, four rows of four, and the input pose unchanged when the facet
        /// is out of range or has no normal.
        fn orient_lay_facet_flat(
            vertices: &[f32],
            indices: &[u32],
            matrix: &[f64],
            facet: i32,
        ) -> Vec<f64>;

        /// The pose that sits lowest and least supported, chosen by height plus
        /// weighted overhanging area: sixteen `f64`, row-major, or empty when no
        /// facet has a normal.
        fn orient_best_resting_rotation(
            vertices: &[f32],
            indices: &[u32],
            matrix: &[f64],
        ) -> Vec<f64>;

        /// The application's own triangle mesh: the document holds one of these per
        /// object and one per volume, and no engine handle stands behind it.
        type Mesh;

        /// Builds a mesh from flat arrays, or says why they cannot be one.
        fn mesh_new(vertices: &[f32], indices: &[u32]) -> Result<Box<Mesh>>;

        /// How many triangles the mesh holds.
        fn mesh_triangle_count(mesh: &Mesh) -> usize;

        /// The xyz triples, three per vertex.
        fn mesh_vertices(mesh: &Mesh) -> &[f32];

        /// The vertex triples, three per triangle.
        fn mesh_indices(mesh: &Mesh) -> &[u32];

        /// The lower then the upper corner of the bounding box, all zero when the
        /// mesh has no vertices.
        fn mesh_bounds(mesh: &Mesh) -> [f32; 6];

        /// A copy moved by `offset`, in millimetres.
        fn mesh_translated(mesh: &Mesh, offset: [f32; 3]) -> Box<Mesh>;

        /// A copy transformed by `matrix`: twelve `f64`, row-major, three rows of
        /// four.
        fn mesh_transformed(mesh: &Mesh, matrix: &[f64]) -> Box<Mesh>;

        /// Both meshes as one, `other`'s faces on the end of this one.
        fn mesh_appended(mesh: &Mesh, other: &Mesh) -> Box<Mesh>;

        /// The volume the mesh encloses under `matrix`, in cubic millimetres.
        fn mesh_volume(mesh: &Mesh, matrix: &[f64]) -> f64;

        /// The surface area the mesh covers under `matrix`, in square millimetres.
        fn mesh_surface_area(mesh: &Mesh, matrix: &[f64]) -> f64;

        /// One label per triangle; triangles sharing a vertex share a label.
        fn mesh_shell_labels(mesh: &Mesh) -> Vec<u32>;

        /// The triangles named by `faces`, renumbered onto their own vertices.
        fn mesh_subset(mesh: &Mesh, faces: &[u32]) -> Box<Mesh>;

        /// The mesh as an interleaved position+normal buffer, with the triangle
        /// corners a raycast answers against.
        fn mesh_geometry_frame(vertices: &[f32], indices: &[u32]) -> MeshGeometryFrame;

        /// The same mesh with one colour per triangle; a triangle whose alpha is
        /// zero is left out, and no raycast corners are produced.
        fn mesh_geometry_coloured_frame(
            vertices: &[f32],
            indices: &[u32],
            colours: &[f32],
        ) -> MeshGeometryFrame;

        /// The brush cursor: a flat annulus in the plane perpendicular to `normal`.
        fn mesh_geometry_ring_frame(
            centre: [f32; 3],
            normal: [f32; 3],
            radius: f32,
            thickness: f32,
        ) -> MeshGeometryFrame;

        /// Nearest triangle hit for a ray: the hit point and the ray parameter,
        /// the parameter negative when the ray missed.
        fn mesh_geometry_raycast(
            positions: &[f32],
            origin: [f32; 3],
            direction: [f32; 3],
        ) -> [f32; 4];

        /// The engine's resources tree: presets, bed meshes, icons, fonts.
        fn resources_dir() -> String;

        /// The version this build stamps into G-code and shows in the window.
        fn application_version() -> String;

        /// Rewrites the branding in an exported G-code file; returns the number of
        /// lines rewritten, or -1 when the file cannot be read or written.
        fn brand_gcode(path: &str, configuration_json: &str) -> i32;

        /// sha256 of a toolpath's move and command lines, comments left out.
        fn toolpath_digest(path: &str) -> String;

        /// The `QTemporaryDir` template a scratch directory is made from.
        fn scratch_template() -> String;

        /// Removes scratch directories whose owning process is gone.
        fn sweep_scratch_dirs();

        /// The two edges a plate thumbnail is rendered at, in the order the blocks
        /// are written.
        fn thumbnail_edges() -> Vec<u32>;

        /// Replaces any `; thumbnail` block in a G-code file, at the top of it;
        /// empty on success, else why the file could not be rewritten.
        fn embed_plate_thumbnails(
            path: &str,
            png_primary: &CxxString,
            png_secondary: &CxxString,
        ) -> String;

        /// The resolved matugen palette, as one JSON object.
        fn palette_json() -> String;

        /// One manifest fetch, as JSON: version, url, sha256, notes, error, newer.
        fn update_check(channel: &str) -> String;

        /// Downloads and verifies an artifact; JSON with `ok`, `error`, `staged`.
        fn update_download(url: &str, sha256: &str, version: &str, root: &str) -> String;

        /// Installs a staged artifact; JSON with `ok` and `error`.
        fn update_install(staged: &str, root: &str) -> String;

        /// The install prefix this build replaces.
        fn update_install_root() -> String;

        /// The model behind the QObject shell: every setting the panel shows, its
        /// value, its placement and the bookkeeping a preset diff reads.
        type SettingsModelCore;

        /// The version this build reports.
        fn update_current_version() -> String;

        /// The foreign-settings renames, as source then target, in table order.
        fn foreign_setting_pairs() -> Vec<String>;

        /// This engine's name and value for a foreign setting, or nothing when the
        /// name is listed as having no counterpart here.
        fn translate_foreign_setting(key: &str, value: &str) -> Vec<String>;

        /// The engine name a foreign key maps to, itself when there is no rename.
        fn foreign_target_for(source: &str) -> String;

        /// The first source name that maps to an engine key, empty when none does.
        fn canonical_foreign_source(engine_key: &str) -> String;

        /// The foreign name and value a setting is written under, or nothing when
        /// that slicer has neither.
        fn foreign_name_for(engine_key: &str, engine_value: &str) -> Vec<String>;

        /// The document, as the bytes of a project file.
        fn serialise_project(
            objects: &[ProjectObjectRecord],
            metadata: &ProjectMetadataRecord,
        ) -> Vec<u8>;

        /// The document a project file holds, or why it could not be read.
        fn parse_project(body: &[u8]) -> ProjectOutcome;

        /// The project file version this build writes and the newest it will read.
        fn project_format_version() -> i32;

        /// Reads a plate back through the ABI into the shape a document is built from.
        fn read_plate_snapshot(plate: &EnginePlate) -> PlateOutcome;

        /// Reads the engine's own project file back into the same shape.
        fn read_plate_document(bytes: &[u8]) -> PlateOutcome;

        /// Saves the plate as the engine's project file, with `metadata` inside it.
        fn save_plate_document(plate: &EnginePlate, metadata: &[u8], path: &str) -> EngineOutcome;

        /// Reads a plate back, rebuilds it from nothing but what was read, and
        /// requires the two documents identical.
        fn round_trip_plate(plate: &EnginePlate) -> EngineOutcome;

        /// Slices the plate with HFW, writing the G-code this application prints.
        fn slice_with_hfw(plate: &EnginePlate, config_path: &str, gcode_path: &str)
            -> EngineOutcome;

        /// One paint channel as text.
        fn paint_to_text(paint: &ProjectPaint) -> Vec<u8>;

        /// One paint channel read back from text.
        fn paint_from_text(text: &[u8]) -> ProjectPaintOutcome;

        /// One YAML scalar, single-quoted unless it holds a newline.
        fn yaml_quoted(text: &str) -> String;

        /// `QString::number(value, 'g', 15)`: fifteen significant digits, trailing
        /// zeros dropped, a signed two-digit exponent.
        fn yaml_number(value: f64) -> String;

        /// The inverse for one scalar: trimmed, one matched quote pair stripped.
        fn yaml_unquoted(text: &str) -> String;

        /// One `x xy` point, the separator inside the quotes.
        fn yaml_point(x: f64, y: f64) -> String;

        /// Where presets live on disk.
        fn presets_root() -> String;

        /// The user's own bundle inside it.
        fn presets_custom_vendor_directory() -> String;

        /// The shipped filament presets the bundle does not have yet; the names
        /// arrive newline-joined.
        fn presets_filament_library_plan(directory: &str, library_names: &str) -> Vec<String>;

        /// The file-name and id fragment a printer name turns into.
        fn presets_slug_of(name: &str) -> String;

        /// A version 4 UUID in canonical spelling.
        fn presets_fresh_id() -> String;

        /// Writes a file whole, creating the directory chain.
        fn presets_write_file(path: &str, contents: &str) -> bool;

        /// One preset document in the engine's own shape; an empty `id` gets a
        /// fresh one.
        fn presets_document(
            kind: &str,
            name: &str,
            parent: &str,
            condition: &str,
            values_yaml: &str,
            id: &str,
        ) -> String;

        /// The bundle's manifest, in file order.
        fn presets_custom_printers() -> Vec<PresetPrinterRecord>;

        /// Registers or updates a printer and rewrites the manifest and the vendor
        /// description; empty on success, else why nothing was written.
        fn presets_add_custom_printer(name: &str, tools: i32) -> String;

        /// The preset file a registered printer's settings go to, empty when the
        /// bundle does not list it.
        fn presets_custom_printer_path(name: &str) -> String;

        /// Writes the one empty tool-print preset a bundle needs to be evaluable.
        fn presets_ensure_tool_print_preset() -> String;

        /// Builds the editor from the engine's definitions, the layout document and
        /// the base config, both as the bytes of a Qt resource.
        fn settings_model_new(
            definitions: &[SettingDefinitionRecord],
            layout_json: &str,
            base_config_json: &str,
        ) -> Box<SettingsModelCore>;

        /// The three tabs with their sub-tabs, counts and modified counts.
        fn settings_tab_list(model: &SettingsModelCore) -> Vec<SettingTabRecord>;

        /// The groups of one sub-tab, filtered and with the capability list applied.
        fn settings_groups(
            model: &SettingsModelCore,
            tab: &str,
            subtab: &str,
            filter: &str,
        ) -> Vec<SettingGroupRecord>;

        /// One setting's value, as QML reads it.
        fn settings_value(model: &SettingsModelCore, name: &str) -> SettingValueRecord;

        /// The block a setting belongs to: print, filament or printer.
        fn settings_block_of(model: &SettingsModelCore, name: &str) -> String;

        /// Whether a setting may be overridden from the given block.
        fn settings_can_hold_in(model: &SettingsModelCore, name: &str, block: &str) -> bool;

        /// A bed shape's points, flat `x, y` pairs, and nothing for any other type.
        fn settings_points(model: &SettingsModelCore, name: &str) -> Vec<f64>;

        /// Writes one value, answering whether it was taken and whether it moved.
        fn settings_set_value(
            model: &mut SettingsModelCore,
            name: &str,
            input: &SettingInputRecord,
        ) -> SettingChangeRecord;

        /// Returns one setting to its default: true when it moved.
        fn settings_reset(model: &mut SettingsModelCore, name: &str) -> bool;

        /// Replaces the values and the template from a resolved configuration.
        fn settings_seed_from_config_json(model: &mut SettingsModelCore, json: &str) -> bool;

        /// Resets one sub-tab: true when at least one entry moved.
        fn settings_reset_subtab(model: &mut SettingsModelCore, tab: &str, subtab: &str) -> bool;

        /// Resets one tab: true when at least one entry moved.
        fn settings_reset_tab(model: &mut SettingsModelCore, tab: &str) -> bool;

        /// How many entries in a tab differ from their defaults.
        fn settings_modified_count(model: &SettingsModelCore, tab: &str) -> i32;

        /// One row per changed entry, with the value before and after.
        fn settings_modified_values(model: &SettingsModelCore, tab: &str) -> Vec<SettingModifiedRecord>;

        /// Every setting an object may override, filtered by name or label.
        fn settings_object_defs(model: &SettingsModelCore, filter: &str) -> Vec<SettingItemRecord>;

        /// The tab tree as text, the format `--dump-tabs` prints.
        fn settings_tab_dump(model: &SettingsModelCore) -> String;

        /// Replaces the names the capability list hides.
        fn settings_set_unsupported(model: &mut SettingsModelCore, names: Vec<String>);

        /// Those names, sorted.
        fn settings_unsupported(model: &SettingsModelCore) -> Vec<String>;

        /// The ten-key row per setting, in model order, as `--dump-settings-json`
        /// prints it and the layout tool reads it.
        fn settings_json_dump(model: &SettingsModelCore) -> String;

        /// Every visible name, depth first, one per line.
        fn settings_name_dump(model: &SettingsModelCore) -> String;

        /// True once a resolved preset has seeded the model.
        fn settings_has_resolved_config(model: &SettingsModelCore) -> bool;

        /// True when the layout document parsed: false is the silent collapse onto
        /// the engine's own categories, and the shell refuses to start on it.
        fn settings_layout_loaded(model: &SettingsModelCore) -> bool;

        /// The engine's configuration document with the current values overlaid.
        fn settings_config_json(model: &SettingsModelCore) -> String;

        /// A tab's values as the body of a preset file.
        fn settings_values_yaml(
            model: &SettingsModelCore,
            tab: &str,
            changed_only: bool,
        ) -> String;

        /// Drops every override: true when at least one entry moved.
        fn settings_clear_modified(model: &mut SettingsModelCore) -> bool;

        /// A project file's own records, or `valid` false when it holds no model.
        fn read_three_mf_project(path: &str) -> ThreeMfFileOutcome;

        /// Whether the file is a sliced print: a `.gcode`, or a `.3mf` carrying a
        /// G-code and no mesh.
        fn is_pre_sliced_file(path: &str) -> bool;

        /// Writes the G-code inside a pre-sliced file to `out_path`.
        fn extract_pre_sliced_gcode(path: &str, out_path: &str) -> bool;
    }
}

pub fn quit_application() {
    ffi::quit_application();
}

fn overridden_settings(items: &[ObjectSettingInput]) -> ObjectSettingsRead {
    crate::ConfigBoxJson::overridden_settings(items)
}

fn settings_from_text(
    names: &[String],
    values: &[String],
    definitions: &[SettingDefinitionRecord],
) -> ObjectSettingsRead {
    crate::ConfigBoxJson::settings_from_text(names, values, definitions)
}

fn overrides_at_object(name: &str, definitions: &[SettingDefinitionRecord]) -> bool {
    crate::ConfigBoxJson::overrides_at_object(name, definitions)
}

fn orient_lay_facet_flat(vertices: &[f32], indices: &[u32], matrix: &[f64], facet: i32) -> Vec<f64> {
    crate::Orient::lay_facet_flat(vertices, indices, matrix, facet)
}

fn orient_best_resting_rotation(vertices: &[f32], indices: &[u32], matrix: &[f64]) -> Vec<f64> {
    crate::Orient::best_resting_rotation(vertices, indices, matrix)
}

fn mesh_new(vertices: &[f32], indices: &[u32]) -> Result<Box<Mesh>, String> {
    crate::mesh::Mesh::new(vertices.to_vec(), indices.to_vec()).map(Box::new)
}

fn mesh_triangle_count(mesh: &Mesh) -> usize {
    mesh.triangle_count()
}

fn mesh_vertices(mesh: &Mesh) -> &[f32] {
    mesh.vertices()
}

fn mesh_indices(mesh: &Mesh) -> &[u32] {
    mesh.indices()
}

fn mesh_bounds(mesh: &Mesh) -> [f32; 6] {
    match mesh.bounds() {
        Some((low, high)) => [low[0], low[1], low[2], high[0], high[1], high[2]],
        None => [0.0; 6],
    }
}

fn mesh_translated(mesh: &Mesh, offset: [f32; 3]) -> Box<Mesh> {
    Box::new(mesh.translated(offset))
}

fn mesh_transformed(mesh: &Mesh, matrix: &[f64]) -> Box<Mesh> {
    Box::new(mesh.transformed(matrix))
}

fn mesh_appended(mesh: &Mesh, other: &Mesh) -> Box<Mesh> {
    Box::new(mesh.appended(other))
}

fn mesh_volume(mesh: &Mesh, matrix: &[f64]) -> f64 {
    mesh.volume(matrix)
}

fn mesh_surface_area(mesh: &Mesh, matrix: &[f64]) -> f64 {
    mesh.surface_area(matrix)
}

fn mesh_shell_labels(mesh: &Mesh) -> Vec<u32> {
    mesh.shell_labels()
}

fn mesh_subset(mesh: &Mesh, faces: &[u32]) -> Box<Mesh> {
    Box::new(mesh.subset(faces))
}

fn mesh_geometry_frame(vertices: &[f32], indices: &[u32]) -> MeshGeometryFrame {
    crate::MeshGeometry::mesh_frame(vertices, indices)
}

fn mesh_geometry_coloured_frame(
    vertices: &[f32],
    indices: &[u32],
    colours: &[f32],
) -> MeshGeometryFrame {
    crate::MeshGeometry::coloured_mesh_frame(vertices, indices, colours)
}

fn mesh_geometry_ring_frame(
    centre: [f32; 3],
    normal: [f32; 3],
    radius: f32,
    thickness: f32,
) -> MeshGeometryFrame {
    crate::MeshGeometry::ring_frame(centre, normal, radius, thickness)
}

fn mesh_geometry_raycast(positions: &[f32], origin: [f32; 3], direction: [f32; 3]) -> [f32; 4] {
    crate::MeshGeometry::raycast(positions, origin, direction)
}

fn palette_json() -> String {
    crate::Palette::palette_json()
}

fn update_check(channel: &str) -> String {
    crate::Updater::check_manifest(channel).to_json()
}

fn update_download(url: &str, sha256: &str, version: &str, root: &str) -> String {
    match crate::Updater::download_artifact(url, sha256, version, root) {
        Ok(staged) => serde_json::json!({"ok": true, "error": "", "staged": staged}).to_string(),
        Err(error) => serde_json::json!({"ok": false, "error": error, "staged": ""}).to_string(),
    }
}

fn update_install(staged: &str, root: &str) -> String {
    match crate::Updater::install_artifact(staged, root) {
        Ok(()) => serde_json::json!({"ok": true, "error": ""}).to_string(),
        Err(error) => serde_json::json!({"ok": false, "error": error}).to_string(),
    }
}

fn update_install_root() -> String {
    crate::Updater::install_root()
}

fn update_current_version() -> String {
    crate::Updater::current_version()
}

fn resources_dir() -> String {
    crate::Resources::resources_dir()
}

fn application_version() -> String {
    crate::GcodeBrand::application_version()
}

fn brand_gcode(path: &str, configuration_json: &str) -> i32 {
    crate::GcodeBrand::brand_gcode(path, configuration_json)
}

fn toolpath_digest(path: &str) -> String {
    crate::GcodeBrand::toolpath_digest(path)
}

fn scratch_template() -> String {
    crate::Scratch::scratch_template()
}

fn sweep_scratch_dirs() {
    crate::Scratch::sweep_scratch_dirs();
}

fn thumbnail_edges() -> Vec<u32> {
    crate::PlateThumbnail::edges()
}

fn embed_plate_thumbnails(
    path: &str,
    png_primary: &CxxString,
    png_secondary: &CxxString,
) -> String {
    crate::PlateThumbnail::embed_plate_thumbnails(
        path,
        png_primary.as_bytes(),
        png_secondary.as_bytes(),
    )
}

fn foreign_setting_pairs() -> Vec<String> {
    crate::ForeignSettings::foreign_setting_pairs()
}

fn translate_foreign_setting(key: &str, value: &str) -> Vec<String> {
    flatten(crate::ForeignSettings::translate_foreign_setting(key, value))
}

fn foreign_target_for(source: &str) -> String {
    crate::ForeignSettings::foreign_target_for(source)
}

fn canonical_foreign_source(engine_key: &str) -> String {
    crate::ForeignSettings::canonical_foreign_source(engine_key)
}

fn foreign_name_for(engine_key: &str, engine_value: &str) -> Vec<String> {
    flatten(crate::ForeignSettings::foreign_name_for(engine_key, engine_value))
}

fn serialise_project(
    objects: &[ProjectObjectRecord],
    metadata: &ProjectMetadataRecord,
) -> Vec<u8> {
    crate::ProjectFile::serialise_project(objects, metadata)
}

fn parse_project(body: &[u8]) -> ProjectOutcome {
    crate::ProjectFile::parse_project(body)
}

fn project_format_version() -> i32 {
    crate::ProjectFile::format_version()
}

fn paint_to_text(paint: &ProjectPaint) -> Vec<u8> {
    crate::ProjectFile::paint_to_text(paint)
}

fn paint_from_text(text: &[u8]) -> ProjectPaintOutcome {
    crate::ProjectFile::paint_from_text(text)
}

fn read_plate_snapshot(plate: &EnginePlate) -> PlateOutcome {
    crate::HfwEngine::read_plate_snapshot(plate)
}

fn read_plate_document(bytes: &[u8]) -> PlateOutcome {
    crate::HfwEngine::read_plate_document(bytes)
}

fn save_plate_document(plate: &EnginePlate, metadata: &[u8], path: &str) -> EngineOutcome {
    crate::HfwEngine::save_plate_document(plate, metadata, path)
}

fn round_trip_plate(plate: &EnginePlate) -> EngineOutcome {
    crate::HfwEngine::round_trip_plate(plate)
}

fn slice_with_hfw(plate: &EnginePlate, config_path: &str, gcode_path: &str) -> EngineOutcome {
    crate::HfwEngine::slice_with_hfw(plate, config_path, gcode_path)
}

fn read_three_mf_project(path: &str) -> ThreeMfFileOutcome {
    crate::ThreeMfProject::read_three_mf_project(path)
}

fn is_pre_sliced_file(path: &str) -> bool {
    crate::ThreeMfProject::is_pre_sliced_file(path)
}

fn extract_pre_sliced_gcode(path: &str, out_path: &str) -> bool {
    crate::ThreeMfProject::extract_pre_sliced_gcode(path, out_path)
}

fn flatten(pair: Option<(String, String)>) -> Vec<String> {
    match pair {
        Some((first, second)) => vec![first, second],
        None => Vec::new(),
    }
}

fn yaml_quoted(text: &str) -> String {
    crate::PresetYaml::quoted(text)
}

fn yaml_number(value: f64) -> String {
    crate::PresetYaml::number(value)
}

fn yaml_unquoted(text: &str) -> String {
    crate::PresetYaml::unquoted(text)
}

fn yaml_point(x: f64, y: f64) -> String {
    crate::PresetYaml::point(x, y)
}

fn presets_root() -> String {
    crate::PresetFiles::root()
}

fn presets_custom_vendor_directory() -> String {
    crate::PresetFiles::custom_vendor_directory()
}

fn presets_filament_library_plan(directory: &str, library_names: &str) -> Vec<String> {
    crate::PresetFiles::filament_library_plan(directory, library_names)
}

fn presets_slug_of(name: &str) -> String {
    crate::PresetFiles::slug_of(name)
}

fn presets_fresh_id() -> String {
    crate::PresetFiles::fresh_id()
}

fn presets_write_file(path: &str, contents: &str) -> bool {
    crate::PresetFiles::write_file(path, contents)
}

fn presets_document(
    kind: &str,
    name: &str,
    parent: &str,
    condition: &str,
    values_yaml: &str,
    id: &str,
) -> String {
    crate::PresetFiles::document(kind, name, parent, condition, values_yaml, id)
}

fn presets_custom_printers() -> Vec<PresetPrinterRecord> {
    crate::PresetFiles::custom_printers()
        .into_iter()
        .map(|printer| PresetPrinterRecord {
            name: printer.name,
            slug: printer.slug,
            tools: printer.tools,
        })
        .collect()
}

fn presets_add_custom_printer(name: &str, tools: i32) -> String {
    crate::PresetFiles::add_custom_printer(name, tools)
}

fn presets_custom_printer_path(name: &str) -> String {
    crate::PresetFiles::custom_printer_path(name)
}

fn presets_ensure_tool_print_preset() -> String {
    crate::PresetFiles::ensure_tool_print_preset()
}

fn settings_model_new(
    definitions: &[SettingDefinitionRecord],
    layout_json: &str,
    base_config_json: &str,
) -> Box<SettingsModelCore> {
    crate::SettingsModel::new(definitions, layout_json, base_config_json)
}

fn settings_tab_list(model: &SettingsModelCore) -> Vec<SettingTabRecord> {
    crate::SettingsModel::tab_list(model)
}

fn settings_groups(
    model: &SettingsModelCore,
    tab: &str,
    subtab: &str,
    filter: &str,
) -> Vec<SettingGroupRecord> {
    crate::SettingsModel::groups(model, tab, subtab, filter)
}

fn settings_value(model: &SettingsModelCore, name: &str) -> SettingValueRecord {
    crate::SettingsModel::value(model, name)
}

fn settings_block_of(model: &SettingsModelCore, name: &str) -> String {
    crate::SettingsModel::block_of(model, name)
}

fn settings_can_hold_in(model: &SettingsModelCore, name: &str, block: &str) -> bool {
    crate::SettingsModel::can_hold_in(model, name, block)
}

fn settings_points(model: &SettingsModelCore, name: &str) -> Vec<f64> {
    crate::SettingsModel::points(model, name)
}

fn settings_set_value(
    model: &mut SettingsModelCore,
    name: &str,
    input: &SettingInputRecord,
) -> SettingChangeRecord {
    crate::SettingsModel::set_value(model, name, input)
}

fn settings_reset(model: &mut SettingsModelCore, name: &str) -> bool {
    crate::SettingsModel::reset(model, name)
}

fn settings_seed_from_config_json(model: &mut SettingsModelCore, json: &str) -> bool {
    crate::SettingsModel::seed_from_config_json(model, json)
}

fn settings_reset_subtab(model: &mut SettingsModelCore, tab: &str, subtab: &str) -> bool {
    crate::SettingsModel::reset_subtab(model, tab, subtab)
}

fn settings_reset_tab(model: &mut SettingsModelCore, tab: &str) -> bool {
    crate::SettingsModel::reset_tab(model, tab)
}

fn settings_modified_count(model: &SettingsModelCore, tab: &str) -> i32 {
    crate::SettingsModel::modified_count(model, tab)
}

fn settings_modified_values(model: &SettingsModelCore, tab: &str) -> Vec<SettingModifiedRecord> {
    crate::SettingsModel::modified_values(model, tab)
}

fn settings_object_defs(model: &SettingsModelCore, filter: &str) -> Vec<SettingItemRecord> {
    crate::SettingsModel::object_setting_defs(model, filter)
}

fn settings_tab_dump(model: &SettingsModelCore) -> String {
    crate::SettingsModel::tab_dump(model)
}

fn settings_set_unsupported(model: &mut SettingsModelCore, names: Vec<String>) {
    crate::SettingsModel::set_unsupported(model, &names)
}

fn settings_unsupported(model: &SettingsModelCore) -> Vec<String> {
    crate::SettingsModel::unsupported(model)
}

fn settings_json_dump(model: &SettingsModelCore) -> String {
    crate::SettingsModel::settings_json(model)
}

fn settings_name_dump(model: &SettingsModelCore) -> String {
    crate::SettingsModel::name_dump(model)
}

fn settings_has_resolved_config(model: &SettingsModelCore) -> bool {
    crate::SettingsModel::has_resolved_config(model)
}

fn settings_layout_loaded(model: &SettingsModelCore) -> bool {
    crate::SettingsModel::layout_loaded(model)
}

fn settings_config_json(model: &SettingsModelCore) -> String {
    crate::SettingsModel::config_json(model)
}

fn settings_values_yaml(model: &SettingsModelCore, tab: &str, changed_only: bool) -> String {
    crate::SettingsModel::values_yaml(model, tab, changed_only)
}

fn settings_clear_modified(model: &mut SettingsModelCore) -> bool {
    crate::SettingsModel::clear_modified(model)
}
