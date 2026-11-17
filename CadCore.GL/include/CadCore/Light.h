#pragma once
// =============================================================================
// CadCore.GL / Light.h
//
// Light sources and the scene-wide lighting configuration.
//
// TWO LIGHTING CONFIGURATIONS (LightingMode)
//   AmbientOnly : no light sources at all. Every surface gets
//                 ambientLight * material.ambient - a flat, shadow-free colour.
//                 Shape is then read from the edge overlay.
//   Lights      : `lightCount` light sources (two by default), each with a
//                 POSITION and a DIRECTION, plus a small ambient fill.
//
// Independently of the mode, the three terms of the lighting equation can be
// switched on/off: ambient, diffuse, specular.
// =============================================================================

#include "CadCore/Math.h"

namespace CadCore {

constexpr int kMaxLights = 8;   // must match MAX_LIGHTS in Shaders.h

enum class LightingMode : int {
    AmbientOnly = 0,
    Lights      = 1
};

enum class LightType : int {
    Directional = 0,  ///< parallel rays along `direction`; `position` is ignored
    Point       = 1,  ///< radiates from `position` in all directions; `direction` ignored
    Spot        = 2   ///< cone from `position` along `direction` (uses both inputs)
};

struct Light {
    bool      enabled   = true;
    LightType type      = LightType::Spot;
    Vec3      position  {0.0f, 0.0f, 1000.0f};  ///< world position, mm
    Vec3      direction {0.0f, 0.0f, -1.0f};    ///< direction the light travels
    Vec3      color     {1.0f, 1.0f, 1.0f};
    float     intensity = 1.0f;
    float     spotInnerDeg = 50.0f;  ///< half-angle of the fully lit cone
    float     spotOuterDeg = 70.0f;  ///< half-angle where the light reaches zero
    float     range = 0.0f;          ///< mm; 0 = no distance fall-off (usual for CAD)
};

struct LightingSettings {
    LightingMode mode = LightingMode::Lights;

    Vec3  ambientColor{1.0f, 1.0f, 1.0f};
    float ambientOnlyIntensity = 1.0f;   ///< ambient strength in AmbientOnly mode
    float litAmbientIntensity  = 0.25f;  ///< ambient fill strength in Lights mode

    bool useAmbient  = true;   ///< the three terms of the lighting equation
    bool useDiffuse  = true;
    bool useSpecular = true;

    int   lightCount = 2;
    Light lights[kMaxLights];

    LightingSettings() {
        // Light 1 - key light: front-right, above, aimed at the origin.
        lights[0].position  = {600.0f, -700.0f, 900.0f};
        lights[0].direction = {-600.0f, 700.0f, -900.0f};
        lights[0].intensity = 0.85f;
        // Light 2 - fill light: back-left, lower, aimed at the origin.
        lights[1].position  = {-700.0f, 500.0f, 400.0f};
        lights[1].direction = {700.0f, -500.0f, -400.0f};
        lights[1].intensity = 0.40f;
    }
};

} // namespace CadCore
