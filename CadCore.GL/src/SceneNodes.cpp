// =============================================================================
// CadCore.GL / SceneNodes.cpp - MeshNode and PointCloudNode implementation
// =============================================================================
#include "CadCore/MeshNode.h"
#include "CadCore/PointCloudNode.h"

#include <numeric>
#include <stdexcept>
#include <unordered_map>

namespace CadCore {

   namespace {

      Vec3 PointAt(const std::vector<float>& xyz, std::size_t i) {
         return { xyz[i * 3], xyz[i * 3 + 1], xyz[i * 3 + 2] };
      }

      AABB BoundsOf(const std::vector<float>& xyz) {
         AABB b;
         for (std::size_t i = 0; i + 2 < xyz.size(); i += 3) b.Expand({ xyz[i], xyz[i + 1], xyz[i + 2] });
         return b;
      }

      /// Area-weighted smooth vertex normals.
      std::vector<float> SmoothNormals(const std::vector<float>& pos, const std::vector<std::uint32_t>& idx) {
         std::vector<float> n(pos.size(), 0.0f);
         for (std::size_t t = 0; t + 2 < idx.size(); t += 3) {
            const std::uint32_t i[3] = { idx[t], idx[t + 1], idx[t + 2] };
            // The cross product's length is twice the triangle area, which gives
            // the area weighting for free.
            Vec3 fn = Cross(PointAt(pos, i[1]) - PointAt(pos, i[0]), PointAt(pos, i[2]) - PointAt(pos, i[0]));
            for (std::uint32_t v : i) { n[v * 3] += fn.x; n[v * 3 + 1] += fn.y; n[v * 3 + 2] += fn.z; }
         }
         for (std::size_t v = 0; v + 2 < n.size(); v += 3) {
            Vec3 u = Normalize({ n[v], n[v + 1], n[v + 2] });
            n[v] = u.x; n[v + 1] = u.y; n[v + 2] = u.z;
         }
         return n;
      }

      /// Gives every point an ID such that points at the same position (within
      /// `tolerance`) share one ID. STL files and flat-shaded B-Rep faces repeat a
      /// corner once per triangle; welding is what reveals which triangles and which
      /// edge segments actually touch.
      std::vector<std::uint32_t> WeldPoints(const std::vector<float>& xyz, float tolerance) {
         struct Cell {
            long long x, y, z;
            bool operator==(const Cell& o) const { return x == o.x && y == o.y && z == o.z; }
         };
         struct CellHash {
            std::size_t operator()(const Cell& c) const {
               std::size_t h = static_cast<std::size_t>(c.x) * 73856093u;
               h ^= static_cast<std::size_t>(c.y) * 19349663u;
               h ^= static_cast<std::size_t>(c.z) * 83492791u;
               return h;
            }
         };
         const float inv = 1.0f / tolerance;
         std::unordered_map<Cell, std::uint32_t, CellHash> cells;
         std::vector<std::uint32_t> ids(xyz.size() / 3);
         for (std::size_t i = 0; i < ids.size(); ++i) {
            Cell c{ std::llround(xyz[i * 3] * inv), std::llround(xyz[i * 3 + 1] * inv),
                   std::llround(xyz[i * 3 + 2] * inv) };
            ids[i] = cells.emplace(c, static_cast<std::uint32_t>(cells.size())).first->second;
         }
         return ids;
      }

      /// "Union-find": keeps track of which items belong to the same group.
      /// Unite(a, b) merges two groups; Find(a) returns the group's representative.
      class Groups {
         public:
         explicit Groups(std::size_t n) : m_parent(n) { std::iota(m_parent.begin(), m_parent.end(), 0u); }
         std::uint32_t Find(std::uint32_t a) {
            while (m_parent[a] != a) { m_parent[a] = m_parent[m_parent[a]]; a = m_parent[a]; }
            return a;
         }
         void Unite(std::uint32_t a, std::uint32_t b) { m_parent[Find(a)] = Find(b); }
         private:
         std::vector<std::uint32_t> m_parent;
      };

