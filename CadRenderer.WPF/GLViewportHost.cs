// =============================================================================
// CadRenderer.WPF / GLViewportHost.cs
//
// Puts a real Win32 child window inside the WPF layout and lets the native
// engine draw into it.
//
// WHY A CHILD WINDOW?
//   WPF draws with DirectX and has no OpenGL surface. HwndHost is WPF's
//   official way to embed a native window (HWND); OpenGL then renders straight
//   to that window at full speed.
//
// CONSEQUENCES ("airspace")
//   * The child window sits on top of WPF content, so WPF elements cannot be
//     drawn over the 3D view. Keep toolbars and panels beside it.
//   * WPF mouse events (MouseDown, MouseMove ...) do NOT fire for a hosted
//     HWND - the mouse messages go to the child window. They are therefore
//     handled here, in WndProc, as raw Win32 messages.
//
// RENDERING IS ON DEMAND
//   A CAD view only changes when the user does something. RequestRender()
//   invalidates the window; Windows then sends one WM_PAINT, however many
//   requests were made. The GPU idles when nothing changes.
// =============================================================================
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;
using System.Windows.Interop;
using CadBridge;

namespace CadRenderer.WPF {
   /// <summary>How a click in the 3D view changes the selection.</summary>
   public enum ClickAction {
      Replace,    // plain click        : select only this
      Add,        // Ctrl  + click      : add to the selection
      Remove      // Shift + click      : take out of the selection
   }

   public sealed class GLViewportHost : HwndHost {
      /// <summary>The engine. Null until the window exists, or if creation failed.</summary>
      public CadViewport? Viewport { get; private set; }

      /// <summary>Raised once, after the OpenGL context and the engine are ready.</summary>
      public event EventHandler? ViewportReady;

      /// <summary>Raised with a message when OpenGL could not be initialised or a frame failed.</summary>
      public event Action<string>? ViewportFailed;

      /// <summary>
      /// Raised when the user CLICKS in the view (left button pressed and
      /// released without dragging). The argument says what is under the cursor:
      /// a hit, nothing (NodeId = -1), or an entity that may not be picked (Blocked).
      /// The second argument says how the click should change the selection.
      /// </summary>
      public event Action<PickHit, ClickAction>? NodeClicked;

      private IntPtr _hwnd;
      private bool _renderFailed;

      // Frames still to draw after a resize. Some drivers present the first
      // frame after a resize from the old, smaller back buffer (stretched and
      // blurred); drawing one more frame replaces it with a sharp one.
      private int _extraFrames;

      // Mouse drag state.
      private enum DragMode { None, Orbit, Pan }
      private DragMode _drag = DragMode.None;
      private int _lastX, _lastY;

      // Click detection: a left press stays a "click" until the mouse moves
      // further than a few pixels; from then on it is an orbit drag.
      private bool _clickPending;
      private int _downX, _downY;
      private const int ClickTolerancePx = 3;

      // True while the mouse is over an entity whose type is filtered out; the
      // pointer then shows the red "cannot pick" sign (see NoPickCursor.cs).
      private bool _hoverBlocked;

      /// <summary>Asks for a redraw. Cheap; many calls collapse into one frame.</summary>
      public void RequestRender () {
         if (_hwnd != IntPtr.Zero) InvalidateRect (_hwnd, IntPtr.Zero, false);
      }

      // ---------------------------------------------------------------------
      // HwndHost: create / destroy the native child window
      // ---------------------------------------------------------------------

      protected override HandleRef BuildWindowCore (HandleRef hwndParent) {
         EnsureWindowClass ();

         _hwnd = CreateWindowEx (0, WindowClassName, "",
                                WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
                                0, 0, 16, 16,
                                hwndParent.Handle, IntPtr.Zero, GetModuleHandle (null), IntPtr.Zero);
         if (_hwnd == IntPtr.Zero)
            throw new Win32Exception (Marshal.GetLastWin32Error (), "Could not create the OpenGL child window.");

         // Create the OpenGL context and the engine on this window. A failure
         // (old driver, remote desktop without GPU ...) must not crash the
         // application, so it is reported through an event instead.
         string? error = null;
         try { Viewport = new CadViewport (_hwnd); } catch (Exception ex) { error = ex.Message; }

         // Notify after WPF has finished building the window.
         Dispatcher.BeginInvoke (new Action (() => {
            if (error != null) { ViewportFailed?.Invoke (error); return; }
            ViewportReady?.Invoke (this, EventArgs.Empty);
            _extraFrames = 2;      // same safeguard for the very first frames
            RequestRender ();
         }));

         return new HandleRef (this, _hwnd);
      }

