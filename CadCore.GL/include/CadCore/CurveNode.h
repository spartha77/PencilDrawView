#pragma once
// =============================================================================
// CadCore.GL / CurveNode.h
//
// A curve in space: a line, an arc, a spline ... anything that is drawn as a
// LINE and has no surface. The engine does not know what kind of curve it is;
// it receives the curve already cut into short straight pieces (a "polyline")
// and draws those with a constant width in pixels.
//
// Unlike the edge lines of a mesh, a curve is drawn in EVERY display mode and
// in its own colour, and it can be picked by clicking near it on screen.
//
// GPU LAYOUT: slot 0 = segment end points (vec3, two per straight piece).
// =============================================================================

#include "CadCore/GpuObjects.h"
#include "CadCore/SceneNode.h"

namespace CadCore {

class CurveNode : public Node {
public:
    explicit CurveNode(std::string name) : Node(NodeType::Curve, std::move(name)) {}

    Vec3  color{0.95f, 0.75f, 0.20f};   ///< line colour
    float widthPx = 2.0f;               ///< line width on screen, in pixels

    /// Sets the curve as a chain of points: x,y,z per point, at least two.
    /// `closed` joins the last point back to the first (a full circle).
    void SetPoints(std::vector<float> points, bool closed);

    /// The chain of points as given (x,y,z per point).
    const std::vector<float>& Points() const { return m_points; }
    bool IsClosed() const { return m_closed; }
    std::size_t PointCount() const { return m_points.size() / 3; }

    AABB LocalBounds() const override { return m_bounds; }

    /// Used by the renderer (GL context must be current).
    void Draw();

private:
    std::vector<float> m_points;        // the chain
    std::vector<float> m_segments;      // the same as independent pieces: 2 points each
    bool        m_closed = false;
    AABB        m_bounds;
    VertexArray m_vao;
    GpuBuffer   m_vbo;
    bool        m_dirty = true;
};

} // namespace CadCore
