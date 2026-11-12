#pragma once
// =============================================================================
// CadBridge.CLI / Optional/OcctTessellator.h
//
// OPTIONAL - only compiled when CADBRIDGE_WITH_OCCT is defined.
//
// The renderer draws triangles and line segments; it cannot evaluate B-Rep
// surfaces itself. A CAD kernel has to "tessellate" the B-Rep first. This
// helper does that with OpenCASCADE (OCCT 7.6 or newer):
//   * every FACE becomes triangles with true surface normals,
//   * every topological EDGE becomes a polyline - these are the lines the
//     bold-edge overlay draws, so a cylinder shows its two rim circles and
//     its seam, not the facets of its triangulation.
//
// Using another kernel (Parasolid, ACIS, your own)? Produce the same four
// arrays and call CadViewport::AddMesh(..., isBRep: true, ...) directly.
// =============================================================================

#include <cstdint>
#include <vector>

struct BRepTessellation {
   std::vector<float>         positions;     // x,y,z per vertex
   std::vector<float>         normals;       // x,y,z per vertex
   std::vector<std::uint32_t> indices;       // three per triangle
   std::vector<float>         edgeSegments;  // x,y,z,x,y,z per line segment
   std::vector<std::uint32_t> faceOfTriangle;  // B-Rep face number of every triangle
   std::vector<float>         vertices;        // x,y,z per B-Rep vertex
};

/// `topoDsShape` points to a `const TopoDS_Shape`.
///   linearDeflectionMm   : max distance between the mesh and the true surface
///                          (0.05 - 0.2 mm is typical for display)
///   angularDeflectionRad : max angle between adjacent facets (0.3 - 0.5 typical)
BRepTessellation TessellateShape(const void* topoDsShape,
   double linearDeflectionMm,
   double angularDeflectionRad);
