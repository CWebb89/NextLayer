//! The viewport's meshes, built in the one layout `QQuick3DGeometry` takes, and
//! the ray the pointer picking runs against.
//!
//! Ported from `src/qt/src/MeshGeometry.cpp`. The class itself is a
//! `QQuick3DGeometry` and a registered QML singleton, and a QObject cannot be a
//! Rust type, so the Qt half stays where it is: `src/qt/src/MeshGeometry.hpp`
//! keeps the two properties, the `boundsChanged` signal, `setVertexData`,
//! `setStride`, `setBounds`, `addAttribute`, `setPrimitiveType`, `clear()` and
//! `update()`, and is the only place a buffer reaches the renderer. Everything
//! that computes what goes into one — the interleaved vertex layouts, the
//! bounding box, the brush ring and the ray/triangle intersection — is here.
//!
//! Every function is a pure transformation of its arguments: the buffers the
//! shell has to keep between calls (the draw buffer Qt already holds, and the
//! triangle corners a raycast answers against) stay in the shell, because a
//! `MeshGeometry` is one object with a lifetime and this file has none.
//!
//! Coordinates are printer coordinates throughout: millimetres, Z-up, no scaling
//! and no re-centring. The bed grid, the toolpath and the model share one space,
//! and the camera fits itself to the bounds a frame reports.
//!
//! A mesh arrives as flat arrays rather than as the engine's `TriangleMesh`: the
//! header flattens the engine's `indexed_triangle_set` and passes `&[f32]` and
//! `&[u32]`, which is the plain-typed boundary the rest of `bridge.rs` keeps.
//!
//! **This port reproduces one behaviour of the C++ on purpose.** `setColoredMesh`
//! cleared the geometry buffer and left the raycast copy alone, so `raycast()`
//! went on answering against the last mesh `setMesh` drew. The raycast copy is
//! the shell's member and only `setMesh` writes it, which reproduces exactly that.

use crate::bridge::MeshGeometryFrame;

/// Position then normal, six `f32` per vertex: what a plain mesh and the brush
/// ring are drawn with.
const FLOATS_PER_VERTEX: usize = 6;

/// The same with a colour on the end, ten `f32` per vertex: the painted-facet
/// overlay, and the facet highlight the hover draws.
const FLOATS_PER_COLOURED_VERTEX: usize = 10;

const FLOAT_BYTES: usize = std::mem::size_of::<f32>();

/// Where the colour starts inside a vertex, in bytes: past the position and the
/// normal, which is six `f32`.
const COLOUR_OFFSET: i32 = 6 * FLOAT_BYTES as i32;

/// The brush ring is drawn as this many quads.
const RING_SEGMENTS: usize = 64;

/// How far off the surface the ring floats, in millimetres: it lies on the facet
/// under the pointer, and without the lift it loses the depth fight with it.
const RING_LIFT: f32 = 0.06;

/// A triangle this close to the ray's origin is the surface the ray came from,
/// not a hit on the far side of it.
const RAY_EPSILON: f32 = 1e-4;

/// The mesh as an interleaved position+normal buffer, with the triangle corners a
/// raycast answers against.
///
/// A triangle with no area is dropped from both: it has no normal to shade with,
/// and there is nothing there to hit.
pub fn mesh_frame(vertices: &[f32], indices: &[u32]) -> MeshGeometryFrame {
    let (low, high) = bounds_of(vertices);
    let mut frame = MeshGeometryFrame {
        vertices: Vec::with_capacity(indices.len() * FLOATS_PER_VERTEX * FLOAT_BYTES),
        positions: Vec::with_capacity(indices.len() * 3),
        low,
        high,
        stride: (FLOATS_PER_VERTEX * FLOAT_BYTES) as u32,
        colour_offset: -1,
    };
    for face in indices.chunks_exact(3) {
        let Some(corners) = corners(vertices, face) else {
            continue;
        };
        let Some(normal) = unit_normal(corners[0], corners[1], corners[2]) else {
            continue;
        };
        for position in corners {
            push_vertex(&mut frame.vertices, position, normal);
            frame.positions.extend_from_slice(&position);
        }
    }
    frame
}

