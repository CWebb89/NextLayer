#include "Orient.hpp"

#include <Eigen/Geometry>

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace next_layer {

namespace {

using Slic3r::Domain::Transform3d;
using Slic3r::Domain::TriangleMesh;
using Vec3d = Slic3r::Domain::Vec3d;

constexpr double kDownTolerance = -0.99;
constexpr double kOverhangLimit = -0.55;
constexpr double kSupportWeight = 0.35;

Transform3d withRotation(const Transform3d& current, const Eigen::Matrix3d& rotation)
{
    Transform3d result = Transform3d::Identity();
    result.linear() = rotation * current.linear();
    result.translation() = current.translation();
    return result;
}

Eigen::Matrix3d alignTo(Vec3d normal)
{
    if (normal.norm() < 1e-12)
        return Eigen::Matrix3d::Identity();
    const Vec3d target(0.0, 0.0, -1.0);
    const Eigen::Quaterniond rotation =
        Eigen::Quaterniond::FromTwoVectors(normal.normalized(), target);
    return rotation.toRotationMatrix();
}

Vec3d facetNormal(const TriangleMesh& mesh, int facet)
{
    const auto& face = mesh.its.indices[static_cast<size_t>(facet)];
    const Vec3d a = mesh.its.vertices[face[0]].cast<double>();
    const Vec3d b = mesh.its.vertices[face[1]].cast<double>();
    const Vec3d c = mesh.its.vertices[face[2]].cast<double>();
    const Vec3d normal = (b - a).cross(c - a);
    const double length = normal.norm();
    return length < 1e-12 ? Vec3d(0.0, 0.0, 0.0) : normal / length;
}

double facetArea(const TriangleMesh& mesh, int facet)
{
    const auto& face = mesh.its.indices[static_cast<size_t>(facet)];
    const Vec3d a = mesh.its.vertices[face[0]].cast<double>();
    const Vec3d b = mesh.its.vertices[face[1]].cast<double>();
    const Vec3d c = mesh.its.vertices[face[2]].cast<double>();
    return (b - a).cross(c - a).norm() * 0.5;
}

struct Score
{
    double height = std::numeric_limits<double>::max();
    double overhang = 0.0;
    double contact = 0.0;

    double value() const { return height + kSupportWeight * overhang - 0.05 * contact; }
};

Score scoreOrientation(const Transform3d& transform, const TriangleMesh& mesh,
                       const std::vector<Vec3d>& normals, const std::vector<double>& areas)
{
    double minZ = std::numeric_limits<double>::max();
    double maxZ = std::numeric_limits<double>::lowest();
    for (const auto& vertex : mesh.its.vertices) {
        const double z = (transform * vertex.cast<double>()).z();
        minZ = std::min(minZ, z);
        maxZ = std::max(maxZ, z);
    }

    Score score;
    if (mesh.its.vertices.empty())
        return score;
    score.height = maxZ - minZ;

    const Eigen::Matrix3d rotation = transform.linear();
    for (size_t index = 0; index < normals.size(); ++index) {
        const Vec3d normal = rotation * normals[index];
        if (normal.z() <= kOverhangLimit)
            score.overhang += areas[index];
        if (normal.z() <= kDownTolerance)
            score.contact += areas[index];
    }
    return score;
}

}

Transform3d layFacetFlat(const TriangleMesh& mesh, const Transform3d& current, int facet)
{
    if (facet < 0 || static_cast<size_t>(facet) >= mesh.its.indices.size())
        return current;
    const Vec3d normal = facetNormal(mesh, facet);
    if (normal.norm() < 1e-9)
        return current;
    return withRotation(current, alignTo(normal));
}

std::optional<Transform3d> bestRestingRotation(const TriangleMesh& mesh,
                                               const Transform3d& current)
{
    const size_t facets = mesh.its.indices.size();
    if (facets == 0)
        return std::nullopt;

    std::vector<Vec3d> normals(facets);
    std::vector<double> areas(facets, 0.0);
    for (size_t facet = 0; facet < facets; ++facet) {
        normals[facet] = facetNormal(mesh, static_cast<int>(facet));
        areas[facet] = facetArea(mesh, static_cast<int>(facet));
    }

    struct Candidate
    {
        Vec3d normal;
        double area = 0.0;
    };
    std::vector<Candidate> candidates;
    for (size_t facet = 0; facet < facets; ++facet) {
        const Vec3d normal = normals[facet];
        if (normal.norm() < 1e-9)
            continue;
        auto match = std::find_if(candidates.begin(), candidates.end(),
                                  [&normal](const Candidate& candidate) {
                                      return candidate.normal.dot(normal) > 0.9995;
                                  });
        if (match == candidates.end())
            candidates.push_back(Candidate{normal, areas[facet]});
        else
            match->area += areas[facet];
    }
    if (candidates.empty())
        return std::nullopt;

    std::sort(candidates.begin(), candidates.end(),
              [](const Candidate& a, const Candidate& b) { return a.area > b.area; });
    if (candidates.size() > 64)
        candidates.resize(64);

    std::optional<Transform3d> best;
    Score bestScore;
    for (const Candidate& candidate : candidates) {
        const Transform3d transformed = withRotation(current, alignTo(candidate.normal));
        const Score score = scoreOrientation(transformed, mesh, normals, areas);
        if (!best.has_value() || score.value() < bestScore.value()) {
            best = transformed;
            bestScore = score;
        }
    }

    return best;
}

}
