#pragma once
// =============================================================================
// CadCore.GL / Material.h
//
// Surface appearance of a scene node (classic ambient / diffuse / specular
// reflectances, i.e. the Blinn-Phong model).
// =============================================================================

#include "CadCore/Math.h"

namespace CadCore {

   struct Material {
      Vec3  ambient{ 0.62f, 0.66f, 0.72f };  ///< reflectance for ambient light (normally = diffuse)
      Vec3  diffuse{ 0.62f, 0.66f, 0.72f };  ///< the "colour" of the part
      Vec3  specular{ 0.25f, 0.25f, 0.25f };  ///< colour/strength of the highlight
      float shininess = 48.0f;               ///< highlight exponent; larger = tighter highlight
      float opacity = 1.0f;                ///< 1 = opaque; < 1 is drawn in the blended pass
      Vec3  edgeColor{ 0.05f, 0.05f, 0.07f };  ///< colour of the edge overlay

      /// Sets the part colour (ambient and diffuse together), keeping the highlight.
      void SetColor(const Vec3& rgb) { ambient = rgb; diffuse = rgb; }

      static Material Make(const Vec3& color, const Vec3& spec, float shine, float alpha = 1.0f) {
         Material m;
         m.SetColor(color);
         m.specular = spec;
         m.shininess = shine;
         m.opacity = alpha;
         return m;
      }

      // A few ready-made materials -------------------------------------------------
      static Material Steel() { return Make({ 0.42f, 0.46f, 0.52f }, { 0.55f, 0.55f, 0.55f }, 80.0f); }
      static Material Aluminium() { return Make({ 0.82f, 0.83f, 0.85f }, { 0.70f, 0.70f, 0.70f }, 110.0f); }
      static Material Brass() { return Make({ 0.78f, 0.62f, 0.25f }, { 0.60f, 0.55f, 0.35f }, 60.0f); }
      static Material Plastic(const Vec3& color) { return Make(color, { 0.20f, 0.20f, 0.20f }, 24.0f); }
      static Material Glass() { return Make({ 0.60f, 0.78f, 0.90f }, { 0.90f, 0.90f, 0.90f }, 160.0f, 0.35f); }
   };

} // namespace CadCore
