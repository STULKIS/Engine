#version 450 core

in vec2 vNDC;
in vec2 vUV;

layout(location = 0) out vec4 oColor;

uniform mat4 uInvViewProj;
uniform vec3 uCameraPos;
uniform float uTime;

uniform vec3 uSunPosition;     // unit vector toward the sun
uniform vec3 uMoonPosition;    // unit vector toward the moon
uniform vec3 uSunColor;
uniform float uSunIntensity;
uniform float uDaylight;       // 0 night .. 1 day
uniform float uStars;          // 0 day .. 1 night
uniform int uAnimeSky;         // 1 = stylised (hand painted) sky

#include "common/noise.glsl"

// Stylised sky: a small, saturated palette with a banded gradient ladder and
// flat, two-tone clouds. Everything is generated here; no texture is sampled.
vec3 AnimeSkyGradient(vec3 dir, float daylight) {
    vec3 dayZenith = vec3(0.13, 0.38, 0.88);
    vec3 dayHorizon = vec3(0.66, 0.87, 0.99);
    vec3 nightZenith = vec3(0.015, 0.025, 0.09);
    vec3 nightHorizon = vec3(0.07, 0.09, 0.2);

    vec3 zenith = mix(nightZenith, dayZenith, daylight);
    vec3 horizon = mix(nightHorizon, dayHorizon, daylight);

    float up = clamp(dir.y, 0.0, 1.0);
    float curve = pow(up, 0.5);
    // Five steps, softened just enough not to alias on a moving camera.
    float scaled = curve * 5.0;
    float banded = (floor(scaled) + smoothstep(0.35, 0.65, fract(scaled))) / 5.0;
    float t = mix(curve, banded, 0.72);
    return mix(horizon, zenith, clamp(t, 0.0, 1.0));
}

vec3 SkyGradient(vec3 dir, float daylight) {
    vec3 dayZenith = vec3(0.075, 0.21, 0.55);
    vec3 dayHorizon = vec3(0.58, 0.72, 0.9);
    vec3 nightZenith = vec3(0.004, 0.009, 0.026);
    vec3 nightHorizon = vec3(0.018, 0.032, 0.062);

    vec3 zenith = mix(nightZenith, dayZenith, daylight);
    vec3 horizon = mix(nightHorizon, dayHorizon, daylight);

    float up = max(dir.y, 0.0);
    vec3 sky = mix(horizon, zenith, pow(up, 0.42));
    return sky;
}

