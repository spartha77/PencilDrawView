// =============================================================================
// CadCore.GL / ShaderProgram.cpp
// =============================================================================
#include "CadCore/ShaderProgram.h"

#include <stdexcept>
#include <vector>

namespace CadCore {

   namespace {

      /// Compiles one stage. The version line and the body are passed as two strings
      /// so the body can be shared between GL versions.
      GLuint CompileStage(GLenum type, const char* stageName, const char* debugName,
         const std::string& versionLine, const char* body) {
         GLuint shader = glCreateShader(type);
         const char* parts[2] = { versionLine.c_str(), body };
         glShaderSource(shader, 2, parts, nullptr);
         glCompileShader(shader);

         GLint ok = GL_FALSE;
         glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
         if (ok != GL_TRUE) {
            GLint len = 0;
            glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &len);
            std::vector<char> log(static_cast<size_t>(len) + 1, '\0');
            glGetShaderInfoLog(shader, len, nullptr, log.data());
            glDeleteShader(shader);
            throw std::runtime_error(std::string("Shader '") + debugName + "' (" + stageName +
               " stage) failed to compile:\n" + log.data());
         }
         return shader;
      }

   } // namespace

   void ShaderProgram::Build(const char* debugName, const std::string& versionLine,
      const char* vertexSrc, const char* fragmentSrc,
      const char* geometrySrc) {
      Release();

      GLuint vs = CompileStage(GL_VERTEX_SHADER, "vertex", debugName, versionLine, vertexSrc);
      GLuint gs = geometrySrc
         ? CompileStage(GL_GEOMETRY_SHADER, "geometry", debugName, versionLine, geometrySrc) : 0;
      GLuint fs = CompileStage(GL_FRAGMENT_SHADER, "fragment", debugName, versionLine, fragmentSrc);

      m_id = glCreateProgram();
      glAttachShader(m_id, vs);
      if (gs) glAttachShader(m_id, gs);
      glAttachShader(m_id, fs);
      glLinkProgram(m_id);

      // The stage objects are no longer needed once the program is linked.
      glDeleteShader(vs);
      if (gs) glDeleteShader(gs);
      glDeleteShader(fs);

      GLint ok = GL_FALSE;
      glGetProgramiv(m_id, GL_LINK_STATUS, &ok);
      if (ok != GL_TRUE) {
         GLint len = 0;
         glGetProgramiv(m_id, GL_INFO_LOG_LENGTH, &len);
         std::vector<char> log(static_cast<size_t>(len) + 1, '\0');
         glGetProgramInfoLog(m_id, len, nullptr, log.data());
         Release();
         throw std::runtime_error(std::string("Shader '") + debugName +
            "' failed to link:\n" + log.data());
      }
   }

   void ShaderProgram::Release() {
      if (m_id != 0) { glDeleteProgram(m_id); m_id = 0; }
      m_locations.clear();
   }

   GLint ShaderProgram::Loc(const char* name) const {
      auto it = m_locations.find(name);
      if (it != m_locations.end()) return it->second;
      GLint loc = glGetUniformLocation(m_id, name);
      m_locations.emplace(name, loc);
      return loc;
   }

} // namespace CadCore
