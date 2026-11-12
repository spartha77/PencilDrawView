// =============================================================================
// CadCore.GL / Renderer.cpp
// =============================================================================
#include "CadCore/Renderer.h"

#include "CadCore/MeshNode.h"
#include "CadCore/PointCloudNode.h"
#include "CadCore/Shaders.h"

#include <cstdlib>
#include <cstring>
#include <stdexcept>

namespace CadCore {

   namespace {

      // -----------------------------------------------------------------------------
      // C++ mirrors of the GLSL uniform blocks in Shaders.h (std140 layout).
      // Only 16-byte-aligned members are used, so these structs match byte for byte.
      // -----------------------------------------------------------------------------
      struct FrameUbo {
         float view[16];
         float proj[16];
         float viewProj[16];
         float eyePos[4];     // w = 1 perspective, 0 orthographic
         float viewDir[4];
         float viewport[4];
      };

      struct LightUboEntry {
         float positionType[4];
         float directionIntensity[4];
         float colorEnabled[4];
         float spot[4];
      };

      struct LightUbo {
         float        ambient[4];
         std::int32_t config[4];   // count, mode, term flags, unused
         LightUboEntry lights[kMaxLights];
      };

      constexpr GLuint kFrameBinding = 0;
      constexpr GLuint kLightBinding = 1;

      // Axis arrow model, in millimetres, pointing along +X from the origin.
      constexpr float kArrowShaftLength = 10.0f;   // cylinder length
      constexpr float kArrowShaftRadius = 2.0f;    // cylinder is 4 mm in diameter
      constexpr float kArrowHeadLength = 4.0f;    // cone length
      constexpr float kArrowHeadRadius = 3.5f;    // cone base radius
      constexpr int   kArrowSegments = 24;

      /// Builds the cylinder + cone arrow as an indexed triangle mesh.
      void BuildArrow(std::vector<float>& positions, std::vector<float>& normals,
         std::vector<std::uint32_t>& indices) {
         auto addVertex = [&](const Vec3& p, const Vec3& n) -> std::uint32_t {
            positions.insert(positions.end(), { p.x, p.y, p.z });
            normals.insert(normals.end(), { n.x, n.y, n.z });
            return static_cast<std::uint32_t>(positions.size() / 3 - 1);
            };
         auto ringPoint = [](float x, float radius, float angle) {
            return Vec3(x, radius * std::cos(angle), radius * std::sin(angle));
            };
         const float step = 2.0f * kPi / kArrowSegments;

         // A flat disc facing -X: closes the back of the shaft and of the cone.
         auto addDisc = [&](float x, float radius) {
            const Vec3 n(-1.0f, 0.0f, 0.0f);
            std::uint32_t center = addVertex({ x, 0.0f, 0.0f }, n);
            std::uint32_t first = center + 1;
            for (int i = 0; i <= kArrowSegments; ++i) addVertex(ringPoint(x, radius, i * step), n);
            for (int i = 0; i < kArrowSegments; ++i)
               indices.insert(indices.end(), { center, first + i + 1, first + i });
            };

         // Shaft: back cap, then the cylinder wall (normals point radially outwards).
         addDisc(0.0f, kArrowShaftRadius);
         std::uint32_t first = static_cast<std::uint32_t>(positions.size() / 3);
         for (int i = 0; i <= kArrowSegments; ++i) {
            const Vec3 n(0.0f, std::cos(i * step), std::sin(i * step));
            addVertex(ringPoint(0.0f, kArrowShaftRadius, i * step), n);
            addVertex(ringPoint(kArrowShaftLength, kArrowShaftRadius, i * step), n);
         }
         for (int i = 0; i < kArrowSegments; ++i) {
            std::uint32_t a = first + 2 * i, b = a + 1, c = a + 2, d = a + 3;
            indices.insert(indices.end(), { a, c, b, b, c, d });
         }

         // Head: base disc, then the cone wall. A cone's surface normal leans
         // forward by the cone's slope: n = (R, h cos t, h sin t) / sqrt(R^2 + h^2).
         addDisc(kArrowShaftLength, kArrowHeadRadius);
         const float R = kArrowHeadRadius, h = kArrowHeadLength;
         const float inv = 1.0f / std::sqrt(R * R + h * h);
         auto coneNormal = [&](float angle) {
            return Vec3(R * inv, h * inv * std::cos(angle), h * inv * std::sin(angle));
            };
         for (int i = 0; i < kArrowSegments; ++i) {
            const float a0 = i * step, a1 = (i + 1) * step;
            std::uint32_t v0 = addVertex(ringPoint(kArrowShaftLength, R, a0), coneNormal(a0));
            std::uint32_t v1 = addVertex(ringPoint(kArrowShaftLength, R, a1), coneNormal(a1));
            std::uint32_t tip = addVertex({ kArrowShaftLength + h, 0.0f, 0.0f }, coneNormal((a0 + a1) * 0.5f));
            indices.insert(indices.end(), { v0, v1, tip });
         }
      }

