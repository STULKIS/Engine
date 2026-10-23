#version 450 core

in vec2 vUV;
layout(location = 0) out vec4 oColor;

uniform sampler2D uSceneTexture;
uniform sampler2D uBloomTexture;
uniform float uExposure;
uniform float uBloomStrength;
uniform int uUseBloom;
uniform int uUseTonemap;
uniform float uVignette;

// --- Stylised (NPR) outline ------------------------------------------------
uniform sampler2D uDepthTexture;   // resolved scene depth
uniform vec2 uInvResolution;       // 1 / render target size
uniform int uOutlineEnabled;
uniform float uOutlineThickness;   // pixels
uniform float uOutlineStrength;    // 0..1
uniform vec3 uOutlineColor;
uniform float uNearPlane;
uniform float uFarPlane;
uniform int uCelShading;
uniform float uSaturation;
uniform int uPosterizeLevels;      // 0 = off

// --- Atmosphere -----------------------------------------------------------
uniform int uGodRaysEnabled;
uniform float uGodRayStrength;
uniform vec2 uSunScreenPos;        // sun position in [0,1] screen space
uniform float uSunVisibility;      // 0 when the sun is below the horizon
uniform vec3 uSunColorPost;
uniform float uContrast;
uniform float uTemperature;        // negative = cooler, positive = warmer

