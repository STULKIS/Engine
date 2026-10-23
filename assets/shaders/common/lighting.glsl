// Shared lighting, shadow and atmosphere helpers.
// Expects the following uniforms to be declared by the including shader:
//   uniform vec3 uCameraPos;
//   uniform vec3 uSunDirection;   // direction the light travels
//   uniform vec3 uSunColor;
//   uniform float uSunIntensity;
//   uniform vec3 uAmbientSky;
//   uniform vec3 uAmbientGround;
//   uniform vec3 uFogColor;
//   uniform float uFogDensity;
//   uniform float uFogHeightFalloff;
//   uniform float uViewDistance;
//   uniform sampler2D uShadowMap;
//   uniform mat4 uLightSpaceMatrix;
//   uniform float uShadowTexelSize;
//   uniform int uShadowEnabled;
// and, for the stylised (anime) path:
//   uniform vec3 uCameraPos;
//   uniform int uCelShading;
//   uniform float uCelSteps;
//   uniform float uRimStrength;
//   uniform vec3 uRimColor;

float SampleShadow(vec3 worldPosition, vec3 normal, float slopeBias) {
    if (uShadowEnabled == 0) {
        return 1.0;
    }
    vec4 lightSpace = uLightSpaceMatrix * vec4(worldPosition + normal * 0.08, 1.0);
    vec3 proj = lightSpace.xyz / lightSpace.w;
    proj = proj * 0.5 + 0.5;
    if (proj.x < 0.0 || proj.x > 1.0 || proj.y < 0.0 || proj.y > 1.0 || proj.z > 1.0) {
        return 1.0;
    }

    float ndl = max(dot(normal, -uSunDirection), 0.0);
    float bias = max(0.0022 * (1.0 - ndl), 0.00035) + slopeBias;
    float shadow = 0.0;
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            float depth = texture(uShadowMap, proj.xy + vec2(float(x), float(y)) * uShadowTexelSize).r;
            shadow += (proj.z - bias > depth) ? 0.0 : 1.0;
        }
    }
    return shadow / 9.0;
}

vec3 AmbientLight(vec3 normal) {
    float up = normal.y * 0.5 + 0.5;
    return mix(uAmbientGround, uAmbientSky, up);
}

// Quantises a light term into hard bands with a narrow anti-aliased edge, which
// is what makes cel shading read as flat colour instead of a gradient.
float CelBand(float ndl) {
    float steps = max(2.0, uCelSteps);
    float scaled = clamp(ndl, 0.0, 1.0) * steps;
    float lower = floor(scaled);
    float fraction = scaled - lower;
    float stepped = (lower + smoothstep(0.46, 0.54, fraction)) / steps;
    return clamp(stepped, 0.0, 1.0);
}

vec3 ApplySunlight(vec3 albedo, vec3 normal, vec3 worldPosition, float shadow) {
    float ndl = max(dot(normal, -uSunDirection), 0.0);
    vec3 ambient = AmbientLight(normal) * albedo;

    if (uCelShading == 1) {
        // Two-tone shadow: the shadow map is thresholded so the terminator is
        // crisp instead of a PCF gradient.
        float shadowTerm = smoothstep(0.35, 0.65, shadow);
        float lit = CelBand(ndl) * mix(0.22, 1.0, shadowTerm);
        vec3 color = albedo * (uSunColor * uSunIntensity * lit) + ambient * 1.08;

        // Rim light: bright edge where the surface turns away from the camera.
        vec3 viewDirection = normalize(uCameraPos - worldPosition);
        float facing = clamp(dot(normal, viewDirection), 0.0, 1.0);
        float rim = pow(1.0 - facing, 3.0);
        color += uRimColor * rim * uRimStrength * (0.35 + 0.65 * shadowTerm) * (0.35 + 0.65 * ndl);
        return color;
    }

    // Slight wrap lighting softens the terminator on vegetation and terrain.
    float wrapped = clamp((ndl + 0.18) / 1.18, 0.0, 1.0);
    vec3 direct = uSunColor * uSunIntensity * wrapped * shadow;
    return albedo * direct + ambient;
}

float FogAmount(vec3 worldPosition) {
    vec3 delta = worldPosition - uCameraPos;
    float distance = length(delta);
    // Analytic height fog: denser near the waterline, thinning with altitude.
    float heightFactor = exp(-max(worldPosition.y, -20.0) * uFogHeightFalloff);
    float density = uFogDensity * (0.55 + 0.45 * heightFactor);
    float amount = 1.0 - exp(-density * distance);
    // Fade distant geometry a little earlier so the world edge is hidden.
    amount = max(amount, smoothstep(uViewDistance * 0.72, uViewDistance, distance));
    return clamp(amount, 0.0, 0.96);
}

vec3 ApplyFog(vec3 color, vec3 worldPosition) {
    float amount = FogAmount(worldPosition);
    vec3 fogTint = uFogColor;
    // Slight blue lift with distance gives the scene depth.
    return mix(color, fogTint, amount);
}
