#pragma once
// =============================================================================
// CadCore.GL / Viewer.h
//
// FACADE - the one class a host application talks to.
// It owns the scene graph, the camera, the render settings and the renderer,
// and exposes them through simple calls that use only plain types (floats,
// ints, pointers). That keeps the C++/CLI bridge a thin 1:1 wrapper, and lets
// the same engine be driven from GLFW, Qt, wxWidgets ... on other platforms.
//
// THREADING: call everything from the thread that owns the GL context (for
// the WPF host that is the UI thread). Each call makes the context current
// before it touches OpenGL.
// =============================================================================

#include "CadCore/Camera.h"
#include "CadCore/GLContext.h"
#include "CadCore/Material.h"
#include "CadCore/MeshNode.h"
#include "CadCore/Renderer.h"
#include "CadCore/Scene.h"
#include <cstddef>
#include <cstdint>

namespace CadCore {

   /// Bit flags for SetPickTypes / SetFilterOutPickTypes.
   enum PickTypeBits : unsigned {
      kPickVertex = 1,
      kPickEdge = 2,
      kPickFace = 4,
      kPickSolid = 8,
      kPickAll = 15
   };

   /// How Viewer::Select combines a pick with the current selection.
   enum class SelectMode : int {
      Replace = 0,   ///< plain click
      Add = 1,   ///< Ctrl + click
      Remove = 2    ///< Shift + click
   };

   /// What is under a pixel.
   struct PickResult {
      int      nodeId = -1;               ///< -1 = nothing there
      PickKind kind = PickKind::None;
      int      index = -1;               ///< vertex / edge / face index inside the node
      Vec3     point;                      ///< world position (snapped for vertex / edge)
      bool     blocked = false;            ///< the entity's type is filtered out
      bool IsHit() const { return nodeId >= 0 && !blocked; }
   };

   class Viewer {
      public:
      /// `context` is not owned and must outlive the viewer.
      explicit Viewer(IGLContext* context);
      ~Viewer();
      Viewer(const Viewer&) = delete;
      Viewer& operator=(const Viewer&) = delete;

      /// Loads the OpenGL entry points (GLEW) and creates shaders. Throws on failure.
      void Initialize();

      void Resize(int widthPx, int heightPx);

      /// Renders one frame and presents it.
      void Render();

      /// e.g. "4.6.0 NVIDIA 566.03 | NVIDIA RTX A2000"
      const std::string& GLVersion() const { return m_renderer.GLVersion(); }

      // ---- camera ------------------------------------------------------------------
      void Orbit(float dxPx, float dyPx) { m_camera.Orbit(dxPx, dyPx); }
      void Pan(float dxPx, float dyPx) { m_camera.Pan(dxPx, dyPx); }
      void Zoom(float wheelNotches) { m_camera.Zoom(wheelNotches); }
      void SetProjection(ProjectionMode mode) { m_camera.SetProjection(mode); }
      void SetStandardView(StandardView view) { m_camera.SetStandardView(view); }
      void SetFieldOfView(float degrees) { m_camera.SetFovDeg(degrees); }
      /// Frames all visible geometry (the grid is not counted).
      void FitAll();

      // ---- appearance (grid, axes, display mode, lighting ...) ----------------------
      RenderSettings& Settings() { return m_settings; }

      // ---- scene graph ---------------------------------------------------------------
      // Node IDs: 0 is the root. "parentId" must be an existing node (any type).
      // The Add* functions throw std::invalid_argument on bad input.

      int AddGroup(int parentId, const std::string& name);

      /// Adds a triangle mesh.
      ///   positions       : 3 floats per vertex
      ///   normals         : 3 floats per vertex, or nullptr to compute smooth normals
      ///   indices         : 3 per triangle
      ///   edgeSegments    : optional B-Rep edge polylines as independent segments,
      ///                     6 floats (start xyz, end xyz) per segment. Pass nullptr
      ///                     to derive feature edges from the triangles instead.
      int AddMesh(int parentId, const std::string& name, MeshKind kind,
         const float* positions, const float* normals, std::size_t vertexCount,
         const std::uint32_t* indices, std::size_t indexCount,
         const float* edgeSegments = nullptr, std::size_t edgeSegmentCount = 0);

