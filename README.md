# CadGL — OpenGL 4.6 CAD renderer for WPF

Three projects, one solution (`CadGL.sln`):

| Project | Type | Language | Role |
|---|---|---|---|
| `CadCore.GL` | `.vcxproj`, static library | native C++17 | The renderer. Cross-platform, no Windows code except one file. |
| `CadBridge.CLI` | `.vcxproj`, CLR class library | C++/CLI | Thin wrapper that exposes the renderer to .NET. |
| `CadRenderer.WPF` | `.csproj` | C# / WPF | The application. Contains no OpenGL code. |

```
 C# / WPF            MainWindow  ──►  GLViewportHost (HwndHost: native child window, mouse)
 CadRenderer.WPF          │
──────────────────────────┼─────────────────────────────────────────────────────────────
 C++/CLI                  ▼
 CadBridge.CLI       CadBridge.CadViewport   (managed class holding native pointers)
──────────────────────────┼─────────────────────────────────────────────────────────────
 native C++               ▼
 CadCore.GL          Viewer (facade) ─► Scene (tree of nodes) ─► Renderer ─► OpenGL 4.6
                          └─ Camera, RenderSettings, WglContext (Windows only)
```

## File structure

```
CadGL/
├── CadGL.sln
├── vcpkg.json                      GLEW dependency (vcpkg manifest)
├── README.md
├── docs/                           screenshots from the test renders
│
├── CadCore.GL/                     ── native OpenGL engine (static lib) ──
│   ├── CadCore.GL.vcxproj
│   ├── CMakeLists.txt              builds the same sources on Linux / macOS
│   ├── include/CadCore/
│   │   ├── Math.h                  Vec3, Mat4, AABB (no GLM needed)
│   │   ├── GLContext.h             IGLContext: the only platform abstraction
│   │   ├── GpuObjects.h            RAII buffer + VAO (Direct State Access)
│   │   ├── ShaderProgram.h         compile / link / set uniforms
│   │   ├── Shaders.h               all GLSL sources
│   │   ├── Material.h              ambient / diffuse / specular / opacity
│   │   ├── Light.h                 lights + the two lighting configurations
│   │   ├── Camera.h                orbit, pan, zoom, 4 projection modes
│   │   ├── SceneNode.h             Node base class, GroupNode
│   │   ├── MeshNode.h              triangles + edges (B-Rep, STL)
│   │   ├── PointCloudNode.h        points
│   │   ├── CurveNode.h             lines / arcs, drawn as polylines
│   │   ├── Scene.h                 owns the tree, hands out node IDs
│   │   ├── StlLoader.h             binary + ASCII STL
│   │   ├── Renderer.h              draws one frame
│   │   └── Viewer.h                facade the host application calls
│   ├── src/
│   │   ├── ShaderProgram.cpp   Scene.cpp     SceneNodes.cpp
│   │   ├── StlLoader.cpp       Renderer.cpp  Viewer.cpp
│   ├── platform/
│   │   └── WglContext.h/.cpp       Windows: creates the 4.6 core context on an HWND
│   └── examples/
│       └── glfw_viewer.cpp         stand-alone host (Linux/macOS), used for testing
│
├── CadBridge.CLI/                  ── C++/CLI bridge (CLR dll) ──
│   ├── CadBridge.CLI.vcxproj
│   ├── CadViewport.h/.cpp          public ref class CadViewport
│   └── Optional/
│       └── OcctTessellator.h/.cpp  OpenCASCADE B-Rep → triangles + edges (off by default)
│
└── CadRenderer.WPF/                ── WPF application ──
    ├── CadRenderer.WPF.csproj
    ├── App.xaml(.cs)
    ├── MainWindow.xaml(.cs)        control panel + scene tree
    ├── GLViewportHost.cs           HwndHost: child window, mouse → camera
    ├── SceneItem.cs                one row of the scene tree
    ├── NoPickCursor.cs             the red "cannot pick" mouse pointer
    ├── Model/                      ── model: no drawing code, no "using CadBridge" ──
    │   ├── IEntity.cs              IEntity, IGeometry, ITopology
    │   ├── Arc.cs                  Arc (geometry), Point3
    │   └── Edge.cs                 Edge (topology: holds a curve)
    ├── ViewModels/                 ── view models: know how to show an entity ──
    │   ├── IDrawable.cs            interface with Draw()
    │   ├── ArcVM.cs                takes an IEntity, builds and draws its arc
    │   └── NodeVM.cs               places an existing part / group (was Entity.cs)
    ├── Transform.cs                4x4 matrices for SetNodeTransform
    ├── FixedStepClock.cs           fixed time step for simulations
    └── DemoGeometry.cs             test box and point cloud
```

## Dependencies

