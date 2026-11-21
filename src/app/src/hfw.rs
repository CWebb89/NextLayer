//! The engine boundary: HFW's C ABI, declared in one place.
//!
//! `crates/capi/include/hfw.h` in the HFW repository is the contract; this module
//! is its Rust spelling, with the handles wrapped so a caller cannot leak one. The
//! library is found at build time by `build.rs`, which links `libhfw_capi.a` from
//! `NEXT_LAYER_HFW_LIB` or from the development build tree beside this repository.

use std::ffi::{CStr, CString};
use std::os::unix::ffi::OsStrExt;
use std::path::Path;

/// The ABI version this module was written against.
pub const ABI_VERSION: u32 = 1;

/// Paint channels the engine carries. Tool paint is not one of them: HFW drives one
/// extruder, so a third channel would change nothing the engine reads.
pub const PAINT_SUPPORT: i32 = 0;
pub const PAINT_SEAM: i32 = 1;

/// What one triangle of a paint channel can be.
pub const PAINT_NONE: u8 = 0;
pub const PAINT_ENFORCER: u8 = 1;
pub const PAINT_BLOCKER: u8 = 2;

/// Timeline marker kinds, as the plate stores them.
pub const MARKER_CUSTOM: i32 = 0;
pub const MARKER_PAUSE: i32 = 1;
pub const MARKER_COLOUR: i32 = 2;

#[repr(C)]
struct HfwMesh {
    _private: [u8; 0],
}

#[repr(C)]
struct HfwPlate {
    _private: [u8; 0],
}

#[repr(C)]
struct HfwGcode {
    _private: [u8; 0],
}

#[repr(C)]
struct HfwSnapshot {
    _private: [u8; 0],
}

/// Axis-aligned bounds of a mesh or an object, in millimetres.
#[repr(C)]
#[derive(Debug, Clone, Copy, Default, PartialEq)]
pub struct Bounds {
    pub min: [f64; 3],
    pub max: [f64; 3],
}

/// A placement applied to a mesh.
#[repr(C)]
#[derive(Debug, Clone, Copy, PartialEq)]
pub struct Transform {
    pub scale: [f64; 3],
    pub rotation_degrees: [f64; 3],
    pub translation_mm: [f64; 3],
}

impl Default for Transform {
    fn default() -> Self {
        Transform {
            scale: [1.0; 3],
            rotation_degrees: [0.0; 3],
            translation_mm: [0.0; 3],
        }
    }
}

