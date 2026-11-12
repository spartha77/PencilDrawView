#pragma once
// =============================================================================
// CadCore.GL / platform/WglContext.h   (WINDOWS ONLY)
//
// Creates an OpenGL core-profile context on a Win32 window (HWND) using WGL.
// This is the only Windows-specific file of the native engine; it is what the
// WPF host uses (through the C++/CLI bridge).
//
// The header deliberately does not include <windows.h>; handles are void*.
// =============================================================================

#include "CadCore/GLContext.h"

namespace CadCore {

   class WglContext : public IGLContext {
      public:
      /// `hwnd` is the HWND to render into. Requests OpenGL 4.6 core and falls
      /// back to 4.5 core. Throws std::runtime_error if neither is available.
      explicit WglContext(void* hwnd);
      ~WglContext() override;

      WglContext(const WglContext&) = delete;
      WglContext& operator=(const WglContext&) = delete;

      void MakeCurrent() override;
      void SwapBuffers() override;
      void OnWindowResized() override;

      private:
      void* m_hwnd = nullptr;   // HWND
      void* m_hdc = nullptr;   // HDC
      void* m_hglrc = nullptr;   // HGLRC
   };

} // namespace CadCore
