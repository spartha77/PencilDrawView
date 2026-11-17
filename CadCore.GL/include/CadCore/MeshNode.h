#pragma once
// =============================================================================
// CadCore.GL / MeshNode.h
//
// Triangle mesh node. Used for
//   * B-Rep bodies  : the CAD kernel tessellates each face into triangles and
//                     discretises each topological EDGE into a polyline. Both
//                     are handed to this node; the edge polylines are what the
//                     "bold edges" overlay draws (not the triangle edges).
//   * STL files     : triangles only. Edges are derived automatically as
//                     "feature edges" - see ComputeFeatureEdges().
//
// GPU LAYOUT
//   Two VAOs sharing nothing but the idea:
//     surface VAO : slot 0 = positions (vec3), slot 1 = normals (vec3), + indices
//     edge VAO    : slot 0 = line-segment end points (vec3, 2 per segment)
//   Buffers are uploaded lazily on the first draw after a change.
// =============================================================================

#include "CadCore/GpuObjects.h"
#include "CadCore/Material.h"
#include "CadCore/SceneNode.h"
#include <cstdint>

namespace CadCore {

enum class MeshKind : int {
    BRep    = 0,
    Stl     = 1,
    Generic = 2
};

class MeshNode : public Node {
public:
    MeshNode(std::string name, MeshKind kind)
        : Node(NodeType::Mesh, std::move(name)), m_kind(kind) {}

    MeshKind Kind() const { return m_kind; }

    Material material;

    /// Sets the surface triangles.
    ///   positions : x,y,z per vertex
    ///   normals   : nx,ny,nz per vertex. Pass an empty vector to have smooth
    ///               normals computed from the triangles.
    ///   indices   : three vertex indices per triangle
    void SetGeometry(std::vector<float> positions, std::vector<float> normals,
                     std::vector<std::uint32_t> indices);

    /// Sets the edge overlay as independent line segments: x,y,z of the start
    /// point followed by x,y,z of the end point, for every segment.
    void SetEdges(std::vector<float> segmentPoints);

    /// Derives the edge overlay from the triangles. An edge is kept when it is
    /// a boundary (one triangle), non-manifold, or when the two triangles that
    /// share it meet at more than `creaseAngleDeg`. Smoothly curved regions
    /// therefore show no edges, sharp corners and outlines do.
    void ComputeFeatureEdges(float creaseAngleDeg = 30.0f);

    bool HasEdges() const { return !m_edges.empty(); }
    std::size_t TriangleCount() const { return m_indices.size() / 3; }

    AABB LocalBounds() const override { return m_bounds; }

    // CPU copy of the triangles (read by picking).
    const std::vector<float>&         Positions() const { return m_positions; }
    const std::vector<std::uint32_t>& Indices()   const { return m_indices; }

    // ---- topology: faces, edges, vertices (used for picking) ------------------
    //
    // A mesh is only triangles and line segments. To pick a FACE, an EDGE or a
    // VERTEX the way a CAD model has them, the triangles and segments must be
    // grouped:
    //   face   = set of triangles         (one B-Rep surface)
    //   edge   = chain of line segments   (one B-Rep curve)
    //   vertex = point where edges end or turn a corner
    // BuildTopology() derives these groups. When the CAD kernel knows the real
    // topology, pass it with SetFaceIds / SetVertices BEFORE BuildTopology.

    /// Real face number of every triangle (one entry per triangle, any numbers).
    void SetFaceIds(std::vector<std::uint32_t> faceOfTriangle);
    /// Real vertex positions: x,y,z per vertex.
    void SetVertices(std::vector<float> xyz);

    /// Groups triangles into faces and edge segments into edges, and finds the
    /// vertices. Without SetFaceIds, a face is a patch of triangles that meet
    /// at less than `creaseAngleDeg`. Call again after changing the geometry.
    void BuildTopology(float creaseAngleDeg = 30.0f);

    std::size_t FaceCount()   const { return m_faceCount; }
    std::size_t EdgeCount()   const { return m_edgeCount; }
    std::size_t VertexCount() const { return m_vertices.size() / 3; }

    /// Face index of a triangle (triangle = position in the index list / 3), or -1.
    int FaceOfTriangle(std::size_t triangle) const {
        return triangle < m_faceOfTriangle.size() ? static_cast<int>(m_faceOfTriangle[triangle]) : -1;
    }

    /// Result of a nearest-vertex / nearest-edge query (local coordinates).
    struct Nearest {
        int   index = -1;          ///< vertex or edge index; -1 = the mesh has none
        Vec3  point;               ///< the vertex, or the closest point on the edge
        float distance = 1e30f;    ///< distance from the query point
    };
    Nearest NearestVertex(const Vec3& p) const;
    Nearest NearestEdge(const Vec3& p) const;

    // ---- used by the renderer (GL context must be current) --------------------
    void DrawTriangles();
    void DrawEdges();
    /// Draws only the listed faces / edges (selection highlight). The lists
    /// must be sorted; the GPU buffer is rebuilt only when a list changes.
    void DrawFaces(const std::vector<int>& faceIndices);
    void DrawEdgeSet(const std::vector<int>& edgeIndices);

private:
    void UploadIfNeeded();

    MeshKind m_kind;

    // CPU copies (kept so feature edges can be recomputed and for re-upload).
    std::vector<float>         m_positions;
    std::vector<float>         m_normals;
    std::vector<std::uint32_t> m_indices;
    std::vector<float>         m_edges;
    AABB                       m_bounds;

    // GPU objects.
    VertexArray m_surfaceVao, m_edgeVao;
    GpuBuffer   m_positionVbo, m_normalVbo, m_indexBuffer, m_edgeVbo;
    bool        m_surfaceDirty = true;
    bool        m_edgesDirty   = true;

    // Topology (see BuildTopology). The triangle and segment order is never
    // changed, so indices given by the caller stay valid.
    std::vector<std::uint32_t> m_userFaceIds;      // from SetFaceIds (may be empty)
    std::vector<float>         m_userVertices;     // from SetVertices (may be empty)
    std::vector<std::uint32_t> m_faceOfTriangle;   // face index per triangle
    std::vector<std::uint32_t> m_edgeOfSegment;    // edge index per line segment
    std::vector<float>         m_vertices;         // x,y,z per vertex
    std::size_t                m_faceCount = 0, m_edgeCount = 0;

    // Small GPU buffers holding just the highlighted faces / edges. They are
    // refilled only when a different set is asked for.
    VertexArray m_faceVao, m_edgeHighlightVao;
    GpuBuffer   m_faceIndexBuffer, m_edgeHighlightVbo;
    std::vector<int> m_cachedFaces, m_cachedEdges;      // what the buffers hold now
    bool        m_faceCacheValid = false, m_edgeCacheValid = false;
    int         m_cachedFaceIndexCount = 0, m_cachedEdgeVertexCount = 0;
};

} // namespace CadCore
