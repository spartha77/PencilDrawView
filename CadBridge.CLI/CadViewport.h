#pragma once
// =============================================================================
// CadBridge.CLI / CadViewport.h
//
// THE BRIDGE between .NET (WPF, C#) and the native OpenGL engine.
//
// This is C++/CLI: one DLL that contains both managed and native code, so a
// managed class can simply hold a pointer to a native object and call it.
// (That is different from P/Invoke, where C# calls exported C functions one
// by one. With C++/CLI the C# side just sees an ordinary .NET class and no
// [DllImport] declarations are needed.)
//
//     C# (WPF)  --->  CadBridge.CadViewport (this class, managed)
//                         |
//                         +--->  CadCore::Viewer      (native engine facade)
//                         +--->  CadCore::WglContext  (native OpenGL context)
//
// RULES FOR THE C# SIDE
//   * Create it with the HWND of the child window to draw into.
//   * Call everything from the UI thread.
//   * Call Dispose() (or use `using`) before the window is destroyed.
//   * Scene nodes are referred to by integer IDs; 0 (RootId) is the root.
//   * Native errors arrive as System::InvalidOperationException.
// =============================================================================

// Native types are only named here; their headers are included in the .cpp.
namespace CadCore {
   class Viewer;
   class WglContext;
}

namespace CadBridge {

   /// How the 3D scene is projected onto the screen.
   public enum class ProjectionKind {
      Orthographic = 0,   ///< parallel projection, no vanishing point
      Perspective = 1,   ///< free perspective camera
      OnePoint = 2,   ///< perspective with exactly one vanishing point
      TwoPoint = 3    ///< perspective with two vanishing points (verticals stay vertical)
   };

   public enum class StandardViewKind {
      Isometric = 0, Front = 1, Back = 2, Left = 3, Right = 4, Top = 5, Bottom = 6
   };

   public enum class DisplayKind {
      Shaded = 0,
      ShadedWithEdges = 1,
      Wireframe = 2
   };

   /// The two lighting configurations.
   public enum class LightingKind {
      AmbientOnly = 0,    ///< ambient light only - no light sources
      Lights = 1     ///< light sources (two by default) plus a little ambient fill
   };

   public enum class LightKind {
      Directional = 0,    ///< uses the direction only
      Point = 1,    ///< uses the position only
      Spot = 2     ///< uses position AND direction
   };

   public enum class MaterialPreset {
      Steel = 0, Aluminium = 1, Brass = 2, Glass = 3
   };

   /// Entity types for picking. A [Flags] enum: combine with | in C#,
   /// e.g. PickType.Vertex | PickType.Edge.
   [System::Flags]
      public enum class PickType {
      None = 0,
      Vertex = 1,     ///< corner point; the pick snaps onto it
      Edge = 2,     ///< edge curve; the pick snaps to the nearest point on it
      Face = 4,     ///< one surface
      Solid = 8,     ///< the whole part
      All = 15
   };

   /// What CadViewport.Pick found under the mouse.
   public value struct PickHit {
      int      NodeId;    ///< scene node the entity belongs to; -1 = nothing there
      PickType Type;      ///< exactly one of Vertex / Edge / Face / Solid (None when nothing)
      int      Index;     ///< vertex / edge / face number inside the node (-1 for Solid)
      float    X, Y, Z;   ///< world position in mm (snapped for Vertex and Edge)
      bool     Blocked;   ///< true = this entity's type is in FilterOutPickTypes

      /// True when something pickable was found.
      property bool IsHit{ bool get() { return NodeId >= 0 && !Blocked; } }
   };