      void CopyVec(float dst[4], const Vec3& v, float w) {
         dst[0] = v.x; dst[1] = v.y; dst[2] = v.z; dst[3] = w;
      }

   } // namespace

   // =============================================================================
   // Setup / teardown
   // =============================================================================

   void Renderer::Initialize() {
      if (m_initialized) return;

      // Everything below relies on Direct State Access, core since OpenGL 4.5.
      const char* version = reinterpret_cast<const char*>(glGetString(GL_VERSION));
      const char* renderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
      m_glVersion = std::string(version ? version : "?") + " | " + (renderer ? renderer : "?");

      // Ask the DRIVER which version this context is. GLEW's own GLEW_VERSION_4_5
      // flag is not used: GLEW clears it when the driver lacks any single 4.5
      // function (some drivers omit a few glGetn* calls in a core profile), even
      // though the context really is 4.5 or 4.6.
      GLint major = 0, minor = 0;
      glGetIntegerv(GL_MAJOR_VERSION, &major);
      glGetIntegerv(GL_MINOR_VERSION, &minor);
      if (major < 4 || (major == 4 && minor < 5))
         throw std::runtime_error("OpenGL 4.5 or newer is required, but this adapter offers: " + m_glVersion);

      // What the engine really depends on: the Direct State Access functions.
      if (!glCreateBuffers || !glNamedBufferData || !glNamedBufferSubData ||
         !glCreateVertexArrays || !glVertexArrayVertexBuffer || !glVertexArrayAttribFormat ||
         !glCreateFramebuffers || !glNamedRenderbufferStorageMultisample || !glBlitNamedFramebuffer ||
         !glProgramUniformMatrix4fv)
         throw std::runtime_error("The driver reports OpenGL " + m_glVersion +
            ", but GLEW could not load the 4.5 functions. "
            "Check the GLEW build (header, .lib and .dll must come from the same GLEW).");

      // Use GLSL 4.60 when the driver's shader compiler offers it, otherwise 4.50.
      // The compiler is asked directly (the string starts with "major.minor"),
      // because some drivers report a GL version their GLSL compiler lags behind.
      const char* glsl = reinterpret_cast<const char*>(glGetString(GL_SHADING_LANGUAGE_VERSION));
      const bool glsl460 = glsl && std::atof(glsl) >= 4.595;
      const std::string versionLine = glsl460 ? "#version 460 core\n" : "#version 450 core\n";
      m_glVersion += std::string(" | GLSL ") + (glsl ? glsl : "?");

      const std::string frame = Shaders::kFrameBlock;
      const std::string light = Shaders::kLightBlock;
      const std::string meshVs = frame + Shaders::kMeshVertex;

      m_meshProgram.Build("mesh", versionLine, meshVs.c_str(),
         (frame + light + Shaders::kMeshFragment).c_str());
      m_gizmoProgram.Build("gizmo", versionLine, meshVs.c_str(),
         (frame + Shaders::kGizmoFragment).c_str());
      m_lineProgram.Build("line", versionLine, (frame + Shaders::kLineVertex).c_str(),
         Shaders::kLineFragment, (frame + Shaders::kLineGeometry).c_str());
      m_pointProgram.Build("point", versionLine, (frame + Shaders::kPointVertex).c_str(),
         Shaders::kPointFragment);
      m_gridProgram.Build("grid", versionLine, (frame + Shaders::kGridVertex).c_str(),
         Shaders::kGridFragment);

      // Per-frame uniform buffers: allocate once, overwrite every frame.
      m_frameUbo.SetData(nullptr, sizeof(FrameUbo), GL_DYNAMIC_DRAW);
      m_lightUbo.SetData(nullptr, sizeof(LightUbo), GL_DYNAMIC_DRAW);

      // Grid: a single quad drawn as a 4-vertex triangle strip.
      const float corners[] = { -1.0f, -1.0f, 1.0f, -1.0f, -1.0f, 1.0f, 1.0f, 1.0f };
      m_gridVbo.SetData(corners, sizeof(corners));
      m_gridVao.Create();
      m_gridVao.SetVertexBuffer(0, m_gridVbo, 2 * sizeof(float));
      m_gridVao.SetFloatAttribute(0, 0, 2);

      // Axis arrow (one model, drawn three times with different orientations).
      std::vector<float> positions, normals;
      std::vector<std::uint32_t> indices;
      BuildArrow(positions, normals, indices);
      m_arrowPositions.SetData(positions);
      m_arrowNormals.SetData(normals);
      m_arrowIndices.SetData(indices);
      m_arrowVao.Create();
      m_arrowVao.SetVertexBuffer(0, m_arrowPositions, 3 * sizeof(float));
      m_arrowVao.SetVertexBuffer(1, m_arrowNormals, 3 * sizeof(float));
      m_arrowVao.SetIndexBuffer(m_arrowIndices);
      m_arrowVao.SetFloatAttribute(0, 0, 3);
      m_arrowVao.SetFloatAttribute(1, 1, 3);
      m_arrowIndexCount = static_cast<int>(indices.size());

      // Selection marker: a single point whose position is rewritten when needed.
      const float origin[3] = { 0.0f, 0.0f, 0.0f };
      m_markerVbo.SetData(origin, sizeof(origin), GL_DYNAMIC_DRAW);
      m_markerVao.Create();
      m_markerVao.SetVertexBuffer(0, m_markerVbo, 3 * sizeof(float));
      m_markerVao.SetFloatAttribute(0, 0, 3);

      GLint maxSamples = 1;
      glGetIntegerv(GL_MAX_SAMPLES, &maxSamples);
      m_samples = std::max(1, std::min(4, static_cast<int>(maxSamples)));

      m_initialized = true;
      CreateFramebuffer();
   }