      protected override void DestroyWindowCore (HandleRef hwnd) {
         // Free the engine while the window (and so the GL context) still exists.
         Viewport?.Dispose ();
         Viewport = null;
         DestroyWindow (hwnd.Handle);
         _hwnd = IntPtr.Zero;
      }

      // ---------------------------------------------------------------------
      // Window messages of the child window
      // ---------------------------------------------------------------------

      protected override IntPtr WndProc (IntPtr hwnd, int msg, IntPtr wParam, IntPtr lParam, ref bool handled) {
         switch (msg) {
            case WM_SIZE:
               // Real pixel size of the client area (already DPI-scaled).
               Viewport?.Resize (LowWord (lParam), HighWord (lParam));
               _extraFrames = 2;
               RequestRender ();
               break;

            case WM_ERASEBKGND:
               // OpenGL repaints every pixel; erasing first would only flicker.
               handled = true;
               return new IntPtr (1);

            case WM_PAINT:
               RenderFrame ();
               ValidateRect (hwnd, IntPtr.Zero);   // tell Windows the window is up to date
               if (_extraFrames > 0) {
                  _extraFrames--;
                  RequestRender ();
               }
               handled = true;
               return IntPtr.Zero;

            // ---- mouse: left = orbit, middle or Shift+left = pan, wheel = zoom ----
            // The RIGHT button is deliberately not handled anywhere in this
            // class: it is reserved for a context menu. Its messages pass
            // through untouched, so Windows will raise WM_CONTEXTMENU for it.
            case WM_LBUTTONDOWN:
               _clickPending = true;
               _downX = LowWord (lParam);
               _downY = HighWord (lParam);
               BeginDrag (LeftButtonMode (), lParam);
               handled = true;
               break;
            case WM_MBUTTONDOWN:
               _clickPending = false;
               BeginDrag (DragMode.Pan, lParam);
               handled = true;
               break;

            case WM_MOUSEMOVE:
               // wParam says which buttons are down right now. If neither drag
               // button is, the button-up was missed (it can go to another
               // window), so end the drag instead of following the mouse.
               if (_drag != DragMode.None && (ButtonsDown (wParam) & (MK_LBUTTON | MK_MBUTTON)) == 0)
                  EndDrag ();

               if (_drag == DragMode.None)
                  UpdateHoverCursor (LowWord (lParam), HighWord (lParam));

               if (_drag != DragMode.None && Viewport != null) {
                  int x = LowWord (lParam), y = HighWord (lParam);
                  if (_clickPending) {
                     // Still within the click tolerance: do not move the
                     // camera, so a slightly shaky click stays a click.
                     if (Math.Abs (x - _downX) <= ClickTolerancePx &&
                         Math.Abs (y - _downY) <= ClickTolerancePx) break;
                     _clickPending = false;      // it became a drag
                     _lastX = x; _lastY = y;     // start smoothly from here
                  }
                  float dx = x - _lastX, dy = y - _lastY;
                  _lastX = x; _lastY = y;
                  if (_drag == DragMode.Orbit) Viewport.Orbit (dx, dy);
                  else Viewport.Pan (dx, dy);
                  RequestRender ();
               }
               break;

            case WM_LBUTTONUP:
            case WM_MBUTTONUP: {
                  // One button was released; the other drag button may still be
                  // held (left + middle pressed together). Carry on with that
                  // one, and only let go of the mouse when both are up.
                  // Left button released without having dragged = a click: select.
                  // This must be decided BEFORE EndDrag(): releasing the mouse
                  // capture sends WM_CAPTURECHANGED, which clears _clickPending.
                  bool clicked = msg == WM_LBUTTONUP && _clickPending;
                  if (msg == WM_LBUTTONUP) _clickPending = false;

                  int down = ButtonsDown (wParam);
                  if ((down & MK_MBUTTON) != 0) _drag = DragMode.Pan;
                  else if ((down & MK_LBUTTON) != 0) _drag = LeftButtonMode ();
                  else EndDrag ();
                  if (clicked && Viewport != null) {
                     // Ctrl = add to the selection, Shift = remove from it.
                     // With both held, Ctrl wins: adding cannot lose anything.
                     ClickAction action = (GetKeyState (VK_CONTROL) & 0x8000) != 0 ? ClickAction.Add
                                        : (GetKeyState (VK_SHIFT) & 0x8000) != 0 ? ClickAction.Remove
                                        : ClickAction.Replace;
                     NodeClicked?.Invoke (Viewport.Pick (LowWord (lParam), HighWord (lParam)), action);
                  }

                  handled = true;
                  break;
               }

            case WM_SETCURSOR:
               // Windows asks which pointer to show whenever the mouse moves
               // over the window. The low word of lParam says which part of
               // the window it is over; HTCLIENT = the drawing area.
               if (_hoverBlocked && LowWord (lParam) == HTCLIENT) {
                  SetCursor (NoPickCursor.Handle);
                  handled = true;
                  return new IntPtr (1);       // "pointer set, do not change it"
               }
               break;

            case WM_CAPTURECHANGED:
               // lParam = the window that now has the mouse. Windows also sends
               // this when OUR window captures again while it already had the
               // mouse (second button pressed) - that must not cancel the drag.
               if (lParam != hwnd) { _drag = DragMode.None; _clickPending = false; }   // e.g. Alt+Tab
               break;

            case WM_MOUSEWHEEL:
               // One wheel notch = 120 units; positive = wheel pushed away = zoom in.
               Viewport?.Zoom (HighWord (wParam) / 120.0f);
               RequestRender ();
               handled = true;
               break;
         }
         return base.WndProc (hwnd, msg, wParam, lParam, ref handled);
      }