   public ref class CadViewport sealed {
      public:
      /// ID of the scene root. Pass it as parentId to add top-level nodes.
      literal int RootId = 0;

      /// Creates the OpenGL context on `hwnd` and initialises the engine.
      explicit CadViewport(System::IntPtr hwnd);

      /// Dispose(): frees the scene, shaders and the OpenGL context.
      ~CadViewport();
      /// Finalizer: see the note in the .cpp - GL objects cannot be freed here.
      !CadViewport();

      /// Driver / GPU description, e.g. "4.6.0 NVIDIA 566.03 | RTX A2000 | GLSL 4.60".
      property System::String^ GLVersion{ System::String ^ get(); }

         // ---- frame ----------------------------------------------------------------
      void Resize(int widthPx, int heightPx);
      void Render();

      // ---- camera ---------------------------------------------------------------
      void Orbit(float dxPx, float dyPx);
      void Pan(float dxPx, float dyPx);
      void Zoom(float wheelNotches);
      void SetProjection(ProjectionKind kind);
      void SetStandardView(StandardViewKind view);
      void SetFieldOfView(float degrees);
      void FitAll();

      // ---- display --------------------------------------------------------------
      void SetDisplayMode(DisplayKind kind);
      /// Edge line width in pixels (1.5 normal, about 3.5 for bold edges).
      void SetEdgeWidth(float pixels);
      void SetBackground(float r, float g, float b);
      /// Grid in the XY plane centred on the origin. sizeMm = full width
      /// (1000 = 1 m x 1 m, 10000 = 10 m x 10 m); minorSpacingMm = finest lines.
      void SetGrid(bool visible, float sizeMm, float minorSpacingMm);
      /// Axis arrows at the origin; they keep a constant size on screen.
      void SetAxes(bool visible, float pixelsPerMm);

      // ---- lighting -------------------------------------------------------------
      void SetLightingMode(LightingKind kind);
      /// Switches the ambient / diffuse / specular terms on or off.
      void SetLightingTerms(bool ambient, bool diffuse, bool specular);
      /// Ambient light colour and strength, stored separately for each mode.
      void SetAmbientLight(LightingKind forMode, float r, float g, float b, float intensity);
      /// Number of light sources used in Lights mode (1..8, default 2).
      void SetLightCount(int count);
      /// Sets light `index` (0-based). Position in mm; direction = where it shines.
      void SetLight(int index, LightKind kind,
         float posX, float posY, float posZ,
         float dirX, float dirY, float dirZ,
         float r, float g, float b, float intensity);
      /// Spot cone half-angles in degrees; rangeMm = 0 means no distance fall-off.
      void SetSpotCone(int index, float innerDeg, float outerDeg, float rangeMm);
      void SetLightEnabled(int index, bool enabled);

      // ---- scene graph ----------------------------------------------------------
      int AddGroup(int parentId, System::String^ name);

      /// Adds a triangle mesh.
      ///   positions    : x,y,z per vertex
      ///   normals      : x,y,z per vertex, or null to compute smooth normals
      ///   indices      : three per triangle
      ///   edgeSegments : B-Rep edges as line segments (x,y,z,x,y,z per segment),
      ///                  or null to derive feature edges from the triangles
      int AddMesh(int parentId, System::String^ name, bool isBRep,
         cli::array<float>^ positions, cli::array<float>^ normals,
         cli::array<System::UInt32>^ indices, cli::array<float>^ edgeSegments);

      /// Loads a binary or ASCII STL file.
      int AddStlFile(int parentId, System::String^ path);

      /// positions: x,y,z per point. colors: r,g,b (0..1) per point, or null.
      int AddPointCloud(int parentId, System::String^ name,
         cli::array<float>^ positions, cli::array<float>^ colors,
         float pointSizePx);

      bool RemoveNode(int id);
      void ClearScene();

      // ---- picking and selection ------------------------------------------------
      /// Entity types that may be picked. PickType::None (empty) = anything.
      /// When several types are allowed the most specific one under the mouse
      /// wins: Vertex, then Edge, then Face, then Solid.
      property PickType PickTypes{ PickType get(); void set(PickType value); }

         /// Entity types that must NOT be picked. Pick() reports them with
         /// Blocked = true (the host shows a "not allowed" cursor over them).
         /// Wins over PickTypes.
      property PickType FilterOutPickTypes{ PickType get(); void set(PickType value); }

         /// Snap distance in mm for Vertex and Edge picks (default 0.5).
      property float SnapToleranceMm{ float get(); void set(float value); }

         /// The entity under a viewport pixel (x from the left, y from the top).
      PickHit Pick(int xPx, int yPx);

      // Several entities can be selected at once. A miss or a blocked hit never
      // changes the selection. Each call returns true if the selection changed.

      /// Plain click: the hit becomes the only selected entity.
      bool Select(PickHit hit);
      /// Ctrl + click: adds the hit (no change if it is already selected).
      bool AddToSelection(PickHit hit);
      /// Shift + click: removes the hit from the selection. If that exact entity
      /// is not selected but its whole part is, the part is deselected.
      bool RemoveFromSelection(PickHit hit);

      /// Selects one whole node (for a group, everything below it), replacing
      /// the current selection. -1 = no selection.
      void SetSelectedNode(int id);
      void ClearSelection();

      /// Number of selected entities.
      property int SelectionCount{ int get(); }
         /// The selected entities, in the order they were selected.
      cli::array<PickHit>^ GetSelection();

      /// Optional: the real B-Rep topology of a mesh, when the CAD kernel knows it.
      ///   faceOfTriangle : one face number per triangle, or null
      ///   vertexPoints   : x,y,z per B-Rep vertex, or null
      /// Without this, faces / edges / vertices are derived from the geometry.
      bool SetMeshTopology(int id, cli::array<System::UInt32>^ faceOfTriangle,
         cli::array<float>^ vertexPoints);
      /// Colour that selected parts are drawn in.
      void SetHighlightColor(float r, float g, float b);

      /// The node's "show" flag. Hiding a group hides everything below it.
      bool SetNodeVisible(int id, bool visible);
      /// 16 floats, column-major (translation in elements 12, 13, 14).
      bool SetNodeTransform(int id, cli::array<float>^ columnMajor16);
      bool SetNodeColor(int id, float r, float g, float b);
      bool SetNodeOpacity(int id, float opacity);
      bool SetNodeMaterial(int id,
         float diffuseR, float diffuseG, float diffuseB,
         float specularR, float specularG, float specularB,
         float shininess, float opacity);
      bool ApplyMaterialPreset(int id, MaterialPreset preset);

#ifdef CADBRIDGE_WITH_OCCT
      /// Tessellates an OpenCASCADE shape and adds it as a B-Rep mesh with its
      /// true topological edges. `topoDsShape` is a native `const TopoDS_Shape*`.
      /// Only available when the project is built with CADBRIDGE_WITH_OCCT.
      int AddBRepShape(int parentId, System::String^ name, System::IntPtr topoDsShape,
         double linearDeflectionMm, double angularDeflectionRad);
#endif

      private:
      CadCore::Viewer* Native();          // throws ObjectDisposedException if disposed

      CadCore::WglContext* m_context;
      CadCore::Viewer* m_viewer;
   };

} // namespace CadBridge
