//! Resting poses for an object: one named facet laid flat, and the pose that scores
//! best across every distinct face normal.
//!
//! Ported from `src/qt/src/Orient.cpp`. Geometry arrives as flat arrays because the
//! C++ that still owns the document holds the engine's mesh, and a pose leaves the
//! same way: sixteen `f64` row-major, four rows of four, the shape
//! `src/qt/src/ProjectFile.hpp` already carries a transform in.
//!
//! Three things here are not free choices:
//!
//! - The rotation is composed on the left of the object's current linear part
//!   (`rotation * linear`, translation kept), and the normal it aligns is the one in
//!   the mesh's own frame. An object that is already tilted therefore keeps its tilt:
//!   the chosen face is not flat against the bed afterwards. The pose is *scored* as
//!   composed, which is what makes the existing tilt part of the answer.
//! - The score is `height + 0.35 * overhang - 0.05 * contact` with all three terms in
//!   the numbers they arrive in: millimetres of printed height, and square
//!   millimetres of overhanging and bed-contacting facet area. Nothing is normalised
//!   by the mesh's total area, so the balance between height and area depends on how
//!   large the object is.
//! - Candidates keep the first facet's normal of each group whose normals agree
//!   within `0.9995`, the summed area of the group, are sorted by area descending,
//!   truncated to 64, and ties in area are broken by facet index. The C++ left that
//!   tie to `std::sort`'s permutation; the facet index reproduces what it produces
//!   today and stops the answer depending on it.

use crate::mesh::Mesh;

/// The z a face normal has to reach to count as overhanging.
const OVERHANG_LIMIT: f64 = -0.55;

/// The z a face normal has to reach to count as resting on the bed.
const DOWN_TOLERANCE: f64 = -0.99;

/// What a square millimetre of overhang costs against a millimetre of height.
const SUPPORT_WEIGHT: f64 = 0.35;

/// What a square millimetre of bed contact buys back.
const CONTACT_WEIGHT: f64 = 0.05;

/// Normals whose dot product is above this are the same resting surface.
const PARALLEL_DOT: f64 = 0.9995;

/// How many resting surfaces are scored.
const CANDIDATE_LIMIT: usize = 64;

/// Below this a cross product has no direction to give a normal.
const NORMAL_EPSILON: f64 = 1e-12;

/// Below this a facet normal is not a surface to rest on.
const FLAT_EPSILON: f64 = 1e-9;

/// Eigen's `dummy_precision` for `double`: the point at which two vectors are close
/// enough to opposite that the two-vector rotation gives up on its own formula.
const OPPOSITE_EPSILON: f64 = 1e-12;

/// A pose is four rows of four; this is the array `carried` and `linear_of` read.
const MATRIX_VALUES: usize = 16;

/// The direction a resting face's normal is turned to.
const DOWN: [f64; 3] = [0.0, 0.0, -1.0];

/// The pose that puts one facet of the mesh face down.
///
/// `matrix` is the object's current pose, sixteen `f64` row-major, and `facet` is a
/// triangle index. Returns the composed pose as sixteen `f64`, the input matrix
/// unchanged when the facet is out of range or has no normal, and nothing at all when
/// the mesh or the matrix cannot be read as one.
pub fn lay_facet_flat(vertices: &[f32], indices: &[u32], matrix: &[f64], facet: i32) -> Vec<f64> {
    if matrix.len() != MATRIX_VALUES {
        return Vec::new();
    }
    let Some(mesh) = mesh_of(vertices, indices) else {
        return matrix.to_vec();
    };
    let Some(facet) = usize::try_from(facet)
        .ok()
        .filter(|facet| *facet < mesh.triangle_count())
    else {
        return matrix.to_vec();
    };
    let normal = facet_normal(&mesh, facet);
    if norm(normal) < FLAT_EPSILON {
        return matrix.to_vec();
    }
    with_rotation(matrix, &align_to(normal))
}