void main() {
    vec4 farPoint = uInvViewProj * vec4(vNDC, 1.0, 1.0);
    vec3 dir = normalize(farPoint.xyz / farPoint.w - uCameraPos);

    vec3 color = uAnimeSky == 1 ? AnimeSkyGradient(dir, uDaylight) : SkyGradient(dir, uDaylight);

    // Warm horizon glow toward the sun, strongest near sunrise/sunset.
    vec2 sunHorizontal = normalize(uSunPosition.xz + vec2(0.0001));
    vec2 viewHorizontal = normalize(dir.xz + vec2(0.0001));
    float sunAlignment = max(dot(sunHorizontal, viewHorizontal), 0.0);
    float dusk = exp(-abs(uSunPosition.y) * 5.0);
    float horizonBand = exp(-max(dir.y, 0.0) * 7.0);
    color += vec3(0.95, 0.36, 0.12) * pow(sunAlignment, 3.0) * horizonBand * dusk * 0.75;
    color += uSunColor * pow(sunAlignment, 10.0) * horizonBand * 0.18;

    float sunAngle = dot(dir, uSunPosition);
    float sunVisible = smoothstep(-0.05, 0.02, uSunPosition.y);

    if (uAnimeSky == 1) {
        // Flat disc with a thin ring, like a painted background.
        float disc = smoothstep(0.99855, 0.99885, sunAngle);
        float ring = smoothstep(0.9974, 0.9977, sunAngle) - smoothstep(0.99815, 0.99845, sunAngle);
        color += uSunColor * disc * sunVisible * 34.0;
        color += uSunColor * ring * sunVisible * 2.2;
    } else {
        // Sun disk with a soft limb plus atmospheric glow.
        float disk = smoothstep(0.99975, 0.99992, sunAngle);
        float glow = pow(max(sunAngle, 0.0), 260.0) * 0.6 + pow(max(sunAngle, 0.0), 18.0) * 0.12;
        color += uSunColor * disk * sunVisible * 26.0;
        color += uSunColor * glow * sunVisible * (0.5 + uSunIntensity * 0.25);
    }

    // Moon disk and faint halo at night.
    float moonAngle = max(dot(dir, uMoonPosition), 0.0);
    float moonDisk = smoothstep(0.99955, 0.99982, moonAngle);
    color += vec3(0.72, 0.78, 0.9) * moonDisk * uStars * 2.2;
    color += vec3(0.25, 0.32, 0.5) * pow(moonAngle, 90.0) * uStars * 0.15;

    // Stars: sparse hash points, faded near the horizon.
    if (uStars > 0.01) {
        vec3 starCell = floor(dir * 420.0);
        float star = Hash31(starCell);
        float brightness = uAnimeSky == 1 ? smoothstep(0.9993, 1.0, star) * 1.4
                                          : smoothstep(0.9968, 1.0, star);
        float twinkle = 0.7 + 0.3 * sin(uTime * 2.2 + Hash31(starCell + 7.0) * 6.28);
        color += vec3(0.85, 0.9, 1.0) * brightness * twinkle * uStars *
                 smoothstep(-0.02, 0.22, dir.y) * 1.3;
    }

    // Procedural cloud deck projected onto a virtual plane.
    if (dir.y > 0.015) {
        vec2 cloudUV = dir.xz / (dir.y + 0.18);
        cloudUV = cloudUV * 0.55 + vec2(uTime * 0.004, uTime * 0.0016);
        float base = FBM2(cloudUV * 1.4 + 7.3);
        float detail = FBM2(cloudUV * 3.3 - 2.1);
        float coverage = smoothstep(0.52, 0.78, base * 0.72 + detail * 0.28);
        coverage *= smoothstep(0.015, 0.16, dir.y);

        float sunLight = pow(max(dot(normalize(vec3(dir.x, 0.45, dir.z)), uSunPosition), 0.0), 3.0);
        vec3 cloudShadow = vec3(0.32, 0.36, 0.43) * (0.25 + uDaylight * 0.75);
        vec3 cloudLit = mix(cloudShadow, vec3(1.02, 0.99, 0.95), 0.35 + sunLight * 0.55);
        cloudLit *= 0.45 + uDaylight * 0.75;
        cloudLit += uSunColor * uSunIntensity * sunLight * 0.06;

        if (uAnimeSky == 1) {
            // Flat two-tone clouds: a crisp silhouette, a warm rim on the sun
            // side and a cool lavender shadow instead of a smooth gradient.
            float edge = smoothstep(0.585, 0.62, base * 0.72 + detail * 0.28);
            float inner = smoothstep(0.63, 0.70, base * 0.72 + detail * 0.28);
            vec3 shadow = vec3(0.70, 0.74, 0.93) * (0.30 + uDaylight * 0.75);
            vec3 litCloud = vec3(1.02, 1.0, 0.99) * (0.45 + uDaylight * 0.7);
            litCloud = mix(litCloud, uSunColor * 1.1, sunLight * 0.45);
            vec3 stylised = mix(shadow, litCloud, inner);
            stylised += uSunColor * sunLight * 0.10;
            color = mix(color, stylised, edge * 0.92);
        } else {
            color = mix(color, cloudLit, coverage * 0.82);
        }
    }

    // Below the horizon, fade into the atmospheric fog colour.
    float below = smoothstep(0.0, -0.12, dir.y);
    vec3 groundHaze = vec3(0.16, 0.18, 0.2) * (0.2 + uDaylight * 0.8);
    color = mix(color, groundHaze, below * 0.85);

    oColor = vec4(color, 1.0);
}