      /// Renumbers arbitrary group labels to 0, 1, 2 ... in order of first use.
      /// Returns the number of distinct labels.
      std::size_t Compact(std::vector<std::uint32_t>& labels) {
         std::unordered_map<std::uint32_t, std::uint32_t> remap;
         for (std::uint32_t& l : labels)
            l = remap.emplace(l, static_cast<std::uint32_t>(remap.size())).first->second;
         return remap.size();
      }

   } // namespace

   // =============================================================================
   // MeshNode
   // =============================================================================

   void MeshNode::SetGeometry(std::vector<float> positions, std::vector<float> normals,
      std::vector<std::uint32_t> indices) {
      if (positions.size() % 3 != 0) throw std::invalid_argument("Mesh positions must be x,y,z triples.");
      if (indices.size() % 3 != 0)   throw std::invalid_argument("Mesh indices must be triangle triples.");
      if (!normals.empty() && normals.size() != positions.size())
         throw std::invalid_argument("Mesh normals must match the number of positions.");

      // An out-of-range index would make the GPU read past the vertex buffer.
      const std::size_t vertexCount = positions.size() / 3;
      for (std::uint32_t i : indices)
         if (i >= vertexCount) throw std::invalid_argument("Mesh index is out of range.");

      m_positions = std::move(positions);
      m_indices = std::move(indices);
      m_normals = normals.empty() ? SmoothNormals(m_positions, m_indices) : std::move(normals);
      m_bounds = BoundsOf(m_positions);
      m_surfaceDirty = true;
      m_faceOfTriangle.clear();
      m_faceCount = 0;
      m_faceCacheValid = false;
   }

   void MeshNode::SetEdges(std::vector<float> segmentPoints) {
      if (segmentPoints.size() % 6 != 0)
         throw std::invalid_argument("Edge data must be pairs of x,y,z points.");
      m_edges = std::move(segmentPoints);
      m_edgesDirty = true;
      m_edgeOfSegment.clear();
      m_edgeCount = 0;
      m_edgeCacheValid = false;
   }

