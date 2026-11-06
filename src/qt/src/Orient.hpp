#pragma once

#include <Slic3r/Domain/TriangleMesh.hpp>
#include <Slic3r/Domain/Types.hpp>

#include <optional>

namespace next_layer {

/// The rotation that puts the mesh on one of its own flat faces: the face's
/// normal is turned to point straight down and the object's own up-axis is
/// preserved as far as the face allows.
Slic3r::Domain::Transform3d layFacetFlat(const Slic3r::Domain::TriangleMesh& mesh,
                                         const Slic3r::Domain::Transform3d& current,
                                         int facet);

/// The rotation that makes the object sit as low and as support-free as possible:
/// every distinct face normal is tried as a resting surface, and the orientation
/// scoring best on printed height plus overhanging area wins. Returns nothing
/// when the mesh has no faces.
std::optional<Slic3r::Domain::Transform3d> bestRestingRotation(
    const Slic3r::Domain::TriangleMesh& mesh,
    const Slic3r::Domain::Transform3d& current);

}
