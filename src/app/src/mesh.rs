//! The application's own triangle mesh.
//!
//! Geometry belongs to this crate rather than to the engine: a document holds one
//! of these per object and one per volume, undo shares it, and the engine is handed
//! a copy when it is time to slice. Coordinates are millimetres in the object's own
//! frame, kept as `f32` because that is what every mesh file carries — a wider type
//! here would round the file once more than the file already has.
//!
//! The operations are the ones `PlateDocument.cpp` used to ask the C++ engine for:
//! bounds, the signed volume and surface area a transform makes printed geometry
//! out of, a translated or transformed copy, the concatenation of two meshes, and
//! the split into shells that a part-split needs. They are the same arithmetic the
//! engine performs, so a document handed to the slicer is unchanged.

/// A matrix is twelve `f64`, row-major, three rows of four: the linear part and
/// the translation of an affine transform.
pub const IDENTITY: [f64; 12] = [
    1.0, 0.0, 0.0, 0.0, //
    0.0, 1.0, 0.0, 0.0, //
    0.0, 0.0, 1.0, 0.0,
];

/// An indexed triangle mesh: vertices as xyz triples, faces as vertex triples.
pub struct Mesh {
    vertices: Vec<f32>,
    indices: Vec<u32>,
}

impl Mesh {
    /// A mesh from flat arrays, refusing faces that name a vertex it does not have.
    pub fn new(vertices: Vec<f32>, indices: Vec<u32>) -> Result<Mesh, String> {
        if !vertices.len().is_multiple_of(3) {
            return Err(format!(
                "{} vertex coordinates is not a whole number of vertices",
                vertices.len()
            ));
        }
        if !indices.len().is_multiple_of(3) {
            return Err(format!(
                "{} vertex indices is not a whole number of triangles",
                indices.len()
            ));
        }
        let count = vertices.len() / 3;
        if let Some(vertex) = indices.iter().find(|index| **index as usize >= count) {
            return Err(format!(
                "a triangle names vertex {vertex}, and the mesh has {count}"
            ));
        }
        Ok(Mesh { vertices, indices })
    }

    /// A copy of a mesh the engine loaded.
    pub fn from_hfw(mesh: &crate::hfw::Mesh) -> Result<Mesh, String> {
        Mesh::new(mesh.vertices()?, mesh.indices()?)
    }

    /// The xyz triples, three per vertex.
    pub fn vertices(&self) -> &[f32] {
        &self.vertices
    }

    /// The vertex triples, three per triangle.
    pub fn indices(&self) -> &[u32] {
        &self.indices
    }

    /// How many triangles the mesh has.
    pub fn triangle_count(&self) -> usize {
        self.indices.len() / 3
    }

    /// The lower and upper corner of the bounding box, or `None` for an empty mesh.
    pub fn bounds(&self) -> Option<([f32; 3], [f32; 3])> {
        let first = self.vertex(0);
        let mut low = first;
        let mut high = first;
        for index in 1..self.vertices.len() / 3 {
            let vertex = self.vertex(index);
            for axis in 0..3 {
                low[axis] = low[axis].min(vertex[axis]);
                high[axis] = high[axis].max(vertex[axis]);
            }
        }
        (!self.vertices.is_empty()).then_some((low, high))
    }

    /// This mesh moved by `offset`, in millimetres.
    pub fn translated(&self, offset: [f32; 3]) -> Mesh {
        let mut moved = self.vertices.clone();
        for (index, value) in moved.iter_mut().enumerate() {
            *value += offset[index % 3];
        }
        Mesh {
            vertices: moved,
            indices: self.indices.clone(),
        }
    }

    /// This mesh transformed by `matrix`, the way the engine transforms one: the
    /// vertex is carried in `f64` and stored back as the `f32` the mesh keeps.
    pub fn transformed(&self, matrix: &[f64]) -> Mesh {
        let mut moved = Vec::with_capacity(self.vertices.len());
        for index in 0..self.vertices.len() / 3 {
            let [x, y, z] = self.vertex(index);
            let carried = carry(matrix, x as f64, y as f64, z as f64);
            moved.extend_from_slice(&carried);
        }
        Mesh {
            vertices: moved,
            indices: self.indices.clone(),
        }
    }

    /// Both meshes as one, `other`'s faces renumbered onto the end of this one.
    pub fn appended(&self, other: &Mesh) -> Mesh {
        let offset = self.vertices.len() / 3;
        let mut vertices = self.vertices.clone();
        vertices.extend_from_slice(&other.vertices);
        let mut indices = self.indices.clone();
        indices.extend(other.indices.iter().map(|index| index + offset as u32));
        Mesh { vertices, indices }
    }