      /// Loads a binary or ASCII STL file (UTF-8 path). Throws std::runtime_error.
      int AddStlFile(int parentId, const std::string& utf8Path, const std::string& name);

      /// positions: 3 floats per point. colors: 3 floats (0..1) per point or nullptr.
      int AddPointCloud(int parentId, const std::string& name,
         const float* positions, const float* colors, std::size_t pointCount,
         float pointSizePx = 3.0f);

      bool RemoveNode(int id);       ///< removes the node and its subtree
      void ClearScene();

      // Each setter returns false when the ID does not exist (or has the wrong type).
      bool SetNodeVisible(int id, bool visible);        ///< the "show" flag
      bool SetNodeTransform(int id, const float* columnMajor16);
      bool SetNodeMaterial(int id, const Material& material);
      bool SetNodeColor(int id, float r, float g, float b);   ///< mesh or point cloud
      bool SetNodeOpacity(int id, float opacity);

      // ---- picking and selection ------------------------------------------------------
      /// Returns the ID of the front-most visible mesh or point cloud under the
      /// pixel (x from the left, y from the TOP of the viewport), or -1 for none.
      /// Uses the camera as of the last rendered frame.
      PickResult Pick(int xPx, int yPx) const;

      /// Which entity types Pick() may return, as a combination of PickTypeBits.
      /// 0 (empty) means "anything".
      void     SetPickTypes(unsigned bits) { m_pickTypes = bits & kPickAll; }
      unsigned PickTypes() const { return m_pickTypes; }
      /// Types that must NOT be picked. Pick() reports them with `blocked = true`
      /// so the host can show a "not allowed" cursor. Wins over SetPickTypes.
      void     SetFilterOutPickTypes(unsigned bits) { m_filterOut = bits & kPickAll; }
      unsigned FilterOutPickTypes() const { return m_filterOut; }
      /// How close (mm) the cursor's point on the surface must be to a vertex or
      /// an edge to snap to it. Default 0.5 mm.
      void  SetSnapTolerance(float mm) { m_snapTolerance = std::max(mm, 0.0f); }
      float SnapTolerance() const { return m_snapTolerance; }

      /// Changes the selection with something Pick() returned. A miss or a
      /// blocked hit never changes anything. Returns true if the selection changed.
      ///   Replace : the hit becomes the only selected entity
      ///   Add     : the hit is added (no change if it is already selected)
      ///   Remove  : the hit is taken out of the selection. If that exact entity
      ///             is not selected but its whole part is, the part is deselected.
      bool Select(const PickResult& hit, SelectMode mode = SelectMode::Replace);

      /// Selects one whole node - for a group, its subtree - replacing the
      /// current selection. -1 clears the selection.
      void SetSelectedNode(int id);
      void ClearSelection() { m_settings.selection.clear(); }

      /// Everything that is selected, in the order it was selected.
      const std::vector<SelectedEntity>& Selection() const { return m_settings.selection; }

      /// Gives a mesh its real B-Rep topology (optional; otherwise it is derived):
      ///   faceOfTriangle : one face number per triangle, or nullptr
      ///   vertices       : x,y,z per B-Rep vertex, or nullptr
      bool SetMeshTopology(int id, const std::uint32_t* faceOfTriangle, std::size_t triangleCount,
         const float* vertices, std::size_t vertexCount);

      Scene& GetScene() { return m_scene; }
      Camera& GetCamera() { return m_camera; }

      private:
      IGLContext* m_context;
      Scene          m_scene;
      Camera         m_camera;
      RenderSettings m_settings;
      Renderer       m_renderer;
      bool           m_initialized = false;
      unsigned       m_pickTypes = 0;          // 0 = anything
      unsigned       m_filterOut = 0;
      float          m_snapTolerance = 0.5f;   // mm
   };

} // namespace CadCore
