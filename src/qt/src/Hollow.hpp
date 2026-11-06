#pragma once

#include <Slic3r/Domain/TriangleMesh.hpp>

namespace next_layer {

/// Hollow a solid through the engine's own voxel hollowing: the result is the
/// original surface plus an inner wall `thickness` millimetres inside it, so the
/// slicer sees a shell with nothing but walls. Returns false when the voxel pass
/// produced nothing usable, leaving `mesh` untouched.
bool hollowMesh(Slic3r::Domain::TriangleMesh& mesh, double thickness);

}