      private void BeginDrag (DragMode mode, IntPtr lParam) {
         _lastX = LowWord (lParam);
         _lastY = HighWord (lParam);
         // Capture = keep receiving mouse moves outside the window. Taken once;
         // a second button pressed during a drag re-uses the existing capture.
         if (GetCapture () != _hwnd) SetCapture (_hwnd);
         SetHoverBlocked (false);   // normal pointer while dragging
         SetFocus (_hwnd);     // the mouse wheel goes to the focused window
         _drag = mode;
      }

      /// <summary>
      /// Called for every mouse move without a button down. Shows the "cannot
      /// pick" pointer when the entity under the mouse is filtered out.
      /// </summary>
      private void UpdateHoverCursor (int x, int y) {
         // Nothing can be blocked unless some type is filtered out, so the
         // pick (a ray test against the scene) is skipped in the normal case.
         bool blocked = Viewport != null
                     && Viewport.FilterOutPickTypes != PickType.None
                     && Viewport.Pick (x, y).Blocked;
         SetHoverBlocked (blocked);
      }

      private void SetHoverBlocked (bool blocked) {
         if (blocked == _hoverBlocked) return;
         _hoverBlocked = blocked;
         SetCursor (blocked ? NoPickCursor.Handle : LoadCursor (IntPtr.Zero, new IntPtr (IDC_ARROW)));
      }

      private void EndDrag () {
         _drag = DragMode.None;
         // Always hand the mouse back. If the capture stayed with this window,
         // no WPF control (buttons, tree, ...) would receive clicks any more.
         if (GetCapture () == _hwnd) ReleaseCapture ();
      }

      /// <summary>Left button orbits; with Shift held it pans.</summary>
      private static DragMode LeftButtonMode () =>
          (GetKeyState (VK_SHIFT) & 0x8000) != 0 ? DragMode.Pan : DragMode.Orbit;

      /// <summary>The MK_* button flags carried in the wParam of mouse messages.</summary>
      private static int ButtonsDown (IntPtr wParam) => (int)(wParam.ToInt64 () & 0xFFFF);

      private void RenderFrame () {
         if (Viewport == null || _renderFailed) return;
         try {
            Viewport.Render ();
         } catch (Exception ex) {
            // Report once and stop drawing instead of failing on every paint.
            _renderFailed = true;
            ViewportFailed?.Invoke (ex.Message);
         }
      }

      // Mouse coordinates and wheel deltas are SIGNED 16-bit values packed
      // into lParam / wParam (they are negative left of / above the window).
      private static int LowWord (IntPtr value) => unchecked((short)(value.ToInt64 () & 0xFFFF));
      private static int HighWord (IntPtr value) => unchecked((short)((value.ToInt64 () >> 16) & 0xFFFF));

      // ---------------------------------------------------------------------
      // Win32 window class
      // ---------------------------------------------------------------------

      private const string WindowClassName = "CadRenderer.GLViewport";
      private static bool s_classRegistered;