/// The pose that makes the object sit as low and as support-free as possible.
///
/// Every distinct face normal is tried as a resting surface and the pose with the
/// lowest score wins; sixteen `f64` row-major, or nothing when the mesh has no facet
/// with a normal.
pub fn best_resting_rotation(vertices: &[f32], indices: &[u32], matrix: &[f64]) -> Vec<f64> {
    if matrix.len() != MATRIX_VALUES {
        return Vec::new();
    }
    let Some(mesh) = mesh_of(vertices, indices) else {
        return Vec::new();
    };
    let facets = mesh.triangle_count();
    if facets == 0 {
        return Vec::new();
    }

    let mut normals = Vec::with_capacity(facets);
    let mut areas = Vec::with_capacity(facets);
    for facet in 0..facets {
        normals.push(facet_normal(&mesh, facet));
        areas.push(facet_area(&mesh, facet));
    }

    let mut candidates: Vec<Candidate> = Vec::new();
    for facet in 0..facets {
        let normal = normals[facet];
        if norm(normal) < FLAT_EPSILON {
            continue;
        }
        let agreeing = candidates
            .iter()
            .position(|candidate| dot(candidate.normal, normal) > PARALLEL_DOT);
        match agreeing {
            Some(index) => candidates[index].area += areas[facet],
            None => candidates.push(Candidate {
                normal,
                area: areas[facet],
                facet,
            }),
        }
    }
    if candidates.is_empty() {
        return Vec::new();
    }

    candidates.sort_by(|left, right| {
        right
            .area
            .total_cmp(&left.area)
            .then(left.facet.cmp(&right.facet))
    });
    candidates.truncate(CANDIDATE_LIMIT);

    let mut chosen: Option<(Vec<f64>, f64)> = None;
    for candidate in &candidates {
        let pose = with_rotation(matrix, &align_to(candidate.normal));
        let score = score_orientation(&pose, &mesh, &normals, &areas).value();
        let better = match &chosen {
            None => true,
            Some((_, best)) => score < *best,
        };
        if better {
            chosen = Some((pose, score));
        }
    }
    chosen.map(|(pose, _)| pose).unwrap_or_default()
}

/// One resting surface: the normal of the first facet that named it, the area of
/// every facet that agreed with it, and that first facet's index.
struct Candidate {
    normal: [f64; 3],
    area: f64,
    facet: usize,
}

/// What one pose is worth, before the weights are applied.
struct Score {
    height: f64,
    overhang: f64,
    contact: f64,
}

impl Score {
    fn value(&self) -> f64 {
        self.height + SUPPORT_WEIGHT * self.overhang - CONTACT_WEIGHT * self.contact
    }
}

/// The printed height of `mesh` under `pose`, and the facet area hanging or resting.
fn score_orientation(pose: &[f64], mesh: &Mesh, normals: &[[f64; 3]], areas: &[f64]) -> Score {
    let mut low = f64::MAX;
    let mut high = f64::MIN;
    for vertex in mesh.vertices().chunks_exact(3) {
        let z = carried(pose, 2, vertex);
        low = low.min(z);
        high = high.max(z);
    }

    let linear = linear_of(pose);
    let mut score = Score {
        height: high - low,
        overhang: 0.0,
        contact: 0.0,
    };
    for (index, normal) in normals.iter().enumerate() {
        let world = apply(&linear, *normal);
        if world[2] <= OVERHANG_LIMIT {
            score.overhang += areas[index];
        }
        if world[2] <= DOWN_TOLERANCE {
            score.contact += areas[index];
        }
    }
    score
}

/// The rotation that turns `normal` down, as a 3x3 row-major matrix.
///
/// The shortest arc between the two directions, which is what
/// `Eigen::Quaterniond::FromTwoVectors` computes. A normal already pointing straight
/// down gives the identity. A normal pointing straight up is the case the general
/// formula cannot express, because the arc is a half turn and its axis is only
/// determined up to a turn about the vertical: the axis chosen here is the limit of
/// the general formula's own `from × down`, and a horizontal axis when that limit is
/// itself undefined.
fn align_to(normal: [f64; 3]) -> [f64; 9] {
    if norm(normal) < NORMAL_EPSILON {
        return identity3();
    }
    let from = normalized(normal);
    let along = dot(DOWN, from);
    if along < -1.0 + OPPOSITE_EPSILON {
        let axis = opposite_axis(from);
        return quaternion_matrix(axis, 0.0);
    }
    let axis = cross(from, DOWN);
    let span = ((1.0 + along) * 2.0).sqrt();
    quaternion_matrix(scale(axis, 1.0 / span), span * 0.5)
}

/// A horizontal axis a half turn can be taken about, for a `from` that points up.
fn opposite_axis(from: [f64; 3]) -> [f64; 3] {
    let arc = cross(from, DOWN);
    if norm(arc) > NORMAL_EPSILON {
        return normalized(arc);
    }
    let across = if from[0].abs() < from[2].abs() {
        [1.0, 0.0, 0.0]
    } else {
        [0.0, 0.0, 1.0]
    };
    normalized(cross(from, across))
}

