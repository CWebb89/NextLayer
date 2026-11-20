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

extern "C" {
    fn hfw_abi_version() -> u32;
    fn hfw_last_error() -> *const std::os::raw::c_char;

    fn hfw_mesh_load_file(path: *const std::os::raw::c_char) -> *mut HfwMesh;
    fn hfw_mesh_from_bytes(bytes: *const u8, len: usize) -> *mut HfwMesh;
    fn hfw_mesh_transform(mesh: *const HfwMesh, transform: Transform) -> *mut HfwMesh;
    fn hfw_mesh_bounds(mesh: *const HfwMesh, out: *mut Bounds) -> i32;
    fn hfw_mesh_triangle_count(mesh: *const HfwMesh) -> usize;
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