// The whole exported surface, declared here as the modules that call it arrive.
// The declarations with no Rust caller yet are the ones `HfwEngine` and
// `SliceRuntime` will use when they port, so unused is the expected state rather
// than a defect; `every_declared_symbol_resolves_in_the_linked_engine` keeps each
// one honest against the library that is actually linked.
#[allow(dead_code)]
extern "C" {
    fn hfw_abi_version() -> u32;
    fn hfw_last_error() -> *const std::os::raw::c_char;

    fn hfw_mesh_load_file(path: *const std::os::raw::c_char) -> *mut HfwMesh;
    fn hfw_mesh_from_bytes(bytes: *const u8, len: usize) -> *mut HfwMesh;
    fn hfw_mesh_transform(mesh: *const HfwMesh, transform: Transform) -> *mut HfwMesh;
    fn hfw_mesh_bounds(mesh: *const HfwMesh, out: *mut Bounds) -> i32;
    fn hfw_mesh_triangle_count(mesh: *const HfwMesh) -> usize;
    fn hfw_mesh_vertex_count(mesh: *const HfwMesh) -> usize;
    fn hfw_mesh_vertices(mesh: *const HfwMesh, out: *mut f64, count: usize) -> i32;
    fn hfw_mesh_index_count(mesh: *const HfwMesh) -> usize;
    fn hfw_mesh_indices(mesh: *const HfwMesh, out: *mut u32, count: usize) -> i32;
    fn hfw_mesh_volume(mesh: *const HfwMesh) -> f64;
    fn hfw_mesh_free(mesh: *mut HfwMesh);

    fn hfw_plate_new() -> *mut HfwPlate;
    fn hfw_plate_add(plate: *mut HfwPlate, mesh: *const HfwMesh) -> u64;
    fn hfw_plate_add_positioned(plate: *mut HfwPlate, mesh: *const HfwMesh) -> u64;
    fn hfw_plate_object_count(plate: *const HfwPlate) -> usize;
    fn hfw_plate_object_placement(
        plate: *mut HfwPlate,
        id: u64,
        transform: Transform,
    ) -> i32;
    fn hfw_plate_object_enabled(plate: *mut HfwPlate, id: u64, enabled: i32) -> i32;
    fn hfw_plate_object_remove(plate: *mut HfwPlate, id: u64) -> i32;
    fn hfw_plate_arrange(plate: *mut HfwPlate, bed_x: f64, bed_y: f64, spacing: f64) -> i32;
    fn hfw_plate_bounds(plate: *const HfwPlate, out: *mut Bounds) -> i32;
    fn hfw_plate_slice(
        plate: *const HfwPlate,
        profile: *const std::os::raw::c_char,
        first_layer_height: f64,
        layer_height: f64,
    ) -> *mut HfwGcode;
    fn hfw_plate_free(plate: *mut HfwPlate);

    /// The plate as it is, sharing each object's geometry until it is written to,
    /// so an undo step costs a handle rather than a copy of every mesh.
    fn hfw_plate_snapshot(plate: *const HfwPlate) -> *mut HfwSnapshot;
    /// Puts the snapshot's contents back into a plate. The snapshot is not
    /// consumed: the same one restores again, which is what redo is.
    fn hfw_plate_restore(plate: *mut HfwPlate, snapshot: *const HfwSnapshot) -> i32;
    fn hfw_snapshot_free(snapshot: *mut HfwSnapshot);

    fn hfw_gcode_text(gcode: *const HfwGcode) -> *const std::os::raw::c_char;
    fn hfw_gcode_len(gcode: *const HfwGcode) -> usize;
    fn hfw_gcode_verified(gcode: *const HfwGcode) -> i32;
    fn hfw_gcode_free(gcode: *mut HfwGcode);

    /// The mesh as binary (non-zero) or ASCII (zero) STL bytes, `*out_len` long, to
    /// be released with `hfw_buffer_free`.
    fn hfw_mesh_stl_bytes(
        mesh: *const HfwMesh,
        binary: i32,
        out_len: *mut usize,
    ) -> *mut std::os::raw::c_char;

    /// A mesh from millimetre coordinates and triangle indices: nothing welded and
    /// no triangle dropped, so triangle `i` of the handle is triangle `i` of
    /// `indices` and per-triangle data stays aligned with it.
    fn hfw_mesh_new(
        vertices: *const f64,
        vertex_count: usize,
        indices: *const u32,
        triangle_count: usize,
    ) -> *mut HfwMesh;

    /// A mesh split by the plane through `point` facing `normal`, into the part
    /// below and the part above, both closed. A cut that misses the mesh puts all of
    /// it in one half.
    fn hfw_mesh_cut(
        mesh: *const HfwMesh,
        normal: *const f64,
        point: *const f64,
        below: *mut *mut HfwMesh,
        above: *mut *mut HfwMesh,
    ) -> i32;

    /// Writes one paint channel on a mesh handle: `len` bytes, one per triangle in
    /// the mesh's own order, each 0 none, 1 enforcer or 2 blocker. A `len` of zero
    /// clears the channel; a `len` that is not the triangle count is refused, never
    /// truncated.
    fn hfw_mesh_paint_set(
        mesh: *mut HfwMesh,
        channel: i32,
        states: *const u8,
        len: usize,
    ) -> i32;

    /// Reads one channel back into `out`: the number of bytes written, 0 when the
    /// channel was never painted, -1 when `len` is smaller than the triangle count.
    fn hfw_mesh_paint_get(
        mesh: *const HfwMesh,
        channel: i32,
        out: *mut u8,
        len: usize,
    ) -> isize;

    /// Moves object `id` from a row-major 4x4 matrix, sixteen doubles.
    ///
    /// The matrix composes as this engine does, `T * Rz * Ry * Rx * S`. Anything
    /// else -- a shear, a collapsed axis, a perspective row -- is refused rather
    /// than truncated, because the plate records a scale triple and an Euler triple.
    /// Returns -1 when the object is not on the plate or the matrix is not
    /// representable.
    fn hfw_plate_object_placement_matrix(
        plate: *mut HfwPlate,
        id: u64,
        matrix: *const f64,
    ) -> i32;

    /// Reads object `id`'s placement back as sixteen doubles, row-major.
    fn hfw_plate_object_placement_matrix_get(
        plate: *const HfwPlate,
        id: u64,
        out: *mut f64,
    ) -> i32;

    /// Writes one paint channel on object `id`, one byte per triangle of its
    /// geometry. Placement, rotation and baking preserve triangle order, so states
    /// stay valid across them.
    fn hfw_plate_object_paint(
        plate: *mut HfwPlate,
        id: u64,
        channel: i32,
        states: *const u8,
        len: usize,
    ) -> i32;

    /// Reads one channel back off object `id`: bytes written, 0 when it was never
    /// painted, -1 when `len` is smaller than the triangle count.
    fn hfw_plate_object_paint_get(
        plate: *const HfwPlate,
        id: u64,
        channel: i32,
        out: *mut u8,
        len: usize,
    ) -> isize;

    /// Object `id`'s own layer heights, as `count` interleaved `(z, height)` pairs.
    ///
    /// A count of zero means the plate's uniform layer height rather than one pair
    /// equal to it. A layer whose bottom sits at or above a `z` uses that height
    /// until the next pair takes over.
    fn hfw_plate_object_layer_heights(
        plate: *mut HfwPlate,
        id: u64,
        pairs: *const f64,
        count: usize,
    ) -> i32;

    /// The same pairs back: how many there are, 0 for a uniform object, -1 when the
    /// object is not on the plate. A null `out` or a short `len` reports the count
    /// and writes nothing, so one call sizes the buffer another fills.
    fn hfw_plate_object_layer_heights_get(
        plate: *const HfwPlate,
        id: u64,
        out: *mut f64,
        len: usize,
    ) -> isize;

    /// Object `id`'s overrides as the rows a setter accepts, to be released with
    /// `hfw_string_free`.
    fn hfw_plate_object_settings_json(
        plate: *const HfwPlate,
        id: u64,
    ) -> *mut std::os::raw::c_char;

    /// A volume's own settings as the same rows, to be released with
    /// `hfw_string_free`.
    fn hfw_plate_object_volume_settings_json(
        plate: *const HfwPlate,
        id: u64,
        volume: u64,
    ) -> *mut std::os::raw::c_char;

    /// The plate's in-layer strokes: beads printed inside a layer that the model does
    /// not describe, which is the calibration channel. `count` strokes are described
    /// by parallel arrays -- print Z, point count, width, height, speed, where each
    /// stroke starts in `points`, the interleaved x/y plate coordinates, and the
    /// G-code written before each stroke. A count of zero clears them.
    fn hfw_plate_set_in_layer(
        plate: *mut HfwPlate,
        zs: *const f64,
        counts: *const usize,
        widths: *const f64,
        heights: *const f64,
        speeds: *const f64,
        starts: *const usize,
        points: *const f64,
        prefixes: *const *const std::os::raw::c_char,
        count: usize,
    ) -> i32;

    /// How many in-layer strokes the plate carries, or -1 for a null handle.
    fn hfw_plate_in_layer_count(plate: *const HfwPlate) -> isize;

    /// How many points stroke `index` has; this sizes the buffer the reader fills.
    fn hfw_plate_in_layer_points(plate: *const HfwPlate, index: usize) -> isize;

    /// Reads stroke `index` back with its width, height, speed and G-code prefix.
    fn hfw_plate_in_layer_get(
        plate: *const HfwPlate,
        index: usize,
        z: *mut f64,
        width: *mut f64,
        height: *mut f64,
        speed: *mut f64,
        points: *mut f64,
        capacity: usize,
        prefix: *mut std::os::raw::c_char,
        len: usize,
    ) -> i32;

    /// The plate's timeline markers, sorted by Z: one kind per marker, its print
    /// height, and its G-code as an array of C strings. A count of zero clears the
    /// timeline; a custom marker with no G-code is refused.
    fn hfw_plate_set_markers(
        plate: *mut HfwPlate,
        kinds: *const i32,
        zs: *const f64,
        texts: *const *const std::os::raw::c_char,
        count: usize,
    ) -> i32;

    /// How many markers the plate carries, or -1 for a null handle.
    fn hfw_plate_marker_count(plate: *const HfwPlate) -> isize;

    /// Reads marker `index` back: its kind, its Z and its G-code, truncated to `len`.
    fn hfw_plate_marker_get(
        plate: *const HfwPlate,
        index: usize,
        kind: *mut i32,
        z: *mut f64,
        text: *mut std::os::raw::c_char,
        len: usize,
    ) -> i32;

    /// The application's own bytes, saved and reopened with the plate. The engine
    /// never reads them; a length of zero clears them.
    fn hfw_plate_set_metadata(
        plate: *mut HfwPlate,
        bytes: *const std::os::raw::c_char,
        len: usize,
    ) -> i32;

    /// Those bytes, NUL-terminated with the real length in `out_len` so binary
    /// metadata survives, to be released with `hfw_buffer_free`.
    fn hfw_plate_metadata_get(plate: *const HfwPlate, out_len: *mut usize)
        -> *mut std::os::raw::c_char;

    /// Names object `id`.
    fn hfw_plate_object_name(
        plate: *mut HfwPlate,
        id: u64,
        name: *const std::os::raw::c_char,
    ) -> i32;

    /// Writes object `id`'s name into `out` and returns the bytes it needs excluding
    /// the terminator: a buffer that is too short writes nothing and reports that
    /// length, so one call sizes the next.
    fn hfw_plate_object_name_get(
        plate: *const HfwPlate,
        id: u64,
        out: *mut std::os::raw::c_char,
        len: usize,
    ) -> isize;

    /// Sets one override on object `id`, by name and value as a settings panel states
    /// it. The object takes the plate's settings on its first override.
    fn hfw_plate_object_set(
        plate: *mut HfwPlate,
        id: u64,
        key: *const std::os::raw::c_char,
        value: *const std::os::raw::c_char,
    ) -> i32;

    /// The ids of the plate's objects, in the order they were added: how many the
    /// plate carries, reported even for a null or short buffer.
    fn hfw_plate_object_ids(plate: *const HfwPlate, out: *mut u64, len: usize) -> usize;

    /// Writes object `id`'s enabled flag into `out`, 1 or 0.
    fn hfw_plate_object_enabled_get(
        plate: *const HfwPlate,
        id: u64,
        out: *mut i32,
    ) -> i32;

    /// A copy of object `id`'s geometry, carrying its paint, to be released with
    /// `hfw_mesh_free`.
    fn hfw_plate_object_mesh(plate: *const HfwPlate, id: u64) -> *mut HfwMesh;

    /// Attaches a copy of `mesh` to object `id` as `kind` and returns the volume's
    /// id, which is never zero and never reused.
    fn hfw_plate_object_add_volume(
        plate: *mut HfwPlate,
        id: u64,
        kind: i32,
        mesh: *const HfwMesh,
    ) -> u64;

    /// How many volumes object `id` carries, or -1 when it is not on the plate.
    fn hfw_plate_object_volume_count(plate: *const HfwPlate, id: u64) -> isize;

    /// The ids of object `id`'s volumes, in the order they were added: how many it
    /// carries, reported even for a null or short buffer.
    fn hfw_plate_object_volume_ids(
        plate: *const HfwPlate,
        id: u64,
        out: *mut u64,
        len: usize,
    ) -> usize;

    /// Writes which kind of volume `volume` of object `id` is into `out`.
    fn hfw_plate_object_volume_kind(
        plate: *const HfwPlate,
        id: u64,
        volume: u64,
        out: *mut i32,
    ) -> i32;

    /// A copy of volume `volume`'s geometry, in the object's own frame, to be
    /// released with `hfw_mesh_free`.
    fn hfw_plate_object_volume_mesh(
        plate: *const HfwPlate,
        id: u64,
        volume: u64,
    ) -> *mut HfwMesh;

    /// Sets one setting on modifier volume `volume` of object `id`.
    fn hfw_plate_volume_set(
        plate: *mut HfwPlate,
        id: u64,
        volume: u64,
        key: *const std::os::raw::c_char,
        value: *const std::os::raw::c_char,
    ) -> i32;

    /// The plate as a project file: `.3mf` bytes with the application's own state
    /// beside them, to be released with `hfw_buffer_free`.
    fn hfw_plate_project_bytes(plate: *const HfwPlate, out_len: *mut usize)
        -> *mut std::os::raw::c_char;

    /// A plate opened from project-file bytes, with `profile` as its settings layer
    /// or null for the engine's defaults.
    fn hfw_plate_open_project(
        bytes: *const std::os::raw::c_char,
        len: usize,
        profile: *const std::os::raw::c_char,
    ) -> *mut HfwPlate;

    /// The same as `hfw_plate_slice`, with the plate laid on the bed the profile
    /// describes rather than printed where it sits.
    fn hfw_plate_slice_placed(
        plate: *const HfwPlate,
        profile: *const std::os::raw::c_char,
        first_layer_height: f64,
        layer_height: f64,
    ) -> *mut HfwGcode;

    /// Releases a string this library returned.
    fn hfw_string_free(text: *mut std::os::raw::c_char);

    /// Releases a byte buffer this library returned, with the length it reported.
    fn hfw_buffer_free(bytes: *mut std::os::raw::c_char, len: usize);
}

