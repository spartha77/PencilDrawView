// =============================================================================
// CadCore.GL / platform/WglContext.cpp   (WINDOWS ONLY)
//
// HOW A MODERN CONTEXT IS CREATED ON WINDOWS
//   The function that creates a 4.x core context, wglCreateContextAttribsARB,
//   is itself an OpenGL *extension* - and extension functions can only be
//   looked up while some context is already current. Hence the two steps:
//     1. create a plain legacy context with wglCreateContext, make it current;
//     2. let GLEW look up the WGL extensions, call wglCreateContextAttribsARB
//        to get the real 4.6 (or 4.5) core context, and delete the legacy one.
// =============================================================================
#ifdef _WIN32

// <windows.h> must come before GLEW, otherwise GLEW defines its own stand-ins
// for APIENTRY/WINGDIAPI and the compiler warns about macro redefinitions.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <GL/glew.h>
#include <GL/wglew.h>

#include "WglContext.h"

#include <initializer_list>
#include <stdexcept>
#include <string>

namespace CadCore {

WglContext::WglContext(void* hwnd) : m_hwnd(hwnd) {
    HWND window = static_cast<HWND>(hwnd);
    HDC dc = ::GetDC(window);       // the window class uses CS_OWNDC, so this DC is kept
    if (!dc) throw std::runtime_error("GetDC failed for the OpenGL window.");

    auto fail = [&](const std::string& message) {
        ::wglMakeCurrent(nullptr, nullptr);
        ::ReleaseDC(window, dc);
        throw std::runtime_error(message);
    };

    // Pixel format of the window's back buffer. The scene itself is rendered
    // into an off-screen multisampled framebuffer (see Renderer), so a plain
    // double-buffered RGBA format is all the window needs.
    PIXELFORMATDESCRIPTOR pfd = {};
    pfd.nSize      = sizeof(pfd);
    pfd.nVersion   = 1;
    pfd.dwFlags    = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cDepthBits = 24;
    pfd.iLayerType = PFD_MAIN_PLANE;
    const int format = ::ChoosePixelFormat(dc, &pfd);
    if (format == 0 || !::SetPixelFormat(dc, format, &pfd))
        fail("No suitable pixel format for OpenGL.");

    // Step 1: temporary legacy context.
    HGLRC temp = ::wglCreateContext(dc);
    if (!temp || !::wglMakeCurrent(dc, temp)) {
        if (temp) ::wglDeleteContext(temp);
        fail("Could not create an OpenGL context. Is a graphics driver installed?");
    }

    glewExperimental = GL_TRUE;
    const bool glewOk = (glewInit() == GLEW_OK);

    // Remember which driver this window actually got. It goes into the error
    // message, because "no OpenGL 4.5" nearly always means the WRONG adapter
    // (Remote Desktop, missing driver, the integrated GPU of a laptop).
    const char* rendererName = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    const char* versionName  = reinterpret_cast<const char*>(glGetString(GL_VERSION));
    const std::string found = std::string(rendererName ? rendererName : "unknown adapter") +
                              ", OpenGL " + (versionName ? versionName : "unknown");

    // Step 2: the real core-profile context, newest version first.
    HGLRC core = nullptr;
    if (glewOk && WGLEW_ARB_create_context && WGLEW_ARB_create_context_profile) {
        for (int minor : {6, 5}) {
            const int attribs[] = {
                WGL_CONTEXT_MAJOR_VERSION_ARB, 4,
                WGL_CONTEXT_MINOR_VERSION_ARB, minor,
                WGL_CONTEXT_PROFILE_MASK_ARB,  WGL_CONTEXT_CORE_PROFILE_BIT_ARB,
                0};
            core = wglCreateContextAttribsARB(dc, nullptr, attribs);
            if (core) break;
        }
    }

    ::wglMakeCurrent(nullptr, nullptr);
    ::wglDeleteContext(temp);

    if (!core || !::wglMakeCurrent(dc, core)) {
        if (core) ::wglDeleteContext(core);
        fail("OpenGL 4.5 or newer is not available.\n\nThis window is using: " + found +
             "\n\n\"GDI Generic\" or \"Microsoft\" here means no real GPU driver is in use "
             "(Remote Desktop session, virtual machine, or driver not installed).");
    }

    // No vsync wait in SwapBuffers: the WPF host renders on the UI thread and
    // only when something changed, and the desktop compositor prevents tearing.
    if (WGLEW_EXT_swap_control) wglSwapIntervalEXT(0);

    m_hdc   = dc;
    m_hglrc = core;
}

WglContext::~WglContext() {
    if (m_hglrc) {
        if (::wglGetCurrentContext() == static_cast<HGLRC>(m_hglrc))
            ::wglMakeCurrent(nullptr, nullptr);
        ::wglDeleteContext(static_cast<HGLRC>(m_hglrc));
    }
    if (m_hdc) ::ReleaseDC(static_cast<HWND>(m_hwnd), static_cast<HDC>(m_hdc));
}

void WglContext::MakeCurrent() {
    // Switching contexts is costly, so skip it when this one is already current.
    if (::wglGetCurrentContext() != static_cast<HGLRC>(m_hglrc))
        ::wglMakeCurrent(static_cast<HDC>(m_hdc), static_cast<HGLRC>(m_hglrc));
}

void WglContext::OnWindowResized() {
    // Release and re-bind: this makes the driver look at the window again and
    // resize its back buffer. Without it, some drivers (seen on Intel Iris Xe)
    // keep the old buffer for one more frame and stretch it over the window.
    ::wglMakeCurrent(nullptr, nullptr);
    ::wglMakeCurrent(static_cast<HDC>(m_hdc), static_cast<HGLRC>(m_hglrc));
}

void WglContext::SwapBuffers() {
    ::SwapBuffers(static_cast<HDC>(m_hdc));
}

} // namespace CadCore

#endif // _WIN32
