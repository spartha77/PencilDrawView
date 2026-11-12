// =============================================================================
// CadCore.GL / StlLoader.cpp
// =============================================================================
#include "CadCore/StlLoader.h"
#include "CadCore/Math.h"

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>

namespace CadCore {

   namespace {

      /// Appends one triangle (three positions + its flat normal three times).
      void AppendTriangle(const Vec3 v[3], std::vector<float>& positions, std::vector<float>& normals) {
         Vec3 n = Normalize(Cross(v[1] - v[0], v[2] - v[0]));
         for (int i = 0; i < 3; ++i) {
            positions.insert(positions.end(), { v[i].x, v[i].y, v[i].z });
            normals.insert(normals.end(), { n.x, n.y, n.z });
         }
      }

      // BINARY STL: 80-byte header, uint32 triangle count, then 50 bytes per triangle:
      //   float32[3] normal, float32[3] v0, float32[3] v1, float32[3] v2, uint16 attribute
      // All values are little-endian (true for every platform this engine targets).
      void ParseBinary(const std::vector<char>& data, std::uint32_t triCount,
         std::vector<float>& positions, std::vector<float>& normals) {
         positions.reserve(static_cast<std::size_t>(triCount) * 9);
         normals.reserve(static_cast<std::size_t>(triCount) * 9);
         const char* p = data.data() + 84;
         for (std::uint32_t t = 0; t < triCount; ++t, p += 50) {
            float f[12];
            std::memcpy(f, p, sizeof(f));
            Vec3 v[3] = { {f[3], f[4], f[5]}, {f[6], f[7], f[8]}, {f[9], f[10], f[11]} };
            AppendTriangle(v, positions, normals);
         }
      }

      // ASCII STL: only the "vertex x y z" lines matter; every three form a triangle.
      void ParseAscii(const std::vector<char>& data,
         std::vector<float>& positions, std::vector<float>& normals) {
         const char* p = data.data();          // buffer is NUL-terminated by the caller
         Vec3 v[3];
         int  n = 0;
         while ((p = std::strstr(p, "vertex")) != nullptr) {
            p += 6;
            float xyz[3];
            for (float& value : xyz) {
               char* next = nullptr;
               // strtof skips leading whitespace itself. It honours the C locale, which
               // stays "C" (decimal point) unless the host program calls setlocale().
               value = std::strtof(p, &next);
               if (next == p) throw std::runtime_error("STL file is damaged (bad vertex line).");
               p = next;
            }
            v[n++] = { xyz[0], xyz[1], xyz[2] };
            if (n == 3) { AppendTriangle(v, positions, normals); n = 0; }
         }
      }

   } // namespace

   void LoadStl(const std::string& utf8Path,
      std::vector<float>& positions,
      std::vector<float>& normals,
      std::vector<std::uint32_t>& indices) {
      positions.clear();
      normals.clear();
      indices.clear();

      // u8path keeps non-ASCII file names working on Windows (where a plain
      // std::string path would be interpreted in the ANSI code page).
      std::ifstream file(std::filesystem::u8path(utf8Path), std::ios::binary);
      if (!file) throw std::runtime_error("Cannot open STL file: " + utf8Path);

      std::vector<char> data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
      data.push_back('\0');                       // terminator for the ASCII parser
      const std::size_t size = data.size() - 1;

      // A file is binary when its size matches the triangle count in the header.
      // (Checking for a leading "solid" is unreliable: many binary files have it.)
      bool binary = false;
      std::uint32_t triCount = 0;
      if (size >= 84) {
         std::memcpy(&triCount, data.data() + 80, 4);
         binary = (size == 84 + static_cast<std::size_t>(triCount) * 50);
      }

      if (binary) ParseBinary(data, triCount, positions, normals);
      else        ParseAscii(data, positions, normals);

      if (positions.empty()) throw std::runtime_error("STL file contains no triangles: " + utf8Path);

      indices.resize(positions.size() / 3);
      for (std::size_t i = 0; i < indices.size(); ++i) indices[i] = static_cast<std::uint32_t>(i);
   }

} // namespace CadCore