/// The message this thread's last failed call left behind.
pub fn last_error() -> String {
    let message = unsafe { hfw_last_error() };
    if message.is_null() {
        return String::from("the engine reported no reason");
    }
    unsafe { CStr::from_ptr(message) }.to_string_lossy().into_owned()
}

/// The version of the surface the linked library exports.
pub fn abi_version() -> u32 {
    unsafe { hfw_abi_version() }
}

fn path_argument(path: &Path) -> Result<CString, String> {
    CString::new(path.as_os_str().as_bytes())
        .map_err(|_| format!("{} holds a NUL byte", path.display()))
}

fn text_argument(text: &str) -> Result<CString, String> {
    CString::new(text).map_err(|_| format!("`{text}` holds a NUL byte"))
}

/// How much room a marker's G-code is read into. The ABI truncates to fit, so a text
/// that fills the buffer is refused rather than taken as shorter than it was.
const MARKER_TEXT_CAPACITY: usize = 512;

/// The same for an in-layer stroke's G-code prefix.
const STROKE_PREFIX_CAPACITY: usize = 256;

/// A mesh held by the engine.
pub struct Mesh(*mut HfwMesh);

impl Mesh {
    /// Reads an STL or 3MF file.
    pub fn from_file(path: &Path) -> Result<Mesh, String> {
        let argument = path_argument(path)?;
        let mesh = unsafe { hfw_mesh_load_file(argument.as_ptr()) };
        if mesh.is_null() {
            return Err(format!("{}: {}", path.display(), last_error()));
        }
        Ok(Mesh(mesh))
    }

