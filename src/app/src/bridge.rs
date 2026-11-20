//! The functions the C++ that remains calls into Rust.
//!
//! Every ported file keeps its C++ callers working through this one
//! `#[cxx::bridge]`, whose generated header is `next-layer-app/src/bridge.rs.h`.
//! It is the reverse of `hfw`: that boundary is Rust asking a C library, this one
//! is C++ asking this crate, and both disappear as `src/qt/src` empties.

pub use crate::mesh::Mesh;

pub use ffi::{
    ProjectMarkerRecord, ProjectMetadataRecord, ProjectObjectRecord, ProjectOutcome, ProjectPaint,
    ProjectPaintOutcome, ProjectSetting, ProjectVolumeRecord,
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

    unsafe extern "C++" {
        include!("next-layer-app/include/next-layer-qt.hpp");

        /// Leaves Qt's event loop, so the registered teardown runs.
        fn quit_application();
    }

    extern "Rust" {
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

        /// One paint channel as text.
        fn paint_to_text(paint: &ProjectPaint) -> Vec<u8>;

        /// One paint channel read back from text.
        fn paint_from_text(text: &[u8]) -> ProjectPaintOutcome;
    }
}

pub fn quit_application() {
    ffi::quit_application();
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

fn flatten(pair: Option<(String, String)>) -> Vec<String> {
    match pair {
        Some((first, second)) => vec![first, second],
        None => Vec::new(),
    }
}
