#pragma once
// =============================================================================
// CadCore.GL / Camera.h
//
// CAD "turntable" camera: the eye orbits a target point on a sphere.
//   azimuth   : angle around the world Z (up) axis, measured from +X
//   elevation : angle above the XY plane (-90 .. +90)
//   distance  : eye-to-target distance (zoom)
// Orbit changes the two angles, pan moves the target, zoom changes distance.
//
// PROJECTION MODES
//   Orthographic : parallel projection - no vanishing points. Used for the
//                  isometric and the standard engineering views.
//   Perspective  : ordinary free camera. With a tilted camera none of the three
//                  axes is parallel to the picture plane, so in general this is
//                  a 3-point perspective.
//   TwoPoint     : 2 vanishing points. The picture plane is kept VERTICAL, so
//                  all lines parallel to world Z stay parallel on screen and
//                  only the two horizontal directions converge.
//   OnePoint     : 1 vanishing point. As TwoPoint, but the azimuth also snaps to
//                  the nearest 90 degrees so the picture plane is parallel to
//                  two axes; only the depth direction converges.
//
// HOW 1/2-POINT PERSPECTIVE IS DONE ("shift lens")
//   The number of vanishing points is the number of principal axes that are
//   NOT parallel to the picture plane. To look down at a part and still keep
//   verticals parallel, the camera must not tilt. Instead it looks horizontally
//   and the view frustum is shifted down (an off-centre frustum), exactly like
//   the shift lens an architectural photographer uses. The eye is still placed
//   by azimuth/elevation/distance, so orbit, pan and zoom behave the same.
// =============================================================================

#include "CadCore/Math.h"

namespace CadCore {

   enum class ProjectionMode : int {
      Orthographic = 0,
      Perspective = 1,
      OnePoint = 2,
      TwoPoint = 3
   };

   enum class StandardView : int {
      Isometric = 0,
      Front, Back, Left, Right, Top, Bottom
   };

   class Camera {
      public:
      /// Camera position and axes in world space.
      struct Frame {
         Vec3  eye;
         Vec3  right;        ///< screen +X in world space
         Vec3  up;           ///< screen +Y in world space
         Vec3  forward;      ///< direction the camera looks along
         float targetDepth;  ///< distance of the target measured along `forward`
      };

      // ---- state ---------------------------------------------------------------
      void SetViewport(int widthPx, int heightPx) {
         m_vpW = std::max(widthPx, 1);
         m_vpH = std::max(heightPx, 1);
      }
      int ViewportWidth()  const { return m_vpW; }
      int ViewportHeight() const { return m_vpH; }

      void SetProjection(ProjectionMode mode) { m_mode = mode; }
      ProjectionMode Projection() const { return m_mode; }
      bool IsPerspective() const { return m_mode != ProjectionMode::Orthographic; }

      void  SetFovDeg(float fov) { m_fovDeg = std::clamp(fov, 5.0f, 120.0f); }
      float FovDeg() const { return m_fovDeg; }

      /// Near/far clip distances along the view direction. The renderer sets these
      /// every frame from the scene bounds, which keeps depth precision high.
      void SetClipRange(float nearDist, float farDist) { m_near = nearDist; m_far = farDist; }

      const Vec3& Target() const { return m_target; }
      float Distance() const { return m_dist; }

      // ---- interaction ---------------------------------------------------------

      /// Orbit by a mouse drag (pixels). Dragging right turns the model to the
      /// right; dragging down tips the top of the model towards the viewer.
      void Orbit(float dxPx, float dyPx, float degPerPixel = 0.4f) {
         m_azDeg -= dxPx * degPerPixel;
         m_elDeg = std::clamp(m_elDeg + dyPx * degPerPixel, -90.0f, 90.0f);
         m_azDeg = std::fmod(m_azDeg, 360.0f);
      }

      /// Pan by a mouse drag (pixels). The point under the cursor (at the target's
      /// depth) follows the cursor exactly.
      void Pan(float dxPx, float dyPx) {
         Frame f = ComputeFrame();
         float wpp = WorldUnitsPerPixel(f.targetDepth);
         m_target -= f.right * (dxPx * wpp);
         m_target += f.up * (dyPx * wpp);
      }

      /// Zoom by mouse-wheel notches. Positive = zoom in. Each notch is 10 %.
      void Zoom(float notches) {
         m_dist = std::clamp(m_dist * std::pow(0.9f, notches), 1e-3f, 1e7f);
      }

      void SetStandardView(StandardView v) {
         switch (v) {
         case StandardView::Isometric: m_azDeg = -45.0f; m_elDeg = 35.2644f; break; // atan(1/sqrt 2)
         case StandardView::Front:     m_azDeg = -90.0f; m_elDeg = 0.0f;   break; // eye on -Y
         case StandardView::Back:      m_azDeg = 90.0f; m_elDeg = 0.0f;   break;
         case StandardView::Right:     m_azDeg = 0.0f; m_elDeg = 0.0f;   break; // eye on +X
         case StandardView::Left:      m_azDeg = 180.0f; m_elDeg = 0.0f;   break;
         case StandardView::Top:       m_azDeg = -90.0f; m_elDeg = 90.0f;   break; // X right, Y up
         case StandardView::Bottom:    m_azDeg = -90.0f; m_elDeg = -90.0f;   break;
         }
      }