    /// Builds a mesh from the bytes of an STL file.
    pub fn from_bytes(bytes: &[u8]) -> Result<Mesh, String> {
        let mesh = unsafe { hfw_mesh_from_bytes(bytes.as_ptr(), bytes.len()) };
        if mesh.is_null() {
            return Err(last_error());
        }
        Ok(Mesh(mesh))
    }

    /// Builds a mesh from millimetre coordinates and triangle numbers, keeping the
    /// caller's triangle order so per-triangle data stays addressed to its facets.
    pub fn from_arrays(vertices: &[f64], indices: &[u32]) -> Result<Mesh, String> {
        if !vertices.len().is_multiple_of(3) || !indices.len().is_multiple_of(3) {
            return Err(String::from("the geometry is not whole triangles"));
        }
        let mesh = unsafe {
            hfw_mesh_new(
                vertices.as_ptr(),
                vertices.len() / 3,
                indices.as_ptr(),
                indices.len() / 3,
            )
        };
        if mesh.is_null() {
            return Err(last_error());
        }
        Ok(Mesh(mesh))
    }

    /// This mesh moved by `transform`, leaving this one untouched.
    pub fn transformed(&self, transform: Transform) -> Result<Mesh, String> {
        let mesh = unsafe { hfw_mesh_transform(self.0, transform) };
        if mesh.is_null() {
            return Err(last_error());
        }
        Ok(Mesh(mesh))
    }

    /// Bounds in millimetres, or `None` when the mesh has no area or no height.
    pub fn bounds(&self) -> Option<Bounds> {
        let mut out = Bounds::default();
        let result = unsafe { hfw_mesh_bounds(self.0, &mut out) };
        (result == 0).then_some(out)
    }

    /// Enclosed volume in cubic millimetres.
    pub fn volume(&self) -> f64 {
        unsafe { hfw_mesh_volume(self.0) }
    }

    /// How many triangles the mesh holds.
    pub fn triangles(&self) -> usize {
        unsafe { hfw_mesh_triangle_count(self.0) }
    }

    /// How many vertices the mesh holds.
    pub fn vertex_count(&self) -> usize {
        unsafe { hfw_mesh_vertex_count(self.0) }
    }

    /// The xyz triples, three per vertex.
    pub fn vertices(&self) -> Result<Vec<f32>, String> {
        let count = self.vertex_count();
        let mut flat = vec![0.0f64; count * 3];
        if count == 0 {
            return Ok(Vec::new());
        }
        let read = unsafe { hfw_mesh_vertices(self.0, flat.as_mut_ptr(), count) };
        if read != 0 {
            return Err(last_error());
        }
        Ok(flat.into_iter().map(|value| value as f32).collect())
    }

    /// The same triples in the engine's own millimetres, without a second rounding
    /// through `f32`: a vertex read this way is the one the document saves back.
    pub fn vertices_mm(&self) -> Result<Vec<f64>, String> {
        let count = self.vertex_count();
        let mut flat = vec![0.0f64; count * 3];
        if count == 0 {
            return Ok(Vec::new());
        }
        let read = unsafe { hfw_mesh_vertices(self.0, flat.as_mut_ptr(), count) };
        if read != 0 {
            return Err(last_error());
        }
        Ok(flat)
    }

    /// How many triangles the mesh holds, as counted by the index array.
    pub fn index_count(&self) -> usize {
        unsafe { hfw_mesh_index_count(self.0) }
    }

    /// The vertex triples, three per triangle.
    pub fn indices(&self) -> Result<Vec<u32>, String> {
        let count = self.index_count();
        let mut flat = vec![0u32; count * 3];
        if count == 0 {
            return Ok(Vec::new());
        }
        let read = unsafe { hfw_mesh_indices(self.0, flat.as_mut_ptr(), count) };
        if read != 0 {
            return Err(last_error());
        }
        Ok(flat)
    }
}

impl Drop for Mesh {
    fn drop(&mut self) {
        unsafe { hfw_mesh_free(self.0) }
    }
}

/// One marker on the plate's timeline, as the engine hands it back.
pub struct Marker {
    pub kind: i32,
    pub z: f64,
    pub text: String,
}

/// One bead printed inside a layer, as the plate stores it: interleaved x, y plate
/// coordinates and the G-code written before it.
pub struct InLayerStroke {
    pub z: f64,
    pub points: Vec<f64>,
    pub width: f64,
    pub height: f64,
    pub speed: f64,
    pub prefix: String,
}

/// The plate document the engine slices.
pub struct Plate(*mut HfwPlate);

impl Default for Plate {
    fn default() -> Self {
        Plate::new()
    }
}

impl Plate {
    /// An empty plate with the engine's default settings.
    pub fn new() -> Plate {
        Plate(unsafe { hfw_plate_new() })
    }

    /// An empty plate, or the reason the engine would not make one. Every path that
    /// has to report a failure uses this rather than `new`, which cannot.
    pub fn create() -> Result<Plate, String> {
        let plate = unsafe { hfw_plate_new() };
        if plate.is_null() {
            return Err(last_error());
        }
        Ok(Plate(plate))
    }

    /// Adds a copy of `mesh`, for a caller that wants the engine to place it.
    pub fn add(&mut self, mesh: &Mesh) -> Result<u64, String> {
        let id = unsafe { hfw_plate_add(self.0, mesh.0) };
        if id == 0 {
            return Err(last_error());
        }
        Ok(id)
    }