   void MeshNode::ComputeFeatureEdges(float creaseAngleDeg) {
      m_edges.clear();
      m_edgesDirty = true;
      m_edgeOfSegment.clear();
      m_edgeCount = 0;
      m_edgeCacheValid = false;
      const std::size_t vertexCount = m_positions.size() / 3;
      const std::size_t triCount = m_indices.size() / 3;
      if (triCount == 0) return;

      // --- 1. Weld vertices that share a position --------------------------------
      // STL (and flat-shaded B-Rep faces) duplicate vertices per triangle, so the
      // index buffer alone does not tell us which triangles are neighbours.
      // Positions are snapped to a fine grid and identical cells get one ID.
      struct Cell {
         long long x, y, z;
         bool operator==(const Cell& o) const { return x == o.x && y == o.y && z == o.z; }
      };
      struct CellHash {
         std::size_t operator()(const Cell& c) const {
            std::size_t h = static_cast<std::size_t>(c.x) * 73856093u;
            h ^= static_cast<std::size_t>(c.y) * 19349663u;
            h ^= static_cast<std::size_t>(c.z) * 83492791u;
            return h;
         }
      };
      const float tolerance = std::max(m_bounds.Radius() * 2.0f * 1e-5f, 1e-9f);
      const float inv = 1.0f / tolerance;
      std::unordered_map<Cell, std::uint32_t, CellHash> cells;
      std::vector<std::uint32_t> welded(vertexCount);
      for (std::size_t v = 0; v < vertexCount; ++v) {
         Vec3 p = PointAt(m_positions, v);
         Cell c{ std::llround(p.x * inv), std::llround(p.y * inv), std::llround(p.z * inv) };
         welded[v] = cells.emplace(c, static_cast<std::uint32_t>(cells.size())).first->second;
      }

      // --- 2. Record up to two triangles per (welded) edge -----------------------
      struct EdgeInfo {
         std::uint32_t v0 = 0, v1 = 0;  // original vertex indices (for the positions)
         Vec3 n0, n1;                   // normals of the first two triangles
         int  count = 0;                // number of triangles using this edge
      };
      std::unordered_map<std::uint64_t, EdgeInfo> edges;
      edges.reserve(triCount * 2);

      for (std::size_t t = 0; t < triCount; ++t) {
         const std::uint32_t i[3] = { m_indices[t * 3], m_indices[t * 3 + 1], m_indices[t * 3 + 2] };
         Vec3 raw = Cross(PointAt(m_positions, i[1]) - PointAt(m_positions, i[0]),
            PointAt(m_positions, i[2]) - PointAt(m_positions, i[0]));
         if (Length(raw) < 1e-20f) continue;  // degenerate (zero-area) triangle
         Vec3 fn = Normalize(raw);

         for (int e = 0; e < 3; ++e) {
            std::uint32_t a = i[e], b = i[(e + 1) % 3];
            std::uint32_t wa = welded[a], wb = welded[b];
            if (wa == wb) continue;
            std::uint64_t key = (static_cast<std::uint64_t>(std::min(wa, wb)) << 32) | std::max(wa, wb);
            EdgeInfo& info = edges[key];
            if (info.count == 0) { info.v0 = a; info.v1 = b; info.n0 = fn; }
            else if (info.count == 1) { info.n1 = fn; }
            ++info.count;
         }
      }

      // --- 3. Keep boundary, non-manifold and sharp edges ------------------------
      const float cosCrease = std::cos(DegToRad(creaseAngleDeg));
      for (const auto& kv : edges) {
         const EdgeInfo& e = kv.second;
         const bool sharp = (e.count != 2) || (Dot(e.n0, e.n1) < cosCrease);
         if (!sharp) continue;
         Vec3 a = PointAt(m_positions, e.v0), b = PointAt(m_positions, e.v1);
         m_edges.insert(m_edges.end(), { a.x, a.y, a.z, b.x, b.y, b.z });
      }
   }

   void MeshNode::UploadIfNeeded() {
      if (m_surfaceDirty) {
         m_positionVbo.SetData(m_positions);
         m_normalVbo.SetData(m_normals);
         m_indexBuffer.SetData(m_indices);
         if (!m_surfaceVao.IsCreated()) {
            m_surfaceVao.Create();
            m_surfaceVao.SetVertexBuffer(0, m_positionVbo, 3 * sizeof(float));
            m_surfaceVao.SetVertexBuffer(1, m_normalVbo, 3 * sizeof(float));
            m_surfaceVao.SetIndexBuffer(m_indexBuffer);
            m_surfaceVao.SetFloatAttribute(0, 0, 3);   // layout(location = 0) in vec3 aPosition
            m_surfaceVao.SetFloatAttribute(1, 1, 3);   // layout(location = 1) in vec3 aNormal
         }
         m_surfaceDirty = false;
      }
      if (m_edgesDirty) {
         m_edgeVbo.SetData(m_edges);
         if (!m_edgeVao.IsCreated()) {
            m_edgeVao.Create();
            m_edgeVao.SetVertexBuffer(0, m_edgeVbo, 3 * sizeof(float));
            m_edgeVao.SetFloatAttribute(0, 0, 3);
         }
         m_edgesDirty = false;
      }
   }

