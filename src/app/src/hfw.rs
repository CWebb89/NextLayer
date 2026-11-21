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
        let symbols: [usize; 24] = [
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
            hfw_string_free as usize,
            hfw_buffer_free as usize,
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