/// The mesh as an interleaved position+normal+colour buffer, one colour per
/// triangle: straight RGBA in `0..1`, and a triangle whose alpha is zero is left
/// out, so a facet with no colour draws nothing rather than an unpainted copy of
/// the object on top of itself.
///
/// The raycast corners are empty here: painting does not change what the pointer
/// answers against.
pub fn coloured_mesh_frame(
    vertices: &[f32],
    indices: &[u32],
    colours: &[f32],
) -> MeshGeometryFrame {
    let (low, high) = bounds_of(vertices);
    let mut frame = MeshGeometryFrame {
        vertices: Vec::with_capacity(indices.len() * FLOATS_PER_COLOURED_VERTEX * FLOAT_BYTES),
        positions: Vec::new(),
        low,
        high,
        stride: (FLOATS_PER_COLOURED_VERTEX * FLOAT_BYTES) as u32,
        colour_offset: COLOUR_OFFSET,
    };
    for (face, colour) in indices.chunks_exact(3).zip(colours.chunks_exact(4)) {
        if colour[3] <= 0.0 {
            continue;
        }
        let Some(corners) = corners(vertices, face) else {
            continue;
        };
        let Some(normal) = unit_normal(corners[0], corners[1], corners[2]) else {
            continue;
        };
        for position in corners {
            push_vertex(&mut frame.vertices, position, normal);
            for value in colour {
                frame.vertices.extend_from_slice(&value.to_ne_bytes());
            }
        }
    }
    frame
}

/// The brush cursor: a flat annulus in the plane perpendicular to `normal`,
/// lifted off the surface so the facet it lies on does not fight it for depth.
///
/// The normals the ring is drawn with are the caller's `normal` as given, not
/// normalised: the ring is drawn unlit, so they are there for the attribute the
/// material expects and nothing reads their length.
///
/// The raycast corners are empty: the cursor is not a thing the pointer hits.
pub fn ring_frame(
    centre: [f32; 3],
    normal: [f32; 3],
    radius: f32,
    thickness: f32,
) -> MeshGeometryFrame {
    let mut up = cross(normal, [0.0, 0.0, 1.0]);
    if dot(up, up) < 1e-4 {
        up = cross(normal, [1.0, 0.0, 0.0]);
    }
    let up = unit(up);
    let right = unit(cross(normal, up));
    let lift = scale(unit(normal), RING_LIFT);

    let inner = (radius - thickness).max(radius * 0.5);
    let point_at = |angle: f32, distance: f32| -> [f32; 3] {
        add(
            add(add(centre, lift), scale(right, angle.cos() * distance)),
            scale(up, angle.sin() * distance),
        )
    };

    let mut frame = MeshGeometryFrame {
        vertices: Vec::with_capacity(RING_SEGMENTS * 6 * FLOATS_PER_VERTEX * FLOAT_BYTES),
        positions: Vec::new(),
        low: [centre[0] - radius, centre[1] - radius, centre[2] - radius],
        high: [centre[0] + radius, centre[1] + radius, centre[2] + radius],
        stride: (FLOATS_PER_VERTEX * FLOAT_BYTES) as u32,
        colour_offset: -1,
    };
    for segment in 0..RING_SEGMENTS {
        let start = segment as f32 / RING_SEGMENTS as f32 * 2.0 * std::f32::consts::PI;
        let end = (segment + 1) as f32 / RING_SEGMENTS as f32 * 2.0 * std::f32::consts::PI;
        let quad = [
            point_at(start, inner),
            point_at(start, radius),
            point_at(end, radius),
            point_at(end, inner),
        ];
        for corner in [0, 1, 2, 0, 2, 3] {
            push_vertex(&mut frame.vertices, quad[corner], normal);
        }
    }
    frame
}