    /// Adds a copy of `mesh` where its own coordinates put it.
    pub fn add_positioned(&mut self, mesh: &Mesh) -> Result<u64, String> {
        let id = unsafe { hfw_plate_add_positioned(self.0, mesh.0) };
        if id == 0 {
            return Err(last_error());
        }
        Ok(id)
    }

    /// How many objects the plate holds, enabled or not.
    pub fn len(&self) -> usize {
        unsafe { hfw_plate_object_count(self.0) }
    }

    /// True when the plate holds no objects.
    pub fn is_empty(&self) -> bool {
        self.len() == 0
    }

    /// Moves an object without touching its triangles.
    pub fn place(&mut self, id: u64, transform: Transform) -> Result<(), String> {
        let result = unsafe { hfw_plate_object_placement(self.0, id, transform) };
        if result != 0 {
            return Err(last_error());
        }
        Ok(())
    }

    /// Includes an object in the job, or leaves it out.
    pub fn set_enabled(&mut self, id: u64, enabled: bool) -> Result<(), String> {
        let result = unsafe { hfw_plate_object_enabled(self.0, id, enabled as i32) };
        if result != 0 {
            return Err(last_error());
        }
        Ok(())
    }

    /// Removes an object; every other object keeps its id.
    pub fn remove(&mut self, id: u64) -> Result<(), String> {
        let result = unsafe { hfw_plate_object_remove(self.0, id) };
        if result != 0 {
            return Err(last_error());
        }
        Ok(())
    }

    /// Lays the plate out on a bed, leaving `spacing` millimetres between objects.
    pub fn arrange(&mut self, bed_x: f64, bed_y: f64, spacing: f64) -> Result<(), String> {
        let result = unsafe { hfw_plate_arrange(self.0, bed_x, bed_y, spacing) };
        if result != 0 {
            return Err(last_error());
        }
        Ok(())
    }

    /// Bounds of every object on the plate, or `None` when it holds no geometry.
    pub fn bounds(&self) -> Option<Bounds> {
        let mut out = Bounds::default();
        let result = unsafe { hfw_plate_bounds(self.0, &mut out) };
        (result == 0).then_some(out)
    }

    /// Slices the plate under `profile`, which is the settings document this
    /// application dumps or a preset another slicer exported.
    ///
    /// A layer height of zero takes the profile's own.
    pub fn slice(
        &self,
        profile: &Path,
        first_layer_height: f64,
        layer_height: f64,
    ) -> Result<Gcode, String> {
        let argument = path_argument(profile)?;
        let gcode = unsafe {
            hfw_plate_slice(self.0, argument.as_ptr(), first_layer_height, layer_height)
        };
        if gcode.is_null() {
            return Err(last_error());
        }
        Ok(Gcode(gcode))
    }

    /// Slices the plate under `profile`, laying it on the bed the profile describes
    /// first: what a bare model named on the command line needs, and what a plate the
    /// window laid out must not have done to it.
    pub fn slice_placed(
        &self,
        profile: &Path,
        first_layer_height: f64,
        layer_height: f64,
    ) -> Result<Gcode, String> {
        let argument = path_argument(profile)?;
        let gcode = unsafe {
            hfw_plate_slice_placed(self.0, argument.as_ptr(), first_layer_height, layer_height)
        };
        if gcode.is_null() {
            return Err(last_error());
        }
        Ok(Gcode(gcode))
    }

    /// The ids of the plate's objects, in the order they were added.
    pub fn object_ids(&self) -> Result<Vec<u64>, String> {
        let count = unsafe { hfw_plate_object_ids(self.0, std::ptr::null_mut(), 0) };
        let mut ids = vec![0u64; count];
        if count > 0 {
            let read = unsafe { hfw_plate_object_ids(self.0, ids.as_mut_ptr(), ids.len()) };
            if read != count {
                return Err(last_error());
            }
        }
        Ok(ids)
    }

    /// Object `id`'s name. One call sizes the buffer the next fills, so a name is
    /// never truncated into a different name.
    pub fn object_name(&self, id: u64) -> Result<String, String> {
        let needed = unsafe { hfw_plate_object_name_get(self.0, id, std::ptr::null_mut(), 0) };
        if needed < 0 {
            return Err(last_error());
        }
        let mut buffer = vec![0u8; needed as usize + 1];
        let read = unsafe {
            hfw_plate_object_name_get(
                self.0,
                id,
                buffer.as_mut_ptr() as *mut std::os::raw::c_char,
                buffer.len(),
            )
        };
        if read != needed {
            return Err(last_error());
        }
        String::from_utf8(buffer[..needed as usize].to_vec())
            .map_err(|error| format!("the object name is not UTF-8: {error}"))
    }

    /// Names object `id`.
    pub fn set_object_name(&mut self, id: u64, name: &str) -> Result<(), String> {
        let name = text_argument(name)?;
        let result = unsafe { hfw_plate_object_name(self.0, id, name.as_ptr()) };
        if result != 0 {
            return Err(last_error());
        }
        Ok(())
    }

    /// Object `id`'s placement as sixteen `f64`, row-major, four rows of four.
    pub fn object_placement_matrix(&self, id: u64) -> Result<[f64; 16], String> {
        let mut matrix = [0.0f64; 16];
        let read = unsafe { hfw_plate_object_placement_matrix_get(self.0, id, matrix.as_mut_ptr()) };
        if read != 0 {
            return Err(last_error());
        }
        Ok(matrix)
    }

    /// Moves object `id` from sixteen `f64`, row-major.
    pub fn place_matrix(&mut self, id: u64, matrix: &[f64; 16]) -> Result<(), String> {
        let result = unsafe { hfw_plate_object_placement_matrix(self.0, id, matrix.as_ptr()) };
        if result != 0 {
            return Err(last_error());
        }
        Ok(())
    }

    /// Whether object `id` is part of the job.
    pub fn object_enabled(&self, id: u64) -> Result<bool, String> {
        let mut enabled = 0i32;
        let read = unsafe { hfw_plate_object_enabled_get(self.0, id, &mut enabled) };
        if read != 0 {
            return Err(last_error());
        }
        Ok(enabled != 0)
    }

