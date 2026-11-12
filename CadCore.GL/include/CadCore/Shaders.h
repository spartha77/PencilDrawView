#pragma once
// =============================================================================
// CadCore.GL / Shaders.h - GLSL sources
//
// The "#version" line is NOT part of these strings; ShaderProgram::Build puts
// "#version 460 core" (or 450 on a 4.5 driver) in front of each stage.
//
// SHARED DATA - Uniform Buffer Objects (UBOs)
//   Values that are the same for every draw call of a frame live in two UBOs
//   that are uploaded once per frame and are visible to every program:
//     binding 0  FrameBlock : camera matrices, eye position, viewport size
//     binding 1  LightBlock : lighting mode, ambient light, the light sources
//   Per-object values (model matrix, material) are ordinary uniforms.
//   The C++ mirror structs of both blocks are in Renderer.cpp - the layouts
//   must be kept identical (std140 rules; only vec4/mat4/ivec4 are used so
//   there is no hidden padding).
// =============================================================================

namespace CadCore {
   namespace Shaders {

      // ----------------------------------------------------------------------------
      // Blocks shared by all programs (concatenated in front of each stage body).
      // ----------------------------------------------------------------------------
      inline const char* const kFrameBlock = R"GLSL(
layout(std140, binding = 0) uniform FrameBlock {
    mat4 uView;
    mat4 uProj;
    mat4 uViewProj;
    vec4 uEyePos;    // xyz = eye position (world); w = 1 perspective, 0 orthographic
    vec4 uViewDir;   // xyz = direction the camera looks along (world)
    vec4 uViewport;  // xy  = framebuffer size in pixels
};

// Unit vector from a surface point towards the viewer. In an orthographic view
// all view rays are parallel, so the direction is the same for every point.
vec3 ViewVector(vec3 worldPos) {
    return uEyePos.w > 0.5 ? normalize(uEyePos.xyz - worldPos) : -uViewDir.xyz;
}
)GLSL";

      inline const char* const kLightBlock = R"GLSL(
#define MAX_LIGHTS 8
struct LightData {
    vec4 positionType;        // xyz = position (world, mm); w = 0 directional, 1 point, 2 spot
    vec4 directionIntensity;  // xyz = direction the light travels (unit); w = intensity
    vec4 colorEnabled;        // rgb = colour; a = 1 enabled, 0 disabled
    vec4 spot;                // x = cos(inner half-angle), y = cos(outer half-angle), z = range (0 = infinite)
};
layout(std140, binding = 1) uniform LightBlock {
    vec4      uAmbient;       // rgb = ambient colour * ambient intensity
    ivec4     uLightConfig;   // x = number of lights, y = mode (0 ambient only, 1 lights),
                              // z = term flags: bit0 ambient, bit1 diffuse, bit2 specular
    LightData uLights[MAX_LIGHTS];
};
)GLSL";

      // ----------------------------------------------------------------------------
      // MESH - lit surfaces (B-Rep faces, STL triangles)
      // ----------------------------------------------------------------------------
      inline const char* const kMeshVertex = R"GLSL(
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;

uniform mat4 uModel;       // local -> world
uniform mat3 uNormalMat;   // normal matrix of uModel

out vec3 vWorldPos;
out vec3 vNormal;

void main() {
    vec4 world  = uModel * vec4(aPosition, 1.0);
    vWorldPos   = world.xyz;
    vNormal     = uNormalMat * aNormal;
    gl_Position = uViewProj * world;
}
)GLSL";

      // Blinn-Phong:  colour = ambient + sum over lights of (diffuse + specular)
      inline const char* const kMeshFragment = R"GLSL(
in vec3 vWorldPos;
in vec3 vNormal;

uniform vec3  uMatAmbient;
uniform vec3  uMatDiffuse;
uniform vec3  uMatSpecular;
uniform float uMatShininess;
uniform float uMatOpacity;

out vec4 FragColor;

