#pragma once
// =============================================================================
// CadCore.GL / ShaderProgram.h
//
// Compiles and links a GLSL program (vertex + fragment, optional geometry).
// Uniforms are set with glProgramUniform* (DSA), so a program never has to be
// bound just to change a value.
// =============================================================================

#include "CadCore/Math.h"
#include <GL/glew.h>
#include <string>
#include <unordered_map>

namespace CadCore {

   class ShaderProgram {
      public:
      ShaderProgram() = default;
      ~ShaderProgram() { Release(); }
      ShaderProgram(const ShaderProgram&) = delete;
      ShaderProgram& operator=(const ShaderProgram&) = delete;

      /// Compiles and links. `versionLine` (e.g. "#version 460 core\n") is put in
      /// front of every stage so the same sources run on a 4.6 or a 4.5 context.
      /// `geometrySrc` may be nullptr. Throws std::runtime_error with the driver's
      /// log on failure; `debugName` is included in the message.
      void Build(const char* debugName, const std::string& versionLine,
         const char* vertexSrc, const char* fragmentSrc,
         const char* geometrySrc = nullptr);

      void Use() const { glUseProgram(m_id); }
      void Release();

      void SetInt(const char* name, int v) const { glProgramUniform1i(m_id, Loc(name), v); }
      void SetFloat(const char* name, float v) const { glProgramUniform1f(m_id, Loc(name), v); }
      void SetVec3(const char* name, const Vec3& v) const { glProgramUniform3f(m_id, Loc(name), v.x, v.y, v.z); }
      void SetVec4(const char* name, float x, float y, float z, float w) const {
         glProgramUniform4f(m_id, Loc(name), x, y, z, w);
      }
      void SetMat3(const char* name, const float* m9) const {
         glProgramUniformMatrix3fv(m_id, Loc(name), 1, GL_FALSE, m9);
      }
      void SetMat4(const char* name, const Mat4& m) const {
         glProgramUniformMatrix4fv(m_id, Loc(name), 1, GL_FALSE, m.m);
      }

      private:
      /// Uniform location with a small cache (-1 for unknown names is harmless).
      GLint Loc(const char* name) const;

      GLuint m_id = 0;
      mutable std::unordered_map<std::string, GLint> m_locations;
   };

} // namespace CadCore
