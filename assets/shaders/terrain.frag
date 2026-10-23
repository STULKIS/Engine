#version 450 core

in vec3 vWorldPos;
in vec3 vNormal;
in vec2 vUV;
in vec4 vColor;

layout(location = 0) out vec4 oColor;

uniform sampler2D uGrassTexture;
uniform sampler2D uRockTexture;
uniform sampler2D uSoilTexture;

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
uniform float uWaterLevel;
uniform float uSnowLine;

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


// Stylised (NPR) shading controls. The realistic path ignores these.
uniform int uCelShading;
uniform float uCelSteps;
uniform float uRimStrength;
uniform vec3 uRimColor;


#include "common/noise.glsl"
#include "common/lighting.glsl"

void main() {
    vec3 N = normalize(vNormal);
    float slope = 1.0 - N.y;

    // Detail textures: the grass map is tiled in mesh UV space, rock and soil
    // are projected from above so they never stretch on cliffs.
    vec3 grassDetail = texture(uGrassTexture, vUV).rgb;
    vec3 rockDetail = texture(uRockTexture, vWorldPos.xz * 0.055).rgb;
    vec3 soilDetail = texture(uSoilTexture, vWorldPos.xz * 0.085).rgb;

    vec3 grass = vColor.rgb * (0.62 + grassDetail * 0.85);
    vec3 rock = mix(vColor.rgb, vec3(0.52, 0.51, 0.49), 0.55) * (0.55 + rockDetail * 0.9);
    vec3 soil = vColor.rgb * mix(vec3(0.75, 0.6, 0.45), vec3(1.0), soilDetail.r) * 1.05;

    // Blend grass, rock and soil by slope.
    float rockWeight = smoothstep(0.18, 0.55, slope);
    float soilWeight = smoothstep(0.08, 0.24, slope) * (1.0 - rockWeight);
    vec3 albedo = mix(grass, soil, soilWeight);
    albedo = mix(albedo, rock, rockWeight);

    // Shoreline band of wet sand.
    float shore = 1.0 - smoothstep(uWaterLevel + 0.15, uWaterLevel + 3.4, vWorldPos.y);
    albedo = mix(albedo, vec3(0.52, 0.46, 0.35) * (0.75 + soilDetail.g * 0.5), shore * 0.75);

    // High-altitude snow.
    float snowBlend = smoothstep(uSnowLine, uSnowLine + 12.0, vWorldPos.y) * (1.0 - smoothstep(0.35, 0.62, slope));
    albedo = mix(albedo, vec3(0.86, 0.9, 0.96), snowBlend * 0.9);

    // Fine-scale tonal break-up and a little macro colour variation.
    float grain = ValueNoise2(vWorldPos.xz * 1.7) * 0.12 + ValueNoise2(vWorldPos.xz * 0.19) * 0.18;
    albedo *= 0.9 + grain;

    // Sparse grass tufts as high frequency colour accents.
    float tufts = smoothstep(0.62, 0.95, FBM2(vWorldPos.xz * 0.9));
    albedo += vec3(0.035, 0.05, 0.02) * tufts * (1.0 - rockWeight);

    float shadow = SampleShadow(vWorldPos, N, 0.0006);
    vec3 lit = ApplySunlight(albedo, N, vWorldPos, shadow);

    // Subtle ambient occlusion in creases (approximated by slope).
    lit *= 1.0 - slope * 0.18;

    lit = ApplyFog(lit, vWorldPos);
    oColor = vec4(lit, 1.0);
}