    /// A copy of object `id`'s geometry, carrying its paint.
    pub fn object_mesh(&self, id: u64) -> Result<Mesh, String> {
        let mesh = unsafe { hfw_plate_object_mesh(self.0, id) };
        if mesh.is_null() {
            return Err(last_error());
        }
        Ok(Mesh(mesh))
    }

    /// Object `id`'s accepted overrides as the JSON rows a setter takes.
    pub fn object_settings_json(&self, id: u64) -> Result<String, String> {
        let text = unsafe { hfw_plate_object_settings_json(self.0, id) };
        if text.is_null() {
            return Err(last_error());
        }
        let bytes = unsafe { CStr::from_ptr(text) }.to_bytes().to_vec();
        unsafe { hfw_string_free(text) };
        String::from_utf8(bytes)
            .map_err(|error| format!("the settings JSON is not UTF-8: {error}"))
    }

    /// Sets one override on object `id`.
    pub fn set_object_setting(
        &mut self,
        id: u64,
        name: &str,
        value: &str,
    ) -> Result<(), String> {
        let name = text_argument(name)?;
        let value = text_argument(value)?;
        let result = unsafe { hfw_plate_object_set(self.0, id, name.as_ptr(), value.as_ptr()) };
        if result != 0 {
            return Err(last_error());
        }
        Ok(())
    }

    /// Attaches a copy of `mesh` to object `id` and returns the volume's id.
    pub fn add_volume(&mut self, id: u64, kind: i32, mesh: &Mesh) -> Result<u64, String> {
        let volume = unsafe { hfw_plate_object_add_volume(self.0, id, kind, mesh.0) };
        if volume == 0 {
            return Err(last_error());
        }
        Ok(volume)
    }

    /// The ids of object `id`'s volumes, in the order they were added.
    pub fn volume_ids(&self, id: u64) -> Result<Vec<u64>, String> {
        let count = unsafe { hfw_plate_object_volume_ids(self.0, id, std::ptr::null_mut(), 0) };
        let mut ids = vec![0u64; count];
        if count > 0 {
            let read =
                unsafe { hfw_plate_object_volume_ids(self.0, id, ids.as_mut_ptr(), ids.len()) };
            if read != count {
                return Err(last_error());
            }
        }
        Ok(ids)
    }

    /// Which kind of volume `volume` of object `id` is.
    pub fn volume_kind(&self, id: u64, volume: u64) -> Result<i32, String> {
        let mut kind = 0i32;
        let read = unsafe { hfw_plate_object_volume_kind(self.0, id, volume, &mut kind) };
        if read != 0 {
            return Err(last_error());
        }
        Ok(kind)
    }

    /// A copy of volume `volume`'s geometry, in the object's own frame.
    pub fn volume_mesh(&self, id: u64, volume: u64) -> Result<Mesh, String> {
        let mesh = unsafe { hfw_plate_object_volume_mesh(self.0, id, volume) };
        if mesh.is_null() {
            return Err(last_error());
        }
        Ok(Mesh(mesh))
    }

    /// A volume's accepted settings as the JSON rows a setter takes.
    pub fn volume_settings_json(&self, id: u64, volume: u64) -> Result<String, String> {
        let text = unsafe { hfw_plate_object_volume_settings_json(self.0, id, volume) };
        if text.is_null() {
            return Err(last_error());
        }
        let bytes = unsafe { CStr::from_ptr(text) }.to_bytes().to_vec();
        unsafe { hfw_string_free(text) };
        String::from_utf8(bytes)
            .map_err(|error| format!("the volume settings JSON is not UTF-8: {error}"))
    }

    /// Sets one setting on modifier volume `volume` of object `id`.
    pub fn set_volume_setting(
        &mut self,
        id: u64,
        volume: u64,
        name: &str,
        value: &str,
    ) -> Result<(), String> {
        let name = text_argument(name)?;
        let value = text_argument(value)?;
        let result = unsafe {
            hfw_plate_volume_set(self.0, id, volume, name.as_ptr(), value.as_ptr())
        };
        if result != 0 {
            return Err(last_error());
        }
        Ok(())
    }

    /// Writes one paint channel on object `id`: one byte per triangle of its own
    /// geometry, `0` none, `1` enforcer, `2` blocker.
    pub fn set_paint(&mut self, id: u64, channel: i32, states: &[u8]) -> Result<(), String> {
        let result = unsafe {
            hfw_plate_object_paint(self.0, id, channel, states.as_ptr(), states.len())
        };
        if result != 0 {
            return Err(last_error());
        }
        Ok(())
    }

    /// One paint channel read back: one byte per triangle, or nothing when the
    /// channel was never painted.
    pub fn paint(&self, id: u64, channel: i32, triangles: usize) -> Result<Vec<u8>, String> {
        if triangles == 0 {
            return Ok(Vec::new());
        }
        let mut states = vec![0u8; triangles];
        let read = unsafe {
            hfw_plate_object_paint_get(self.0, id, channel, states.as_mut_ptr(), states.len())
        };
        if read < 0 {
            return Err(last_error());
        }
        if read == 0 {
            return Ok(Vec::new());
        }
        states.truncate(read as usize);
        Ok(states)
    }

    /// Object `id`'s own layer heights as interleaved `(z, height)` pairs; no pairs
    /// means the plate's uniform height.
    pub fn set_layer_heights(&mut self, id: u64, pairs: &[f64]) -> Result<(), String> {
        let result = unsafe {
            hfw_plate_object_layer_heights(self.0, id, pairs.as_ptr(), pairs.len() / 2)
        };
        if result != 0 {
            return Err(last_error());
        }
        Ok(())
    }

    /// Object `id`'s layer heights back, interleaved `(z, height)`.
    pub fn layer_heights(&self, id: u64) -> Result<Vec<f64>, String> {
        let pairs =
            unsafe { hfw_plate_object_layer_heights_get(self.0, id, std::ptr::null_mut(), 0) };
        if pairs < 0 {
            return Err(last_error());
        }
        let mut values = vec![0.0f64; pairs as usize * 2];
        if pairs > 0 {
            let read = unsafe {
                hfw_plate_object_layer_heights_get(self.0, id, values.as_mut_ptr(), pairs as usize)
            };
            if read != pairs {
                return Err(last_error());
            }
        }
        Ok(values)
    }