   void MeshNode::DrawTriangles() {
      if (m_indices.empty()) return;
      UploadIfNeeded();
      m_surfaceVao.Bind();
      glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(m_indices.size()), GL_UNSIGNED_INT, nullptr);
   }

   void MeshNode::DrawEdges() {
      if (m_edges.empty()) return;
      UploadIfNeeded();
      m_edgeVao.Bind();
      glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(m_edges.size() / 3));
   }

   // -----------------------------------------------------------------------------
   // Topology
   // -----------------------------------------------------------------------------

   void MeshNode::SetFaceIds(std::vector<std::uint32_t> faceOfTriangle) {
      if (!faceOfTriangle.empty() && faceOfTriangle.size() != m_indices.size() / 3)
         throw std::invalid_argument("Face IDs must have one entry per triangle.");
      m_userFaceIds = std::move(faceOfTriangle);
   }

   void MeshNode::SetVertices(std::vector<float> xyz) {
      if (xyz.size() % 3 != 0) throw std::invalid_argument("Vertices must be x,y,z triples.");
      m_userVertices = std::move(xyz);
   }

   void MeshNode::BuildTopology(float creaseAngleDeg) {
      const float tolerance = std::max(m_bounds.Radius() * 2.0f * 1e-5f, 1e-9f);
      const float cosCrease = std::cos(DegToRad(creaseAngleDeg));
      m_faceCacheValid = m_edgeCacheValid = false;

      // ---- FACES: group the triangles -------------------------------------------
      const std::size_t triCount = m_indices.size() / 3;
      if (m_userFaceIds.size() == triCount && triCount > 0) {
         m_faceOfTriangle = m_userFaceIds;            // the kernel told us
      }
      else {
         // Two triangles belong to the same face when they share an edge and
         // meet smoothly (angle below the crease angle). Start with every
         // triangle alone and merge across each smooth shared edge.
         const std::vector<std::uint32_t> welded = WeldPoints(m_positions, tolerance);
         std::vector<Vec3> normal(triCount);
         for (std::size_t t = 0; t < triCount; ++t) {
            const std::uint32_t* i = &m_indices[t * 3];
            normal[t] = Normalize(Cross(PointAt(m_positions, i[1]) - PointAt(m_positions, i[0]),
               PointAt(m_positions, i[2]) - PointAt(m_positions, i[0])));
         }
         struct Sharing { std::uint32_t first = 0; int count = 0; bool smooth = true; };
         std::unordered_map<std::uint64_t, Sharing> shared;
         shared.reserve(triCount * 2);
         Groups groups(triCount);
         for (std::size_t t = 0; t < triCount; ++t) {
            for (int e = 0; e < 3; ++e) {
               const std::uint32_t a = welded[m_indices[t * 3 + e]];
               const std::uint32_t b = welded[m_indices[t * 3 + (e + 1) % 3]];
               if (a == b) continue;
               Sharing& sh = shared[(static_cast<std::uint64_t>(std::min(a, b)) << 32) | std::max(a, b)];
               if (sh.count == 0) sh.first = static_cast<std::uint32_t>(t);
               else if (sh.count == 1 && Dot(normal[sh.first], normal[t]) >= cosCrease)
                  groups.Unite(sh.first, static_cast<std::uint32_t>(t));
               ++sh.count;
            }
         }
         m_faceOfTriangle.resize(triCount);
         for (std::size_t t = 0; t < triCount; ++t) m_faceOfTriangle[t] = groups.Find(static_cast<std::uint32_t>(t));
      }
      m_faceCount = Compact(m_faceOfTriangle);

      // ---- EDGES and VERTICES: group the line segments ---------------------------
      const std::size_t segCount = m_edges.size() / 6;
      const std::vector<std::uint32_t> point = WeldPoints(m_edges, tolerance);   // 2 point IDs per segment

      // For every welded point: how many segment ends meet there, and which two.
      struct Meeting { int count = 0; std::uint32_t end[2] = { 0, 0 }; };   // end = index into `point`
      std::unordered_map<std::uint32_t, Meeting> meetings;
      meetings.reserve(segCount * 2);
      for (std::uint32_t e = 0; e < segCount * 2; ++e) {
         Meeting& m = meetings[point[e]];
         if (m.count < 2) m.end[m.count] = e;
         ++m.count;
      }

      // An edge runs straight through a point where exactly two segments meet
      // without a sharp turn. Everywhere else the edge ends: that is a vertex.
      Groups chains(segCount);
      m_vertices.clear();
      for (const auto& kv : meetings) {
         const Meeting& m = kv.second;
         bool isVertex = (m.count != 2);
         if (!isVertex) {
            // Directions pointing AWAY from the shared point along both segments.
            // `e ^ 1` is the other end of the same segment (ends are stored in pairs).
            const Vec3 here = PointAt(m_edges, m.end[0]);
            const Vec3 a = Normalize(PointAt(m_edges, m.end[0] ^ 1u) - here);
            const Vec3 b = Normalize(PointAt(m_edges, m.end[1] ^ 1u) - here);
            isVertex = Dot(a, b) > -cosCrease;       // not (nearly) opposite = a corner
         }
         if (isVertex) {
            const Vec3 p = PointAt(m_edges, m.end[0]);
            m_vertices.insert(m_vertices.end(), { p.x, p.y, p.z });
         }
         else {
            chains.Unite(m.end[0] / 2, m.end[1] / 2);
         }
      }
      m_edgeOfSegment.resize(segCount);
      for (std::uint32_t s = 0; s < segCount; ++s) m_edgeOfSegment[s] = chains.Find(s);
      m_edgeCount = Compact(m_edgeOfSegment);

      if (!m_userVertices.empty()) m_vertices = m_userVertices;   // the kernel's own vertices win
   }

   MeshNode::Nearest MeshNode::NearestVertex(const Vec3& p) const {
      Nearest best;
      for (std::size_t i = 0; i + 2 < m_vertices.size(); i += 3) {
         const Vec3 v(m_vertices[i], m_vertices[i + 1], m_vertices[i + 2]);
         const float d = Length(v - p);
         if (d < best.distance) { best.distance = d; best.point = v; best.index = static_cast<int>(i / 3); }
      }
      return best;
   }

   MeshNode::Nearest MeshNode::NearestEdge(const Vec3& p) const {
      Nearest best;
      for (std::size_t s = 0; s < m_edgeOfSegment.size(); ++s) {
         // Closest point on the segment a-b: project p onto the line, then clamp
         // the result to the segment's two ends.
         const Vec3 a = PointAt(m_edges, s * 2), b = PointAt(m_edges, s * 2 + 1);
         const Vec3 ab = b - a;
         const float len2 = Dot(ab, ab);
         const float u = len2 > 0.0f ? std::clamp(Dot(p - a, ab) / len2, 0.0f, 1.0f) : 0.0f;
         const Vec3 c = a + ab * u;
         const float d = Length(c - p);
         if (d < best.distance) { best.distance = d; best.point = c; best.index = static_cast<int>(m_edgeOfSegment[s]); }
      }
      return best;
   }

   void MeshNode::DrawFaces(const std::vector<int>& faceIndices) {
      if (faceIndices.empty() || m_faceOfTriangle.empty()) return;
      UploadIfNeeded();
      if (!m_faceCacheValid || faceIndices != m_cachedFaces) {
         // Mark the wanted faces, then collect the indices of their triangles.
         std::vector<char> wanted(m_faceCount, 0);
         for (int f : faceIndices)
            if (f >= 0 && static_cast<std::size_t>(f) < m_faceCount) wanted[static_cast<std::size_t>(f)] = 1;
         std::vector<std::uint32_t> indices;
         for (std::size_t t = 0; t < m_faceOfTriangle.size(); ++t)
            if (wanted[m_faceOfTriangle[t]])
               indices.insert(indices.end(), { m_indices[t * 3], m_indices[t * 3 + 1], m_indices[t * 3 + 2] });
         m_faceIndexBuffer.SetData(indices, GL_DYNAMIC_DRAW);
         if (!m_faceVao.IsCreated()) {
            // Same vertex buffers as the whole mesh, different index buffer.
            m_faceVao.Create();
            m_faceVao.SetVertexBuffer(0, m_positionVbo, 3 * sizeof(float));
            m_faceVao.SetVertexBuffer(1, m_normalVbo, 3 * sizeof(float));
            m_faceVao.SetFloatAttribute(0, 0, 3);
            m_faceVao.SetFloatAttribute(1, 1, 3);
         }
         m_faceVao.SetIndexBuffer(m_faceIndexBuffer);
         m_cachedFaces = faceIndices;
         m_faceCacheValid = true;
         m_cachedFaceIndexCount = static_cast<int>(indices.size());
      }
      if (m_cachedFaceIndexCount == 0) return;
      m_faceVao.Bind();
      glDrawElements(GL_TRIANGLES, m_cachedFaceIndexCount, GL_UNSIGNED_INT, nullptr);
   }

   void MeshNode::DrawEdgeSet(const std::vector<int>& edgeIndices) {
      if (edgeIndices.empty() || m_edgeOfSegment.empty()) return;
      if (!m_edgeCacheValid || edgeIndices != m_cachedEdges) {
         std::vector<char> wanted(m_edgeCount, 0);
         for (int e : edgeIndices)
            if (e >= 0 && static_cast<std::size_t>(e) < m_edgeCount) wanted[static_cast<std::size_t>(e)] = 1;
         std::vector<float> points;
         for (std::size_t s = 0; s < m_edgeOfSegment.size(); ++s)
            if (wanted[m_edgeOfSegment[s]])
               points.insert(points.end(), m_edges.begin() + s * 6, m_edges.begin() + s * 6 + 6);
         m_edgeHighlightVbo.SetData(points, GL_DYNAMIC_DRAW);
         if (!m_edgeHighlightVao.IsCreated()) {
            m_edgeHighlightVao.Create();
            m_edgeHighlightVao.SetFloatAttribute(0, 0, 3);
         }
         m_edgeHighlightVao.SetVertexBuffer(0, m_edgeHighlightVbo, 3 * sizeof(float));
         m_cachedEdges = edgeIndices;
         m_edgeCacheValid = true;
         m_cachedEdgeVertexCount = static_cast<int>(points.size() / 3);
      }
      if (m_cachedEdgeVertexCount == 0) return;
      m_edgeHighlightVao.Bind();
      glDrawArrays(GL_LINES, 0, m_cachedEdgeVertexCount);
   }

   // =============================================================================
   // PointCloudNode
   // =============================================================================

   void PointCloudNode::SetPoints(std::vector<float> positions, std::vector<float> colors) {
      if (positions.size() % 3 != 0) throw std::invalid_argument("Point positions must be x,y,z triples.");
      if (!colors.empty() && colors.size() != positions.size())
         throw std::invalid_argument("Point colours must match the number of points.");
      m_positions = std::move(positions);
      m_colors = std::move(colors);
      m_bounds = BoundsOf(m_positions);
      m_dirty = true;
   }

   void PointCloudNode::Draw() {
      if (m_positions.empty()) return;
      if (m_dirty) {
         // Recreate the VAO so a colour attribute from a previous upload cannot
         // stay enabled after the colours were removed.
         m_vao.Release();
         m_vao.Create();
         m_positionVbo.SetData(m_positions);
         m_vao.SetVertexBuffer(0, m_positionVbo, 3 * sizeof(float));
         m_vao.SetFloatAttribute(0, 0, 3);
         if (!m_colors.empty()) {
            m_colorVbo.SetData(m_colors);
            m_vao.SetVertexBuffer(1, m_colorVbo, 3 * sizeof(float));
            m_vao.SetFloatAttribute(1, 1, 3);
         }
         m_dirty = false;
      }
      m_vao.Bind();
      glDrawArrays(GL_POINTS, 0, static_cast<GLsizei>(m_positions.size() / 3));
   }

} // namespace CadCore