      /// Centres the target on `box` and sets the distance so it fills the view.
      void Fit(const AABB& box) {
         if (!box.IsValid()) return;
         m_target = box.Center();
         float r = std::max(box.Radius(), 1e-3f);
         float aspect = static_cast<float>(m_vpW) / static_cast<float>(m_vpH);
         m_dist = r / std::sin(DegToRad(m_fovDeg) * 0.5f) / std::min(aspect, 1.0f) * 1.05f;
      }

      // ---- derived quantities --------------------------------------------------

      Frame ComputeFrame() const {
         float azDeg = m_azDeg, elDeg = m_elDeg;
         const bool shift = UsesShiftLens();
         if (m_mode == ProjectionMode::OnePoint) azDeg = std::round(azDeg / 90.0f) * 90.0f;
         if (shift) elDeg = std::clamp(elDeg, -kMaxShiftElevationDeg, kMaxShiftElevationDeg);

         const float az = DegToRad(azDeg), el = DegToRad(elDeg);
         const float ca = std::cos(az), sa = std::sin(az);
         const float ce = std::cos(el), se = std::sin(el);

         Frame f;
         f.eye = m_target + Vec3(ce * ca, ce * sa, se) * m_dist;
         f.right = Vec3(-sa, ca, 0.0f);
         if (shift) {
            // Shift lens: look horizontally, keep the picture plane vertical.
            f.forward = Vec3(-ca, -sa, 0.0f);
            f.up = Vec3(0.0f, 0.0f, 1.0f);
            f.targetDepth = m_dist * ce;
         }
         else {
            // Ordinary camera looking straight at the target. Building the axes
            // analytically (instead of a "look-at" with a fixed up vector) keeps
            // them valid even when looking straight down the Z axis.
            f.forward = Vec3(-ce * ca, -ce * sa, -se);
            f.up = Vec3(-ca * se, -sa * se, ce);
            f.targetDepth = m_dist;
         }
         return f;
      }

      Mat4 ViewMatrix() const {
         Frame f = ComputeFrame();
         Mat4 v;
         v.m[0] = f.right.x;    v.m[4] = f.right.y;    v.m[8] = f.right.z;    v.m[12] = -Dot(f.right, f.eye);
         v.m[1] = f.up.x;       v.m[5] = f.up.y;       v.m[9] = f.up.z;       v.m[13] = -Dot(f.up, f.eye);
         v.m[2] = -f.forward.x; v.m[6] = -f.forward.y; v.m[10] = -f.forward.z; v.m[14] = Dot(f.forward, f.eye);
         return v;
      }

      Mat4 ProjectionMatrix() const {
         const float aspect = static_cast<float>(m_vpW) / static_cast<float>(m_vpH);
         const float tanHalf = std::tan(DegToRad(m_fovDeg) * 0.5f);

         if (m_mode == ProjectionMode::Orthographic) {
            // Sized so that switching ortho <-> perspective keeps the target's size.
            const float halfH = m_dist * tanHalf;
            return Mat4::Ortho(-halfH * aspect, halfH * aspect, -halfH, halfH, m_near, m_far);
         }

         const float n = std::max(m_near, 1e-4f);
         const float halfH = n * tanHalf;
         float centerY = 0.0f;
         if (UsesShiftLens()) {
            // Slide the frustum vertically so the target stays in the middle of
            // the screen even though the camera itself looks horizontally.
            Frame f = ComputeFrame();
            const float targetY = Dot(f.up, m_target - f.eye);  // target height in view space
            centerY = targetY * n / std::max(f.targetDepth, 1e-4f);
         }
         return Mat4::Frustum(-halfH * aspect, halfH * aspect,
            centerY - halfH, centerY + halfH, n, std::max(m_far, n * 1.01f));
      }

      /// World length covered by one screen pixel, `depth` mm in front of the eye.
      /// (In orthographic mode the depth does not matter.)
      float WorldUnitsPerPixel(float depth) const {
         const float tanHalf = std::tan(DegToRad(m_fovDeg) * 0.5f);
         const float d = IsPerspective() ? depth : m_dist;
         return 2.0f * d * tanHalf / static_cast<float>(m_vpH);
      }

      private:
      bool UsesShiftLens() const {
         return m_mode == ProjectionMode::OnePoint || m_mode == ProjectionMode::TwoPoint;
      }

      /// A shift lens cannot look straight down, so 1/2-point modes limit elevation.
      static constexpr float kMaxShiftElevationDeg = 75.0f;

      Vec3  m_target{ 0.0f, 0.0f, 0.0f };
      float m_azDeg = -45.0f;
      float m_elDeg = 35.2644f;
      float m_dist = 500.0f;
      float m_fovDeg = 35.0f;
      float m_near = 1.0f;
      float m_far = 10000.0f;
      int   m_vpW = 800;
      int   m_vpH = 600;
      ProjectionMode m_mode = ProjectionMode::Perspective;
   };

} // namespace CadCore
