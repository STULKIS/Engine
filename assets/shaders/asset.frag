#version 450 core

// Fragment stage for imported mesh assets. Shares the atmosphere, shadow and
// cel-lighting helpers with the rest of the world, and adds an optional albedo
// texture so painted placeholder art (or real art) reads properly.

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

uniform sampler2D uAlbedoTexture;
uniform int uHasTexture;

#include "common/noise.glsl"
#include "common/lighting.glsl"

void main() {
    vec3 N = normalize(vNormal);

    vec3 albedo = vAlbedo;
    if (uHasTexture == 1) {
        vec3 sampled = texture(uAlbedoTexture, vUV).rgb;
        // Keep the entity tint as a multiplier but never let it crush the paint.
        albedo = sampled * mix(vec3(1.0), vAlbedo, 0.65);
    }

    // A little painted grain so flat imported faces still feel hand-made.
    float grain = ValueNoise2(vWorldPos.xz * 3.0 + vWorldPos.y * 2.0);
    albedo *= 0.94 + grain * 0.12;

    float shadow = SampleShadow(vWorldPos, N, 0.0008);
    vec3 lit = ApplySunlight(albedo, N, vWorldPos, shadow);
    lit = ApplyFog(lit, vWorldPos);

    // Alpha 0 = this object may be ink-outlined by the post pass.
    oColor = vec4(lit, 0.0);
}