/// The rotation a unit quaternion names, in the order `Eigen::Quaternion::toRotationMatrix`
/// multiplies it out.
fn quaternion_matrix(vector: [f64; 3], scalar: f64) -> [f64; 9] {
    let tx = 2.0 * vector[0];
    let ty = 2.0 * vector[1];
    let tz = 2.0 * vector[2];
    let twx = tx * scalar;
    let twy = ty * scalar;
    let twz = tz * scalar;
    let txx = tx * vector[0];
    let txy = ty * vector[0];
    let txz = tz * vector[0];
    let tyy = ty * vector[1];
    let tyz = tz * vector[1];
    let tzz = tz * vector[2];
    [
        1.0 - (tyy + tzz),
        txy - twz,
        txz + twy,
        txy + twz,
        1.0 - (txx + tzz),
        tyz - twx,
        txz - twy,
        tyz + twx,
        1.0 - (txx + tyy),
    ]
}

/// `matrix` with `rotation` composed on the left of its linear part, translation kept.
fn with_rotation(matrix: &[f64], rotation: &[f64; 9]) -> Vec<f64> {
    let linear = multiply(rotation, &linear_of(matrix));
    vec![
        linear[0], linear[1], linear[2], matrix[3],
        linear[3], linear[4], linear[5], matrix[7],
        linear[6], linear[7], linear[8], matrix[11],
        0.0, 0.0, 0.0, 1.0,
    ]
}

/// The linear part of a pose, as 3x3 row-major.
fn linear_of(matrix: &[f64]) -> [f64; 9] {
    [
        matrix[0], matrix[1], matrix[2],
        matrix[4], matrix[5], matrix[6],
        matrix[8], matrix[9], matrix[10],
    ]
}

/// One facet's normal in the mesh's own frame, unit length or all zero.
fn facet_normal(mesh: &Mesh, facet: usize) -> [f64; 3] {
    let (tangent, bitangent) = facet_span(mesh, facet);
    let product = cross(tangent, bitangent);
    let length = norm(product);
    if length < NORMAL_EPSILON {
        [0.0; 3]
    } else {
        divide(product, length)
    }
}

/// One facet's area, in square millimetres.
fn facet_area(mesh: &Mesh, facet: usize) -> f64 {
    let (tangent, bitangent) = facet_span(mesh, facet);
    norm(cross(tangent, bitangent)) * 0.5
}

/// The two edges of a facet out of its first corner, in `f64`.
fn facet_span(mesh: &Mesh, facet: usize) -> ([f64; 3], [f64; 3]) {
    let indices = mesh.indices();
    let at = facet * 3;
    let first = corner(mesh, indices[at] as usize);
    let second = corner(mesh, indices[at + 1] as usize);
    let third = corner(mesh, indices[at + 2] as usize);
    (sub(second, first), sub(third, first))
}

fn corner(mesh: &Mesh, vertex: usize) -> [f64; 3] {
    let vertices = mesh.vertices();
    let at = vertex * 3;
    [
        vertices[at] as f64,
        vertices[at + 1] as f64,
        vertices[at + 2] as f64,
    ]
}

/// The mesh the flat arrays describe, or nothing when they cannot be one.
///
/// The copy is what crossing this boundary costs: the C++ side owns the engine's
/// mesh, and this crate's own constructor is what proves the indices name vertices
/// that exist, so every read below is in range.
fn mesh_of(vertices: &[f32], indices: &[u32]) -> Option<Mesh> {
    Mesh::new(vertices.to_vec(), indices.to_vec()).ok()
}

fn carried(matrix: &[f64], row: usize, vertex: &[f32]) -> f64 {
    let base = row * 4;
    matrix[base] * vertex[0] as f64
        + matrix[base + 1] * vertex[1] as f64
        + matrix[base + 2] * vertex[2] as f64
        + matrix[base + 3]
}

fn apply(linear: &[f64; 9], vector: [f64; 3]) -> [f64; 3] {
    [
        linear[0] * vector[0] + linear[1] * vector[1] + linear[2] * vector[2],
        linear[3] * vector[0] + linear[4] * vector[1] + linear[5] * vector[2],
        linear[6] * vector[0] + linear[7] * vector[1] + linear[8] * vector[2],
    ]
}

fn multiply(left: &[f64; 9], right: &[f64; 9]) -> [f64; 9] {
    let mut out = [0.0; 9];
    for row in 0..3 {
        for column in 0..3 {
            out[row * 3 + column] = left[row * 3] * right[column]
                + left[row * 3 + 1] * right[3 + column]
                + left[row * 3 + 2] * right[6 + column];
        }
    }
    out
}

fn identity3() -> [f64; 9] {
    [1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0]
}

fn normalized(vector: [f64; 3]) -> [f64; 3] {
    divide(vector, norm(vector))
}

fn divide(vector: [f64; 3], length: f64) -> [f64; 3] {
    [vector[0] / length, vector[1] / length, vector[2] / length]
}

fn scale(vector: [f64; 3], factor: f64) -> [f64; 3] {
    [vector[0] * factor, vector[1] * factor, vector[2] * factor]
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
