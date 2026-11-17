#pragma once
// =============================================================================
// CadCore.GL / StlLoader.h - binary and ASCII STL reader (no dependencies)
// =============================================================================

#include <cstdint>
#include <string>
#include <vector>

namespace CadCore {

/// Reads an STL file (binary or ASCII is detected automatically).
/// Output is one flat-shaded triangle per three vertices:
///   positions : x,y,z per vertex      normals : nx,ny,nz per vertex
///   indices   : 0,1,2,3,...
/// The facet normals stored in the file are ignored (they are often missing or
/// wrong); normals are recomputed from the vertices.
/// `utf8Path` is UTF-8 on every platform. Throws std::runtime_error on failure.
void LoadStl(const std::string& utf8Path,
             std::vector<float>& positions,
             std::vector<float>& normals,
             std::vector<std::uint32_t>& indices);

} // namespace CadCore