    /// The triangles named by `faces`, renumbered onto their own vertex table.
    pub fn subset(&self, faces: &[u32]) -> Mesh {
        let mut vertices = Vec::new();
        let mut indices = Vec::with_capacity(faces.len() * 3);
        let mut moved: std::collections::HashMap<u32, u32> = std::collections::HashMap::new();
        for face in faces {
            for corner in 0..3 {
                let vertex = self.indices[*face as usize * 3 + corner];
                let next = *moved.entry(vertex).or_insert_with(|| {
                    let index = (vertices.len() / 3) as u32;
                    let base = vertex as usize * 3;
                    vertices.extend_from_slice(&self.vertices[base..base + 3]);
                    index
                });
                indices.push(next);
            }
        }
        Mesh { vertices, indices }
    }

    /// The volume the mesh encloses under `matrix`, in cubic millimetres.
    ///
    /// Signed tetrahedra against the origin, as the engine measures it; the sign
    /// is dropped because an inside-out mesh encloses the same amount of material.
    pub fn volume(&self, matrix: &[f64]) -> f64 {
        let mut volume = 0.0;
        for face in 0..self.triangle_count() {
            let [a, b, c] = self.corners(face, matrix);
            volume += dot(a, cross(b, c)) / 6.0;
        }
        volume.abs()
    }

    /// The surface area the mesh covers under `matrix`, in square millimetres.
    pub fn surface_area(&self, matrix: &[f64]) -> f64 {
        let mut area = 0.0;
        for face in 0..self.triangle_count() {
            let [a, b, c] = self.corners(face, matrix);
            area += norm(cross(sub(b, a), sub(c, a))) / 2.0;
        }
        area
    }

    /// One label per triangle, triangles that share a vertex sharing a label.
    ///
    /// The labels are contiguous from zero, so a caller groups faces by them and
    /// gets the shells a part split divides an object into.
    pub fn shell_labels(&self) -> Vec<u32> {
        let faces = self.triangle_count();
        let mut parent: Vec<usize> = (0..faces).collect();
        let mut shell_of_vertex: Vec<Option<usize>> = vec![None; self.vertices.len() / 3];

        for face in 0..faces {
            for corner in 0..3 {
                let vertex = self.indices[face * 3 + corner] as usize;
                match shell_of_vertex[vertex] {
                    Some(first) => unite(&mut parent, first, face),
                    None => shell_of_vertex[vertex] = Some(face),
                }
            }
        }

        let mut labels = vec![0u32; faces];
        let mut next = 0u32;
        let mut label_of_root: Vec<Option<u32>> = vec![None; faces];
        for (face, label) in labels.iter_mut().enumerate() {
            let root = find(&mut parent, face);
            match label_of_root[root] {
                Some(existing) => *label = existing,
                None => {
                    label_of_root[root] = Some(next);
                    *label = next;
                    next += 1;
                }
            }
        }
        labels
    }

    fn vertex(&self, index: usize) -> [f32; 3] {
        let base = index * 3;
        [
            self.vertices[base],
            self.vertices[base + 1],
            self.vertices[base + 2],
        ]
    }

    fn corners(&self, face: usize, matrix: &[f64]) -> [[f64; 3]; 3] {
        let mut out = [[0.0; 3]; 3];
        for (corner, slot) in out.iter_mut().enumerate() {
            let vertex = self.vertex(self.indices[face * 3 + corner] as usize);
            *slot = [
                carried(matrix, 0, vertex),
                carried(matrix, 1, vertex),
                carried(matrix, 2, vertex),
            ];
        }
        out
    }
}

fn carried(matrix: &[f64], row: usize, vertex: [f32; 3]) -> f64 {
    let base = row * 4;
    matrix[base] * vertex[0] as f64
        + matrix[base + 1] * vertex[1] as f64
        + matrix[base + 2] * vertex[2] as f64
        + matrix[base + 3]
}

fn carry(matrix: &[f64], x: f64, y: f64, z: f64) -> [f32; 3] {
    let mut out = [0.0f32; 3];
    for (row, slot) in out.iter_mut().enumerate() {
        let base = row * 4;
        *slot = (matrix[base] * x
            + matrix[base + 1] * y
            + matrix[base + 2] * z
            + matrix[base + 3]) as f32;
    }
    out
}

fn find(parent: &mut [usize], mut root: usize) -> usize {
    while parent[root] != root {
        parent[root] = parent[parent[root]];
        root = parent[root];
    }
    root
}

