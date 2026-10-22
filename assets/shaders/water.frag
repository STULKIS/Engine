#version 450 core

in vec3 vWorldPos;
in vec3 vNormal;
in vec2 vUV;
in float vWaveHeight;

layout(location = 0) out vec4 oColor;

uniform sampler2D uHeightTexture;
uniform float uWaterLevel;
uniform float uTerrainSize;

uniform vec3 uCameraPos;
uniform float uTime;
uniform vec3 uSunPosition;
uniform vec3 uSunDirection;
uniform vec3 uSunColor;
uniform float uSunIntensity;
uniform vec3 uAmbientSky;
uniform vec3 uAmbientGround;
uniform vec3 uFogColor;
uniform float uFogDensity;
uniform float uFogHeightFalloff;
uniform float uViewDistance;

uniform sampler2D uShadowMap;
uniform mat4 uLightSpaceMatrix;
uniform float uShadowTexelSize;
uniform int uShadowEnabled;

#include "common/noise.glsl"
#include "common/lighting.glsl"

void main() {
    vec3 N = normalize(vNormal);
    // Small-scale normal detail breaks up the smooth analytic waves.
    vec2 detailUV = vWorldPos.xz * 0.12 + uTime * 0.02;
    float d0 = FBM2(detailUV);
    float d1 = FBM2(detailUV + vec2(0.35, 0.0));
    float d2 = FBM2(detailUV + vec2(0.0, 0.35));
    N = normalize(N + vec3((d0 - d1) * 1.5, 0.0, (d0 - d2) * 1.5));

    vec3 V = normalize(uCameraPos - vWorldPos);

    // Sample the terrain height under the water for depth tint and shoreline foam.
    vec2 heightUV = vWorldPos.xz / uTerrainSize + 0.5;
    float terrainHeight = texture(uHeightTexture, clamp(heightUV, 0.0, 1.0)).r;
    float depth = max(uWaterLevel - terrainHeight, 0.0);

    vec3 deepColor = vec3(0.008, 0.055, 0.075);
    vec3 shallowColor = vec3(0.075, 0.22, 0.21);
    vec3 body = mix(shallowColor, deepColor, saturate(depth * 0.22));

    // Fresnel reflection of the sky.
    float fresnel = 0.02 + 0.98 * pow(1.0 - saturate(dot(N, V)), 5.0);
    vec3 skyReflection = mix(uFogColor, uAmbientSky * 1.6, 0.55);

    // Sun specular highlight.
    vec3 H = normalize(-uSunDirection + V);
    float specular = pow(max(dot(N, H), 0.0), 320.0) * 3.0;
    float sunFacing = max(dot(N, uSunPosition), 0.0);

    vec3 color = mix(body, skyReflection, fresnel * 0.85);
    color += uSunColor * uSunIntensity * specular * saturate(0.35 + sunFacing);
    color += uSunColor * uSunIntensity * body * sunFacing * 0.05;

    // Foam at the shoreline plus wind-driven whitecaps.
    float foamMask = (1.0 - smoothstep(0.0, 0.9, depth)) * 0.75;
    float whitecaps = smoothstep(0.72, 0.95, FBM2(vWorldPos.xz * 0.35 + uTime * 0.1)) *
                      smoothstep(0.35, 0.9, vWaveHeight + 0.25);
    color = mix(color, vec3(0.82, 0.9, 0.92), saturate(foamMask + whitecaps * 0.45));

    // Distance fog and gentle alpha falloff in the shallows.
    color = ApplyFog(color, vWorldPos);
    float alpha = saturate(0.35 + depth * 0.45 + fresnel);
    oColor = vec4(color, alpha);
}
