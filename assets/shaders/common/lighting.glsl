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
//   uniform float uShadowTexelSize;      // 1 / shadow map size (near cascade)
//   uniform int uShadowEnabled;
//   uniform sampler2D uShadowMapFar;     // second cascade
//   uniform mat4 uLightSpaceMatrixFar;
//   uniform float uShadowTexelSizeFar;
//   uniform float uCascadeSplit;         // world distance where cascade 1 takes over
// and, for the stylised (anime) path:
//   uniform vec3 uCameraPos;
//   uniform int uCelShading;
//   uniform float uCelSteps;
//   uniform float uRimStrength;
//   uniform vec3 uRimColor;
//   uniform vec3 uShadowTint;
//   uniform float uSpecularStrength;
//   uniform float uWarmEdge;

// 8 taps on a rotated Poisson disk. The rotation is derived from the fragment
// position, so the pattern decorrelates between neighbouring pixels and the
// result reads as a smooth penumbra instead of a visible 3x3 grid.
const vec2 kPoissonDisk[8] = vec2[8](
    vec2(-0.613, 0.617), vec2(0.170, -0.040), vec2(-0.299, -0.792), vec2(0.645, 0.493),
    vec2(-0.651, -0.197), vec2(0.421, -0.628), vec2(-0.090, 0.912), vec2(0.811, 0.111));

float PcfRotated(sampler2D shadowMap, vec3 proj, float texel, float bias, float radius) {
    float angle = fract(sin(dot(gl_FragCoord.xy, vec2(12.9898, 78.233))) * 43758.5453) * 6.2831853;
    float c = cos(angle);
    float s = sin(angle);
    mat2 rotation = mat2(c, -s, s, c);

    float shadow = 0.0;
    for (int i = 0; i < 8; ++i) {
        vec2 offset = rotation * kPoissonDisk[i] * texel * radius;
        float depth = texture(shadowMap, proj.xy + offset).r;
        shadow += (proj.z - bias > depth) ? 0.0 : 1.0;
    }
    return shadow / 8.0;
}

float SampleShadow(vec3 worldPosition, vec3 normal, float slopeBias) {
    if (uShadowEnabled == 0) {
        return 1.0;
    }

    float ndl = max(dot(normal, -uSunDirection), 0.0);
    float bias = max(0.0012 * (1.0 - ndl), 0.00018) + slopeBias;

    // Cascade selection: a tight near map keeps leaves and rocks crisp, a wide
    // far map covers the landscape. The near cascade is faded out over a small
    // range so the two maps never produce a hard seam.
    float cameraDistance = length(worldPosition - uCameraPos);
    if (cameraDistance < uCascadeSplit * 0.86) {
        vec4 lightSpace = uLightSpaceMatrix * vec4(worldPosition + normal * 0.05, 1.0);
        vec3 proj = lightSpace.xyz / lightSpace.w;
        proj = proj * 0.5 + 0.5;
        if (proj.x < 0.0 || proj.x > 1.0 || proj.y < 0.0 || proj.y > 1.0 || proj.z > 1.0) {
            return 1.0;
        }
        return PcfRotated(uShadowMap, proj, uShadowTexelSize, bias, 2.2);
    }

    vec4 lightSpace = uLightSpaceMatrixFar * vec4(worldPosition + normal * 0.05, 1.0);
    vec3 proj = lightSpace.xyz / lightSpace.w;
    proj = proj * 0.5 + 0.5;
    if (proj.x < 0.0 || proj.x > 1.0 || proj.y < 0.0 || proj.y > 1.0 || proj.z > 1.0) {
        return 1.0;
    }
    return PcfRotated(uShadowMapFar, proj, uShadowTexelSizeFar, bias, 1.6);
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
        // Stylised two-tone lighting. The shadow side is *tinted*, never black,
        // so material colour stays readable in shade (the look modern anime
        // games use); the terminator is a hard band instead of a gradient.
        vec3 viewDirection = normalize(uCameraPos - worldPosition);
        float shadowTerm = smoothstep(0.35, 0.65, shadow);
        float band = CelBand(ndl);
        float litAmount = band * shadowTerm;

        vec3 base = mix(uShadowTint * 0.75, vec3(1.0), litAmount);
        vec3 color = albedo * base;
        color += uSunColor * uSunIntensity * albedo * litAmount * 0.55;
        color += ambient * 0.62;

        // Quantised specular blob (stylised Blinn-Phong on the lit side).
        vec3 halfVector = normalize(-uSunDirection + viewDirection);
        float nh = max(dot(normal, halfVector), 0.0);
        float spec = smoothstep(0.70, 0.79, nh) * shadowTerm * uSpecularStrength;
        color += uSunColor * spec * 0.55;

        // Rim light: widest where the surface turns away from the camera.
        float facing = clamp(dot(normal, viewDirection), 0.0, 1.0);
        float rim = pow(1.0 - facing, 3.0);
        color += uRimColor * rim * uRimStrength * mix(0.30, 1.0, shadowTerm) * (0.30 + 0.70 * band);

        // Thin warm band just inside the terminator: the coloured shadow edge
        // that makes stylised lighting read as paint rather than plastic.
        float terminator = smoothstep(0.40, 0.56, ndl) * (1.0 - smoothstep(0.56, 0.74, ndl));
        color += vec3(1.0, 0.36, 0.24) * terminator * uWarmEdge * 0.30;
        return color;
    }

    // Slight wrap lighting softens the terminator on vegetation and terrain.
    float wrapped = clamp((ndl + 0.18) / 1.18, 0.0, 1.0);
    vec3 direct = uSunColor * uSunIntensity * wrapped * shadow;
    return albedo * direct + ambient;
}

// Light bleeding through thin surfaces (leaves, grass). Brightest when looking
// toward the sun through the foliage, which is the "glowing backlit leaf" look.
float BacklightTerm(vec3 normal, vec3 worldPosition) {
    vec3 viewDirection = normalize(uCameraPos - worldPosition);
    float through = clamp(dot(viewDirection, -uSunDirection), 0.0, 1.0);
    float forwards = clamp(-dot(normal, viewDirection), 0.0, 1.0);
    return pow(through, 3.0) * (0.35 + 0.65 * forwards);
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
