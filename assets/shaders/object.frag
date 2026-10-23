#version 450 core

// Fragment stage for scene entities. Shares the atmosphere/shadow/cel helpers
// with terrain and foliage, so props pick up the same look as the world.

in vec3 vWorldPos;
in vec3 vNormal;
in vec2 vUV;
in vec3 vAlbedo;

layout(location = 0) out vec4 oColor;

uniform vec3 uCameraPos;
uniform vec3 uSunDirection;
uniform vec3 uSunColor;
uniform float uSunIntensity;
uniform vec3 uAmbientSky;
uniform vec3 uAmbientGround;
uniform vec3 uFogColor;
uniform float uFogDensity;
uniform float uFogHeightFalloff;
uniform float uViewDistance;
uniform float uTime;

uniform sampler2D uShadowMap;
uniform mat4 uLightSpaceMatrix;
uniform float uShadowTexelSize;
uniform int uShadowEnabled;

uniform sampler2D uShadowMapFar;
uniform mat4 uLightSpaceMatrixFar;
uniform float uShadowTexelSizeFar;
uniform float uCascadeSplit;
uniform vec3 uShadowTint;
uniform float uSpecularStrength;
uniform float uWarmEdge;

uniform int uCelShading;
uniform float uCelSteps;
uniform float uRimStrength;
uniform vec3 uRimColor;

#include "common/noise.glsl"
#include "common/lighting.glsl"

void main() {
    vec3 N = normalize(vNormal);

    // Subtle procedural grain keeps large flat primitives from looking plastic
    // without introducing a texture dependency.
    float grain = ValueNoise2(vWorldPos.xz * 2.1 + vWorldPos.y * 1.3) * 0.5 +
                  ValueNoise2(vWorldPos.xz * 8.0) * 0.5;
    vec3 albedo = vAlbedo * (0.88 + grain * 0.24);

    float shadow = SampleShadow(vWorldPos, N, 0.0008);
    vec3 lit = ApplySunlight(albedo, N, vWorldPos, shadow);
    lit = ApplyFog(lit, vWorldPos);

    // Alpha 0 = this object may be ink-outlined by the post pass.
    oColor = vec4(lit, 0.0);
}