      private static void EnsureWindowClass () {
         if (s_classRegistered) return;

         // The class only needs default behaviour, so it uses DefWindowProc
         // directly as its window procedure. HwndHost later subclasses each
         // window, which is how the WndProc override above gets its messages.
         IntPtr defWindowProc = GetProcAddress (GetModuleHandle ("user32.dll"), "DefWindowProcW");

         var wc = new WNDCLASSEX {
            cbSize = (uint)Marshal.SizeOf<WNDCLASSEX> (),
            // CS_OWNDC: the window keeps one device context for its whole
            // life, which is what an OpenGL context must be bound to.
            style = CS_OWNDC | CS_HREDRAW | CS_VREDRAW,
            lpfnWndProc = defWindowProc,
            hInstance = GetModuleHandle (null),
            hCursor = LoadCursor (IntPtr.Zero, new IntPtr (IDC_ARROW)),
            hbrBackground = IntPtr.Zero,       // no background brush: no flicker
            lpszClassName = WindowClassName
         };

         if (RegisterClassEx (ref wc) == 0) {
            int err = Marshal.GetLastWin32Error ();
            if (err != ERROR_CLASS_ALREADY_EXISTS)
               throw new Win32Exception (err, "Could not register the OpenGL window class.");
         }
         s_classRegistered = true;
      }

      // ---------------------------------------------------------------------
      // P/Invoke (only what is needed to create a plain child window)
      // ---------------------------------------------------------------------

      private const int WS_CHILD = 0x40000000, WS_VISIBLE = 0x10000000;
      private const int WS_CLIPCHILDREN = 0x02000000, WS_CLIPSIBLINGS = 0x04000000;
      private const uint CS_VREDRAW = 0x0001, CS_HREDRAW = 0x0002, CS_OWNDC = 0x0020;
      private const int IDC_ARROW = 32512;
      private const int ERROR_CLASS_ALREADY_EXISTS = 1410;
      private const int VK_SHIFT = 0x10, VK_CONTROL = 0x11;
      private const int MK_LBUTTON = 0x0001, MK_MBUTTON = 0x0010;

      private const int WM_SETCURSOR = 0x0020, HTCLIENT = 1;
      private const int WM_SIZE = 0x0005, WM_PAINT = 0x000F, WM_ERASEBKGND = 0x0014;
      private const int WM_MOUSEMOVE = 0x0200, WM_MOUSEWHEEL = 0x020A, WM_CAPTURECHANGED = 0x0215;
      private const int WM_LBUTTONDOWN = 0x0201, WM_LBUTTONUP = 0x0202;
      private const int WM_MBUTTONDOWN = 0x0207, WM_MBUTTONUP = 0x0208;

      [StructLayout (LayoutKind.Sequential, CharSet = CharSet.Unicode)]
      private struct WNDCLASSEX {
         public uint cbSize;
         public uint style;
         public IntPtr lpfnWndProc;
         public int cbClsExtra;
         public int cbWndExtra;
         public IntPtr hInstance;
         public IntPtr hIcon;
         public IntPtr hCursor;
         public IntPtr hbrBackground;
         public string? lpszMenuName;
         public string lpszClassName;
         public IntPtr hIconSm;
      }

      [DllImport ("user32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
      private static extern ushort RegisterClassEx (ref WNDCLASSEX wc);

      [DllImport ("user32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
      private static extern IntPtr CreateWindowEx (int exStyle, string className, string windowName, int style,
                                                  int x, int y, int width, int height,
                                                  IntPtr parent, IntPtr menu, IntPtr instance, IntPtr param);

      [DllImport ("user32.dll")]
      private static extern bool DestroyWindow (IntPtr hwnd);

      [DllImport ("user32.dll")]
      private static extern bool InvalidateRect (IntPtr hwnd, IntPtr rect, bool erase);

      [DllImport ("user32.dll")]
      private static extern bool ValidateRect (IntPtr hwnd, IntPtr rect);

      [DllImport ("user32.dll")]
      private static extern IntPtr SetCapture (IntPtr hwnd);

      [DllImport ("user32.dll")]
      private static extern bool ReleaseCapture ();

      [DllImport ("user32.dll")]
      private static extern IntPtr GetCapture ();

      [DllImport ("user32.dll")]
      private static extern IntPtr SetCursor (IntPtr cursor);

      [DllImport ("user32.dll")]
      private static extern IntPtr SetFocus (IntPtr hwnd);

      [DllImport ("user32.dll")]
      private static extern short GetKeyState (int virtualKey);

      // The second argument is a resource ID passed in a pointer-sized slot.
      [DllImport ("user32.dll", EntryPoint = "LoadCursorW")]
      private static extern IntPtr LoadCursor (IntPtr instance, IntPtr cursorId);

      [DllImport ("kernel32.dll", CharSet = CharSet.Unicode)]
      private static extern IntPtr GetModuleHandle (string? moduleName);

      [DllImport ("kernel32.dll", CharSet = CharSet.Ansi, ExactSpelling = true)]
      private static extern IntPtr GetProcAddress (IntPtr module, string procName);
   }
}