| Dependency | Needed by | Purpose | How to get it |
|---|---|---|---|
| **GLEW** 2.1+ | CadCore.GL | Loads the OpenGL 4.x functions from the driver | `glew-2.3.1` folder next to the `.sln` (below) |
| **opengl32.lib** | CadBridge.CLI | Windows' OpenGL entry point | Windows SDK |
| gdi32 / user32 | CadBridge.CLI | Pixel format, device context | Windows SDK |
| C++/CLI support | CadBridge.CLI | Compiling `/clr` code | VS Installer → *C++/CLI support for build tools* |
| .NET 10 SDK | WPF + bridge | Target framework | Ships with Visual Studio 2026 |
| GLFW 3 | example host only | Window on Linux/macOS | optional |
| OpenCASCADE 7.6+ | optional tessellator | B-Rep → triangles | optional |

Not used: GLM, GLFW (in the WPF build), SharpGL, OpenTK, or any NuGet package.

A graphics driver with **OpenGL 4.5 or newer** is required (4.6 is used when present).

## Building (Visual Studio 2026)

1. Visual Studio Installer: workloads *Desktop development with C++* and *.NET desktop
   development*, plus the individual component *C++/CLI support for v145 build tools*.
2. Put GLEW in a folder `glew-2.3.1` next to `CadGL.sln`, with `include\GL\glew.h`,
   `lib\Debug\x64\glew32sd.lib` and `lib\Release\x64\glew32s.lib` (see the note below).
3. Open `CadGL.sln`, choose **x64**, set `CadRenderer.WPF` as start-up project, build.

Notes
- **Visual Studio 2022:** change `<PlatformToolset>v145</PlatformToolset>` to `v143` in both
  `.vcxproj` files, and `net10.0` to `net8.0` in the bridge and the `.csproj`.
- **GLEW from a folder (how the two C++ projects are set up now):** vcpkg is switched off in
  both projects. They expect GLEW in `glew-2.3.1` next to `CadGL.sln`, through two user
  macros defined in each `.vcxproj`:
  `$(GLEW_INCLUDES)` = `$(SolutionDir)glew-2.3.1\include` (C/C++ > Additional Include
  Directories) and `$(GLEW_LIB_INCLUDES)` = `$(SolutionDir)glew-2.3.1\lib\$(Configuration)\$(Platform)`
  (CadCore.GL > Librarian > Additional Library Directories). Debug links
  `glew32sd.lib;glew32d.lib`, Release links `glew32s.lib;glew32.lib`; the static one is
  listed first and, with `GLEW_STATIC` defined, is the one used, so no GLEW DLL is deployed.
  To use vcpkg instead, remove the macros and the `<Lib>` entries and set
  `VcpkgEnableManifest` back to `true`.
- Build from Visual Studio or `msbuild`; `dotnet build` cannot build the C++ projects.

## Where each requirement lives

| Requirement | Implementation |
|---|---|
| Zoom / orbit / pan | `Camera.h` (`Orbit`, `Pan`, `Zoom`); mouse in `GLViewportHost.cs` |
| Orthographic | `ProjectionMode::Orthographic` |
| Perspective, 1 or 2 vanishing points | `ProjectionMode::OnePoint` / `TwoPoint` — see the comment at the top of `Camera.h` |
| Multiple lights at given positions | `Light.h`, up to 8, uploaded as a uniform buffer |
| **Lighting config 1: ambient only** | `LightingMode::AmbientOnly` |
| **Lighting config 2: two lights, position + direction** | `LightingMode::Lights`, `lightCount = 2` |
| Ambient / diffuse / specular on-off | `LightingSettings::useAmbient / useDiffuse / useSpecular` |
| Grid in XY, 1000 × 1000 mm, 1 mm lines | `kGridFragment` in `Shaders.h`; size is a setting (10 000 mm = 10 m) |
| Axis arrows, constant size, at origin | `BuildArrow` + step 7 in `Renderer.cpp` |
| B-Rep, STL, point clouds | `MeshNode`, `StlLoader`, `PointCloudNode` |
| Material and colour | `Material.h`, `SetNodeMaterial`, `SetNodeColor` |
| Bold edge highlight | `SetEdgeWidth(3.5f)`; geometry shader `kLineGeometry` |
| Scene tree with show flags | `SceneNode.h`, `Scene.h`, `Renderer::Collect` |
| Modern OpenGL | 4.6 core profile, DSA, VAOs, UBOs, MSAA framebuffer |
| Cross-platform GL layer | everything in `CadCore.GL` except `platform/` |

## Using it from C#