fn unite(parent: &mut [usize], a: usize, b: usize) {
    let (left, right) = (find(parent, a), find(parent, b));
    if left != right {
        parent[right] = left;
    }
}

fn sub(a: [f64; 3], b: [f64; 3]) -> [f64; 3] {
    [a[0] - b[0], a[1] - b[1], a[2] - b[2]]
}

fn cross(a: [f64; 3], b: [f64; 3]) -> [f64; 3] {
    [
        a[1] * b[2] - a[2] * b[1],
        a[2] * b[0] - a[0] * b[2],
        a[0] * b[1] - a[1] * b[0],
    ]
}

fn dot(a: [f64; 3], b: [f64; 3]) -> f64 {
    a[0] * b[0] + a[1] * b[1] + a[2] * b[2]
}

fn norm(a: [f64; 3]) -> f64 {
    dot(a, a).sqrt()
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::path::PathBuf;

    fn repository_root() -> PathBuf {
        PathBuf::from(env!("CARGO_MANIFEST_DIR"))
            .join("../..")
            .canonicalize()
            .expect("the repository root")
    }

    fn cube() -> Mesh {
        let loaded = crate::hfw::Mesh::from_file(&repository_root().join("oracle/models/cube_20.stl"))
            .expect("the fixture cube loads");
        Mesh::from_hfw(&loaded).expect("the engine's cube is a mesh")
    }

    fn translation(x: f64, y: f64, z: f64) -> [f64; 12] {
        [1.0, 0.0, 0.0, x, 0.0, 1.0, 0.0, y, 0.0, 0.0, 1.0, z]
    }

    #[test]
    fn an_engine_mesh_becomes_one_of_ours_and_measures_the_same() {
        let mesh = cube();
        assert_eq!(mesh.triangle_count(), 12);
        let (low, high) = mesh.bounds().expect("the cube has volume");
        assert_eq!(low, [-10.0, -10.0, 0.0]);
        assert_eq!(high, [10.0, 10.0, 20.0]);
        assert!((mesh.volume(&IDENTITY) - 8000.0).abs() < 1e-6);
        assert!((mesh.surface_area(&IDENTITY) - 2400.0).abs() < 1e-6);
        assert_eq!(mesh.shell_labels(), vec![0; 12]);
    }

    #[test]
    fn a_transformed_copy_moves_and_the_original_stays_put() {
        let mesh = cube();
        let moved = mesh.transformed(&translation(125.0, 110.0, 5.0));
        let (low, high) = moved.bounds().expect("the copy has volume");
        assert_eq!(low, [115.0, 100.0, 5.0]);
        assert_eq!(high, [135.0, 120.0, 25.0]);
        assert!((moved.volume(&IDENTITY) - 8000.0).abs() < 1e-6);
        assert_eq!(mesh.bounds().expect("the original is untouched").0, [-10.0, -10.0, 0.0]);
    }

    #[test]
    fn two_cubes_appended_are_one_mesh_of_two_shells() {
        let mesh = cube();
        let both = mesh.appended(&mesh.translated([0.0, 0.0, 40.0]));
        assert_eq!(both.triangle_count(), 24);
        assert!((both.volume(&IDENTITY) - 16000.0).abs() < 1e-6);
        let labels = both.shell_labels();
        assert!(labels[..12].iter().all(|label| *label == 0));
        assert!(labels[12..].iter().all(|label| *label == 1));
    }

    #[test]
    fn a_subset_keeps_only_the_faces_it_names() {
        let mesh = cube();
        let half = mesh.subset(&[0, 1, 2, 3, 4, 5]);
        assert_eq!(half.triangle_count(), 6);
        assert_eq!(half.bounds().expect("the half has area").0, mesh.bounds().unwrap().0);
        let both = mesh.appended(&mesh.translated([0.0, 0.0, 40.0]));
        let labels = both.shell_labels();
        let second: Vec<u32> = (12..24).collect();
        let only_second = both.subset(&second);
        assert_eq!(only_second.triangle_count(), 12);
        assert!(only_second.bounds().expect("it has area").0[2] > 39.0);
        assert_eq!(labels[12], 1);
    }

    #[test]
    fn a_face_naming_a_vertex_that_is_not_there_is_refused() {
        let refused = Mesh::new(vec![0.0; 9], vec![0, 1, 3]);
        assert!(refused.is_err(), "an out-of-range index was accepted");
        let ragged = Mesh::new(vec![0.0; 8], vec![0, 1, 2]);
        assert!(ragged.is_err(), "a ragged vertex array was accepted");
    }
}