void main() {
    // Two-sided lighting: STL and tessellated data often have inconsistent
    // triangle winding, so back faces are lit as if they faced the viewer.
    vec3 N = normalize(vNormal);
    if (!gl_FrontFacing) N = -N;
    vec3 V = ViewVector(vWorldPos);

    int  terms = uLightConfig.z;
    vec3 color = vec3(0.0);

    // --- ambient term: the only one in "ambient only" mode ------------------
    if ((terms & 1) != 0) color += uAmbient.rgb * uMatAmbient;

    // --- light sources (skipped entirely in "ambient only" mode) ------------
    if (uLightConfig.y == 1) {
        for (int i = 0; i < uLightConfig.x; ++i) {
            LightData light = uLights[i];
            if (light.colorEnabled.a < 0.5) continue;

            int   type = int(light.positionType.w + 0.5);
            vec3  L;              // unit vector from the surface towards the light
            float atten = 1.0;

            if (type == 0) {
                L = -light.directionIntensity.xyz;             // directional
            } else {
                vec3  toLight = light.positionType.xyz - vWorldPos;
                float dist    = length(toLight);
                L = toLight / max(dist, 1e-6);
                if (light.spot.z > 0.0) {                       // optional range fall-off
                    float k = clamp(1.0 - dist / light.spot.z, 0.0, 1.0);
                    atten *= k * k;
                }
                if (type == 2) {                                // spot cone
                    float cosAngle = dot(-L, light.directionIntensity.xyz);
                    atten *= smoothstep(light.spot.y, light.spot.x, cosAngle);
                }
            }

            vec3  radiance = light.colorEnabled.rgb * light.directionIntensity.w * atten;
            float NdotL    = max(dot(N, L), 0.0);

            if ((terms & 2) != 0) color += radiance * uMatDiffuse * NdotL;

            if ((terms & 4) != 0 && NdotL > 0.0) {
                vec3 H = normalize(L + V);                      // Blinn half vector
                color += radiance * uMatSpecular * pow(max(dot(N, H), 0.0), uMatShininess);
            }
        }
    }

    FragColor = vec4(color, uMatOpacity);
}
)GLSL";

      // ----------------------------------------------------------------------------
      // GIZMO - the axis arrows. Not affected by scene lighting so the X/Y/Z colours
      // always read clearly; a simple view-facing shade gives them volume.
      // (Uses kMeshVertex.)
      // ----------------------------------------------------------------------------
      inline const char* const kGizmoFragment = R"GLSL(
in vec3 vWorldPos;
in vec3 vNormal;
uniform vec3 uColor;
out vec4 FragColor;

void main() {
    float facing = abs(dot(normalize(vNormal), ViewVector(vWorldPos)));
    FragColor = vec4(uColor * (0.45 + 0.55 * facing), 1.0);
}
)GLSL";

      // ----------------------------------------------------------------------------
      // LINES - edge overlay / wireframe with a real pixel width.
      //
      // Core-profile OpenGL only guarantees 1-pixel lines (glLineWidth > 1 is
      // deprecated and vendor dependent). To get reliable BOLD edges, a geometry
      // shader turns every line segment into a screen-aligned quad that is
      // uLineWidth pixels wide.
      // ----------------------------------------------------------------------------
      inline const char* const kLineVertex = R"GLSL(
layout(location = 0) in vec3 aPosition;
uniform mat4 uModel;

void main() {
    gl_Position = uViewProj * uModel * vec4(aPosition, 1.0);
}
)GLSL";

      inline const char* const kLineGeometry = R"GLSL(
layout(lines) in;
layout(triangle_strip, max_vertices = 4) out;

uniform float uLineWidth;   // pixels

void main() {
    vec4 p0 = gl_in[0].gl_Position;
    vec4 p1 = gl_in[1].gl_Position;

    // Clip against the eye plane first: a point behind the eye (w <= 0) cannot
    // be divided by w, so pull it forward along the segment.
    const float kMinW = 1e-5;
    if (p0.w < kMinW && p1.w < kMinW) return;
    if (p0.w < kMinW)      p0 = mix(p0, p1, (kMinW - p0.w) / (p1.w - p0.w));
    else if (p1.w < kMinW) p1 = mix(p1, p0, (kMinW - p1.w) / (p0.w - p1.w));

    // Segment direction in pixels, and its perpendicular.
    vec2 halfViewport = uViewport.xy * 0.5;
    vec2 s0 = p0.xy / p0.w * halfViewport;
    vec2 s1 = p1.xy / p1.w * halfViewport;
    vec2 dir = s1 - s0;
    float len = length(dir);
    dir = len > 1e-6 ? dir / len : vec2(1.0, 0.0);
    vec2 offsetPx  = vec2(-dir.y, dir.x) * (uLineWidth * 0.5);
    vec2 offsetNdc = offsetPx / halfViewport;

    // Offsets are multiplied by w so they survive the perspective divide.
    gl_Position = vec4(p0.xy + offsetNdc * p0.w, p0.zw); EmitVertex();
    gl_Position = vec4(p0.xy - offsetNdc * p0.w, p0.zw); EmitVertex();
    gl_Position = vec4(p1.xy + offsetNdc * p1.w, p1.zw); EmitVertex();
    gl_Position = vec4(p1.xy - offsetNdc * p1.w, p1.zw); EmitVertex();
    EndPrimitive();
}
)GLSL";

      inline const char* const kLineFragment = R"GLSL(