    /// Sets the plate's timeline: one marker per print height.
    pub fn set_markers(
        &mut self,
        kinds: &[i32],
        heights: &[f64],
        texts: &[&str],
    ) -> Result<(), String> {
        let owned = texts
            .iter()
            .map(|text| text_argument(text))
            .collect::<Result<Vec<CString>, String>>()?;
        let pointers = owned.iter().map(|text| text.as_ptr()).collect::<Vec<_>>();
        let result = unsafe {
            hfw_plate_set_markers(
                self.0,
                kinds.as_ptr(),
                heights.as_ptr(),
                pointers.as_ptr(),
                kinds.len(),
            )
        };
        if result != 0 {
            return Err(last_error());
        }
        Ok(())
    }

    /// How many markers the plate's timeline carries.
    pub fn marker_count(&self) -> Result<usize, String> {
        let count = unsafe { hfw_plate_marker_count(self.0) };
        if count < 0 {
            return Err(last_error());
        }
        Ok(count as usize)
    }

    /// Marker `index`: its kind, its Z and its G-code. A text that fills the read
    /// buffer is refused, because the ABI truncates and a truncated marker prints
    /// something else.
    pub fn marker(&self, index: usize) -> Result<Marker, String> {
        let mut kind = 0i32;
        let mut z = 0.0f64;
        let mut text = vec![0u8; MARKER_TEXT_CAPACITY];
        let read = unsafe {
            hfw_plate_marker_get(
                self.0,
                index,
                &mut kind,
                &mut z,
                text.as_mut_ptr() as *mut std::os::raw::c_char,
                text.len(),
            )
        };
        if read != 0 {
            return Err(last_error());
        }
        let text = terminated_text(&text, MARKER_TEXT_CAPACITY, &format!("marker {index}"))?;
        Ok(Marker { kind, z, text })
    }

    /// Sets the beads the plate prints inside a layer that its model does not
    /// describe.
    pub fn set_in_layer(&mut self, strokes: &[InLayerStroke]) -> Result<(), String> {
        let mut zs = Vec::with_capacity(strokes.len());
        let mut counts = Vec::with_capacity(strokes.len());
        let mut widths = Vec::with_capacity(strokes.len());
        let mut heights = Vec::with_capacity(strokes.len());
        let mut speeds = Vec::with_capacity(strokes.len());
        let mut starts = Vec::with_capacity(strokes.len());
        let mut points = Vec::new();
        let mut prefixes = Vec::with_capacity(strokes.len());
        for stroke in strokes {
            zs.push(stroke.z);
            counts.push(stroke.points.len() / 2);
            widths.push(stroke.width);
            heights.push(stroke.height);
            speeds.push(stroke.speed);
            starts.push(points.len() / 2);
            points.extend_from_slice(&stroke.points);
            prefixes.push(text_argument(&stroke.prefix)?);
        }
        let pointers = prefixes.iter().map(|text| text.as_ptr()).collect::<Vec<_>>();
        let result = unsafe {
            hfw_plate_set_in_layer(
                self.0,
                zs.as_ptr(),
                counts.as_ptr(),
                widths.as_ptr(),
                heights.as_ptr(),
                speeds.as_ptr(),
                starts.as_ptr(),
                points.as_ptr(),
                pointers.as_ptr(),
                strokes.len(),
            )
        };
        if result != 0 {
            return Err(last_error());
        }
        Ok(())
    }

    /// How many in-layer strokes the plate carries.
    pub fn in_layer_count(&self) -> Result<usize, String> {
        let count = unsafe { hfw_plate_in_layer_count(self.0) };
        if count < 0 {
            return Err(last_error());
        }
        Ok(count as usize)
    }

    /// Stroke `index` back, with its point count read first so the buffer is the
    /// stroke's own size.
    pub fn in_layer(&self, index: usize) -> Result<InLayerStroke, String> {
        let count = unsafe { hfw_plate_in_layer_points(self.0, index) };
        if count < 0 {
            return Err(last_error());
        }
        let mut points = vec![0.0f64; count as usize * 2];
        let mut z = 0.0f64;
        let mut width = 0.0f64;
        let mut height = 0.0f64;
        let mut speed = 0.0f64;
        let mut prefix = vec![0u8; STROKE_PREFIX_CAPACITY];
        let read = unsafe {
            hfw_plate_in_layer_get(
                self.0,
                index,
                &mut z,
                &mut width,
                &mut height,
                &mut speed,
                points.as_mut_ptr(),
                count as usize,
                prefix.as_mut_ptr() as *mut std::os::raw::c_char,
                prefix.len(),
            )
        };
        if read != 0 {
            return Err(last_error());
        }
        let prefix = terminated_text(&prefix, STROKE_PREFIX_CAPACITY, &format!("stroke {index}"))?;
        Ok(InLayerStroke {
            z,
            points,
            width,
            height,
            speed,
            prefix,
        })
    }

    /// The application's own bytes, saved and reopened with the plate.
    pub fn set_metadata(&mut self, bytes: &[u8]) -> Result<(), String> {
        let result = unsafe {
            hfw_plate_set_metadata(
                self.0,
                bytes.as_ptr() as *const std::os::raw::c_char,
                bytes.len(),
            )
        };
        if result != 0 {
            return Err(last_error());
        }
        Ok(())
    }

    /// Those bytes back, or nothing when there are none.
    pub fn metadata(&self) -> Result<Vec<u8>, String> {
        let mut length = 0usize;
        let bytes = unsafe { hfw_plate_metadata_get(self.0, &mut length) };
        if bytes.is_null() {
            return Ok(Vec::new());
        }
        let copy = unsafe { std::slice::from_raw_parts(bytes as *const u8, length) }.to_vec();
        unsafe { hfw_string_free(bytes) };
        Ok(copy)
    }

    /// The plate as the engine's project file, which is also how two documents are
    /// compared.
    pub fn project_bytes(&self) -> Result<Vec<u8>, String> {
        let mut length = 0usize;
        let bytes = unsafe { hfw_plate_project_bytes(self.0, &mut length) };
        if bytes.is_null() {
            return Err(last_error());
        }
        let copy = unsafe { std::slice::from_raw_parts(bytes as *const u8, length) }.to_vec();
        unsafe { hfw_buffer_free(bytes, length) };
        Ok(copy)
    }