```csharp
CadViewport vp = ViewportHost.Viewport;          // available after ViewportReady

// --- lighting configuration 1: ambient only ---
vp.SetLightingMode(LightingKind.AmbientOnly);

// --- lighting configuration 2: two lights with position and direction ---
vp.SetLightingMode(LightingKind.Lights);
vp.SetLightCount(2);
//           index  type            position (mm)        direction         colour   intensity
vp.SetLight(0, LightKind.Spot,  600, -700, 900,   -600, 700, -900,   1, 1, 1,  0.85f);
vp.SetLight(1, LightKind.Spot, -700,  500, 400,    700, -500, -400,  1, 1, 1,  0.40f);
vp.SetLightingTerms(ambient: true, diffuse: true, specular: true);

// --- grid: 1 m (as specified) or 10 m, 1 mm finest lines, origin at the centre ---
vp.SetGrid(visible: true, sizeMm: 10000f, minorSpacingMm: 1f);

// --- scene tree ---
int assembly = vp.AddGroup(CadViewport.RootId, "Assembly");
int part  = vp.AddMesh(assembly, "Bracket", isBRep: true, positions, normals, indices, edgeSegments);
int stl   = vp.AddStlFile(assembly, @"C:\parts\cover.stl");
int cloud = vp.AddPointCloud(CadViewport.RootId, "Scan", xyz, rgb, pointSizePx: 3f);

vp.ApplyMaterialPreset(part, MaterialPreset.Steel);
vp.SetNodeColor(stl, 0.9f, 0.5f, 0.1f);
vp.SetNodeVisible(assembly, false);              // hides the whole subtree

// --- view ---
vp.SetProjection(ProjectionKind.TwoPoint);
vp.SetDisplayMode(DisplayKind.ShadedWithEdges);
vp.SetEdgeWidth(3.5f);                           // bold edges
vp.FitAll();
ViewportHost.RequestRender();                    // nothing is drawn until you ask
```

Conventions: millimetres; right-handed world with **Z up**; the grid is the XY plane.

## Picking and selection

Left click in the view picks the entity under the mouse and highlights it.

| Type | What is picked | Highlight |
|---|---|---|
| Vertex | a corner point, if within 0.5 mm of the cursor's point on the surface; the pick snaps onto it | dot |
| Edge | an edge curve, if within 0.5 mm; snaps to the nearest point on the edge | thick line + dot at the snapped point |
| Face | the surface under the cursor | face in the highlight colour |
| Solid | the whole part | part in the highlight colour |

```csharp
vp.PickTypes          = PickType.Vertex | PickType.Edge;   // empty (PickType.None) = anything
vp.FilterOutPickTypes = PickType.Face;                     // never picked; Pick() reports Blocked
vp.SnapToleranceMm    = 0.5f;

PickHit hit = vp.Pick(x, y);          // x from the left, y from the top of the viewport
if (hit.IsHit) vp.Select(hit);        // hit.Type, hit.Index, hit.X/Y/Z (snapped, mm)
```

Several entities can be selected at once:

| Input | Effect |
|---|---|
| click | select only this entity; on empty space: deselect all |
| Ctrl + click | add to the selection (`AddToSelection`); empty space: no change |
| Shift + click | remove from the selection (`RemoveFromSelection`); empty space: no change |

`GetSelection()` returns the selected entities; `SelectionCount` their number.

- When several types are allowed, the most specific one under the mouse wins: vertex, edge,
  face, solid. A type in `FilterOutPickTypes` is reported as `Blocked` instead of falling
  through to the next type; the viewport shows the red "cannot pick" pointer over it.
- Faces, edges and vertices are derived from the mesh (`MeshNode::BuildTopology`): a face is
  a patch of triangles meeting at less than 30 degrees, an edge is a chain of edge segments,
  a vertex is where edges end or turn a corner. A CAD kernel can supply the real topology
  with `SetMeshTopology`.
- In a point cloud a single point counts as a Vertex and the cloud as a Solid.
- A curve counts as an Edge, its two ends as Vertices (index 0 = start, 1 = end).

## Model entities and view models

The model does not draw. A view model takes a model entity and implements `IDrawable`.

```
CadModel (no drawing code)                 CadRenderer.WPF (view models)
--------------------------                 -----------------------------
IEntity                                    IDrawable { void Draw(); }
 ├─ IGeometry      Arc                      ├─ ArcVM(IEntity, viewport)   builds + draws an arc
 └─ ITopology      Edge { Geometry }        └─ NodeVM(viewport, nodeId)   places an existing part
    (container of the geometry it holds)
```

```csharp
var arc  = new Arc(new Point3(0, 0, 0), radius: 50, startAngleDegrees: 0, sweepAngleDegrees: 90);
var edge = new Edge(arc);                        // topology: a container holding the arc

var vm = new ArcVM(edge, ViewportHost.Viewport); // or new ArcVM(arc, ...): same picture
ViewportHost.Drawables.Add(vm);

arc.SweepAngleDegrees = 180;                     // change the MODEL only
ViewportHost.Redraw();                           // ArcVM.Draw() notices and updates the view
```

