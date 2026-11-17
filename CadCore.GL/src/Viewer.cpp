// =============================================================================
// CadCore.GL / Viewer.cpp
// =============================================================================
#include "CadCore/Viewer.h"

#include "CadCore/CurveNode.h"
#include "CadCore/PointCloudNode.h"
#include "CadCore/StlLoader.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <utility>

namespace CadCore {

Viewer::Viewer(IGLContext* context) : m_context(context) {
    if (!m_context) throw std::invalid_argument("Viewer needs an OpenGL context.");
}

Viewer::~Viewer() {
    // GL objects (scene buffers, shaders) must be freed with the context current.
    m_context->MakeCurrent();
    m_scene.Clear();
    m_renderer.Shutdown();
}

void Viewer::Initialize() {
    if (m_initialized) return;
    m_context->MakeCurrent();

    // GLEW looks up every OpenGL function the driver offers. glewExperimental
    // makes it probe functions directly, which core-profile contexts require.
    glewExperimental = GL_TRUE;
    GLenum err = glewInit();
#ifdef GLEW_ERROR_NO_GLX_DISPLAY
    // Harmless on Linux when the context comes from EGL/Wayland instead of GLX.
    if (err == GLEW_ERROR_NO_GLX_DISPLAY) err = GLEW_OK;
#endif
    if (err != GLEW_OK)
        throw std::runtime_error(std::string("GLEW initialisation failed: ") +
                                 reinterpret_cast<const char*>(glewGetErrorString(err)));

    m_renderer.Initialize();
    m_initialized = true;
}

void Viewer::Resize(int widthPx, int heightPx) {
    m_context->OnWindowResized();
    m_context->MakeCurrent();
    m_camera.SetViewport(widthPx, heightPx);
    m_renderer.Resize(widthPx, heightPx);
}

void Viewer::Render() {
    if (!m_initialized) return;
    m_context->MakeCurrent();
    m_renderer.Render(m_scene, m_camera, m_settings);
    m_context->SwapBuffers();
}

void Viewer::FitAll() {
    AABB box = m_scene.VisibleBounds();
    if (!box.IsValid()) {
        // Empty scene: frame a 200 mm cube around the origin.
        box.Expand({-100.0f, -100.0f, -100.0f});
        box.Expand({100.0f, 100.0f, 100.0f});
    }
    m_camera.Fit(box);
}

// -----------------------------------------------------------------------------
// Scene graph
// -----------------------------------------------------------------------------

int Viewer::AddGroup(int parentId, const std::string& name) {
    return m_scene.Add(parentId, std::make_unique<GroupNode>(name));
}

int Viewer::AddMesh(int parentId, const std::string& name, MeshKind kind,
                    const float* positions, const float* normals, std::size_t vertexCount,
                    const std::uint32_t* indices, std::size_t indexCount,
                    const float* edgeSegments, std::size_t edgeSegmentCount) {
    if (!positions || !indices || vertexCount == 0 || indexCount == 0)
        throw std::invalid_argument("A mesh needs positions and indices.");

    auto mesh = std::make_unique<MeshNode>(name, kind);
    mesh->SetGeometry(
        std::vector<float>(positions, positions + vertexCount * 3),
        normals ? std::vector<float>(normals, normals + vertexCount * 3) : std::vector<float>(),
        std::vector<std::uint32_t>(indices, indices + indexCount));

    if (edgeSegments && edgeSegmentCount > 0)
        mesh->SetEdges(std::vector<float>(edgeSegments, edgeSegments + edgeSegmentCount * 6));
    else
        mesh->ComputeFeatureEdges();
    mesh->BuildTopology();          // faces / edges / vertices for picking

    return m_scene.Add(parentId, std::move(mesh));
}

int Viewer::AddStlFile(int parentId, const std::string& utf8Path, const std::string& name) {
    std::vector<float> positions, normals;
    std::vector<std::uint32_t> indices;
    LoadStl(utf8Path, positions, normals, indices);

    auto mesh = std::make_unique<MeshNode>(name, MeshKind::Stl);
    mesh->SetGeometry(std::move(positions), std::move(normals), std::move(indices));
    mesh->ComputeFeatureEdges();    // STL has no edge data of its own
    mesh->BuildTopology();
    return m_scene.Add(parentId, std::move(mesh));
}

int Viewer::AddPointCloud(int parentId, const std::string& name,
                          const float* positions, const float* colors, std::size_t pointCount,
                          float pointSizePx) {
    if (!positions || pointCount == 0) throw std::invalid_argument("A point cloud needs points.");

    auto cloud = std::make_unique<PointCloudNode>(name);
    cloud->SetPoints(
        std::vector<float>(positions, positions + pointCount * 3),
        colors ? std::vector<float>(colors, colors + pointCount * 3) : std::vector<float>());
    cloud->pointSizePx = pointSizePx;
    return m_scene.Add(parentId, std::move(cloud));
}

int Viewer::AddCurve(int parentId, const std::string& name,
                     const float* points, std::size_t pointCount, bool closed, float widthPx) {
    if (!points || pointCount < 2) throw std::invalid_argument("A curve needs at least two points.");
    auto curve = std::make_unique<CurveNode>(name);
    curve->SetPoints(std::vector<float>(points, points + pointCount * 3), closed);
    curve->widthPx = widthPx;
    return m_scene.Add(parentId, std::move(curve));
}

bool Viewer::SetCurvePoints(int id, const float* points, std::size_t pointCount, bool closed) {
    Node* node = m_scene.Find(id);
    if (!node || node->Type() != NodeType::Curve) return false;
    if (!points || pointCount < 2) throw std::invalid_argument("A curve needs at least two points.");
    static_cast<CurveNode*>(node)->SetPoints(std::vector<float>(points, points + pointCount * 3), closed);
    return true;
}

bool Viewer::RemoveNode(int id) {
    m_context->MakeCurrent();       // the node's GPU buffers are deleted here
    const bool removed = m_scene.Remove(id);
    // The selection may have been the removed node or something inside it.
    if (removed) {
        // Drop selected entities that belonged to the removed node or its subtree.
        std::vector<SelectedEntity>& sel = m_settings.selection;
        sel.erase(std::remove_if(sel.begin(), sel.end(),
                                 [this](const SelectedEntity& e) { return m_scene.Find(e.nodeId) == nullptr; }),
                  sel.end());
    }
    return removed;
}

void Viewer::ClearScene() {
    m_context->MakeCurrent();
    m_scene.Clear();
    ClearSelection();
}

// -----------------------------------------------------------------------------
// Picking
//
// A click is turned into a RAY through the scene: the clicked pixel is
// "unprojected" (inverse of view * projection) once at the near plane and once
// at the far plane. The ray is then tested against every visible node:
//   * meshes       - against each triangle (after a quick bounding-box test),
//   * point clouds - a point counts as hit when it lies within a few pixels of
//                    the click on screen.
// The hit closest to the eye wins. This runs on the CPU and needs no extra
// rendering pass; it is fast enough for interactive clicks on typical parts
// (roughly 10-20 ms per million triangles under the cursor's bounding boxes).
// -----------------------------------------------------------------------------
namespace {

struct PickQuery {
    Vec3  origin, dir;          // world-space ray (dir is unit length)
    Mat4  viewProj;
    float width, height;        // viewport size in pixels
    float pixelX, pixelY;       // clicked pixel (y from the top)
    float bestT = 1e30f;        // distance along the ray of the best hit so far
    const Node* bestNode = nullptr;
    Mat4        bestWorld;      // world transform of bestNode
    std::size_t bestElement = 0;  // triangle number (mesh) or point number (cloud)
    Vec3        bestPoint;        // curves only: the point of the curve nearest the click (world)
};

/// Moller-Trumbore ray / triangle intersection. `t` = distance along the ray.
bool RayTriangle(const Vec3& o, const Vec3& d, const Vec3& a, const Vec3& b, const Vec3& c, float& t) {
    const Vec3 e1 = b - a, e2 = c - a;
    const Vec3 p = Cross(d, e2);
    const float det = Dot(e1, p);
    if (std::fabs(det) < 1e-20f) return false;          // ray parallel to the triangle
    const float inv = 1.0f / det;
    const Vec3 s = o - a;
    const float u = Dot(s, p) * inv;
    if (u < 0.0f || u > 1.0f) return false;
    const Vec3 q = Cross(s, e1);
    const float v = Dot(d, q) * inv;
    if (v < 0.0f || u + v > 1.0f) return false;
    t = Dot(e2, q) * inv;
    return t > 0.0f;
}

/// Does the ray pass through the box at all? ("slab" test)
bool RayHitsBox(const Vec3& o, const Vec3& d, const AABB& box) {
    float tMin = 0.0f, tMax = 1e30f;
    const float org[3] = {o.x, o.y, o.z}, dir[3] = {d.x, d.y, d.z};
    const float lo[3] = {box.min.x, box.min.y, box.min.z}, hi[3] = {box.max.x, box.max.y, box.max.z};
    for (int i = 0; i < 3; ++i) {
        if (std::fabs(dir[i]) < 1e-20f) {
            if (org[i] < lo[i] || org[i] > hi[i]) return false;
            continue;
        }
        float t0 = (lo[i] - org[i]) / dir[i], t1 = (hi[i] - org[i]) / dir[i];
        if (t0 > t1) std::swap(t0, t1);
        tMin = std::max(tMin, t0);
        tMax = std::min(tMax, t1);
        if (tMin > tMax) return false;
    }
    return true;
}

void PickNode(const Node* node, const Mat4& parentWorld, PickQuery& q) {
    if (!node->IsVisible()) return;                     // hidden subtrees cannot be picked
    const Mat4 world = parentWorld * node->LocalTransform();

    if (node->Type() == NodeType::Mesh) {
        const MeshNode* mesh = static_cast<const MeshNode*>(node);
        Mat4 toLocal;
        if (Invert(world, toLocal)) {
            // Test in the mesh's own coordinates: transforming one ray is far
            // cheaper than transforming every vertex. The direction is not
            // re-normalised, so `t` stays a world-space distance.
            const Vec3 o = toLocal.TransformPoint(q.origin);
            const Vec3 d = toLocal.TransformPoint(q.origin + q.dir) - o;
            if (RayHitsBox(o, d, mesh->LocalBounds())) {
                const std::vector<float>& pos = mesh->Positions();
                const std::vector<std::uint32_t>& idx = mesh->Indices();
                auto vertex = [&pos](std::uint32_t i) { return Vec3(pos[i * 3], pos[i * 3 + 1], pos[i * 3 + 2]); };
                for (std::size_t k = 0; k + 2 < idx.size(); k += 3) {
                    float t;
                    if (RayTriangle(o, d, vertex(idx[k]), vertex(idx[k + 1]), vertex(idx[k + 2]), t) && t < q.bestT) {
                        q.bestT = t;
                        q.bestNode = node;
                        q.bestWorld = world;
                        q.bestElement = k / 3;
                    }
                }
            }
        }
    } else if (node->Type() == NodeType::Curve) {
        // A line has no area for a ray to hit. Instead: is the click within a
        // few PIXELS of the curve as it appears on screen? Each straight piece
        // is projected to the screen and the click's distance to it is measured.
        const CurveNode* curve = static_cast<const CurveNode*>(node);
        const std::vector<float>& pts = curve->Points();
        const std::size_t n = pts.size() / 3;
        const std::size_t pieces = curve->IsClosed() ? n : n - 1;
        const float tolerance = std::max(curve->widthPx * 0.5f + 3.0f, 5.0f);
        const Mat4 m = q.viewProj * world;                       // local -> clip space
        auto local = [&pts](std::size_t i) { return Vec3(pts[i * 3], pts[i * 3 + 1], pts[i * 3 + 2]); };
        for (std::size_t i = 0; i < pieces && n >= 2; ++i) {
            const Vec3 a = local(i), b = local((i + 1) % n);
            const float wa = m.m[3] * a.x + m.m[7] * a.y + m.m[11] * a.z + m.m[15];
            const float wb = m.m[3] * b.x + m.m[7] * b.y + m.m[11] * b.z + m.m[15];
            if (wa <= 1e-6f || wb <= 1e-6f) continue;            // piece reaches behind the eye
            const float ax = ((m.m[0] * a.x + m.m[4] * a.y + m.m[8] * a.z + m.m[12]) / wa * 0.5f + 0.5f) * q.width;
            const float ay = (0.5f - (m.m[1] * a.x + m.m[5] * a.y + m.m[9] * a.z + m.m[13]) / wa * 0.5f) * q.height;
            const float bx = ((m.m[0] * b.x + m.m[4] * b.y + m.m[8] * b.z + m.m[12]) / wb * 0.5f + 0.5f) * q.width;
            const float by = (0.5f - (m.m[1] * b.x + m.m[5] * b.y + m.m[9] * b.z + m.m[13]) / wb * 0.5f) * q.height;
            // Closest point of the screen segment to the click, as a fraction u of its length.
            const float ex = bx - ax, ey = by - ay, len2 = ex * ex + ey * ey;
            const float u = len2 > 0.0f ? std::clamp(((q.pixelX - ax) * ex + (q.pixelY - ay) * ey) / len2, 0.0f, 1.0f) : 0.0f;
            const float dx = ax + ex * u - q.pixelX, dy = ay + ey * u - q.pixelY;
            if (dx * dx + dy * dy > tolerance * tolerance) continue;
            // The pieces are short, so the same fraction along the 3D piece is
            // close enough to the true nearest point.
            const Vec3 hit = world.TransformPoint(a + (b - a) * u);
            const float t = Dot(hit - q.origin, q.dir);
            if (t > 0.0f && t < q.bestT) {
                q.bestT = t;
                q.bestNode = node;
                q.bestWorld = world;
                q.bestElement = i;
                q.bestPoint = hit;
            }
        }
    } else if (node->Type() == NodeType::PointCloud) {
        const PointCloudNode* cloud = static_cast<const PointCloudNode*>(node);
        const std::vector<float>& pos = cloud->Positions();
        const float radius = std::max(cloud->pointSizePx * 0.5f + 2.0f, 4.0f);   // click tolerance, px
        const Mat4 m = q.viewProj * world;                                       // local -> clip space
        for (std::size_t k = 0; k + 2 < pos.size(); k += 3) {
            const float x = pos[k], y = pos[k + 1], z = pos[k + 2];
            const float cw = m.m[3] * x + m.m[7] * y + m.m[11] * z + m.m[15];
            if (cw <= 1e-6f) continue;                                           // behind the eye
            const float sx = ((m.m[0] * x + m.m[4] * y + m.m[8] * z + m.m[12]) / cw * 0.5f + 0.5f) * q.width;
            const float sy = (0.5f - (m.m[1] * x + m.m[5] * y + m.m[9] * z + m.m[13]) / cw * 0.5f) * q.height;
            const float dx = sx - q.pixelX, dy = sy - q.pixelY;
            if (dx * dx + dy * dy > radius * radius) continue;
            const float t = Dot(world.TransformPoint({x, y, z}) - q.origin, q.dir);
            if (t > 0.0f && t < q.bestT) {
                q.bestT = t;
                q.bestNode = node;
                q.bestWorld = world;
                q.bestElement = k / 3;
            }
        }
    }

    for (const auto& child : node->Children()) PickNode(child.get(), world, q);
}

} // namespace

PickResult Viewer::Pick(int xPx, int yPx) const {
    PickResult result;
    const float w = static_cast<float>(m_camera.ViewportWidth());
    const float h = static_cast<float>(m_camera.ViewportHeight());
    if (xPx < 0 || yPx < 0 || xPx >= w || yPx >= h) return result;

    PickQuery q;
    q.viewProj = m_camera.ProjectionMatrix() * m_camera.ViewMatrix();
    Mat4 inverse;
    if (!Invert(q.viewProj, inverse)) return result;

    // Pixel centre -> normalised device coordinates (-1..1, y up).
    q.width = w;  q.height = h;
    q.pixelX = xPx + 0.5f;  q.pixelY = yPx + 0.5f;
    const float nx = q.pixelX / w * 2.0f - 1.0f;
    const float ny = 1.0f - q.pixelY / h * 2.0f;

    // Unproject the pixel at the near (z = -1) and far (z = +1) clip planes.
    auto unproject = [&inverse](float x, float y, float z) {
        const float* m = inverse.m;
        const float iw = 1.0f / (m[3] * x + m[7] * y + m[11] * z + m[15]);
        return Vec3((m[0] * x + m[4] * y + m[8]  * z + m[12]) * iw,
                    (m[1] * x + m[5] * y + m[9]  * z + m[13]) * iw,
                    (m[2] * x + m[6] * y + m[10] * z + m[14]) * iw);
    };
    const Vec3 nearPoint = unproject(nx, ny, -1.0f);
    const Vec3 farPoint  = unproject(nx, ny, 1.0f);
    q.origin = nearPoint;
    q.dir    = Normalize(farPoint - nearPoint);

    PickNode(m_scene.Root(), Mat4::Identity(), q);
    if (!q.bestNode) return result;                     // nothing under the cursor

    // ---- which KIND of entity is it? ---------------------------------------------
    // The ray gives a point on a surface. From most to least specific:
    //   vertex  - if a vertex lies within the snap tolerance of that point
    //   edge    - if an edge does
    //   face    - the surface the point is on
    //   solid   - the whole part
    // Only types that are allowed or filtered out take part; the first one that
    // matches decides. If it is a filtered-out type the result is "blocked".
    const unsigned allowed  = m_pickTypes ? m_pickTypes : static_cast<unsigned>(kPickAll);
    const unsigned consider = allowed | m_filterOut;
    const Vec3 hitWorld = q.origin + q.dir * q.bestT;

    auto decide = [&](PickKind kind, unsigned bit, int index, const Vec3& point) {
        result.nodeId  = q.bestNode->Id();
        result.kind    = kind;
        result.index   = index;
        result.point   = point;
        result.blocked = (m_filterOut & bit) != 0;      // "filter out" wins over "allowed"
    };

    if (q.bestNode->Type() == NodeType::Mesh) {
        const MeshNode* mesh = static_cast<const MeshNode*>(q.bestNode);
        Mat4 toLocal;
        const bool haveLocal = Invert(q.bestWorld, toLocal);
        const Vec3 hitLocal = haveLocal ? toLocal.TransformPoint(hitWorld) : hitWorld;

        if (haveLocal && (consider & kPickVertex)) {
            const MeshNode::Nearest n = mesh->NearestVertex(hitLocal);
            const Vec3 world = q.bestWorld.TransformPoint(n.point);
            if (n.index >= 0 && Length(world - hitWorld) <= m_snapTolerance) {
                decide(PickKind::Vertex, kPickVertex, n.index, world);
                return result;
            }
        }
        if (haveLocal && (consider & kPickEdge)) {
            const MeshNode::Nearest n = mesh->NearestEdge(hitLocal);
            const Vec3 world = q.bestWorld.TransformPoint(n.point);
            if (n.index >= 0 && Length(world - hitWorld) <= m_snapTolerance) {
                decide(PickKind::Edge, kPickEdge, n.index, world);
                return result;
            }
        }
        if (consider & kPickFace)       decide(PickKind::Face, kPickFace, mesh->FaceOfTriangle(q.bestElement), hitWorld);
        else if (consider & kPickSolid) decide(PickKind::Solid, kPickSolid, -1, hitWorld);
    } else if (q.bestNode->Type() == NodeType::Curve) {
        // Curve: its two ends are vertices (snap within the tolerance), the
        // curve itself counts as an edge, and as a whole as a solid.
        const CurveNode* curve = static_cast<const CurveNode*>(q.bestNode);
        const std::vector<float>& pts = curve->Points();
        const std::size_t n = pts.size() / 3;
        if ((consider & kPickVertex) && !curve->IsClosed() && n >= 2) {
            for (std::size_t end : {std::size_t(0), n - 1}) {
                const Vec3 world = q.bestWorld.TransformPoint({pts[end * 3], pts[end * 3 + 1], pts[end * 3 + 2]});
                if (Length(world - q.bestPoint) <= m_snapTolerance) {
                    decide(PickKind::Vertex, kPickVertex, end == 0 ? 0 : 1, world);
                    return result;
                }
            }
        }
        if (consider & kPickEdge)       decide(PickKind::Edge, kPickEdge, 0, q.bestPoint);
        else if (consider & kPickSolid) decide(PickKind::Solid, kPickSolid, -1, q.bestPoint);
    } else {
        // Point cloud: a single point counts as a vertex, the whole cloud as a solid.
        const PointCloudNode* cloud = static_cast<const PointCloudNode*>(q.bestNode);
        const std::vector<float>& pos = cloud->Positions();
        const Vec3 world = q.bestWorld.TransformPoint(
            {pos[q.bestElement * 3], pos[q.bestElement * 3 + 1], pos[q.bestElement * 3 + 2]});
        if (consider & kPickVertex)     decide(PickKind::Vertex, kPickVertex, static_cast<int>(q.bestElement), world);
        else if (consider & kPickSolid) decide(PickKind::Solid, kPickSolid, -1, world);
    }
    return result;
}

bool Viewer::Select(const PickResult& hit, SelectMode mode) {
    if (!hit.IsHit() || hit.kind == PickKind::None) return false;

    SelectedEntity entity;
    entity.kind   = hit.kind;
    entity.nodeId = hit.nodeId;
    entity.index  = hit.kind == PickKind::Solid ? -1 : hit.index;
    entity.point  = hit.point;

    std::vector<SelectedEntity>& sel = m_settings.selection;
    auto same = [&entity](const SelectedEntity& e) { return e.SameEntity(entity); };
    auto found = std::find_if(sel.begin(), sel.end(), same);

    switch (mode) {
    case SelectMode::Replace:
        sel.assign(1, entity);
        return true;

    case SelectMode::Add:
        if (found != sel.end()) return false;           // already selected
        sel.push_back(entity);
        return true;

    case SelectMode::Remove:
        if (found != sel.end()) { sel.erase(found); return true; }
        // The click landed on, say, a face of a part that is selected as a
        // whole: the user sees a highlighted part and wants it deselected.
        found = std::find_if(sel.begin(), sel.end(), [&entity](const SelectedEntity& e) {
            return e.kind == PickKind::Solid && e.nodeId == entity.nodeId;
        });
        if (found != sel.end()) { sel.erase(found); return true; }
        return false;
    }
    return false;
}

void Viewer::SetSelectedNode(int id) {
    m_settings.selection.clear();
    if (id < 0) return;
    SelectedEntity entity;
    entity.kind   = PickKind::Solid;
    entity.nodeId = id;
    m_settings.selection.push_back(entity);
}

bool Viewer::SetMeshTopology(int id, const std::uint32_t* faceOfTriangle, std::size_t triangleCount,
                             const float* vertices, std::size_t vertexCount) {
    Node* node = m_scene.Find(id);
    if (!node || node->Type() != NodeType::Mesh) return false;
    MeshNode* mesh = static_cast<MeshNode*>(node);
    if (faceOfTriangle) mesh->SetFaceIds(std::vector<std::uint32_t>(faceOfTriangle, faceOfTriangle + triangleCount));
    if (vertices)       mesh->SetVertices(std::vector<float>(vertices, vertices + vertexCount * 3));
    m_context->MakeCurrent();       // highlight buffers may be refilled later; keep GL state valid
    mesh->BuildTopology();
    return true;
}

bool Viewer::SetNodeVisible(int id, bool visible) {
    Node* node = m_scene.Find(id);
    if (!node) return false;
    node->SetVisible(visible);
    return true;
}

bool Viewer::SetNodeTransform(int id, const float* columnMajor16) {
    Node* node = m_scene.Find(id);
    if (!node || !columnMajor16) return false;
    Mat4 m;
    std::memcpy(m.m, columnMajor16, sizeof(m.m));
    node->SetLocalTransform(m);
    return true;
}

bool Viewer::SetNodeMaterial(int id, const Material& material) {
    Node* node = m_scene.Find(id);
    if (!node || node->Type() != NodeType::Mesh) return false;
    static_cast<MeshNode*>(node)->material = material;
    return true;
}

bool Viewer::SetNodeColor(int id, float r, float g, float b) {
    Node* node = m_scene.Find(id);
    if (!node) return false;
    if (node->Type() == NodeType::Mesh) {
        static_cast<MeshNode*>(node)->material.SetColor({r, g, b});
        return true;
    }
    if (node->Type() == NodeType::PointCloud) {
        static_cast<PointCloudNode*>(node)->color = {r, g, b};
        return true;
    }
    if (node->Type() == NodeType::Curve) {
        static_cast<CurveNode*>(node)->color = {r, g, b};
        return true;
    }
    return false;
}

bool Viewer::SetNodeOpacity(int id, float opacity) {
    Node* node = m_scene.Find(id);
    if (!node || node->Type() != NodeType::Mesh) return false;
    static_cast<MeshNode*>(node)->material.opacity = std::clamp(opacity, 0.0f, 1.0f);
    return true;
}

} // namespace CadCore
