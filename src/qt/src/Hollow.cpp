// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Curtis Webb

#include "Hollow.hpp"

#include <libslic3r/SLA/Hollowing.hpp>

#include <algorithm>
#include <exception>

namespace next_layer {

bool hollowMesh(Slic3r::Domain::TriangleMesh& mesh, double thickness)
{
    Slic3r::sla::HollowingConfig config;
    config.min_thickness = std::clamp(thickness, 0.2, 20.0);
    config.quality = 0.5;
    config.closing_distance = 0.5;
    config.enabled = true;

    try {
        Slic3r::sla::hollow_mesh(mesh, config);
    } catch (const std::exception&) {
        return false;
    }
    return !mesh.its.indices.empty();
}

}
