#pragma once
// =============================================================================
// CadCore.GL / Renderer.h
//
// Draws a Scene through a Camera. One call to Render() produces one frame:
//
//   1. walk the scene graph, collect visible nodes (invisible subtrees skipped)
//   2. opaque surfaces            (lit, pushed back slightly so edges win)
//   3. edge overlay / wireframe   (thick lines via geometry shader)
//   4. point clouds
//   5. reference grid             (blended, does not write depth)
//   6. transparent surfaces       (sorted back to front)
//   7. axis arrows at the origin  (constant size on screen, always on top)
//   8. resolve the multisampled image into the window's framebuffer
//
// The frame is rendered into an off-screen MULTISAMPLED framebuffer (4x MSAA)
// and then copied ("blitted") to the window. That gives smooth edges without
// needing a multisample pixel format on the window itself.
// =============================================================================

#include "CadCore/Camera.h"
#include "CadCore/GpuObjects.h"
#include "CadCore/Light.h"
#include "CadCore/Scene.h"
#include "CadCore/ShaderProgram.h"
#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace CadCore {

class MeshNode;
class PointCloudNode;

enum class DisplayMode : int {
    Shaded          = 0,  ///< lit surfaces only
    ShadedWithEdges = 1,  ///< lit surfaces + edge overlay (the usual CAD look)
    Wireframe       = 2   ///< edges only
};

/// What kind of entity was picked / is selected.
enum class PickKind : int {
    None   = 0,
    Vertex = 1,   ///< a corner point (or one point of a point cloud)
    Edge   = 2,   ///< one edge curve
    Face   = 3,   ///< one surface
    Solid  = 4    ///< the whole part
};

/// One selected entity. Several can be selected at once.
///   Solid               : the node `nodeId`; for a group, everything below it
///   Vertex / Edge / Face: entity number `index` inside the node `nodeId`
struct SelectedEntity {
    PickKind kind   = PickKind::None;
    int      nodeId = -1;
    int      index  = -1;   ///< vertex / edge / face index inside the node (-1 for Solid)
    Vec3     point;         ///< world position of the vertex or of the snapped point

    /// Same entity? (The snapped point does not count: two picks on one edge
    /// are the same edge.)
    bool SameEntity(const SelectedEntity& o) const {
        return kind == o.kind && nodeId == o.nodeId && index == o.index;
    }
};

/// Everything that controls how a frame looks. Plain data - change it any time.
struct RenderSettings {
    DisplayMode displayMode = DisplayMode::ShadedWithEdges;
    float edgeWidthPx = 1.5f;                 ///< raise (e.g. 3.5) for BOLD edges
    Vec3  background{0.20f, 0.22f, 0.25f};

    // Reference grid in the XY plane, centred on the world origin.
    bool  showGrid = true;
    float gridSizeMm = 1000.0f;               ///< full width; 10000 gives a 10 m x 10 m grid
    float gridMinorSpacingMm = 1.0f;          ///< finest line spacing
    Vec3  gridColor{0.62f, 0.64f, 0.70f};

    // Axis arrows at the origin. The arrow model is 10 mm shaft (4 mm dia) +
    // cone; it is rescaled every frame so it always covers the same number of
    // pixels: on screen 1 model-mm = axisPixelsPerMm pixels.
    bool  showAxes = true;
    float axisPixelsPerMm = 6.0f;

    // Selection: every entity listed here is drawn in `highlightColor`.
    std::vector<SelectedEntity> selection;
    Vec3 highlightColor{0.098f, 0.435f, 0.549f};   // dark teal-blue

    LightingSettings lighting;
};

class Renderer {
public:
    Renderer() = default;
    ~Renderer() { Shutdown(); }

    /// Creates shaders and built-in geometry. The GL context must be current
    /// and GLEW initialised. Throws std::runtime_error on failure.
    void Initialize();

    /// Sets the output size in pixels (recreates the off-screen framebuffer).
    void Resize(int widthPx, int heightPx);

    /// Renders one frame into framebuffer `targetFbo` (0 = the window).
    /// Updates the camera's clip range from the scene bounds.
    void Render(Scene& scene, Camera& camera, const RenderSettings& settings,
                std::uint32_t targetFbo = 0);

    /// Frees all GL objects (context must be current).
    void Shutdown();

    const std::string& GLVersion() const { return m_glVersion; }

private:
    struct DrawItem {
        Node* node;
        Mat4  world;
        float depth;   // distance along the view direction (for sorting)
        bool  selected; // this node or one of its ancestors is the selected node
    };

    void CreateFramebuffer();
    void DestroyFramebuffer();
    void Collect(Node* node, const Mat4& parentWorld, const Camera::Frame& frame, bool parentSelected);
    /// Draws a mesh's surface; with `faces` only those faces, opaque.
    void DrawSurface(const DrawItem& item, const std::vector<int>* faces = nullptr);

    // Shader programs.
    ShaderProgram m_meshProgram, m_gizmoProgram, m_lineProgram, m_pointProgram, m_gridProgram;

    // Per-frame uniform buffers (binding 0 and 1).
    GpuBuffer m_frameUbo, m_lightUbo;

    // Built-in geometry.
    VertexArray m_gridVao;   GpuBuffer m_gridVbo;
    VertexArray m_markerVao; GpuBuffer m_markerVbo;   // one point: the selection dot
    VertexArray m_arrowVao;  GpuBuffer m_arrowPositions, m_arrowNormals, m_arrowIndices;
    int m_arrowIndexCount = 0;

    // Off-screen multisampled render target.
    std::uint32_t m_fbo = 0, m_colorRb = 0, m_depthRb = 0;
    int m_width = 1, m_height = 1, m_samples = 4;

    // Draw lists, refilled every frame by Collect().
    std::vector<DrawItem> m_opaque, m_transparent, m_points, m_curves;

    // Selection of the frame being drawn, regrouped from RenderSettings::selection:
    //   m_selectedSolids : nodes highlighted as a whole
    //   m_partial        : per node, its selected faces / edges and the dots to draw
    struct Partial {
        Node* node = nullptr;            // set by Collect() when the node is visible
        Mat4  world;
        std::vector<int>  faces, edges;  // sorted entity indices
        std::vector<Vec3> dots;          // world positions of marker dots
        std::vector<char> dotIsVertex;   // 1 = vertex (large dot), 0 = snapped point on an edge
    };
    std::unordered_set<int>          m_selectedSolids;
    std::unordered_map<int, Partial> m_partial;
    Vec3 m_highlight;

    std::string m_glVersion;
    bool m_initialized = false;
};

} // namespace CadCore
