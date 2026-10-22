#version 450 core

in vec3 vWorldPos;
in vec3 vNormal;
in vec2 vUV;
in vec4 vAlbedo;
in float vWindWeight;

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

#include "common/noise.glsl"
#include "common/lighting.glsl"

void main() {
    vec3 N = normalize(vNormal);
    if (!gl_FrontFacing) {
        N = -N;
    }

    vec3 albedo = vAlbedo.rgb;

    // Procedural leaf/clump variation, two scales.
    float leafNoise = ValueNoise2(vWorldPos.xz * 6.0 + vWorldPos.y * 1.7);
    float macro = ValueNoise2(vWorldPos.xz * 0.7 + 4.2);
    albedo *= 0.82 + leafNoise * 0.32 + macro * 0.12;

    // Darken the lower interior of canopies to fake ambient occlusion.
    albedo *= mix(0.7, 1.0, saturate(vWindWeight * 0.6 + N.y * 0.4));

    float shadow = SampleShadow(vWorldPos, N, 0.0012);
    vec3 lit = ApplySunlight(albedo, N, vWorldPos, shadow);

    // Back-lit translucency for grass blades and leaves.
    float backlight = pow(max(dot(normalize(uSunDirection), N), 0.0), 2.0);
    lit += uSunColor * uSunIntensity * backlight * albedo * 0.09;

    lit = ApplyFog(lit, vWorldPos);
    oColor = vec4(lit, 1.0);
}