uniform vec4 uColor;
out vec4 FragColor;

void main() {
    FragColor = uColor;
}
)GLSL";

      // ----------------------------------------------------------------------------
      // POINTS - point clouds, drawn as round dots.
      // ----------------------------------------------------------------------------
      inline const char* const kPointVertex = R"GLSL(
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aColor;      // only fed when the cloud has colours

uniform mat4  uModel;
uniform float uPointSize;                 // diameter in pixels
uniform vec3  uColor;
uniform int   uUseVertexColor;

out vec3 vColor;

void main() {
    gl_Position  = uViewProj * uModel * vec4(aPosition, 1.0);
    gl_PointSize = uPointSize;
    vColor       = uUseVertexColor != 0 ? aColor : uColor;
}
)GLSL";

      inline const char* const kPointFragment = R"GLSL(
in vec3 vColor;
out vec4 FragColor;

void main() {
    // gl_PointCoord runs 0..1 across the point's square; cut it to a disc.
    vec2 c = gl_PointCoord * 2.0 - 1.0;
    if (dot(c, c) > 1.0) discard;
    FragColor = vec4(vColor, 1.0);
}
)GLSL";

      // ----------------------------------------------------------------------------
      // GRID - reference grid in the world XY plane, centred on the origin.
      //
      // The grid is ONE quad. The lines are computed per pixel in the fragment
      // shader, so a 10 m grid with 1 mm cells costs the same as a small one (as
      // geometry it would be 20 000 lines that turn into noise when zoomed out).
      //
      // Four line levels are drawn: 1x, 10x, 100x and 1000x the minor spacing. Each
      // level fades out when its cells get smaller than a few pixels - zoomed in you
      // see the 1 mm grid, zoomed out only the 10/100/1000 mm lines remain (the same
      // behaviour as Blender's floor grid). The X axis is drawn red, the Y axis green.
      // ----------------------------------------------------------------------------
      inline const char* const kGridVertex = R"GLSL(
layout(location = 0) in vec2 aCorner;     // (-1,-1) .. (1,1)
uniform float uHalfSize;                  // half the grid width, mm
out vec2 vPlane;                          // world XY of this fragment

void main() {
    vPlane      = aCorner * uHalfSize;
    gl_Position = uViewProj * vec4(vPlane, 0.0, 1.0);
}
)GLSL";

      inline const char* const kGridFragment = R"GLSL(
in vec2 vPlane;
uniform float uMinorSpacing;              // mm between the finest lines
uniform vec3  uLineColor;
out vec4 FragColor;

void main() {
    const float kWeight[4] = float[4](0.30, 0.50, 0.75, 1.00);

    float alpha   = 0.0;
    float spacing = uMinorSpacing;
    for (int level = 0; level < 4; ++level) {
        vec2 cell = vPlane / spacing;                       // position in cell units
        vec2 px   = max(fwidth(cell), vec2(1e-7));          // cells covered by one pixel
        vec2 dist = abs(fract(cell - 0.5) - 0.5) / px;      // pixels to the nearest line
        float line = 1.0 - clamp(min(dist.x, dist.y), 0.0, 1.0);
        // Fully visible while a cell is wider than ~16 px, gone below ~3 px.
        float fade = 1.0 - smoothstep(0.06, 0.30, max(px.x, px.y));
        alpha = max(alpha, line * fade * kWeight[level]);
        spacing *= 10.0;
    }

    vec3 color = uLineColor;

    // World axes through the origin.
    vec2  planePx = max(fwidth(vPlane), vec2(1e-7));
    float xAxis = 1.0 - clamp(abs(vPlane.y) / planePx.y - 0.75, 0.0, 1.0);
    float yAxis = 1.0 - clamp(abs(vPlane.x) / planePx.x - 0.75, 0.0, 1.0);
    color = mix(color, vec3(0.90, 0.22, 0.22), xAxis);
    color = mix(color, vec3(0.30, 0.75, 0.25), yAxis);
    alpha = max(alpha * 0.85, max(xAxis, yAxis));

    if (alpha < 0.01) discard;
    FragColor = vec4(color, alpha);
}
)GLSL";

   } // namespace Shaders
} // namespace CadCore