/// Nearest triangle hit for a ray, in printer coordinates: the hit point and the
/// ray parameter, which is negative when the ray missed everything.
///
/// `positions` is nine `f32` per triangle, the shape [`mesh_frame`] reports.
pub fn raycast(positions: &[f32], origin: [f32; 3], direction: [f32; 3]) -> [f32; 4] {
    let direction = unit(direction);
    let mut nearest = -1.0f32;
    let mut hit = [0.0f32; 3];

    for triangle in positions.chunks_exact(9) {
        let p0 = [triangle[0], triangle[1], triangle[2]];
        let p1 = [triangle[3], triangle[4], triangle[5]];
        let p2 = [triangle[6], triangle[7], triangle[8]];

        let edge1 = sub(p1, p0);
        let edge2 = sub(p2, p0);
        let pvec = cross(direction, edge2);
        let det = dot(edge1, pvec);
        if det.abs() < 1e-9 {
            continue;
        }

        let inv_det = 1.0 / det;
        let tvec = sub(origin, p0);
        let u = dot(tvec, pvec) * inv_det;
        if u < 0.0 || u > 1.0 {
            continue;
        }

        let qvec = cross(tvec, edge1);
        let v = dot(direction, qvec) * inv_det;
        if v < 0.0 || u + v > 1.0 {
            continue;
        }

        let t = dot(edge2, qvec) * inv_det;
        if t > RAY_EPSILON && (nearest < 0.0 || t < nearest) {
            nearest = t;
            hit = add(origin, scale(direction, t));
        }
    }

    [hit[0], hit[1], hit[2], nearest]
}

/// The lower and upper corner of everything in the array, in the order the frame
/// reports them. All zero when there is nothing in it.
fn bounds_of(vertices: &[f32]) -> ([f32; 3], [f32; 3]) {
    let Some((first, rest)) = vertices.split_first_chunk::<3>() else {
        return ([0.0; 3], [0.0; 3]);
    };
    let mut low = *first;
    let mut high = *first;
    for vertex in rest.chunks_exact(3) {
        for axis in 0..3 {
            low[axis] = low[axis].min(vertex[axis]);
            high[axis] = high[axis].max(vertex[axis]);
        }
    }
    (low, high)
}

/// The three corners of a face, or `None` when it names a vertex the array does
/// not have: a face is the one part of a mesh that can disagree with its own
/// vertex table, and the mesh got here from a file the engine parsed.
fn corners(vertices: &[f32], face: &[u32]) -> Option<[[f32; 3]; 3]> {
    let mut out = [[0.0f32; 3]; 3];
    for (corner, slot) in out.iter_mut().enumerate() {
        let base = *face.get(corner)? as usize * 3;
        *slot = [
            *vertices.get(base)?,
            *vertices.get(base + 1)?,
            *vertices.get(base + 2)?,
        ];
    }
    Some(out)
}

fn unit_normal(p0: [f32; 3], p1: [f32; 3], p2: [f32; 3]) -> Option<[f32; 3]> {
    let normal = cross(sub(p1, p0), sub(p2, p0));
    let length = dot(normal, normal).sqrt();
    (length != 0.0).then(|| scale(normal, 1.0 / length))
}

fn push_vertex(out: &mut Vec<u8>, position: [f32; 3], normal: [f32; 3]) {
    for value in position.iter().chain(normal.iter()) {
        out.extend_from_slice(&value.to_ne_bytes());
    }
}

/// A unit vector, and a zero vector left as it is: the caller hands over the
/// facet normal a pick reported, which is zero only when nothing was picked.
fn unit(vector: [f32; 3]) -> [f32; 3] {
    let length = dot(vector, vector).sqrt();
    if length == 0.0 {
        return vector;
    }
    scale(vector, 1.0 / length)
}

fn add(a: [f32; 3], b: [f32; 3]) -> [f32; 3] {
    [a[0] + b[0], a[1] + b[1], a[2] + b[2]]
}

fn sub(a: [f32; 3], b: [f32; 3]) -> [f32; 3] {
    [a[0] - b[0], a[1] - b[1], a[2] - b[2]]
}

fn scale(vector: [f32; 3], factor: f32) -> [f32; 3] {
    [vector[0] * factor, vector[1] * factor, vector[2] * factor]
}

fn dot(a: [f32; 3], b: [f32; 3]) -> f32 {
    a[0] * b[0] + a[1] * b[1] + a[2] * b[2]
}

fn cross(a: [f32; 3], b: [f32; 3]) -> [f32; 3] {
    [
        a[1] * b[2] - a[2] * b[1],
        a[2] * b[0] - a[0] * b[2],
        a[0] * b[1] - a[1] * b[0],
    ]
}

#[cfg(test)]
mod tests {
    use super::*;

    const TRIANGLE: [f32; 9] = [
        0.0, 0.0, 0.0, //
        10.0, 0.0, 0.0, //
        0.0, 10.0, 0.0,
    ];

    fn read(vertex: &[u8], at: usize) -> f32 {
        f32::from_ne_bytes(vertex[at..at + 4].try_into().expect("four bytes"))
    }

