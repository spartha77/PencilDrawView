// =============================================================================
// CadBridge.CLI / CadViewport.cpp
//
// Each method does three things and nothing more:
//   1. convert managed arguments to native ones (String^ -> std::string,
//      array<float>^ -> pinned const float*),
//   2. call the native CadCore::Viewer,
//   3. turn native C++ exceptions into .NET exceptions.
// No rendering logic lives here; it is all in CadCore.GL.
// =============================================================================
#include "CadViewport.h"

#include "CadCore/Viewer.h"      // native engine facade
#include "WglContext.h"          // native Windows OpenGL context

#ifdef CADBRIDGE_WITH_OCCT
#include "Optional/OcctTessellator.h"
#endif

#include <exception>
#include <string>
#include <vector>

using namespace System;

// A native exception must never cross into managed code as-is (the CLR would
// report an opaque SEHException). Every call into the engine is wrapped.
#define CAD_TRY   try {
#define CAD_CATCH } catch (const std::exception& e) { \
                      throw gcnew InvalidOperationException(gcnew String(e.what())); }

namespace CadBridge {

namespace {

/// .NET strings are UTF-16; the engine uses UTF-8 (file names with non-ASCII
/// characters survive this way).
std::string ToUtf8(String^ s) {
    if (String::IsNullOrEmpty(s)) return std::string();
    cli::array<Byte>^ bytes = Text::Encoding::UTF8->GetBytes(s);
    pin_ptr<Byte> pinned = &bytes[0];
    return std::string(reinterpret_cast<const char*>(pinned), static_cast<size_t>(bytes->Length));
}

/// Throws unless `a` is non-null, non-empty and its length is a multiple of `stride`.
void Require(cli::array<float>^ a, int stride, String^ what) {
    if (a == nullptr || a->Length == 0 || a->Length % stride != 0)
        throw gcnew ArgumentException(what + " must contain a multiple of " + stride + " values.");
}

CadCore::LightingMode ToNative(LightingKind k) { return static_cast<CadCore::LightingMode>(static_cast<int>(k)); }

} // namespace

// -----------------------------------------------------------------------------
// Lifetime
// -----------------------------------------------------------------------------

CadViewport::CadViewport(IntPtr hwnd) : m_context(nullptr), m_viewer(nullptr) {
    if (hwnd == IntPtr::Zero) throw gcnew ArgumentException("hwnd must be a valid window handle.");
    try {
        m_context = new CadCore::WglContext(hwnd.ToPointer());
        m_viewer  = new CadCore::Viewer(m_context);
        m_viewer->Initialize();
    } catch (const std::exception& e) {
        // Undo whatever was created before reporting the error.
        delete m_viewer;  m_viewer = nullptr;
        delete m_context; m_context = nullptr;
        throw gcnew InvalidOperationException(gcnew String(e.what()));
    }
}

// Dispose(): runs on the caller's (UI) thread, where the GL context lives, so
// the native objects can be destroyed properly. Order matters: the viewer
// frees its GL objects first, then the context goes away.
CadViewport::~CadViewport() {
    delete m_viewer;  m_viewer = nullptr;
    delete m_context; m_context = nullptr;
}

// Finalizer: runs on the GC's finalizer thread. An OpenGL context belongs to
// the thread it is current on, so GL objects cannot be released from here.
// If Dispose() was forgotten, the driver reclaims everything at process exit;
// touching the context from this thread would be worse than the leak.
CadViewport::!CadViewport() {
}

CadCore::Viewer* CadViewport::Native() {
    if (m_viewer == nullptr) throw gcnew ObjectDisposedException("CadViewport");
    return m_viewer;
}

String^ CadViewport::GLVersion::get() {
    return gcnew String(Native()->GLVersion().c_str());
}

// -----------------------------------------------------------------------------
// Frame
// -----------------------------------------------------------------------------

void CadViewport::Resize(int widthPx, int heightPx) {
    CAD_TRY Native()->Resize(widthPx, heightPx); CAD_CATCH
}

void CadViewport::Render() {
    CAD_TRY Native()->Render(); CAD_CATCH
}

// -----------------------------------------------------------------------------
// Camera
// -----------------------------------------------------------------------------

void CadViewport::Orbit(float dxPx, float dyPx) { Native()->Orbit(dxPx, dyPx); }
void CadViewport::Pan(float dxPx, float dyPx)   { Native()->Pan(dxPx, dyPx); }
void CadViewport::Zoom(float wheelNotches)      { Native()->Zoom(wheelNotches); }

void CadViewport::SetProjection(ProjectionKind kind) {
    Native()->SetProjection(static_cast<CadCore::ProjectionMode>(static_cast<int>(kind)));
}

void CadViewport::SetStandardView(StandardViewKind view) {
    Native()->SetStandardView(static_cast<CadCore::StandardView>(static_cast<int>(view)));
}

void CadViewport::SetFieldOfView(float degrees) { Native()->SetFieldOfView(degrees); }
void CadViewport::FitAll()                      { Native()->FitAll(); }

// -----------------------------------------------------------------------------
// Display
// -----------------------------------------------------------------------------

void CadViewport::SetDisplayMode(DisplayKind kind) {
    Native()->Settings().displayMode = static_cast<CadCore::DisplayMode>(static_cast<int>(kind));
}

void CadViewport::SetEdgeWidth(float pixels) {
    Native()->Settings().edgeWidthPx = pixels;
}

void CadViewport::SetBackground(float r, float g, float b) {
    Native()->Settings().background = CadCore::Vec3(r, g, b);
}

void CadViewport::SetGrid(bool visible, float sizeMm, float minorSpacingMm) {
    if (sizeMm <= 0.0f || minorSpacingMm <= 0.0f)
        throw gcnew ArgumentException("Grid size and spacing must be positive.");
    CadCore::RenderSettings& s = Native()->Settings();
    s.showGrid = visible;
    s.gridSizeMm = sizeMm;
    s.gridMinorSpacingMm = minorSpacingMm;
}

void CadViewport::SetAxes(bool visible, float pixelsPerMm) {
    CadCore::RenderSettings& s = Native()->Settings();
    s.showAxes = visible;
    if (pixelsPerMm > 0.0f) s.axisPixelsPerMm = pixelsPerMm;
}

// -----------------------------------------------------------------------------
// Lighting
// -----------------------------------------------------------------------------

void CadViewport::SetLightingMode(LightingKind kind) {
    Native()->Settings().lighting.mode = ToNative(kind);
}

void CadViewport::SetLightingTerms(bool ambient, bool diffuse, bool specular) {
    CadCore::LightingSettings& l = Native()->Settings().lighting;
    l.useAmbient  = ambient;
    l.useDiffuse  = diffuse;
    l.useSpecular = specular;
}

void CadViewport::SetAmbientLight(LightingKind forMode, float r, float g, float b, float intensity) {
    CadCore::LightingSettings& l = Native()->Settings().lighting;
    l.ambientColor = CadCore::Vec3(r, g, b);
    if (forMode == LightingKind::AmbientOnly) l.ambientOnlyIntensity = intensity;
    else                                      l.litAmbientIntensity  = intensity;
}

void CadViewport::SetLightCount(int count) {
    if (count < 0 || count > CadCore::kMaxLights)
        throw gcnew ArgumentOutOfRangeException("count", "The engine supports 0 to 8 lights.");
    Native()->Settings().lighting.lightCount = count;
}

void CadViewport::SetLight(int index, LightKind kind,
                           float posX, float posY, float posZ,
                           float dirX, float dirY, float dirZ,
                           float r, float g, float b, float intensity) {
    if (index < 0 || index >= CadCore::kMaxLights)
        throw gcnew ArgumentOutOfRangeException("index", "Light index must be 0 to 7.");
    CadCore::Light& light = Native()->Settings().lighting.lights[index];
    light.type      = static_cast<CadCore::LightType>(static_cast<int>(kind));
    light.position  = CadCore::Vec3(posX, posY, posZ);
    light.direction = CadCore::Vec3(dirX, dirY, dirZ);
    light.color     = CadCore::Vec3(r, g, b);
    light.intensity = intensity;
}

void CadViewport::SetSpotCone(int index, float innerDeg, float outerDeg, float rangeMm) {
    if (index < 0 || index >= CadCore::kMaxLights)
        throw gcnew ArgumentOutOfRangeException("index", "Light index must be 0 to 7.");
    CadCore::Light& light = Native()->Settings().lighting.lights[index];
    light.spotInnerDeg = innerDeg;
    light.spotOuterDeg = outerDeg;
    light.range        = rangeMm;
}

void CadViewport::SetLightEnabled(int index, bool enabled) {
    if (index < 0 || index >= CadCore::kMaxLights)
        throw gcnew ArgumentOutOfRangeException("index", "Light index must be 0 to 7.");
    Native()->Settings().lighting.lights[index].enabled = enabled;
}

// -----------------------------------------------------------------------------
// Scene graph
// -----------------------------------------------------------------------------

int CadViewport::AddGroup(int parentId, String^ name) {
    CAD_TRY return Native()->AddGroup(parentId, ToUtf8(name)); CAD_CATCH
}

int CadViewport::AddMesh(int parentId, String^ name, bool isBRep,
                         cli::array<float>^ positions, cli::array<float>^ normals,
                         cli::array<UInt32>^ indices, cli::array<float>^ edgeSegments) {
    Require(positions, 3, "positions");
    if (indices == nullptr || indices->Length == 0 || indices->Length % 3 != 0)
        throw gcnew ArgumentException("indices must contain three values per triangle.");
    if (normals != nullptr && normals->Length != positions->Length)
        throw gcnew ArgumentException("normals must have the same length as positions.");
    const bool hasEdges = edgeSegments != nullptr && edgeSegments->Length > 0;
    if (hasEdges) Require(edgeSegments, 6, "edgeSegments");

    // pin_ptr keeps the garbage collector from moving the arrays while the
    // native code reads them. The engine copies the data, so the pins are
    // released as soon as this function returns.
    pin_ptr<float>  pPos = &positions[0];
    pin_ptr<UInt32> pIdx = &indices[0];
    pin_ptr<float>  pNrm = nullptr;          // stays null when no normals were given
    pin_ptr<float>  pEdg = nullptr;
    if (normals != nullptr) pNrm = &normals[0];
    if (hasEdges)           pEdg = &edgeSegments[0];

    CAD_TRY
        return Native()->AddMesh(parentId, ToUtf8(name),
                                 isBRep ? CadCore::MeshKind::BRep : CadCore::MeshKind::Generic,
                                 pPos, pNrm, static_cast<size_t>(positions->Length / 3),
                                 pIdx, static_cast<size_t>(indices->Length),
                                 pEdg, hasEdges ? static_cast<size_t>(edgeSegments->Length / 6) : 0);
    CAD_CATCH
}

int CadViewport::AddStlFile(int parentId, String^ path) {
    if (String::IsNullOrEmpty(path)) throw gcnew ArgumentException("path is empty.");
    String^ name = IO::Path::GetFileName(path);
    CAD_TRY return Native()->AddStlFile(parentId, ToUtf8(path), ToUtf8(name)); CAD_CATCH
}

int CadViewport::AddPointCloud(int parentId, String^ name,
                               cli::array<float>^ positions, cli::array<float>^ colors,
                               float pointSizePx) {
    Require(positions, 3, "positions");
    if (colors != nullptr && colors->Length != positions->Length)
        throw gcnew ArgumentException("colors must have the same length as positions.");

    pin_ptr<float> pPos = &positions[0];
    pin_ptr<float> pCol = nullptr;
    if (colors != nullptr) pCol = &colors[0];

    CAD_TRY
        return Native()->AddPointCloud(parentId, ToUtf8(name), pPos, pCol,
                                       static_cast<size_t>(positions->Length / 3), pointSizePx);
    CAD_CATCH
}

int CadViewport::AddPolyline(int parentId, String^ name,
                             cli::array<float>^ points, bool closed, float widthPx) {
    Require(points, 3, "points");
    if (points->Length < 6) throw gcnew ArgumentException("A curve needs at least two points.");
    pin_ptr<float> p = &points[0];
    CAD_TRY
        return Native()->AddCurve(parentId, ToUtf8(name), p,
                                  static_cast<size_t>(points->Length / 3), closed, widthPx);
    CAD_CATCH
}

bool CadViewport::SetPolylinePoints(int id, cli::array<float>^ points, bool closed) {
    Require(points, 3, "points");
    if (points->Length < 6) throw gcnew ArgumentException("A curve needs at least two points.");
    pin_ptr<float> p = &points[0];
    CAD_TRY
        return Native()->SetCurvePoints(id, p, static_cast<size_t>(points->Length / 3), closed);
    CAD_CATCH
}

bool CadViewport::RemoveNode(int id) { return Native()->RemoveNode(id); }
void CadViewport::ClearScene()       { Native()->ClearScene(); }

// PickType (managed, bit flags) and CadCore::PickTypeBits use the same bit
// values, so the sets convert with a plain cast. A single picked KIND
// (CadCore::PickKind: 1..4) maps to the bit 1 << (kind - 1).

PickType CadViewport::PickTypes::get() {
    return static_cast<PickType>(Native()->PickTypes());
}
void CadViewport::PickTypes::set(PickType value) {
    Native()->SetPickTypes(static_cast<unsigned>(value));
}

PickType CadViewport::FilterOutPickTypes::get() {
    return static_cast<PickType>(Native()->FilterOutPickTypes());
}
void CadViewport::FilterOutPickTypes::set(PickType value) {
    Native()->SetFilterOutPickTypes(static_cast<unsigned>(value));
}

float CadViewport::SnapToleranceMm::get()            { return Native()->SnapTolerance(); }
void  CadViewport::SnapToleranceMm::set(float value) { Native()->SetSnapTolerance(value); }

// One picked KIND (native, 1..4)  <->  one PickType bit (managed).
static PickType ToManagedType(CadCore::PickKind kind) {
    return kind == CadCore::PickKind::None
        ? PickType::None
        : static_cast<PickType>(1 << (static_cast<int>(kind) - 1));
}

static CadCore::PickResult ToNativeHit(PickHit hit) {
    CadCore::PickResult r;
    r.nodeId  = hit.NodeId;
    r.index   = hit.Index;
    r.point   = CadCore::Vec3(hit.X, hit.Y, hit.Z);
    r.blocked = hit.Blocked;
    switch (hit.Type) {
    case PickType::Vertex: r.kind = CadCore::PickKind::Vertex; break;
    case PickType::Edge:   r.kind = CadCore::PickKind::Edge;   break;
    case PickType::Face:   r.kind = CadCore::PickKind::Face;   break;
    case PickType::Solid:  r.kind = CadCore::PickKind::Solid;  break;
    default:               r.kind = CadCore::PickKind::None;   break;
    }
    return r;
}

PickHit CadViewport::Pick(int xPx, int yPx) {
    const CadCore::PickResult r = Native()->Pick(xPx, yPx);
    PickHit hit;
    hit.NodeId  = r.nodeId;
    hit.Type    = ToManagedType(r.kind);
    hit.Index   = r.index;
    hit.X = r.point.x;  hit.Y = r.point.y;  hit.Z = r.point.z;
    hit.Blocked = r.blocked;
    return hit;
}

bool CadViewport::Select(PickHit hit) {
    return Native()->Select(ToNativeHit(hit), CadCore::SelectMode::Replace);
}
bool CadViewport::AddToSelection(PickHit hit) {
    return Native()->Select(ToNativeHit(hit), CadCore::SelectMode::Add);
}
bool CadViewport::RemoveFromSelection(PickHit hit) {
    return Native()->Select(ToNativeHit(hit), CadCore::SelectMode::Remove);
}

void CadViewport::SetSelectedNode(int id) { Native()->SetSelectedNode(id); }
void CadViewport::ClearSelection()        { Native()->ClearSelection(); }

int CadViewport::SelectionCount::get() {
    return static_cast<int>(Native()->Selection().size());
}

cli::array<PickHit>^ CadViewport::GetSelection() {
    const std::vector<CadCore::SelectedEntity>& sel = Native()->Selection();
    cli::array<PickHit>^ result = gcnew cli::array<PickHit>(static_cast<int>(sel.size()));
    for (int i = 0; i < result->Length; ++i) {
        const CadCore::SelectedEntity& e = sel[static_cast<size_t>(i)];
        PickHit hit;
        hit.NodeId  = e.nodeId;
        hit.Type    = ToManagedType(e.kind);
        hit.Index   = e.index;
        hit.X = e.point.x;  hit.Y = e.point.y;  hit.Z = e.point.z;
        hit.Blocked = false;
        result[i] = hit;
    }
    return result;
}

bool CadViewport::SetMeshTopology(int id, cli::array<UInt32>^ faceOfTriangle,
                                  cli::array<float>^ vertexPoints) {
    const bool hasFaces    = faceOfTriangle != nullptr && faceOfTriangle->Length > 0;
    const bool hasVertices = vertexPoints != nullptr && vertexPoints->Length > 0;
    if (hasVertices && vertexPoints->Length % 3 != 0)
        throw gcnew ArgumentException("vertexPoints must contain x,y,z triples.");

    pin_ptr<UInt32> pFaces = nullptr;
    pin_ptr<float>  pVerts = nullptr;
    if (hasFaces)    pFaces = &faceOfTriangle[0];
    if (hasVertices) pVerts = &vertexPoints[0];

    CAD_TRY
        return Native()->SetMeshTopology(id,
            pFaces, hasFaces ? static_cast<size_t>(faceOfTriangle->Length) : 0,
            pVerts, hasVertices ? static_cast<size_t>(vertexPoints->Length / 3) : 0);
    CAD_CATCH
}

void CadViewport::SetHighlightColor(float r, float g, float b) {
    Native()->Settings().highlightColor = CadCore::Vec3(r, g, b);
}

bool CadViewport::SetNodeVisible(int id, bool visible) {
    return Native()->SetNodeVisible(id, visible);
}

bool CadViewport::SetNodeTransform(int id, cli::array<float>^ columnMajor16) {
    if (columnMajor16 == nullptr || columnMajor16->Length != 16)
        throw gcnew ArgumentException("A transform needs exactly 16 values.");
    pin_ptr<float> p = &columnMajor16[0];
    return Native()->SetNodeTransform(id, p);
}

bool CadViewport::SetNodeColor(int id, float r, float g, float b) {
    return Native()->SetNodeColor(id, r, g, b);
}

bool CadViewport::SetNodeOpacity(int id, float opacity) {
    return Native()->SetNodeOpacity(id, opacity);
}

bool CadViewport::SetNodeMaterial(int id,
                                  float diffuseR, float diffuseG, float diffuseB,
                                  float specularR, float specularG, float specularB,
                                  float shininess, float opacity) {
    CadCore::Material m = CadCore::Material::Make(
        CadCore::Vec3(diffuseR, diffuseG, diffuseB),
        CadCore::Vec3(specularR, specularG, specularB), shininess, opacity);
    return Native()->SetNodeMaterial(id, m);
}

bool CadViewport::ApplyMaterialPreset(int id, MaterialPreset preset) {
    CadCore::Material m;
    switch (preset) {
    case MaterialPreset::Steel:     m = CadCore::Material::Steel();     break;
    case MaterialPreset::Aluminium: m = CadCore::Material::Aluminium(); break;
    case MaterialPreset::Brass:     m = CadCore::Material::Brass();     break;
    case MaterialPreset::Glass:     m = CadCore::Material::Glass();     break;
    }
    return Native()->SetNodeMaterial(id, m);
}

#ifdef CADBRIDGE_WITH_OCCT
int CadViewport::AddBRepShape(int parentId, String^ name, IntPtr topoDsShape,
                              double linearDeflectionMm, double angularDeflectionRad) {
    if (topoDsShape == IntPtr::Zero) throw gcnew ArgumentNullException("topoDsShape");
    CAD_TRY
        const BRepTessellation t = TessellateShape(topoDsShape.ToPointer(),
                                                   linearDeflectionMm, angularDeflectionRad);
        if (t.indices.empty())
            throw gcnew InvalidOperationException("The shape has no faces to display.");
        const int id = Native()->AddMesh(parentId, ToUtf8(name), CadCore::MeshKind::BRep,
                                 t.positions.data(), t.normals.data(), t.positions.size() / 3,
                                 t.indices.data(), t.indices.size(),
                                 t.edgeSegments.empty() ? nullptr : t.edgeSegments.data(),
                                 t.edgeSegments.size() / 6);
        // Hand over the real faces and vertices so picking uses the B-Rep's own topology.
        Native()->SetMeshTopology(id,
            t.faceOfTriangle.empty() ? nullptr : t.faceOfTriangle.data(), t.faceOfTriangle.size(),
            t.vertices.empty() ? nullptr : t.vertices.data(), t.vertices.size() / 3);
        return id;
    CAD_CATCH
}
#endif

} // namespace CadBridge
