#pragma once
// =============================================================================
// CadCore.GL / GpuObjects.h
//
// RAII wrappers for OpenGL buffer objects and vertex array objects (VAOs).
//
// MODERN OPENGL NOTE - Direct State Access (DSA, core since OpenGL 4.5)
//   Classic OpenGL forces you to *bind* an object before you can modify it
//   (glBindBuffer + glBufferData ...), which leaks global state everywhere.
//   DSA functions take the object name directly:
//       glCreateBuffers / glNamedBufferData
//       glCreateVertexArrays / glVertexArrayVertexBuffer / glVertexArrayAttribFormat
//   The only binds left in this engine are the ones needed to *draw*.
//
// LIFETIME RULE
//   These objects must be destroyed while the GL context is current. The
//   Viewer facade guarantees that for everything owned by the scene graph.
// =============================================================================

#include <GL/glew.h>
#include <cstddef>
#include <vector>

namespace CadCore {

// -----------------------------------------------------------------------------
// GpuBuffer - a VBO, index buffer or UBO, depending on how it is attached.
// -----------------------------------------------------------------------------
class GpuBuffer {
public:
    GpuBuffer() = default;
    ~GpuBuffer() { Release(); }
    GpuBuffer(const GpuBuffer&) = delete;
    GpuBuffer& operator=(const GpuBuffer&) = delete;

    /// (Re)allocates the buffer and copies `bytes` bytes from `data`.
    void SetData(const void* data, std::size_t bytes, GLenum usage = GL_STATIC_DRAW) {
        if (m_id == 0) glCreateBuffers(1, &m_id);
        glNamedBufferData(m_id, static_cast<GLsizeiptr>(bytes), data, usage);
    }

    template <typename T>
    void SetData(const std::vector<T>& v, GLenum usage = GL_STATIC_DRAW) {
        SetData(v.empty() ? nullptr : v.data(), v.size() * sizeof(T), usage);
    }

    /// Overwrites part of an already allocated buffer (used for the UBOs).
    void Update(const void* data, std::size_t bytes, std::size_t offset = 0) {
        glNamedBufferSubData(m_id, static_cast<GLintptr>(offset),
                             static_cast<GLsizeiptr>(bytes), data);
    }

    void Release() {
        if (m_id != 0) { glDeleteBuffers(1, &m_id); m_id = 0; }
    }

    GLuint Id() const { return m_id; }

private:
    GLuint m_id = 0;
};

// -----------------------------------------------------------------------------
// VertexArray - describes where each vertex attribute comes from.
//
// With the 4.3+ "vertex attrib binding" model an attribute (shader input
// `layout(location = N)`) reads from a *binding slot*, and a buffer is plugged
// into that slot. Format and data source are therefore independent.
// -----------------------------------------------------------------------------
class VertexArray {
public:
    VertexArray() = default;
    ~VertexArray() { Release(); }
    VertexArray(const VertexArray&) = delete;
    VertexArray& operator=(const VertexArray&) = delete;

    void Create() {
        if (m_id == 0) glCreateVertexArrays(1, &m_id);
    }

    /// Plugs `buffer` into binding slot `slot`; one vertex every `stride` bytes.
    void SetVertexBuffer(GLuint slot, const GpuBuffer& buffer, GLsizei stride) {
        glVertexArrayVertexBuffer(m_id, slot, buffer.Id(), 0, stride);
    }

    void SetIndexBuffer(const GpuBuffer& buffer) {
        glVertexArrayElementBuffer(m_id, buffer.Id());
    }

    /// Float attribute `location` = `components` floats read from `slot`.
    void SetFloatAttribute(GLuint location, GLuint slot, GLint components, GLuint offset = 0) {
        glEnableVertexArrayAttrib(m_id, location);
        glVertexArrayAttribFormat(m_id, location, components, GL_FLOAT, GL_FALSE, offset);
        glVertexArrayAttribBinding(m_id, location, slot);
    }

    void Bind() const { glBindVertexArray(m_id); }

    void Release() {
        if (m_id != 0) { glDeleteVertexArrays(1, &m_id); m_id = 0; }
    }

    bool IsCreated() const { return m_id != 0; }

private:
    GLuint m_id = 0;
};

} // namespace CadCore