    #[test]
    fn a_face_is_three_vertices_of_position_then_normal() {
        let frame = mesh_frame(&TRIANGLE, &[0, 1, 2]);

        assert_eq!(frame.stride, 24);
        assert_eq!(frame.colour_offset, -1);
        assert_eq!(frame.vertices.len(), 3 * 24);
        assert_eq!(frame.positions.len(), 9);
        assert_eq!(frame.low, [0.0, 0.0, 0.0]);
        assert_eq!(frame.high, [10.0, 10.0, 0.0]);
        assert_eq!(read(&frame.vertices, 0), 0.0);
        assert_eq!(read(&frame.vertices, 8), 0.0);
        assert_eq!(read(&frame.vertices, 20), 1.0);
        assert_eq!(frame.positions, TRIANGLE.to_vec());
    }

    #[test]
    fn a_face_with_no_area_draws_nothing_and_is_not_hit() {
        let frame = mesh_frame(&TRIANGLE, &[0, 0, 1]);

        assert!(frame.vertices.is_empty());
        assert!(frame.positions.is_empty());
        let missed = raycast(&frame.positions, [1.0, 1.0, 10.0], [0.0, 0.0, -1.0]);
        assert!(missed[3] < 0.0);
    }

    #[test]
    fn a_painted_face_carries_its_colour_and_leaves_the_raycast_copy_alone() {
        let frame = coloured_mesh_frame(&TRIANGLE, &[0, 1, 2], &[1.0, 0.5, 0.25, 1.0]);

        assert_eq!(frame.stride, 40);
        assert_eq!(frame.colour_offset, 24);
        assert_eq!(frame.vertices.len(), 3 * 40);
        assert!(frame.positions.is_empty());
        assert_eq!(read(&frame.vertices, 24), 1.0);
        assert_eq!(read(&frame.vertices, 32), 0.25);
        assert_eq!(read(&frame.vertices, 36), 1.0);

        let skipped = coloured_mesh_frame(&TRIANGLE, &[0, 1, 2], &[1.0, 0.5, 0.25, 0.0]);
        assert!(skipped.vertices.is_empty());
        assert!(skipped.positions.is_empty());
    }

    #[test]
    fn a_ring_is_sixty_four_quads_around_its_own_centre() {
        let frame = ring_frame([1.0, 2.0, 3.0], [0.0, 0.0, 1.0], 5.0, 1.0);

        assert_eq!(frame.vertices.len(), RING_SEGMENTS * 6 * 24);
        assert!(frame.positions.is_empty());
        assert_eq!(frame.low, [-4.0, -3.0, -2.0]);
        assert_eq!(frame.high, [6.0, 7.0, 8.0]);
        let lifted = read(&frame.vertices, 8);
        assert!((lifted - 3.06).abs() < 1e-5);
    }

    #[test]
    fn a_ray_hits_the_face_it_was_aimed_at() {
        let frame = mesh_frame(&TRIANGLE, &[0, 1, 2]);

        let hit = raycast(&frame.positions, [5.0, 2.5, 10.0], [0.0, 0.0, -1.0]);
        assert!((hit[0] - 5.0).abs() < 1e-4);
        assert!((hit[1] - 2.5).abs() < 1e-4);
        assert!(hit[2].abs() < 1e-4);
        assert!((hit[3] - 10.0).abs() < 1e-4);

        let beside = raycast(&frame.positions, [5.0, 7.5, 10.0], [0.0, 0.0, -1.0]);
        assert!(beside[3] < 0.0);

        let behind = raycast(&frame.positions, [5.0, 2.5, -10.0], [0.0, 0.0, -1.0]);
        assert!(behind[3] < 0.0);
    }

    #[test]
    fn the_nearest_face_is_the_one_reported() {
        let mut stacked = TRIANGLE.to_vec();
        stacked.extend_from_slice(&[0.0, 0.0, 20.0, 10.0, 0.0, 20.0, 0.0, 10.0, 20.0]);
        let frame = mesh_frame(&stacked, &[0, 1, 2, 3, 4, 5]);

        let hit = raycast(&frame.positions, [2.0, 2.0, 100.0], [0.0, 0.0, -1.0]);
        assert!((hit[2] - 20.0).abs() < 1e-4);
        assert!((hit[3] - 80.0).abs() < 1e-4);
    }
}
