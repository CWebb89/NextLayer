// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Curtis Webb

#include "Cut.hpp"

#include <Slic3r/Biz/Algorithms/ClipperUtils.hpp>
#include <Slic3r/Biz/Algorithms/Tesselate.hpp>

#include <libslic3r/TriangleMeshSlicer.hpp>

#include <algorithm>
#include <array>
#include <vector>

namespace next_layer {

namespace {

using Slic3r::Domain::TriangleMesh;
using Slic3r::Domain::Vec3d;
using Slic3r::Domain::Index3;

struct Soup
{
    TriangleMesh mesh;

    int vertex(const Vec3d& p)
    {
        mesh.its.vertices.push_back(Slic3r::Domain::Vec3f(p.x(), p.y(), p.z()));
        return static_cast<int>(mesh.its.vertices.size()) - 1;
    }

    void triangle(const Vec3d& a, const Vec3d& b, const Vec3d& c)
    {
        mesh.its.indices.push_back(Index3{vertex(a), vertex(b), vertex(c)});
    }

    void soup(const std::vector<Vec3d>& points)
    {
        for (size_t i = 0; i + 2 < points.size(); i += 3)
            triangle(points[i], points[i + 1], points[i + 2]);
    }
};

// A triangle against a horizontal plane: the part below it, as zero, one or two
// triangles. Cutting a triangle always yields a fan of at most three vertices on
// each side, so the walk keeps the polygon order and never needs a lookup table.
void appendBelow(const Vec3d& a, const Vec3d& b, const Vec3d& c, double z, Soup& below,
                 Soup& above)
{
    std::array<Vec3d, 3> points{a, b, c};
    std::vector<Vec3d> low;
    std::vector<Vec3d> high;

    for (int i = 0; i < 3; ++i) {
        const Vec3d& current = points[i];
        const Vec3d& next = points[(i + 1) % 3];
        const bool currentLow = current.z() <= z;
        const bool nextLow = next.z() <= z;

        if (currentLow)
            low.push_back(current);
        else
            high.push_back(current);

        if (currentLow != nextLow) {
            const double t = (z - current.z()) / (next.z() - current.z());
            const Vec3d crossing = current + (next - current) * t;
            low.push_back(crossing);
            high.push_back(crossing);
        }
    }

    for (size_t i = 1; i + 1 < low.size(); ++i)
        below.triangle(low[0], low[i], low[i + 1]);
    for (size_t i = 1; i + 1 < high.size(); ++i)
        above.triangle(high[0], high[i], high[i + 1]);
}

}

bool cutMesh(const TriangleMesh& mesh, double z, CutHalves& out)
{
    out = CutHalves();

    if (mesh.its.indices.empty())
        return false;

    double lowest = mesh.its.vertices.front().z();
    double highest = lowest;
    for (const auto& vertex : mesh.its.vertices) {
        lowest = std::min(lowest, double(vertex.z()));
        highest = std::max(highest, double(vertex.z()));
    }
    if (z <= lowest || z >= highest)
        return false;

    Soup below;
    Soup above;
    for (const auto& face : mesh.its.indices) {
        const auto& a = mesh.its.vertices[face[0]];
        const auto& b = mesh.its.vertices[face[1]];
        const auto& c = mesh.its.vertices[face[2]];
        appendBelow(Vec3d(a.x(), a.y(), a.z()), Vec3d(b.x(), b.y(), b.z()),
                    Vec3d(c.x(), c.y(), c.z()), z, below, above);
    }

    // The cap is the mesh's own cross-section, so the two halves close exactly
    // where the model is: no invented geometry, no gap at a hole.
    const Slic3r::Domain::Polygons contours =
        Slic3r::slice_mesh(mesh.its, float(z), Slic3r::MeshSlicingParams());
    const Slic3r::Domain::ExPolygons section =
        Slic3r::Biz::Algorithms::ClipperUtils::union_ex(contours);

    below.soup(Slic3r::Biz::Algorithms::Tesselate::triangulate_expolygons_3d(
        section, z, Slic3r::Biz::Algorithms::Tesselate::NORMALS_DOWN));
    above.soup(Slic3r::Biz::Algorithms::Tesselate::triangulate_expolygons_3d(
        section, z, Slic3r::Biz::Algorithms::Tesselate::NORMALS_UP));

    out.lower = std::move(below.mesh);
    out.upper = std::move(above.mesh);
    out.cuts = true;
    return true;
}

}
