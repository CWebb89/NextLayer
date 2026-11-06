#pragma once

#include <Slic3r/Domain/TriangleMesh.hpp>

namespace next_layer {

/// A mesh cut by a horizontal plane, each half closed with a cap. Both halves are
/// in the mesh's own coordinates; the caller keeps whichever it wants.
struct CutHalves
{
    Slic3r::Domain::TriangleMesh lower;
    Slic3r::Domain::TriangleMesh upper;
    bool cuts = false;
};

/// Cuts `mesh` at `z` (mesh coordinates). Every triangle straddling the plane is
/// clipped, and the cross-section the slice produces becomes the cap of both
/// halves -- so neither half is left open. Returns false when the plane misses the
/// mesh entirely, which is not a failure, just nothing to cut.
bool cutMesh(const Slic3r::Domain::TriangleMesh& mesh, double z, CutHalves& out);

}
