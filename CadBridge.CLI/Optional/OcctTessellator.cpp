// =============================================================================
// CadBridge.CLI / Optional/OcctTessellator.cpp
//
// OPTIONAL - excluded from the build by default. To enable it:
//   1. add CADBRIDGE_WITH_OCCT to the project's preprocessor definitions,
//   2. set "Excluded From Build" to No for this file,
//   3. add the OCCT include / library folders and link
//      TKernel TKMath TKBRep TKTopAlgo TKMesh TKG3d TKGeomBase.
// This file is compiled as plain native C++ (no /clr), see the .vcxproj.
//
// NOTE: written against the OCCT 7.6/7.7 API. Unlike the rest of the
// solution it has not been compiled by the author (it needs an OCCT install).
// =============================================================================
#ifdef CADBRIDGE_WITH_OCCT

#include "OcctTessellator.h"

#include <BRepAdaptor_Curve.hxx>
#include <BRepLib_ToolTriangulatedShape.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRep_Tool.hxx>
#include <GCPnts_TangentialDeflection.hxx>
#include <Poly_Triangulation.hxx>
#include <TopAbs_Orientation.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopLoc_Location.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>
#include <TopoDS_Vertex.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>
#include <gp_Trsf.hxx>

#include <utility>

BRepTessellation TessellateShape(const void* topoDsShape,
                                 double linearDeflectionMm,
                                 double angularDeflectionRad) {
    const TopoDS_Shape& shape = *static_cast<const TopoDS_Shape*>(topoDsShape);
    BRepTessellation out;

    // Triangulate every face. The result is stored inside the shape itself.
    BRepMesh_IncrementalMesh mesher(shape, linearDeflectionMm, Standard_False,
                                    angularDeflectionRad, Standard_True /*parallel*/);

    // ---- faces -> triangles -----------------------------------------------------
    std::uint32_t faceNumber = 0;
    for (TopExp_Explorer ex(shape, TopAbs_FACE); ex.More(); ex.Next(), ++faceNumber) {
        const TopoDS_Face& face = TopoDS::Face(ex.Current());
        TopLoc_Location location;
        Handle(Poly_Triangulation) tri = BRep_Tool::Triangulation(face, location);
        if (tri.IsNull() || tri->NbTriangles() == 0) continue;

        // Normals evaluated on the real surface (smooth shading on curved faces).
        BRepLib_ToolTriangulatedShape::ComputeNormals(face, tri);

        const gp_Trsf transform = location.Transformation();
        // A REVERSED face has its material on the other side: flip the normals
        // and the triangle winding so the outside faces outwards.
        const bool reversed = (face.Orientation() == TopAbs_REVERSED);
        const std::uint32_t base = static_cast<std::uint32_t>(out.positions.size() / 3);

        for (Standard_Integer i = 1; i <= tri->NbNodes(); ++i) {        // OCCT is 1-based
            const gp_Pnt p = tri->Node(i).Transformed(transform);
            gp_Dir n = tri->Normal(i).Transformed(transform);
            if (reversed) n.Reverse();
            out.positions.insert(out.positions.end(),
                {static_cast<float>(p.X()), static_cast<float>(p.Y()), static_cast<float>(p.Z())});
            out.normals.insert(out.normals.end(),
                {static_cast<float>(n.X()), static_cast<float>(n.Y()), static_cast<float>(n.Z())});
        }
        for (Standard_Integer t = 1; t <= tri->NbTriangles(); ++t) {
            Standard_Integer a, b, c;
            tri->Triangle(t).Get(a, b, c);
            if (reversed) std::swap(b, c);
            out.indices.insert(out.indices.end(),
                {base + static_cast<std::uint32_t>(a - 1),
                 base + static_cast<std::uint32_t>(b - 1),
                 base + static_cast<std::uint32_t>(c - 1)});
            out.faceOfTriangle.push_back(faceNumber);   // lets the viewer pick whole B-Rep faces
        }
    }

    // ---- edges -> polylines -----------------------------------------------------
    // MapShapes lists each edge once even though two faces share it.
    TopTools_IndexedMapOfShape edges;
    TopExp::MapShapes(shape, TopAbs_EDGE, edges);
    for (Standard_Integer i = 1; i <= edges.Extent(); ++i) {
        const TopoDS_Edge& edge = TopoDS::Edge(edges(i));
        if (BRep_Tool::Degenerated(edge)) continue;    // e.g. the tip of a cone

        BRepAdaptor_Curve curve(edge);                 // evaluates in world coordinates
        GCPnts_TangentialDeflection points(curve, angularDeflectionRad, linearDeflectionMm);
        for (Standard_Integer k = 1; k < points.NbPoints(); ++k) {
            const gp_Pnt a = points.Value(k), b = points.Value(k + 1);
            out.edgeSegments.insert(out.edgeSegments.end(),
                {static_cast<float>(a.X()), static_cast<float>(a.Y()), static_cast<float>(a.Z()),
                 static_cast<float>(b.X()), static_cast<float>(b.Y()), static_cast<float>(b.Z())});
        }
    }
    // ---- vertices ------------------------------------------------------------------
    TopTools_IndexedMapOfShape vertices;
    TopExp::MapShapes(shape, TopAbs_VERTEX, vertices);
    for (Standard_Integer i = 1; i <= vertices.Extent(); ++i) {
        const gp_Pnt p = BRep_Tool::Pnt(TopoDS::Vertex(vertices(i)));
        out.vertices.insert(out.vertices.end(),
            {static_cast<float>(p.X()), static_cast<float>(p.Y()), static_cast<float>(p.Z())});
    }
    return out;
}

#endif // CADBRIDGE_WITH_OCCT
