#pragma once
// =============================================================================
// CadCore.GL / PointCloudNode.h
//
// Point cloud node (scan data, probe points ...). Drawn as round screen-space
// points. Each point may carry its own colour; otherwise `color` is used.
//
// GPU LAYOUT: slot 0 = positions (vec3), slot 1 = colours (vec3, optional).
// =============================================================================

#include "CadCore/GpuObjects.h"
#include "CadCore/SceneNode.h"

namespace CadCore {

class PointCloudNode : public Node {
public:
    explicit PointCloudNode(std::string name) : Node(NodeType::PointCloud, std::move(name)) {}

    Vec3  color{0.15f, 0.55f, 0.95f};  ///< used when there are no per-point colours
    float pointSizePx = 3.0f;          ///< diameter on screen, in pixels

    /// positions: x,y,z per point. colors: r,g,b (0..1) per point, or empty.
    void SetPoints(std::vector<float> positions, std::vector<float> colors);

    std::size_t PointCount() const { return m_positions.size() / 3; }
    bool HasPerPointColor() const { return !m_colors.empty(); }

    AABB LocalBounds() const override { return m_bounds; }

    /// CPU copy of the points (read by picking).
    const std::vector<float>& Positions() const { return m_positions; }

    /// Used by the renderer (GL context must be current).
    void Draw();

private:
    std::vector<float> m_positions, m_colors;
    AABB        m_bounds;
    VertexArray m_vao;
    GpuBuffer   m_positionVbo, m_colorVbo;
    bool        m_dirty = true;
};

} // namespace CadCore