    /// Opens a plate from the bytes of a project file.
    pub fn open_project(bytes: &[u8]) -> Result<Plate, String> {
        let plate = unsafe {
            hfw_plate_open_project(
                bytes.as_ptr() as *const std::os::raw::c_char,
                bytes.len(),
                std::ptr::null(),
            )
        };
        if plate.is_null() {
            return Err(last_error());
        }
        Ok(Plate(plate))
    }
}

/// The text in `buffer`, refusing one that reaches the end of it: the ABI truncates
/// to fit, and a truncated name or snippet is a different document.
fn terminated_text(buffer: &[u8], capacity: usize, what: &str) -> Result<String, String> {
    let end = buffer.iter().position(|byte| *byte == 0).ok_or_else(|| {
        format!("{what} has more text than {capacity} bytes can hold")
    })?;
    if end + 1 == capacity {
        return Err(format!("{what} has more text than {capacity} bytes can hold"));
    }
    String::from_utf8(buffer[..end].to_vec())
        .map_err(|error| format!("{what} is not UTF-8: {error}"))
}

impl Drop for Plate {
    fn drop(&mut self) {
        unsafe { hfw_plate_free(self.0) }
    }
}

/// G-code the engine wrote, with its own verifier's verdict.
pub struct Gcode(*mut HfwGcode);

impl Gcode {
    /// The file the engine wrote.
    pub fn text(&self) -> Result<&str, String> {
        let text = unsafe { hfw_gcode_text(self.0) };
        if text.is_null() {
            return Err(last_error());
        }
        let bytes = unsafe { std::slice::from_raw_parts(text as *const u8, self.len()) };
        std::str::from_utf8(bytes).map_err(|error| format!("the G-code is not UTF-8: {error}"))
    }

    /// How many bytes the file holds.
    pub fn len(&self) -> usize {
        unsafe { hfw_gcode_len(self.0) }
    }

    /// True when the file is empty.
    pub fn is_empty(&self) -> bool {
        self.len() == 0
    }

    /// The engine's own independent check of the file it wrote.
    pub fn verified(&self) -> bool {
        unsafe { hfw_gcode_verified(self.0) == 1 }
    }
}

impl Drop for Gcode {
    fn drop(&mut self) {
        unsafe { hfw_gcode_free(self.0) }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn repository_root() -> std::path::PathBuf {
        Path::new(env!("CARGO_MANIFEST_DIR")).join("../..")
    }

    #[test]
    fn the_linked_library_exports_the_surface_this_module_declares() {
        assert_eq!(abi_version(), ABI_VERSION);
    }

    #[test]
    fn every_declared_symbol_resolves_in_the_linked_engine() {
        let symbols: [usize; 42] = [
            hfw_mesh_stl_bytes as usize,
            hfw_mesh_new as usize,
            hfw_mesh_cut as usize,
            hfw_mesh_paint_set as usize,
            hfw_mesh_paint_get as usize,
            hfw_plate_object_placement_matrix as usize,
            hfw_plate_object_placement_matrix_get as usize,
            hfw_plate_object_paint as usize,
            hfw_plate_object_paint_get as usize,
            hfw_plate_object_layer_heights as usize,
            hfw_plate_object_layer_heights_get as usize,
            hfw_plate_object_settings_json as usize,
            hfw_plate_object_volume_settings_json as usize,
            hfw_plate_set_in_layer as usize,
            hfw_plate_in_layer_count as usize,
            hfw_plate_in_layer_points as usize,
            hfw_plate_in_layer_get as usize,
            hfw_plate_set_markers as usize,
            hfw_plate_marker_count as usize,
            hfw_plate_marker_get as usize,
            hfw_plate_set_metadata as usize,
            hfw_plate_metadata_get as usize,
            hfw_plate_object_name as usize,
            hfw_plate_object_name_get as usize,
            hfw_plate_object_set as usize,
            hfw_plate_object_ids as usize,
            hfw_plate_object_enabled_get as usize,
            hfw_plate_object_mesh as usize,
            hfw_plate_object_add_volume as usize,
            hfw_plate_object_volume_count as usize,
            hfw_plate_object_volume_ids as usize,
            hfw_plate_object_volume_kind as usize,
            hfw_plate_object_volume_mesh as usize,
            hfw_plate_volume_set as usize,
            hfw_plate_project_bytes as usize,
            hfw_plate_open_project as usize,
            hfw_plate_slice_placed as usize,
            hfw_string_free as usize,
            hfw_buffer_free as usize,
            hfw_plate_snapshot as usize,
            hfw_plate_restore as usize,
            hfw_snapshot_free as usize,
        ];
        assert!(symbols.iter().all(|symbol| *symbol != 0));
    }

    #[test]
    fn a_cube_loads_with_its_own_size_and_volume() {
        let mesh = Mesh::from_file(&repository_root().join("oracle/models/cube_20.stl"))
            .expect("the fixture cube loads");
        assert!(mesh.triangles() > 0);
        let bounds = mesh.bounds().expect("the cube has area and height");
        assert!((bounds.max[0] - bounds.min[0] - 20.0).abs() < 1e-6);
        assert!((mesh.volume() - 8000.0).abs() < 1.0);
    }

    #[test]
    fn a_plate_of_one_cube_slices_and_the_engine_verifies_the_file() {
        let root = repository_root();
        let mesh =
            Mesh::from_file(&root.join("oracle/models/cube_20.stl")).expect("the fixture cube loads");
        let mut plate = Plate::new();
        let id = plate.add(&mesh).expect("the cube joins the plate");
        assert_eq!(plate.len(), 1);
        plate
            .place(
                id,
                Transform {
                    translation_mm: [125.0, 110.0, 0.0],
                    ..Transform::default()
                },
            )
            .expect("the cube moves onto the bed");
        let gcode = plate
            .slice(&root.join("oracle/configs/base.json"), 0.0, 0.0)
            .expect("the plate slices under the application's own settings document");
        assert!(gcode.verified());
        assert!(gcode.text().expect("the file is UTF-8").contains(";TYPE:"));
    }
}
