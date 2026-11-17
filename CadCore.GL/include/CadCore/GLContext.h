#pragma once
// =============================================================================
// CadCore.GL / GLContext.h
//
// The ONLY platform-specific piece of an OpenGL program is how the context is
// created and how the back buffer is presented. Everything else in this engine
// is plain OpenGL and therefore portable.
//
// The engine talks to the windowing system through this tiny interface:
//   * Windows  -> platform/WglContext   (WGL, used by the WPF host)
//   * Linux/mac-> examples/glfw_viewer  (GLFW; any GLX/EGL/NSOpenGL wrapper works)
// =============================================================================

namespace CadCore {

class IGLContext {
public:
    virtual ~IGLContext() = default;

    /// Makes this context current on the calling thread.
    virtual void MakeCurrent() = 0;

    /// Presents the back buffer of the window this context draws into.
    virtual void SwapBuffers() = 0;

    /// Called when the window changed size. Some drivers only notice the new
    /// size of the window's back buffer when the context is bound again.
    virtual void OnWindowResized() {}
};

} // namespace CadCore