   void Renderer::Shutdown() {
      if (!m_initialized) return;
      DestroyFramebuffer();
      m_meshProgram.Release();  m_gizmoProgram.Release(); m_lineProgram.Release();
      m_pointProgram.Release(); m_gridProgram.Release();
      m_frameUbo.Release();     m_lightUbo.Release();
      m_gridVao.Release();      m_gridVbo.Release();
      m_markerVao.Release();    m_markerVbo.Release();
      m_arrowVao.Release();     m_arrowPositions.Release();
      m_arrowNormals.Release(); m_arrowIndices.Release();
      m_initialized = false;
   }

   void Renderer::Resize(int widthPx, int heightPx) {
      widthPx = std::max(widthPx, 1);
      heightPx = std::max(heightPx, 1);
      if (widthPx == m_width && heightPx == m_height) return;
      m_width = widthPx;
      m_height = heightPx;
      if (m_initialized) CreateFramebuffer();
   }

   void Renderer::CreateFramebuffer() {
      DestroyFramebuffer();

      // Multisampled colour + depth renderbuffers attached to an FBO.
      glCreateRenderbuffers(1, &m_colorRb);
      glNamedRenderbufferStorageMultisample(m_colorRb, m_samples, GL_RGBA8, m_width, m_height);
      glCreateRenderbuffers(1, &m_depthRb);
      glNamedRenderbufferStorageMultisample(m_depthRb, m_samples, GL_DEPTH_COMPONENT24, m_width, m_height);

      glCreateFramebuffers(1, &m_fbo);
      glNamedFramebufferRenderbuffer(m_fbo, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, m_colorRb);
      glNamedFramebufferRenderbuffer(m_fbo, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, m_depthRb);

      if (glCheckNamedFramebufferStatus(m_fbo, GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
         throw std::runtime_error("Could not create the off-screen framebuffer.");
   }

   void Renderer::DestroyFramebuffer() {
      if (m_fbo) { glDeleteFramebuffers(1, &m_fbo);       m_fbo = 0; }
      if (m_colorRb) { glDeleteRenderbuffers(1, &m_colorRb);  m_colorRb = 0; }
      if (m_depthRb) { glDeleteRenderbuffers(1, &m_depthRb);  m_depthRb = 0; }
   }

   // =============================================================================
   // Scene-graph traversal
   // =============================================================================

   // Depth-first walk from the root. The world transform is accumulated on the way
   // down; a node whose "visible" flag is off cuts off its whole subtree.
   void Renderer::Collect(Node* node, const Mat4& parentWorld, const Camera::Frame& frame,
      bool parentSelected) {
      if (!node->IsVisible()) return;

      // Selection is inherited: selecting a group highlights its whole subtree.
      const bool selected = parentSelected || m_selectedSolids.count(node->Id()) != 0;

      const Mat4 world = parentWorld * node->LocalTransform();

      // Remember where this node is if some of its faces / edges / vertices are selected.
      auto partial = m_partial.find(node->Id());
      if (partial != m_partial.end()) { partial->second.node = node; partial->second.world = world; }

      if (node->Type() != NodeType::Group) {
         const AABB box = node->LocalBounds().Transformed(world);
         DrawItem item{ node, world, 0.0f, selected };
         if (box.IsValid()) item.depth = Dot(frame.forward, box.Center() - frame.eye);

         if (node->Type() == NodeType::PointCloud) {
            m_points.push_back(item);
         }
         else {
            const bool transparent = static_cast<MeshNode*>(node)->material.opacity < 0.999f;
            (transparent ? m_transparent : m_opaque).push_back(item);
         }
      }

      for (const auto& child : node->Children()) Collect(child.get(), world, frame, selected);
   }

   void Renderer::DrawSurface(const DrawItem& item, const std::vector<int>* faces) {
      MeshNode* mesh = static_cast<MeshNode*>(item.node);
      const Material& mat = mesh->material;
      float normalMat[9];
      NormalMatrix(item.world, normalMat);

      m_meshProgram.SetMat4("uModel", item.world);
      m_meshProgram.SetMat3("uNormalMat", normalMat);
      // A selected part keeps its lighting and highlight but takes the highlight colour.
      m_meshProgram.SetVec3("uMatAmbient", item.selected ? m_highlight : mat.ambient);
      m_meshProgram.SetVec3("uMatDiffuse", item.selected ? m_highlight : mat.diffuse);
      m_meshProgram.SetVec3("uMatSpecular", mat.specular);
      m_meshProgram.SetFloat("uMatShininess", std::max(mat.shininess, 1.0f));
      m_meshProgram.SetFloat("uMatOpacity", faces ? 1.0f : mat.opacity);
      if (faces) mesh->DrawFaces(*faces);
      else       mesh->DrawTriangles();
   }

   // =============================================================================
   // One frame
   // =============================================================================

   void Renderer::Render(Scene& scene, Camera& camera, const RenderSettings& settings,
      std::uint32_t targetFbo) {
      if (!m_initialized) return;

      camera.SetViewport(m_width, m_height);
      const Camera::Frame frame = camera.ComputeFrame();

      // ---- axis arrows: world scale that gives a constant size in pixels ---------
      // One pixel covers `worldPerPixel` mm at the origin's depth; scaling the
      // arrow model by (pixelsPerMm * worldPerPixel) cancels the zoom exactly.
      const float originDepth = Dot(frame.forward, Vec3() - frame.eye);
      const bool  axesVisible = settings.showAxes &&
         (!camera.IsPerspective() || originDepth > 1e-3f);
      const float axisScale = camera.WorldUnitsPerPixel(originDepth) * settings.axisPixelsPerMm;

      // ---- clip planes from the scene bounds --------------------------------------
      // Fitting near/far tightly around what is actually drawn keeps the depth
      // buffer precise from sub-millimetre details up to a 10 m grid.
      AABB bounds = scene.VisibleBounds();
      if (settings.showGrid) {
         const float h = settings.gridSizeMm * 0.5f;
         bounds.Expand({ -h, -h, 0.0f });
         bounds.Expand({ h, h, 0.0f });
      }
      if (axesVisible) {
         const float len = (kArrowShaftLength + kArrowHeadLength) * axisScale;
         bounds.Expand({ -len, -len, -len });
         bounds.Expand({ len, len, len });
      }
      if (!bounds.IsValid()) { bounds.Expand(camera.Target()); }

      const float radius = std::max(bounds.Radius(), 1e-3f) * 1.05f;
      const float centerDepth = Dot(frame.forward, bounds.Center() - frame.eye);
      if (camera.IsPerspective()) {
         const float farDist = std::max(centerDepth + radius, 1e-2f);
         const float minNear = std::min(farDist * 5e-4f, frame.targetDepth * 0.05f);
         const float nearDist = std::max({ centerDepth - radius, minNear, 1e-3f });
         camera.SetClipRange(nearDist, farDist);
      }
      else {
         camera.SetClipRange(centerDepth - radius, centerDepth + radius);
      }

      // ---- per-frame uniform buffers -----------------------------------------------
      const Mat4 view = camera.ViewMatrix();
      const Mat4 proj = camera.ProjectionMatrix();
      const Mat4 viewProj = proj * view;

      FrameUbo fu{};
      std::memcpy(fu.view, view.m, sizeof(fu.view));
      std::memcpy(fu.proj, proj.m, sizeof(fu.proj));
      std::memcpy(fu.viewProj, viewProj.m, sizeof(fu.viewProj));
      CopyVec(fu.eyePos, frame.eye, camera.IsPerspective() ? 1.0f : 0.0f);
      CopyVec(fu.viewDir, frame.forward, 0.0f);
      fu.viewport[0] = static_cast<float>(m_width);
      fu.viewport[1] = static_cast<float>(m_height);
      m_frameUbo.Update(&fu, sizeof(fu));

      const LightingSettings& ls = settings.lighting;
      LightUbo lu{};
      const float ambientIntensity = ls.mode == LightingMode::AmbientOnly
         ? ls.ambientOnlyIntensity : ls.litAmbientIntensity;
      CopyVec(lu.ambient, ls.ambientColor * ambientIntensity, 1.0f);
      lu.config[0] = std::clamp(ls.lightCount, 0, kMaxLights);
      lu.config[1] = static_cast<int>(ls.mode);
      lu.config[2] = (ls.useAmbient ? 1 : 0) | (ls.useDiffuse ? 2 : 0) | (ls.useSpecular ? 4 : 0);
      for (int i = 0; i < kMaxLights; ++i) {
         const Light& src = ls.lights[i];
         LightUboEntry& dst = lu.lights[i];
         const float outer = std::clamp(src.spotOuterDeg, 1.0f, 89.0f);
         const float inner = std::clamp(src.spotInnerDeg, 0.0f, outer - 0.5f);
         CopyVec(dst.positionType, src.position, static_cast<float>(src.type));
         CopyVec(dst.directionIntensity, Normalize(src.direction, Vec3(0, 0, -1)), src.intensity);
         CopyVec(dst.colorEnabled, src.color, src.enabled ? 1.0f : 0.0f);
         dst.spot[0] = std::cos(DegToRad(inner));
         dst.spot[1] = std::cos(DegToRad(outer));
         dst.spot[2] = std::max(src.range, 0.0f);
      }
      m_lightUbo.Update(&lu, sizeof(lu));

      glBindBufferBase(GL_UNIFORM_BUFFER, kFrameBinding, m_frameUbo.Id());
      glBindBufferBase(GL_UNIFORM_BUFFER, kLightBinding, m_lightUbo.Id());

      // ---- 1. traverse the scene graph ---------------------------------------------
      m_opaque.clear();
      m_transparent.clear();
      m_points.clear();
      // Regroup the selection list: whole nodes in one set, and per node the
      // faces / edges / dots that are selected inside it.
      m_highlight = settings.highlightColor;
      m_selectedSolids.clear();
      m_partial.clear();
      for (const SelectedEntity& e : settings.selection) {
         switch (e.kind) {
         case PickKind::Solid:  m_selectedSolids.insert(e.nodeId); break;
         case PickKind::Face:   m_partial[e.nodeId].faces.push_back(e.index); break;
         case PickKind::Edge:
         case PickKind::Vertex: {
            Partial& p = m_partial[e.nodeId];
            if (e.kind == PickKind::Edge) p.edges.push_back(e.index);
            p.dots.push_back(e.point);
            p.dotIsVertex.push_back(e.kind == PickKind::Vertex ? 1 : 0);
            break;
         }
         default: break;
         }
      }
      for (auto& kv : m_partial) {             // sorted lists make the mesh's buffer cache effective
         std::sort(kv.second.faces.begin(), kv.second.faces.end());
         std::sort(kv.second.edges.begin(), kv.second.edges.end());
      }
      Collect(scene.Root(), Mat4::Identity(), frame, false);

      // ---- frame setup -------------------------------------------------------------
      // Touch the window's own framebuffer first: this is where drivers check
      // whether the window was resized since the last frame.
      glBindFramebuffer(GL_FRAMEBUFFER, targetFbo);
      glViewport(0, 0, m_width, m_height);
      glClearColor(settings.background.x, settings.background.y, settings.background.z, 1.0f);
      glClear(GL_COLOR_BUFFER_BIT);

      glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
      glViewport(0, 0, m_width, m_height);
      glEnable(GL_MULTISAMPLE);
      glEnable(GL_DEPTH_TEST);
      glDepthFunc(GL_LEQUAL);
      glDepthMask(GL_TRUE);
      glDisable(GL_BLEND);
      glDisable(GL_CULL_FACE);            // CAD data: show both sides of every face
      glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
      glClearColor(settings.background.x, settings.background.y, settings.background.z, 1.0f);
      glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

      const bool drawSurfaces = settings.displayMode != DisplayMode::Wireframe;
      const bool drawEdges = settings.displayMode != DisplayMode::Shaded;

      // ---- 2. opaque surfaces --------------------------------------------------------
      m_meshProgram.Use();
      if (drawSurfaces) {
         if (drawEdges) {
            // Push surfaces slightly away so the edge lines lying exactly on
            // them are not swallowed by depth rounding ("z-fighting").
            glEnable(GL_POLYGON_OFFSET_FILL);
            glPolygonOffset(1.0f, 1.0f);
         }
         for (const DrawItem& item : m_opaque) DrawSurface(item);
      }
      else {
         // Wireframe: a mesh without any edge lines (e.g. a perfectly smooth
         // surface) would vanish, so show its triangle outline instead.
         glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
         for (const auto* list : { &m_opaque, &m_transparent })
            for (const DrawItem& item : *list)
               if (!static_cast<MeshNode*>(item.node)->HasEdges()) DrawSurface(item);
         glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
      }

      // ---- selected FACES -----------------------------------------------------------
      // Drawn again in the highlight colour with exactly the same state as the
      // surface pass, so they land on the same depth values and simply replace
      // the faces' colour (no z-fighting, hidden parts stay hidden).
      for (const auto& kv : m_partial) {
         const Partial& p = kv.second;
         if (!p.node || p.faces.empty() || p.node->Type() != NodeType::Mesh) continue;
         m_meshProgram.Use();
         DrawSurface(DrawItem{ p.node, p.world, 0.0f, true }, &p.faces);
      }

      // ---- 3. edge overlay -------------------------------------------------------------
      if (drawEdges) {
         m_lineProgram.Use();
         m_lineProgram.SetFloat("uLineWidth", std::max(settings.edgeWidthPx, 0.5f));
         for (const auto* list : { &m_opaque, &m_transparent }) {
            for (const DrawItem& item : *list) {
               MeshNode* mesh = static_cast<MeshNode*>(item.node);
               if (!mesh->HasEdges()) continue;
               // Shaded view: dark outline. Wireframe: the part's own colour.
               const Vec3& c = drawSurfaces ? mesh->material.edgeColor
                  : item.selected ? m_highlight : mesh->material.diffuse;
               m_lineProgram.SetMat4("uModel", item.world);
               m_lineProgram.SetVec4("uColor", c.x, c.y, c.z, 1.0f);
               mesh->DrawEdges();
            }
         }
      }

      // ---- 4. point clouds -------------------------------------------------------------
      if (!m_points.empty()) {
         glEnable(GL_PROGRAM_POINT_SIZE);   // let the vertex shader set gl_PointSize
         m_pointProgram.Use();
         for (const DrawItem& item : m_points) {
            PointCloudNode* cloud = static_cast<PointCloudNode*>(item.node);
            m_pointProgram.SetMat4("uModel", item.world);
            m_pointProgram.SetFloat("uPointSize", std::max(cloud->pointSizePx, 1.0f));
            m_pointProgram.SetVec3("uColor", item.selected ? m_highlight : cloud->color);
            m_pointProgram.SetInt("uUseVertexColor", !item.selected && cloud->HasPerPointColor() ? 1 : 0);
            cloud->Draw();
         }
      }

      // ---- 5 + 6. blended passes: grid, then transparent surfaces ----------------------
      // Blended geometry is depth-TESTED against the opaque scene but does not
      // WRITE depth, so things behind it stay visible through it.
      glEnable(GL_BLEND);
      glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
      glDepthMask(GL_FALSE);

      if (settings.showGrid) {
         m_gridProgram.Use();
         m_gridProgram.SetFloat("uHalfSize", settings.gridSizeMm * 0.5f);
         m_gridProgram.SetFloat("uMinorSpacing", std::max(settings.gridMinorSpacingMm, 1e-4f));
         m_gridProgram.SetVec3("uLineColor", settings.gridColor);
         m_gridVao.Bind();
         glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
      }

      if (drawSurfaces && !m_transparent.empty()) {
         // Farthest first, so nearer transparent parts blend over farther ones.
         std::sort(m_transparent.begin(), m_transparent.end(),
            [](const DrawItem& a, const DrawItem& b) { return a.depth > b.depth; });
         m_meshProgram.Use();
         for (const DrawItem& item : m_transparent) DrawSurface(item);
      }

      glDisable(GL_POLYGON_OFFSET_FILL);
      glDisable(GL_BLEND);
      glDepthMask(GL_TRUE);

      // ---- selected EDGES / VERTICES ----------------------------------------------------
      // Drawn without depth test so the selection is never hidden behind the part.
      bool anyMarkers = false;
      for (const auto& kv : m_partial)
         if (kv.second.node && (!kv.second.edges.empty() || !kv.second.dots.empty())) anyMarkers = true;

      if (anyMarkers) {
         glDisable(GL_DEPTH_TEST);

         // Edges first, as thick lines ...
         m_lineProgram.Use();
         m_lineProgram.SetFloat("uLineWidth", std::max(settings.edgeWidthPx * 2.5f, 4.0f));
         m_lineProgram.SetVec4("uColor", m_highlight.x, m_highlight.y, m_highlight.z, 1.0f);
         for (const auto& kv : m_partial) {
            const Partial& p = kv.second;
            if (!p.node || p.edges.empty() || p.node->Type() != NodeType::Mesh) continue;
            m_lineProgram.SetMat4("uModel", p.world);
            static_cast<MeshNode*>(p.node)->DrawEdgeSet(p.edges);
         }

         // ... then a dot on every selected vertex and on every point an edge
         // pick snapped to: a white disc with a smaller highlight-coloured disc on top.
         glEnable(GL_PROGRAM_POINT_SIZE);
         m_pointProgram.Use();
         m_pointProgram.SetMat4("uModel", Mat4::Identity());      // dots are in world space
         m_pointProgram.SetInt("uUseVertexColor", 0);
         m_markerVao.Bind();
         for (const auto& kv : m_partial) {
            const Partial& p = kv.second;
            if (!p.node) continue;
            for (std::size_t i = 0; i < p.dots.size(); ++i) {
               const float xyz[3] = { p.dots[i].x, p.dots[i].y, p.dots[i].z };
               m_markerVbo.Update(xyz, sizeof(xyz));
               const bool vertex = p.dotIsVertex[i] != 0;
               m_pointProgram.SetFloat("uPointSize", vertex ? 15.0f : 11.0f);
               m_pointProgram.SetVec3("uColor", Vec3(1.0f, 1.0f, 1.0f));
               glDrawArrays(GL_POINTS, 0, 1);
               m_pointProgram.SetFloat("uPointSize", vertex ? 10.0f : 6.0f);
               m_pointProgram.SetVec3("uColor", m_highlight);
               glDrawArrays(GL_POINTS, 0, 1);
            }
         }

         glEnable(GL_DEPTH_TEST);
      }

      // ---- 7. axis arrows ------------------------------------------------------------------
      if (axesVisible) {
         glClear(GL_DEPTH_BUFFER_BIT);      // draw on top of everything else
         m_gizmoProgram.Use();
         m_arrowVao.Bind();

         // The arrow model points along +X; rotate copies onto +Y and +Z.
         const Mat4 scale = Mat4::Scale(axisScale);
         const struct { Mat4 orientation; Vec3 color; } axes[3] = {
             {Mat4::Identity(),                                      {0.90f, 0.20f, 0.20f}},  // X red
             {Mat4::FromBasis({0, 1, 0}, {-1, 0, 0}, {0, 0, 1}),     {0.25f, 0.75f, 0.25f}},  // Y green
             {Mat4::FromBasis({0, 0, 1}, {0, 1, 0}, {-1, 0, 0}),     {0.25f, 0.45f, 0.95f}},  // Z blue
         };
         for (const auto& axis : axes) {
            const Mat4 model = scale * axis.orientation;
            float normalMat[9];
            NormalMatrix(model, normalMat);
            m_gizmoProgram.SetMat4("uModel", model);
            m_gizmoProgram.SetMat3("uNormalMat", normalMat);
            m_gizmoProgram.SetVec3("uColor", axis.color);
            glDrawElements(GL_TRIANGLES, m_arrowIndexCount, GL_UNSIGNED_INT, nullptr);
         }
      }

      // ---- 8. resolve MSAA into the target framebuffer -----------------------------------
      // The window's framebuffer is bound as the draw target the classic way
      // (not through the DSA "named" call), so the driver validates its size.
      glBindFramebuffer(GL_READ_FRAMEBUFFER, m_fbo);
      glBindFramebuffer(GL_DRAW_FRAMEBUFFER, targetFbo);
      glBlitFramebuffer(0, 0, m_width, m_height, 0, 0, m_width, m_height,
         GL_COLOR_BUFFER_BIT, GL_NEAREST);
      glBindFramebuffer(GL_FRAMEBUFFER, targetFbo);
      glBindVertexArray(0);
      glUseProgram(0);
   }

} // namespace CadCore