- `ArcVM`'s constructor cuts the arc into short straight pieces (never further than
  `ChordToleranceMm`, default 0.01 mm, from the true arc) and creates a curve node with
  `AddPolyline`. `Draw()` re-cuts and re-sends the points only if the arc's numbers changed.
- Curves show in every display mode. They are picked as Edge (snapped to the nearest point
  of the curve), Vertex (either end, within the snap tolerance) or Solid (the whole curve).
- A view model for another entity type follows the same pattern: a class implementing
  `IDrawable` whose constructor takes the `IEntity`, creates the scene node(s), and whose
  `Draw()` brings them up to date.

## Simulation and animation

```csharp
// A view model draws itself: override Draw, set Matrix, call base.Draw.
class Slide : NodeVM
{
    public double Position;
    public Slide(CadViewport viewport, int nodeId) : base(viewport, nodeId) { }
    public override void Draw()
    {
        Matrix = Transform.Translation((float)Position, 0f, 0f);   // transform
        base.Draw();                                               // draw
    }
}

var slide = new Slide(vp, vp.AddMesh(...));
ViewportHost.Drawables.Add(slide);
ViewportHost.StartSimulation(OnContinuousRender);        // every display frame

void OnContinuousRender(long elapsedMicroseconds)        // time since the previous call
{
    slide.Position += 50.0 * elapsedMicroseconds / 1e6;  // 1. advance the model (50 mm/s)
    ViewportHost.Redraw();                               // 2. Draw() on every drawable, show the frame
}

ViewportHost.StopSimulation();                           // back to drawing on demand
```

- `Redraw()` calls `Draw()` on each `IDrawable` in `ViewportHost.Drawables`, then paints the
  frame. `Draw` hands the placement (and changed geometry) to the engine; the engine paints
  everything in one pass, because the order matters (surfaces, then edges, then transparent
  parts sorted back to front, then the selection).
- The callback runs on the UI thread once per frame the monitor displays (60, 120, 144 Hz ...),
  driven by WPF's `CompositionTarget.Rendering`. `MeasuredFrequencyHz` reports the achieved rate.
- For a model that must advance at an exact rate of its own (e.g. 128 Hz), use
  `FixedStepClock`: it turns the varying frame time into equal steps.
- **What is redrawn:** every frame redraws the whole visible scene; that is how GPU rendering
  (and OpenSceneGraph) works. What the scene graph saves is the expensive part: a part's
  triangles are uploaded to graphics memory once and stay there. Moving a part changes only
  its 4x4 matrix. Only `AddMesh` / changed geometry uploads data again.
- `Transform.cs` builds the matrices; `MaxElapsedMicroseconds` (default 0.1 s) limits the step
  after a stall.

## B-Rep display

OpenGL draws triangles and lines; it cannot evaluate B-Rep surfaces. A CAD kernel must
tessellate first. `AddMesh(..., isBRep: true, ...)` takes exactly what a tessellator
produces: face triangles with normals, plus the topological edges as line segments (those
are the lines the edge overlay draws).

`CadBridge.CLI/Optional/OcctTessellator.cpp` does this for an OpenCASCADE `TopoDS_Shape`
and adds `CadViewport.AddBRepShape(...)`. It is excluded from the build by default; the
file header lists the three steps to enable it.

For meshes without edge data (STL), edges are derived automatically: boundary edges and
edges where two faces meet at more than 30° are kept, so smooth regions stay clean.

## What was tested

The native engine was compiled with GCC (`-Wall -Wextra`, no warnings) and run through the
GLFW host on Mesa's software OpenGL 4.5 driver; `docs/` holds the resulting frames
(lighting modes, wireframe, bold edges, all four projections, 1 mm grid, 10 m grid, binary
and ASCII STL). The C# code was type-checked against stand-ins for WPF and the bridge.

Not compiled or run, because they need Windows and Visual Studio: `WglContext.cpp`, the
C++/CLI bridge, the XAML, the three project files, and the optional OCCT tessellator.
Expect to fix small build issues there on the first build.

## Known limits

- WPF controls cannot be drawn on top of the viewport (HwndHost "airspace").
- Picking is a CPU ray test against every triangle of the parts under the cursor; fine for typical parts, slow for meshes of many millions of triangles.
- Silhouette outlines of smooth surfaces (the sides of a cylinder) are not generated;
  only real edges are drawn.
- Transparent parts are sorted per object, not per triangle.
- In 1- and 2-point perspective the camera elevation is limited to ±75° (a view straight
  down cannot keep vertical lines parallel).