vec3 ACESFilmic(vec3 x) {
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

// Soft, saturation-preserving tone curve. Unlike ACES it keeps flat colour
// fields vivid, which is what makes stylised frames read as painted.
vec3 NeutralToneMap(vec3 color) {
    const float startCompression = 0.76;
    const float desaturation = 0.15;

    float x = min(color.r, min(color.g, color.b));
    float offset = x < 0.08 ? x - 6.25 * x * x : 0.04;
    color -= offset;

    float peak = max(color.r, max(color.g, color.b));
    if (peak < startCompression) {
        return color;
    }
    float d = 1.0 - startCompression;
    float newPeak = 1.0 - d * d / (peak + d - startCompression);
    color *= newPeak / peak;

    float g = 1.0 - 1.0 / (desaturation * (peak - newPeak) + 1.0);
    return mix(color, vec3(newPeak), g);
}

// Screen-space light shafts: march from the pixel toward the sun and accumulate
// only bright sky pixels, so shafts appear through gaps in clouds and trees.
vec3 GodRays(vec2 uv) {
    const int kSamples = 20;
    vec2 delta = (uSunScreenPos - uv) / float(kSamples);
    vec2 coord = uv;
    float decay = 1.0;
    float accumulation = 0.0;

    for (int i = 0; i < kSamples; ++i) {
        coord += delta;
        vec2 clampedCoord = clamp(coord, vec2(0.0), vec2(1.0));
        vec3 sampleColor = texture(uSceneTexture, clampedCoord).rgb;
        float luminance = dot(sampleColor, vec3(0.2126, 0.7152, 0.0722));
        // Anything that is not a bright sky/cloud pixel blocks the shaft.
        accumulation += smoothstep(1.05, 2.2, luminance) * decay;
        decay *= 0.96;
    }

    float falloff = smoothstep(0.95, 0.15, length(uv - uSunScreenPos));
    return uSunColorPost * accumulation * falloff * uGodRayStrength / float(kSamples) * 7.0;
}

// Eye-space distance for a depth-buffer sample.
float LinearEyeDepth(vec2 uv) {
    float depth = texture(uDepthTexture, uv).r;
    if (depth >= 0.99999) {
        return uFarPlane * 8.0; // sky / cleared depth
    }
    float ndc = depth * 2.0 - 1.0;
    return (2.0 * uNearPlane * uFarPlane) /
           (uFarPlane + uNearPlane - ndc * (uFarPlane - uNearPlane));
}

// Silhouette detection. A naive depth-gradient test inks every terrain crease
// seen at a grazing angle, which is what made the first version draw outlines
// over the whole frame. This test compares the *two sides* of each axis: a real
// silhouette has one side much farther than the other, while a smooth slope has
// a similar depth on both sides. The result is limited to genuine object
// boundaries, faded out with distance, and masked per object (grass opts out).
float SilhouetteEdge(vec2 uv) {
    vec2 texel = uInvResolution * max(uOutlineThickness, 0.5);
    float center = LinearEyeDepth(uv);
    if (center > uFarPlane * 0.9) {
        return 0.0; // sky: the horizon is handled by the geometry silhouette
    }

    float left = LinearEyeDepth(uv - vec2(texel.x, 0.0));
    float right = LinearEyeDepth(uv + vec2(texel.x, 0.0));
    float down = LinearEyeDepth(uv - vec2(0.0, texel.y));
    float up = LinearEyeDepth(uv + vec2(0.0, texel.y));

    const float kRatio = 1.14;
    const float kSoftness = 0.12;
    float across = abs(left - right) / max(min(left, right), 0.05);
    float along = abs(up - down) / max(min(up, down), 0.05);
    float edge = max(smoothstep(kRatio, kRatio + kSoftness, across),
                     smoothstep(kRatio, kRatio + kSoftness, along));

    // Grass, shrubs and other noisy geometry write 1.0 into the alpha channel;
    // they are skipped so the frame does not fill with ink speckles.
    float mask = max(max(texture(uSceneTexture, uv).a,
                         texture(uSceneTexture, uv + vec2(texel.x, 0.0)).a),
                     max(texture(uSceneTexture, uv - vec2(texel.x, 0.0)).a,
                         max(texture(uSceneTexture, uv + vec2(0.0, texel.y)).a,
                             texture(uSceneTexture, uv - vec2(0.0, texel.y)).a)));
    edge *= 1.0 - smoothstep(0.35, 0.65, mask);

    // Fade distant lines: ink on a 400 m ridge just reads as noise.
    edge *= 1.0 - smoothstep(120.0, 320.0, center);
    return edge;
}

void main() {
    vec3 color = texture(uSceneTexture, vUV).rgb;
    if (uUseBloom == 1) {
        color += texture(uBloomTexture, vUV).rgb * uBloomStrength;
    }
    color *= uExposure;

    if (uCelShading == 1) {
        // Saturation-preserving curve: flat colour fields keep their intensity
        // instead of being desaturated into grey highlights.
        color = NeutralToneMap(max(color, 0.0));
        color = clamp(color * 1.08, 0.0, 1.0);
    } else if (uUseTonemap == 1) {
        color = ACESFilmic(color);
    } else {
        color = clamp(color, 0.0, 1.0);
    }

    // Ink the silhouettes over the shaded image (after tone mapping, so the
    // lines stay dark and crisp instead of being crushed by the shoulder).
    if (uOutlineEnabled == 1) {
        float edge = SilhouetteEdge(vUV);
        color = mix(color, uOutlineColor, edge * uOutlineStrength);
    }

    // Light shafts add atmosphere on top of the shaded image.
    if (uGodRaysEnabled == 1 && uSunVisibility > 0.01) {
        color += GodRays(vUV) * uSunVisibility;
    }

    // Contrast and colour temperature sit before the sRGB encode so they behave
    // like a grading pass instead of a filter.
    color = (color - 0.5) * uContrast + 0.5;
    color += uTemperature * vec3(0.045, 0.005, -0.05);
    color = max(color, 0.0);

    // Colour grade: saturation (and, for the stylised path, optional posterise).
    float luminance = dot(color, vec3(0.2126, 0.7152, 0.0722));
    color = mix(vec3(luminance), color, uSaturation);
    vec2 centered = vUV * 2.0 - 1.0;
    float vignette = 1.0 - dot(centered, centered) * uVignette;
    color *= clamp(vignette, 0.0, 1.0);

    // Manual sRGB encoding (the default framebuffer is not sRGB-enabled).
    color = pow(max(color, 0.0), vec3(1.0 / 2.2));

    // Optional hard quantisation of the final image for a graphic look.
    if (uPosterizeLevels > 1) {
        float levels = float(uPosterizeLevels);
        color = floor(color * levels + 0.5) / levels;
    }

    oColor = vec4(color, 1.0);
}
