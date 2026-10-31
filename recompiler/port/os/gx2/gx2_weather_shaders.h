// gx2_weather_shaders.h - GLSL for the plaza sky (gx2_weather.cpp).
//
// Atmosphere after Hillaire, "A Scalable and Production Ready Sky and
// Atmosphere Rendering Technique" (2020): transmittance, multiple-scattering
// and sky-view look-up tables. Clouds: a ray-marched layer with tiling
// Perlin-Worley noise, Beer-Lambert light march, dual-lobe Henyey-Greenstein
// phase and an octave approximation of multiple scattering, stored by
// direction in a dome texture that a few texels per frame refresh.
// Distances in the atmosphere are kilometres.
#pragma once
namespace gx2weather {
inline constexpr const char* kCommon = R"glsl(
#version 450
layout(std140, binding = 0) uniform Params {
    vec4 cam0, cam1, cam2;   // inverse view rows (xyz rotation, w translation), game units
    vec4 proj;               // x/y projection scale, near, far
    vec4 depthRange;         // depth of the near and far planes
    vec4 sun;                // xyz towards the sun, w seconds
    vec4 moon;               // xyz towards the moon, w moonlight illuminance
    vec4 pole;               // xyz celestial pole, w star rotation (radians)
    vec4 exposure;           // x day scale, y adaptation reference, z city glow, w quality 1..3
    vec4 clouds;             // x coverage, y rain, z frame, w dome phase (0..15)
    vec4 wind;               // xy cloud offset (km), z cirrus amount, w sun visibility
    vec4 target;             // xy size, zw 1/size
    vec4 dome;               // xy size, zw 1/size
    vec4 effects;            // x sun rays, y wetness, z rain grid, w lamps
    vec4 cover;              // rain collision cover mapping
    vec4 storm;              // x lightning flash, y bolt seed, zw bolt azimuth (cos, sin)
    vec4 post;               // x FXAA, y film grain, z lens fringes, w frame noise seed
    vec4 sway;               // xy offset (uv), z roll (radians), w zoom
    vec4 look;               // x strength of the realistic look
    vec4 prev0, prev1, prev2; // previous frame's inverse view rows (temporal AA)
    vec4 taa;                // x history valid, y blend
    vec4 hero;               // big cumulus: x density, yz ground position (km), w spatial scale
    vec4 picture;            // x saturation, y contrast, z exposure (stops), w fog
    vec4 extra;              // x broken-cloud ray boost
    vec4 prevProj;           // previous frame's projection scale, near/far
    vec4 cinema;             // x focus blur radius (px), y vignette, z white balance (-1 cool..1 warm)
    vec4 cloudControl;       // x cloud evolution time, y preserve a mission's original sky
    vec4 gradeTools;         // gamma, shadow lift, highlight softness, sharpening
} p;
layout(binding = 1) uniform sampler2D transmittanceLut;
layout(binding = 2) uniform sampler2D multiScatterLut;
layout(binding = 3) uniform sampler2D skyViewLut;      // top half: sun, bottom half: moon
layout(binding = 4) uniform sampler3D shapeNoise;
layout(binding = 5) uniform sampler3D detailNoise;
layout(binding = 6) uniform sampler2D cloudDome;
layout(binding = 7) uniform sampler2D sceneDepth;
layout(binding = 8) uniform sampler2D sceneColor;
layout(binding = 9) uniform sampler2D rainCover;

const float PI = 3.14159265;
const float Rg = 6360.0, Rt = 6460.0;
const vec3 rayleighScattering = vec3(5.802, 13.558, 33.1) * 1e-3;
const float mieScattering = 3.996e-3 * 0.85;    // clear air after rain
const float mieAbsorption = 4.40e-3 * 0.85;
const vec3 ozoneAbsorption = vec3(0.650, 1.881, 0.085) * 1e-3 * 1.6;
const vec3 groundAlbedo = vec3(0.25);
const float viewHeight = 0.08;                  // km above ground
const float cloudBottom = 1.4, cloudTop = 4.2;  // km

float saturate(float x) { return clamp(x, 0.0, 1.0); }
vec3 saturate(vec3 x) { return clamp(x, vec3(0.0), vec3(1.0)); }
float remap(float v, float a, float b, float c, float d) { return c + (v - a) / (b - a) * (d - c); }
float hash13(vec3 v) { v = fract(v * 0.1031); v += dot(v, v.zyx + 31.32); return fract((v.x + v.y) * v.z); }
float hash12(vec2 v) { vec3 q = fract(vec3(v.xyx) * 0.1031); q += dot(q, q.yzx + 33.33); return fract((q.x + q.y) * q.z); }
float luminance(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }

float raySphere(vec3 o, vec3 d, float r) {      // far intersection, -1 if none
    float b = dot(o, d), c = dot(o, o) - r * r, disc = b * b - c;
    if (disc < 0.0) return -1.0;
    return -b + sqrt(disc);
}
float raySphereNear(vec3 o, vec3 d, float r) {  // first positive intersection
    float b = dot(o, d), c = dot(o, o) - r * r, disc = b * b - c;
    if (disc < 0.0) return -1.0;
    float s = sqrt(disc), t0 = -b - s, t1 = -b + s;
    return t0 > 0.0 ? t0 : t1;
}
void medium(float h, out vec3 scatterR, out float scatterM, out vec3 extinction) {
    float dR = exp(-h / 8.0), dM = exp(-h / 1.2), dO = max(0.0, 1.0 - abs(h - 25.0) / 15.0);
    scatterR = rayleighScattering * dR;
    scatterM = mieScattering * dM;
    extinction = scatterR + vec3(scatterM + mieAbsorption * dM) + ozoneAbsorption * dO;
}
float rayleighPhase(float c) { return 3.0 / (16.0 * PI) * (1.0 + c * c); }
float hgPhase(float c, float g) { float g2 = g * g; return (1.0 - g2) / (4.0 * PI * pow(max(1.0 + g2 - 2.0 * g * c, 1e-4), 1.5)); }
float miePhase(float c) {   // Cornette-Shanks
    const float g = 0.8; float g2 = g * g;
    return 3.0 / (8.0 * PI) * (1.0 - g2) * (1.0 + c * c) / ((2.0 + g2) * pow(max(1.0 + g2 - 2.0 * g * c, 1e-4), 1.5));
}

// Transmittance LUT parameterisation (Bruneton).
vec2 transmittanceUv(float r, float mu) {
    float H = sqrt(Rt * Rt - Rg * Rg), rho = sqrt(max(r * r - Rg * Rg, 0.0));
    float disc = r * r * (mu * mu - 1.0) + Rt * Rt;
    float d = max(0.0, -r * mu + sqrt(max(disc, 0.0)));
    float dMin = Rt - r, dMax = rho + H;
    return vec2((d - dMin) / max(dMax - dMin, 1e-4), rho / H);
}
vec3 transmittance(float r, float mu) { return textureLod(transmittanceLut, transmittanceUv(r, mu), 0.0).rgb; }
vec3 multiScatter(float r, float mu) {
    vec2 uv = vec2(mu * 0.5 + 0.5, (r - Rg) / (Rt - Rg));
    return textureLod(multiScatterLut, clamp(uv, vec2(0.5 / 32.0), vec2(1.0 - 0.5 / 32.0)), 0.0).rgb;
}

// Sky-view LUT: azimuth relative to the light, non-linear latitude.
vec2 skyViewUv(vec3 d, vec3 light) {
    float r = Rg + viewHeight;
    float horizon = PI - acos(sqrt(r * r - Rg * Rg) / r);        // zenith angle of the horizon
    float zenith = acos(clamp(d.y, -1.0, 1.0));
    float v;
    if (zenith < horizon) { float c = zenith / horizon; v = 0.5 * (1.0 - sqrt(1.0 - c)); }
    else { float c = (zenith - horizon) / (PI - horizon); v = 0.5 + 0.5 * sqrt(c); }
    vec2 dh = normalize(vec2(d.x, d.z) + vec2(1e-6, 0.0)), lh = normalize(vec2(light.x, light.z) + vec2(1e-6, 0.0));
    float az = acos(clamp(dot(dh, lh), -1.0, 1.0)) / PI;          // 0..1, symmetric about the light
    return vec2(az, v);
}
vec3 skyView(vec3 d, bool moonHalf) {
    vec2 uv = skyViewUv(d, moonHalf ? p.moon.xyz : p.sun.xyz);
    const vec2 size = vec2(192.0, 108.0);
    uv = clamp(uv, 0.5 / size, 1.0 - 0.5 / size);
    uv.y = (uv.y + (moonHalf ? 1.0 : 0.0)) * 0.5;
    return textureLod(skyViewLut, uv, 0.0).rgb;
}

// Cloud dome: azimuth across, sqrt(sin elevation) up.
vec2 domeUv(vec3 d) { return vec2(atan(d.z, d.x) / (2.0 * PI) + 0.5, sqrt(max(d.y, 0.0))); }
vec3 domeDirection(vec2 uv) {
    float phi = (uv.x - 0.5) * 2.0 * PI, s = uv.y * uv.y, c = sqrt(max(1.0 - s * s, 0.0));
    return vec3(c * cos(phi), s, c * sin(phi));
}
vec4 cloudsAt(vec3 d) {
    vec4 c = textureLod(cloudDome, domeUv(d), 0.0);
    float horizon = smoothstep(-0.03, 0.01, d.y);
    return vec4(c.rgb * horizon, mix(1.0, c.a, horizon));
}

// Light reaching the clouds and the camera, before exposure.
vec3 sunIlluminance() { return vec3(1.0) * smoothstep(-0.12, 0.0, p.sun.y); }
vec3 moonIlluminance() { return vec3(0.62, 0.72, 1.0) * p.moon.w * smoothstep(-0.05, 0.05, p.moon.y); }
// Light pollution of a city at night: warm, strongest at the horizon.
vec3 cityGlow(vec3 d) {
    float h = max(d.y, 0.0);
    return p.exposure.z * vec3(1.0, 0.62, 0.36) * (0.25 * exp(-h * 9.0) + 0.04 * exp(-h * 2.0));
}

vec3 eye() { return vec3(p.cam0.w, p.cam1.w, p.cam2.w); }
vec3 worldDirection(vec3 v) { return vec3(dot(p.cam0.xyz, v), dot(p.cam1.xyz, v), dot(p.cam2.xyz, v)); }
vec3 viewDirection(vec2 uv) { return vec3((uv.x * 2.0 - 1.0) / p.proj.x, (1.0 - uv.y * 2.0) / p.proj.y, -1.0); }
float normalizedDepth(float d) { return clamp((d - p.depthRange.x) / (p.depthRange.y - p.depthRange.x), 0.0, 1.0); }
float skyMask(float d) { return step(0.99999, normalizedDepth(d)); }
float viewDistance(float d) { return p.proj.z * p.proj.w / max(p.proj.w - normalizedDepth(d) * (p.proj.w - p.proj.z), 0.001); }
float airOpacity(float distance,float density) { return 1.0-exp(-max(distance-300.0,0.0)*max(density,0.0)); }
float cloudHeightProfile(float height,float rain) {
    return smoothstep(0.0,0.06,height)*(1.0-smoothstep(mix(0.25,0.7,clamp(rain,0.0,1.0)),1.0,height));
}
vec3 wetSurfaceNormal(vec3 surface,vec2 grain,vec2 ripple,float puddle,float footprint) {
    float water=clamp(puddle,0.0,1.0);
    // Unresolved microstructure contributes roughness, not changing pixel normals.
    float filtered=1.0/(1.0+max(footprint,0.0)*11.2);
    vec2 slope=clamp(grain,vec2(-.5),vec2(.5))*.04*filtered*(1.0-water)
              +clamp(ripple,vec2(-3),vec2(3))*.006*water;
    return normalize(surface+vec3(slope.x,0.0,slope.y));
}
float wetReflectionAmount(float wet,float fresnel,float puddle) {
    // Porous wet asphalt remains rough; only standing water becomes a mirror.
    return clamp(wet*fresnel*mix(.065,.95,clamp(puddle,0.0,1.0)),0.0,.6);
}
float cloudScattering(float cosine,float opticalDepth,float extinction) {
    float od=max(opticalDepth,0.0)*max(extinction,0.0);
    // Normalised lobes: the narrow peak lights thin edges, not the entire cloud.
    float direct=.82*hgPhase(cosine,.76)+.12*hgPhase(cosine,.92)+.06*hgPhase(cosine,-.25);
    float scattered=direct*exp(-od),weight=.35,attenuation=.5,anisotropy=.5;
    for(int i=0;i<2;++i){
        float phase=.85*hgPhase(cosine,.76*anisotropy)+.15*hgPhase(cosine,-.25*anisotropy);
        scattered+=weight*phase*exp(-od*attenuation);
        weight*=.35;attenuation*=.5;anisotropy*=.5;
    }
    return scattered;
}
vec3 exposedCloud(vec3 radiance,float exposure) {
    vec3 c=max(radiance*exposure,vec3(0));
    // Preserve colour ratios and structure before the game's highlight clipping.
    return c/(1.0+luminance(c)*.12);
}
float starVisibility(float sunHeight,float skyBrightness) {
    return (1.0-smoothstep(-.20,-.035,sunHeight))*(1.0-saturate(skyBrightness*12.0));
}
vec3 pictureTools(vec3 c,vec4 tools) {
    c=max(c+tools.y*(1.0-smoothstep(.02,.3,luminance(c))),vec3(0));
    float y=luminance(c),soft=.5+max(y-.5,0.0)/(1.0+max(y-.5,0.0)*clamp(tools.z,0.0,1.0)*2.0);
    if(y>.5)c*=soft/y;
    return pow(c,vec3(1.0/clamp(tools.x,.5,1.5)));
}
float sunDiscVisibility(vec2 center,vec2 radius) {
    // shortcut: off-screen occlusion is unknown; enable off-screen lens glare
    // only when a world-space visibility query is available.
    if(any(lessThan(center,vec2(0))) || any(greaterThan(center,vec2(1)))) return 0.0;
    const vec2 offsets[5]=vec2[5](vec2(0),vec2(.6,0),vec2(-.6,0),vec2(0,.6),vec2(0,-.6));
    float visible=0.0;
    for(int i=0;i<5;++i) {
        vec2 uv=center+offsets[i]*radius;
        if(all(greaterThanEqual(uv,vec2(0))) && all(lessThanEqual(uv,vec2(1))))
            visible+=skyMask(textureLod(sceneDepth,uv,0.0).r)*.2;
    }
    return visible;
}
vec3 worldPoint(vec2 uv, float d) { return eye() + worldDirection(viewDirection(uv)) * viewDistance(d); }
vec2 projectDirection(vec3 rd) {
    vec3 v = vec3(dot(vec3(p.cam0.x, p.cam1.x, p.cam2.x), rd), dot(vec3(p.cam0.y, p.cam1.y, p.cam2.y), rd),
                  dot(vec3(p.cam0.z, p.cam1.z, p.cam2.z), rd));
    if (v.z >= -0.001) return vec2(-10.0);
    return vec2(v.x * p.proj.x / (-v.z) * 0.5 + 0.5, 0.5 - v.y * p.proj.y / (-v.z) * 0.5);
}
vec3 linearColor(vec3 c) { return mix(c / 12.92, pow(max((c + 0.055) / 1.055, vec3(0.0)), vec3(2.4)), step(vec3(0.04045), c)); }
vec3 displayColor(vec3 c) { return mix(c * 12.92, 1.055 * pow(max(c, vec3(0.0)), vec3(1.0 / 2.4)) - 0.055, step(vec3(0.0031308), c)); }
)glsl";

// 256x64: optical depth to the top of the atmosphere.
inline constexpr const char* kTransmittanceShader = R"glsl(
layout(local_size_x = 8, local_size_y = 8) in;
layout(binding = 10, rgba16f) uniform writeonly image2D outImage;
void main() {
    ivec2 id = ivec2(gl_GlobalInvocationID.xy), size = imageSize(outImage);
    if (any(greaterThanEqual(id, size))) return;
    vec2 uv = (vec2(id) + 0.5) / vec2(size);
    float H = sqrt(Rt * Rt - Rg * Rg), rho = H * uv.y, r = sqrt(rho * rho + Rg * Rg);
    float dMin = Rt - r, dMax = rho + H, d = dMin + uv.x * (dMax - dMin);
    float mu = d == 0.0 ? 1.0 : clamp((H * H - rho * rho - d * d) / (2.0 * r * d), -1.0, 1.0);
    vec3 o = vec3(0.0, r, 0.0), dir = vec3(sqrt(max(1.0 - mu * mu, 0.0)), mu, 0.0);
    float t = raySphere(o, dir, Rt);
    vec3 depth = vec3(0.0);
    const int N = 40;
    for (int i = 0; i < N; ++i) {
        vec3 q = o + dir * (float(i) + 0.5) / float(N) * t;
        vec3 sR, ext; float sM;
        medium(length(q) - Rg, sR, sM, ext);
        depth += ext * t / float(N);
    }
    imageStore(outImage, id, vec4(exp(-depth), 1.0));
}
)glsl";

// 32x32: second order scattering and its geometric series (Hillaire 5.5.2).
inline constexpr const char* kMultiScatterShader = R"glsl(
layout(local_size_x = 8, local_size_y = 8) in;
layout(binding = 10, rgba16f) uniform writeonly image2D outImage;
void main() {
    ivec2 id = ivec2(gl_GlobalInvocationID.xy), size = imageSize(outImage);
    if (any(greaterThanEqual(id, size))) return;
    vec2 uv = (vec2(id) + 0.5) / vec2(size);
    float muS = uv.x * 2.0 - 1.0, r = Rg + max(uv.y, 0.002) * (Rt - Rg);
    vec3 sunDir = vec3(sqrt(max(1.0 - muS * muS, 0.0)), muS, 0.0), o = vec3(0.0, r, 0.0);
    vec3 lum = vec3(0.0), fms = vec3(0.0);
    const int SQRT = 8, STEPS = 20;
    for (int i = 0; i < SQRT; ++i) for (int j = 0; j < SQRT; ++j) {
        float theta = 2.0 * PI * (float(i) + 0.5) / float(SQRT);
        float cosPhi = 1.0 - 2.0 * (float(j) + 0.5) / float(SQRT), sinPhi = sqrt(max(1.0 - cosPhi * cosPhi, 0.0));
        vec3 dir = vec3(cos(theta) * sinPhi, cosPhi, sin(theta) * sinPhi);
        float tG = raySphereNear(o, dir, Rg), tT = raySphere(o, dir, Rt);
        bool ground = tG > 0.0;
        float tMax = ground ? tG : tT, dt = tMax / float(STEPS);
        vec3 throughput = vec3(1.0), L = vec3(0.0), F = vec3(0.0);
        for (int s = 0; s < STEPS; ++s) {
            vec3 q = o + dir * (float(s) + 0.5) * dt;
            float h = length(q);
            vec3 sR, ext; float sM;
            medium(h - Rg, sR, sM, ext);
            vec3 S = sR + vec3(sM), stepT = exp(-ext * dt);
            vec3 up = q / h;
            float mu = dot(up, sunDir);
            vec3 sunT = transmittance(h, mu) * step(raySphereNear(q, sunDir, Rg), 0.0);
            vec3 inv = 1.0 / max(ext, vec3(1e-6));
            L += throughput * (S * sunT / (4.0 * PI)) * (1.0 - stepT) * inv;
            F += throughput * S * (1.0 - stepT) * inv;
            throughput *= stepT;
        }
        if (ground) {
            vec3 q = o + dir * tG, up = normalize(q);
            L += throughput * transmittance(Rg, dot(up, sunDir)) * saturate(dot(up, sunDir)) * groundAlbedo / PI;
        }
        lum += L / float(SQRT * SQRT);
        fms += F / float(SQRT * SQRT);
    }
    imageStore(outImage, id, vec4(lum / max(1.0 - fms, vec3(1e-3)), 1.0));
}
)glsl";

// 192x216: single + multiple scattering seen from the plaza, lit by the sun
// (rows 0..107) and by the moon (rows 108..215), for unit illuminance.
inline constexpr const char* kSkyViewShader = R"glsl(
layout(local_size_x = 8, local_size_y = 8) in;
layout(binding = 10, rgba16f) uniform writeonly image2D outImage;
void main() {
    ivec2 id = ivec2(gl_GlobalInvocationID.xy);
    if (id.x >= 192 || id.y >= 216) return;
    bool moonHalf = id.y >= 108;
    vec2 uv = (vec2(id.x, id.y % 108) + 0.5) / vec2(192.0, 108.0);
    vec3 light = moonHalf ? p.moon.xyz : p.sun.xyz;
    float r = Rg + viewHeight, horizon = PI - acos(sqrt(r * r - Rg * Rg) / r), zenith;
    if (uv.y < 0.5) { float c = 1.0 - 2.0 * uv.y; zenith = horizon * (1.0 - c * c); }
    else { float c = 2.0 * uv.y - 1.0; zenith = horizon + (PI - horizon) * c * c; }
    vec2 lh = normalize(vec2(light.x, light.z) + vec2(1e-6, 0.0));
    float az = uv.x * PI, ca = cos(az), sa = sin(az);
    vec2 dh = vec2(lh.x * ca - lh.y * sa, lh.x * sa + lh.y * ca);
    vec3 dir = vec3(dh.x * sin(zenith), cos(zenith), dh.y * sin(zenith));
    vec3 o = vec3(0.0, r, 0.0);
    float tG = raySphereNear(o, dir, Rg), tMax = tG > 0.0 ? tG : raySphere(o, dir, Rt);
    tMax = min(tMax, 400.0);
    float c = dot(dir, light), phR = rayleighPhase(c), phM = miePhase(c);
    vec3 L = vec3(0.0), throughput = vec3(1.0);
    const int N = 32;
    float tPrev = 0.0;
    for (int i = 0; i < N; ++i) {
        float t = tMax * pow((float(i) + 0.5) / float(N), 1.6), dt = t - tPrev; tPrev = t;
        vec3 q = o + dir * t;
        float h = length(q);
        vec3 sR, ext; float sM;
        medium(h - Rg, sR, sM, ext);
        vec3 up = q / h;
        float mu = dot(up, light);
        vec3 lightT = transmittance(h, mu) * step(raySphereNear(q, light, Rg), 0.0);
        vec3 S = sR * phR + vec3(sM * phM);
        vec3 inscatter = lightT * S + multiScatter(h, mu) * (sR + vec3(sM));
        vec3 stepT = exp(-ext * dt);
        L += throughput * inscatter * (1.0 - stepT) / max(ext, vec3(1e-6));
        throughput *= stepT;
    }
    imageStore(outImage, id, vec4(L, 1.0));
}
)glsl";

// Tiling noise volumes: shape (128^3: Perlin-Worley + Worley octaves) and
// detail (32^3: Worley octaves). effects.x = 0 shape, 1 detail.
inline constexpr const char* kNoise = R"glsl(
layout(local_size_x = 4, local_size_y = 4, local_size_z = 4) in;
layout(binding = 11, rgba8) uniform writeonly image3D outVolume;
vec3 hash33(vec3 q) {
    q = vec3(dot(q, vec3(127.1, 311.7, 74.7)), dot(q, vec3(269.5, 183.3, 246.1)), dot(q, vec3(113.5, 271.9, 124.6)));
    return fract(sin(q) * 43758.5453123);
}
float worley(vec3 x, float period) {
    vec3 cell = floor(x), f = fract(x);
    float best = 1e9;
    for (int k = -1; k <= 1; ++k) for (int j = -1; j <= 1; ++j) for (int i = -1; i <= 1; ++i) {
        vec3 o = vec3(i, j, k), c = mod(cell + o, period);
        vec3 d = o + hash33(c) - f;
        best = min(best, dot(d, d));
    }
    return 1.0 - sqrt(best);          // 1 at feature points
}
float worleyFbm(vec3 x, float period) {
    return worley(x * period, period) * 0.625 + worley(x * period * 2.0, period * 2.0) * 0.25 +
           worley(x * period * 4.0, period * 4.0) * 0.125;
}
float gradientNoise(vec3 x, float period) {
    vec3 i = floor(x), f = fract(x), u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
    float n[8];
    for (int k = 0; k < 8; ++k) {
        vec3 o = vec3(k & 1, (k >> 1) & 1, (k >> 2) & 1);
        vec3 g = normalize(hash33(mod(i + o, period)) * 2.0 - 1.0);
        n[k] = dot(g, f - o);
    }
    return mix(mix(mix(n[0], n[1], u.x), mix(n[2], n[3], u.x), u.y), mix(mix(n[4], n[5], u.x), mix(n[6], n[7], u.x), u.y), u.z);
}
float perlinFbm(vec3 x, float period) {
    float sum = 0.0, amp = 0.5, total = 0.0;
    for (int o = 0; o < 6; ++o) { sum += gradientNoise(x * period, period) * amp; total += amp; amp *= 0.5; period *= 2.0; }
    return sum / total * 0.5 + 0.5;
}
void main() {
    ivec3 id = ivec3(gl_GlobalInvocationID), size = imageSize(outVolume);
    if (any(greaterThanEqual(id, size))) return;
    vec3 x = (vec3(id) + 0.5) / vec3(size);
    vec4 v;
    if (p.effects.x < 0.5) {
        float perlin = perlinFbm(x, 4.0);
        float w = worleyFbm(x, 4.0);
        float perlinWorley = saturate(remap(perlin, w - 1.0, 1.0, 0.0, 1.0));
        v = vec4(perlinWorley, worleyFbm(x, 4.0), worleyFbm(x, 8.0), worleyFbm(x, 16.0));
    } else {
        v = vec4(worleyFbm(x, 2.0), worleyFbm(x, 4.0), worleyFbm(x, 8.0), 1.0);
    }
    imageStore(outVolume, id, v);
}
)glsl";

// One phase of the 4x4 interleaved dome refresh per frame.
// Cloud density shared by the dome and the volumetric light shafts.
inline constexpr const char* kCloudDensity = R"glsl(
float weatherAt(vec2 xz) {
    // Weather: fields of cloud drifting with the wind, 0..1.
    vec2 w = xz * 0.021 + p.wind.xy * 0.021;
    return textureLod(shapeNoise, vec3(w, 0.31), 0.0).r * 0.65 + textureLod(shapeNoise, vec3(w * 2.7 + 0.43, 0.77), 0.0).g * 0.35;
}
float density(vec3 q, float hf, bool detail) {
    float cover = p.clouds.x;
    if (cover <= 0.001) return 0.0;
    // Threshold on the shape noise: clear skies keep only the tops of the
    // noise (separate cumulus), overcast keeps nearly all of it.
    // Overcast keeps its structure: thicker and thinner patches, not a
    // uniform blanket.
    float field = weatherAt(q.xz);
    // Below ~85 % the clouds keep gaps (broken cumulus: rays come through);
    // only near full cover and in rain does the layer close.
    float closing = smoothstep(0.75, 1.0, cover) + p.clouds.y;
    float threshold = 1.0 - cover * mix(0.46, 0.62, saturate(closing)) - (field - 0.5) * (0.42 + 0.35 * cover);
    vec3 w = vec3(p.wind.x, 0.0, p.wind.y);
    vec3 evolve = vec3(0.0, p.cloudControl.x * 0.0009, 0.0);
    vec4 n = textureLod(shapeNoise, (q + w * 1.4) * 0.13 + evolve, 0.0);
    float lowFbm = n.g * 0.625 + n.b * 0.25 + n.a * 0.125;
    float base = remap(n.r, lowFbm - 1.0, 1.0, 0.0, 1.0);
    // Flat bases, rounded towers thinning to the top; rain clouds fill the layer.
    float rain = p.clouds.y;
    float profile = cloudHeightProfile(hf,rain);
    base = (base * profile - threshold) / max(1.0 - threshold, 0.05);
    if (base <= 0.0) return 0.0;
    if (detail) {
        vec3 dn = textureLod(detailNoise, (q + w * 2.0) * 1.3, 0.0).rgb;
        float highFbm = dn.r * 0.625 + dn.g * 0.25 + dn.b * 0.125;
        // A finer octave warped by the first: billowing cauliflower edges
        // instead of smooth blobs.
        float fine = textureLod(detailNoise, (q + w * 2.0) * 3.9 + (dn - 0.5) * 0.35, 0.0).r;
        highFbm = highFbm * 0.72 + fine * 0.28;
        float erode = mix(highFbm, 1.0 - highFbm, saturate(hf * 6.0));   // wispy bottoms, billowy tops
        base = remap(base, erode * 0.62, 1.0, 0.0, 1.0);
    }
    return saturate(base * 2.0) * (0.55 + 0.45 * cover) * mix(1.0, 0.55 + 0.9 * field, cover) * (1.0 + 2.2 * rain);   // crisp edges; rain clouds dense and dark
}
// The hero cloud: one towering cumulus congestus (flat base at 1.5 km,
// cauliflower towers to ~8 km) built from stacked lobes and eroded by the
// noise volumes, so its edges billow like a real one.
vec2 g_heroCenter = vec2(0.0);                    // the cloud being marched
float g_heroAmount = 0.0, g_heroTurn = 0.0;
float heroDensity(vec3 q) {                       // q = (x km, height km, z km)
    float scale=max(p.hero.w,.01);
    vec2 d2 = (q.xz - g_heroCenter)/scale;
    d2 = vec2(cos(g_heroTurn) * d2.x - sin(g_heroTurn) * d2.y, sin(g_heroTurn) * d2.x + cos(g_heroTurn) * d2.y);   // each cloud its own shape
    float h = q.y/scale;
    if (h < 1.45 || h > 9.6 || dot(d2, d2) > 42.0) return 0.0;
    // Wide base, staggered towers narrowing upwards, a crown of small domes.
    const vec4 lobes[22] = vec4[22](              // x, z offset, height, radius (km)
        vec4(0.0, 0.0, 2.4, 3.2), vec4(2.6, 0.8, 2.3, 2.5), vec4(-2.7, -0.5, 2.2, 2.4), vec4(0.8, -2.3, 2.3, 2.3),
        vec4(-0.7, 2.2, 2.3, 2.2), vec4(3.6, -1.2, 2.1, 1.8), vec4(-3.4, 1.6, 2.0, 1.7),
        vec4(-2.0,-.2,4.1,2.3),vec4(2.6,.3,4.4,2.4),vec4(0,1,4,2.2),vec4(-3.6,1.2,3.5,1.7),vec4(3.5,-1,3.8,1.9),
        vec4(-2.4,-.4,5.7,1.7),vec4(2,.3,6.1,2),vec4(.3,.5,5.4,1.7),vec4(3,1.1,5.4,1.6),vec4(-1.2,1.3,6.2,1.5),
        vec4(1.5,.4,7,1.4),vec4(2.6,.5,6.9,1.05),vec4(-2.7,-.3,6.7,1.1),vec4(-1.3,.8,7.1,1.1),vec4(.2,1,6.9,1.2));
    vec3 r0 = vec3(d2.x, h, d2.y);
    vec3 w=vec3(p.wind.x,0.0,p.wind.y)*.25+vec3(0,p.cloudControl.x*.0006,0);
    // Large turbulent billows break the sphere scaffold before finer erosion.
    vec3 macroWarp=textureLod(detailNoise,(r0+w)*.075+vec3(.13,.47,.71),0).rgb-.5;
    vec3 billow=r0+macroWarp*1.15;
    float f = 0.0;
    for (int i = 0; i < 22; ++i) {
        vec3 c = vec3(lobes[i].x, lobes[i].z, lobes[i].y);
        f = max(f, 1.0 - length(billow - c) / lobes[i].w);
    }
    f *= smoothstep(1.45, 1.7, h);                // flat base
    float n = textureLod(shapeNoise, (r0 + w) * 0.30, 0.0).r;
    vec3 detailPos=(r0+w)*1.4;
    vec3 warp=textureLod(detailNoise,detailPos*.37+vec3(.17,.43,.71),0.0).rgb-.5;
    vec3 dn = textureLod(detailNoise,detailPos+warp*.24,0.0).rgb;
    float fine=textureLod(detailNoise,detailPos*3.7+vec3(.31,.67,.13),0.0).b;
    // Billows: the noise eats the lobes' edges unevenly (cauliflower).
    float erosion=(1.0-dot(dn,vec3(.625,.25,.125)))*.18+(1.0-fine)*.065;
    float shape = f - (1.0 - n) * .24 - erosion * saturate(f * 4.0);
    return smoothstep(.015,.19,shape)*g_heroAmount;
}
)glsl";

inline constexpr const char* kCloudDome = R"glsl(
layout(local_size_x = 8, local_size_y = 8) in;
layout(binding = 12, rgba16f) uniform image2D domeImage;
void main() {
    ivec2 size = imageSize(domeImage);
    int phase = int(p.clouds.w), grid = int(p.dome.w);
    ivec2 texel = ivec2(gl_GlobalInvocationID.xy) * grid + ivec2(phase % grid, phase / grid);
    if (any(greaterThanEqual(texel, size))) return;
    float jitter = hash13(vec3(texel,17.3));
    vec2 uv = (vec2(texel)+.5)/vec2(size);
    vec3 d = domeDirection(uv);
    d.y = max(d.y, 0.0015); d = normalize(d);
    vec3 o = vec3(0.0, Rg + viewHeight, 0.0);
    float t0 = raySphere(o, d, Rg + cloudBottom), t1 = min(raySphere(o, d, Rg + cloudTop), 90.0);
    vec3 scattered = vec3(0.0);
    float T = 1.0;
    if (t1 > t0) {
        int quality = int(p.exposure.w);
        int steps = quality <= 1 ? 32 : quality == 2 ? 64 : 128;
        int lightSteps = quality <= 1 ? 4 : quality == 2 ? 6 : 10;
        float dt = (t1 - t0) / float(steps);
        // Light: the sun by day, the moon by night (whichever is brighter).
        vec3 sunE = sunIlluminance(), moonE = moonIlluminance();
        bool useMoon = luminance(moonE) > luminance(sunE);
        vec3 L = useMoon ? p.moon.xyz : p.sun.xyz;
        float cosTheta = dot(d, L);
        vec3 skyTop = (skyView(vec3(0.0, 1.0, 0.0), false) * sunE + skyView(vec3(0.0, 1.0, 0.0), true) * moonE) * PI;
        vec3 skySide = (skyView(normalize(vec3(L.x, 0.25, L.z)), false) * sunE + skyView(normalize(vec3(L.x, 0.25, L.z)), true) * moonE) * PI;
        const float sigma = 38.0;                  // extinction per km at density 1
        for (int i = 0; i < steps && T > 0.01; ++i) {
            float t = t0 + (float(i) + jitter) * dt;
            vec3 q = o + d * t;
            float h = length(q) - Rg, hf = saturate((h - cloudBottom) / (cloudTop - cloudBottom));
            vec3 qw = vec3(q.x, h, q.z);
            float rho = density(qw, hf, true);
            if (rho <= 0.0) continue;
            // Light march towards the light, growing steps.
            float od = 0.0, ls = 0.04, lt = 0.0;
            for (int k = 0; k < lightSteps; ++k) {
                lt += ls;
                vec3 lq = q + L * lt;
                float lh = length(lq) - Rg;
                if (lh > cloudTop) break;
                od += density(vec3(lq.x, lh, lq.z), saturate((lh - cloudBottom) / (cloudTop - cloudBottom)), false) * ls;
                ls *= 1.7;
            }
            vec3 up = q / length(q);
            vec3 lightT = transmittance(length(q), dot(up, L));
            vec3 lightE = (useMoon ? moonE : sunE) * lightT;
            float ms=cloudScattering(cosTheta,od,sigma);
            float powder = 1.0 - 0.6 * exp(-sigma * rho * 0.35);
            vec3 ambient = mix(skySide*.5,skyTop,hf)*mix(.4,.9,hf);
            ambient += cityGlow(vec3(0.0)) * 0.5 * (1.0 - hf);  // street light from below
            ambient += sunE * groundAlbedo * saturate(p.sun.y) * transmittance(Rg, p.sun.y) * 0.25 * (1.0 - hf);
            vec3 S = sigma * rho * (lightE * ms * powder + ambient / (4.0 * PI));
            float stepT = exp(-sigma * rho * dt);
            scattered += T * S * (1.0 - stepT) / (sigma * rho);
            T *= stepT;
        }
        // Distant clouds fade into the haze in front of them.
        float haze = exp(-max(t0 - 8.0, 0.0) / 14.0);   // far clouds melt into the horizon haze
        scattered *= haze;
        T = mix(1.0, T, haze);
    }
    // The hero clouds behind the layer (one behind the tower, one across).
    for (int heroIndex = 0; heroIndex < 2; ++heroIndex) {
    g_heroCenter = heroIndex == 0 ? p.hero.yz : p.extra.yz;
    g_heroAmount = heroIndex == 0 ? p.hero.x : p.extra.w;
    g_heroTurn = heroIndex == 0 ? 0.0 : 2.4;
    if (g_heroAmount > 0.0 && T > 0.01) {
        vec3 hc = vec3(g_heroCenter.x, Rg + 4.5*p.hero.w, g_heroCenter.y);
        vec3 oc = o - hc;
        float b = dot(oc, d), cc = dot(oc, oc) - 64.0*p.hero.w*p.hero.w, disc = b * b - cc;
        if (disc > 0.0) {
            float sq = sqrt(disc), ta = max(-b - sq, 0.0), tb = -b + sq;
            if (tb > ta) {
                vec3 sunE = sunIlluminance(), moonE = moonIlluminance();
                bool useMoon = luminance(moonE) > luminance(sunE);
                vec3 L = useMoon ? p.moon.xyz : p.sun.xyz;
                float cosTheta = dot(d, L);
                vec3 skyTop = (skyView(vec3(0.0, 1.0, 0.0), false) * sunE + skyView(vec3(0.0, 1.0, 0.0), true) * moonE) * PI;
                vec3 skySide = (skyView(normalize(vec3(L.x, 0.25, L.z)), false) * sunE + skyView(normalize(vec3(L.x, 0.25, L.z)), true) * moonE) * PI;
                const float sigma = 38.0;
                int quality=int(p.exposure.w);
                int steps = quality <= 1 ? 48 : quality == 2 ? 96 : 192;
                float dt = (tb - ta) / float(steps);
                for (int i = 0; i < steps && T > 0.01; ++i) {
                    float t = ta + (float(i) + jitter) * dt;
                    vec3 q = o + d * t;
                    float h = length(q) - Rg;
                    float rho = heroDensity(vec3(q.x, h, q.z));
                    if (rho <= 0.0) continue;
                    float od = 0.0, ls = 0.035, lt = 0.0;
                    for (int k = 0; k < (quality>=3?10:6); ++k) {
                        lt += ls;
                        vec3 lq = q + L * lt;
                        od += heroDensity(vec3(lq.x, length(lq) - Rg, lq.z)) * ls;
                        ls *= 1.7;
                    }
                    vec3 up = q / length(q);
                    vec3 lightE = (useMoon ? moonE : sunE) * transmittance(length(q), dot(up, L));
                    float ms=cloudScattering(cosTheta,od,sigma);
                    float hf = saturate((h/p.hero.w - 1.5) / 7.0);
                    float powder = 1.0 - 0.6 * exp(-sigma * rho * 0.35);
                    vec3 ambient = mix(skySide*.4,skyTop*.85,hf)*mix(.35,1.0,hf);
                    ambient += sunE * groundAlbedo * saturate(p.sun.y) * transmittance(Rg, p.sun.y) * 0.3 * (1.0 - hf);
                    vec3 S = sigma * rho * (lightE * ms * powder + ambient / (4.0 * PI));
                    float stepT = exp(-sigma * rho * dt);
                    scattered += T * S * (1.0 - stepT) / (sigma * rho);
                    T *= stepT;
                }
            }
        }
    }
    }
    vec4 result = vec4(scattered, T);
    if (p.clouds.z >= p.dome.w * p.dome.w) result = mix(imageLoad(domeImage, texel), result, 0.6);
    imageStore(domeImage, texel, result);
}
)glsl";

// The sky in place of the game's sky dome (scene colour, linear HDR).
inline constexpr const char* kSkyVertex = R"glsl(
layout(location = 0) out vec2 uv;
void main() {
    uv = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(uv * 2.0 - 1.0, 1.0, 1.0);
}
)glsl";
inline constexpr const char* kSkyFragment = R"glsl(
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 outColor;
vec3 rotateAbout(vec3 v, vec3 axis, float a) {
    float c = cos(a), s = sin(a);
    return v * c + cross(axis, v) * s + axis * dot(axis, v) * (1.0 - c);
}
vec3 stars(vec3 d, float pixel) {
    vec3 q = rotateAbout(d, p.pole.xyz, -p.pole.w);
    // Sparse stars with a steep magnitude distribution, crisp points.
    const float scale = 260.0;
    vec3 g = q * scale, cell = floor(g), f = fract(g);
    vec3 result = vec3(0.0);
    float h = hash13(cell);
    if (h > 0.955) {
        vec3 center = vec3(hash13(cell + 1.7), hash13(cell + 4.1), hash13(cell + 8.3)) * 0.7 + 0.15;
        vec3 dv = f - center; dv -= q * dot(dv, q);
        float dist = length(dv) / scale;
        float sigma = max(pixel * 0.5, 0.0002);
        float mag = pow(hash13(cell + 12.9), 22.0) * 9.0 + 0.04;
        float twinkle = 0.85 + 0.15 * sin(p.sun.w * (2.0 + 5.0 * hash13(cell + 3.3)) + h * 50.0);
        vec3 tint = mix(vec3(1.0, 0.82, 0.66), vec3(0.78, 0.86, 1.0), hash13(cell + 6.6));
        result += tint * mag * twinkle * exp(-dist * dist / (2.0 * sigma * sigma));
    }
    // A faint Milky Way along a tilted great circle.
    vec3 galaxyPole = normalize(vec3(0.35, 0.45, 0.82));
    float band = exp(-pow(dot(q, galaxyPole) * 4.5, 2.0));
    float dust = textureLod(detailNoise, q * 0.9 + 0.5, 0.0).r;
    result += vec3(0.55, 0.6, 0.8) * band * smoothstep(0.35, 0.8, dust) * 0.012;
    return result;
}
vec3 moonDisc(vec3 d, float pixel) {
    const float radius = 0.0046;
    vec3 m = p.moon.xyz;
    float dist = acos(clamp(dot(d, m), -1.0, 1.0));
    float visible = smoothstep(-0.03, 0.02, m.y);
    vec3 halo = vec3(0.75, 0.82, 1.0) * (exp(-dist * 70.0) * 0.06 + exp(-dist * 14.0) * 0.012) * visible;
    if (dist > radius * 1.5) return halo;
    vec3 tx = normalize(cross(m, vec3(0.0, 1.0, 0.0))), ty = cross(tx, m);
    vec2 xy = vec2(dot(d - m, tx), dot(d - m, ty)) / radius;
    float rr = dot(xy, xy);
    float edge = 1.0 - smoothstep(1.0 - pixel / radius * 1.5, 1.0, sqrt(rr));
    vec3 n = normalize(xy.x * tx + xy.y * ty + sqrt(max(1.0 - rr, 0.0)) * m);
    float lit = saturate(dot(n, normalize(p.sun.xyz - m * 0.02)) * 1.4 + 0.03);
    float maria = 0.68 + 0.32 * smoothstep(0.3, 0.7, textureLod(detailNoise, vec3(xy * 0.22 + 0.5, 0.4), 0.0).r);
    return vec3(1.0, 0.97, 0.92) * lit * maria * edge * .65 * visible + halo*.35;
}
void main() {
    vec2 st = gl_FragCoord.xy * p.target.zw;
    vec3 d = normalize(worldDirection(viewDirection(st)));
    // Below the horizon (high places: Octo Valley's islands, a high free
    // camera) the sky continues as horizon haze instead of the dark ground
    // side of the atmosphere model.
    vec3 dSky = d;
    if (dSky.y < 0.004) { dSky.y = 0.004 + (-d.y) * 0.04; dSky = normalize(dSky); }
    float pixel = 2.0 / (p.proj.y * p.target.y);           // radians per pixel
    vec3 sunE = sunIlluminance(), moonE = moonIlluminance();
    float r = Rg + viewHeight;
    vec3 sky = skyView(dSky, false) * sunE + skyView(dSky, true) * moonE;
    // Exposure: the eye adapts to dusk.
    float zenith = luminance(skyView(vec3(0.0, 1.0, 0.0), false) * sunE);
    float adapt = clamp(pow(p.exposure.y / max(zenith, 1e-7), 0.42), 1.0, 40.0);
    float scale = p.exposure.x * adapt;
    // Sun disc with limb darkening.
    vec3 celestial = vec3(0.0);
    float cs = dot(d, p.sun.xyz), sunRadius = 0.0047;
    float sd = acos(clamp(cs, -1.0, 1.0));
    if (sd < sunRadius * 1.6 && d.y > -0.02) {
        float x = saturate(sd / sunRadius);
        float limb = 1.0 - 0.6 * (1.0 - sqrt(max(1.0 - x * x, 0.0)));
        float disc = 1.0 - smoothstep(1.0 - pixel / sunRadius, 1.0 + pixel / sunRadius, x);
        celestial += transmittance(r, d.y) * limb * disc * 60.0 / scale * sunE * p.wind.w;
    }
    // The atmosphere LUT already contains circumsolar scattering; lens glare
    // is added after depth occlusion, rather than painted across the sky twice.
    float night = starVisibility(p.sun.y,luminance(sky)*scale);
    vec3 nightSky = vec3(0.0026, 0.0038, 0.0085) + cityGlow(d) * 0.35;
    vec3 backdrop = sky + celestial + (moonDisc(d, pixel) + stars(d, pixel) * night * smoothstep(0.03, 0.35, d.y)
                    * transmittance(r, max(d.y, 0.02)) + nightSky) / scale;
    // High cirrus veil at 9 km, lit from the light's side.
    if (d.y > 0.0 && p.wind.z > 0.0) {
        vec2 c = d.xz / (d.y + 0.08) * 0.35 + p.wind.xy * 0.004;
        float n = textureLod(detailNoise, vec3(c * vec2(0.6, 0.25), 0.15), 0.0).r * 0.6
                + textureLod(detailNoise, vec3(c * vec2(1.7, 0.6) + 0.3, 0.55), 0.0).g * 0.4;
        float veil = saturate(remap(n, 0.55, 0.95, 0.0, 1.0)) * p.wind.z * smoothstep(0.0, 0.2, d.y);
        vec3 lightCol = sunE * transmittance(Rg + 9.0, p.sun.y) * hgPhase(dot(d, p.sun.xyz), 0.6) * 1.6
                      + skyView(vec3(0.0, 1.0, 0.0), false) * sunE * 0.8 + moonE * 0.02;
        backdrop = backdrop * (1.0 - veil * 0.35) + lightCol * veil * 0.5;
    }
    vec4 cl = cloudsAt(dSky);
    vec3 color = backdrop*cl.a*scale+exposedCloud(cl.rgb,scale);
    // The atmosphere and the scene share exposure; an extra dusk gain clips cloud detail.
    float lowSky = saturate(1.0 - p.sun.y * 4.5) * smoothstep(-0.1, 0.02, p.sun.y);
    // Lightning: the bolt below the clouds and the clouds lit from inside.
    if (p.storm.x > 0.001) {
        vec2 boltDir = p.storm.zw;
        vec2 dh = normalize(d.xz + vec2(1e-5, 0.0));
        float az = atan(dh.x * boltDir.y - dh.y * boltDir.x, dot(dh, boltDir));   // radians from the bolt
        float el = asin(clamp(d.y, -1.0, 1.0));
        float top = 0.20, h = saturate(el / top);
        float path = 0.0, amp = 0.012;
        for (int o = 0; o < 4; ++o) {
            path += (hash12(vec2(floor(h * 6.0 * exp2(float(o))), p.storm.y + float(o))) - 0.5) * amp;
            amp *= 0.55;
        }
        float dist = abs(az - path) * cos(el);
        float core = exp(-dist * dist / (2.0 * pixel * pixel * 1.5)) * step(0.0, el) * step(el, top);
        float glow = exp(-dist * 40.0) * 0.15 * step(el, top + 0.05);
        vec3 boltColor = vec3(0.82, 0.86, 1.0);
        float inCloud = exp(-abs(az) * 2.0) * (1.0 - cl.a);
        color += boltColor * p.storm.x * (core * 30.0 + glow * 3.0 + inCloud * 2.5 + (1.0 - cl.a) * 0.6);
    }
    // Atmospheric transmittance supplies the sunset colour without an orange overlay.
    // Photographic sky: a little more saturation than the physical model.
    color = max(mix(vec3(luminance(color)), color, mix(1.0, 0.9, lowSky)), vec3(0.0));
    outColor = vec4(max(color, vec3(0.0)), 1.0);
}
)glsl";

// Half resolution: light shafts from the visible sun through the scene's
// silhouettes and the clouds (radial march in screen space).
inline constexpr const char* kRays = R"glsl(
layout(local_size_x = 8, local_size_y = 8) in;
layout(binding = 10, rgba16f) uniform writeonly image2D outImage;
void main() {
    ivec2 id = ivec2(gl_GlobalInvocationID.xy), size = imageSize(outImage);
    if (any(greaterThanEqual(id, size))) return;
    vec2 uv = (vec2(id) + 0.5) / vec2(size);
    vec2 sunUv = projectDirection(p.sun.xyz);
    float rays = 0.0;
    if (p.sun.y > -0.02 && sunUv.x > -0.6 && sunUv.x < 1.6 && sunUv.y > -0.6 && sunUv.y < 1.6) {
        int steps = int(p.exposure.w) <= 1 ? 24 : int(p.exposure.w) == 2 ? 40 : 56;
        vec2 delta = (sunUv - uv) / float(steps);
        float jitter = hash12(vec2(id) + p.clouds.z * 1.37);
        vec2 q = uv + delta * jitter;
        float weight = 1.0, total = 0.0;
        for (int i = 0; i < steps; ++i) {
            if (all(greaterThanEqual(q, vec2(0.0))) && all(lessThanEqual(q, vec2(1.0)))) {
                float sky = skyMask(textureLod(sceneDepth, q, 0.0).r);
                if (sky > 0.0) {
                    vec3 d = normalize(worldDirection(viewDirection(q)));
                    sky *= cloudsAt(d).a;
                }
                rays += sky * weight;
            }
            total += weight;
            weight *= 0.965;
            q += delta;
        }
        rays /= total;
    }
    // Volumetric light shafts: march the air along the view ray; at each
    // point the clouds (layer and big cumulus) either let the sun through or
    // shade it, so lit and shadowed beams fan out from the cloud gaps like
    // real crepuscular rays, also with the sun off screen.
    float lit = 1.0, air = 0.0;
    if (p.effects.x > 0.0 && p.sun.y > 0.03 && p.clouds.x > 0.03) {
        float depth = textureLod(sceneDepth, uv, 0.0).r;
        bool skyPixel = skyMask(depth) > 0.5;
        vec3 vd = normalize(worldDirection(viewDirection(uv)));
        float reach = skyPixel ? 14.0 : min(viewDistance(depth) * 0.0001, 14.0);   // km (1 game unit ~ 0.1 m)
        if (vd.y > 0.0) reach = min(reach, (cloudBottom - viewHeight) / vd.y);       // up to the cloud base
        if (reach > 0.05) {
            const int steps = 14;
            float jitter = hash12(vec2(id) * 0.73 + p.clouds.z * 2.11);
            float sum = 0.0, total = 0.0, prev = 0.0;
            vec3 L = p.sun.xyz;
            for (int i = 0; i < steps; ++i) {
                float tt = reach * pow((float(i) + jitter) / float(steps), 1.6);
                float dt = tt - prev; prev = tt;
                vec3 q = vec3(vd.x * tt, viewHeight + vd.y * tt, vd.z * tt);
                // Towards the sun through the cloud layer (5 samples).
                float t0 = max((cloudBottom - q.y) / L.y, 0.0), t1 = (cloudTop - q.y) / L.y;
                float od = 0.0, sdt = (t1 - t0) / 5.0;
                for (int k = 0; k < 5; ++k) {
                    vec3 sq = q + L * (t0 + (float(k) + 0.5) * sdt);
                    od += density(sq, saturate((sq.y - cloudBottom) / (cloudTop - cloudBottom)), false) * sdt;
                }
                // The big cumulus clouds shade too (cheap: only near them).
                for (int hI = 0; hI < 2; ++hI) {
                    g_heroCenter = hI == 0 ? p.hero.yz : p.extra.yz;
                    g_heroAmount = hI == 0 ? p.hero.x : p.extra.w;
                    g_heroTurn = hI == 0 ? 0.0 : 2.4;
                    if (g_heroAmount <= 0.0) continue;
                    for (int k = 0; k < 6; ++k) {
                        float hh = (1.6 + float(k) * 1.3) * p.hero.w;
                        if (hh <= q.y) continue;
                        vec3 sq = q + L * ((hh - q.y) / L.y);
                        if (dot(sq.xz - g_heroCenter, sq.xz - g_heroCenter) < 42.0*p.hero.w*p.hero.w) od += heroDensity(sq) * 1.3*p.hero.w;
                    }
                }
                float vis = exp(-38.0 * 0.35 * od);
                sum += vis * dt; total += dt;
            }
            lit = sum / max(total, 1e-4);
            air = saturate(total / 14.0);
        }
    }
    vec3 sunView=vec3(dot(vec3(p.cam0.x,p.cam1.x,p.cam2.x),p.sun.xyz),dot(vec3(p.cam0.y,p.cam1.y,p.cam2.y),p.sun.xyz),
                      dot(vec3(p.cam0.z,p.cam1.z,p.cam2.z),p.sun.xyz));
    vec2 sunRadius=.0047*p.proj.xy*.5/max(-sunView.z,.05);
    float visibility=sunDiscVisibility(sunUv,sunRadius)*cloudsAt(normalize(p.sun.xyz)).a;
    imageStore(outImage, id, vec4(rays, lit, air, visibility));   // r screen rays, g lit air share, b air length
}
)glsl";

// Half resolution: horizon-based ambient occlusion from the depth buffer
// (view space, normals from depth). Stored as visibility in r.
inline constexpr const char* kAmbientOcclusion = R"glsl(
layout(local_size_x = 8, local_size_y = 8) in;
layout(binding = 10, rgba16f) uniform writeonly image2D outImage;
vec3 viewPos(vec2 uv) { float d = textureLod(sceneDepth, uv, 0.0).r; return viewDirection(uv) * viewDistance(d); }
void main() {
    ivec2 id = ivec2(gl_GlobalInvocationID.xy), size = imageSize(outImage);
    if (any(greaterThanEqual(id, size))) return;
    vec2 uv = (vec2(id) + 0.5) / vec2(size), texel = 1.0 / vec2(size);
    float d = textureLod(sceneDepth, uv, 0.0).r;
    if (skyMask(d) > 0.5) { imageStore(outImage, id, vec4(1.0)); return; }
    vec3 P = viewPos(uv);
    vec3 px = viewPos(uv + vec2(texel.x, 0.0)), nx = viewPos(uv - vec2(texel.x, 0.0));
    vec3 py = viewPos(uv + vec2(0.0, texel.y)), ny = viewPos(uv - vec2(0.0, texel.y));
    vec3 dx = abs(px.z - P.z) < abs(nx.z - P.z) ? px - P : P - nx;
    vec3 dy = abs(py.z - P.z) < abs(ny.z - P.z) ? py - P : P - ny;
    vec3 N = normalize(cross(dx, dy));
    if (dot(N, -P) < 0.0) N = -N;
    const float radius = 9.0;                         // game units (an Inkling is ~16)
    float screenRadius = clamp(radius * p.proj.y / max(-P.z, 1.0) * 0.5, 3.0 * texel.y, 0.12);
    int dirs = int(p.exposure.w) >= 3 ? 8 : 6, steps = 5;
    float noise = hash12(vec2(id) + p.clouds.z * 7.13) * 6.2831853;
    float occlusion = 0.0;
    for (int i = 0; i < dirs; ++i) {
        float a = noise + float(i) * 6.2831853 / float(dirs);
        vec2 dir = vec2(cos(a), sin(a)) * vec2(size.y / float(size.x), 1.0) * screenRadius;
        float best = 0.0;
        for (int s = 1; s <= steps; ++s) {
            float t = (float(s) - hash12(vec2(id) + float(i * 7 + s))) / float(steps);
            vec2 q = uv + dir * t;
            if (any(lessThan(q, vec2(0.0))) || any(greaterThan(q, vec2(1.0)))) break;
            vec3 v = viewPos(q) - P;
            float len = length(v);
            float falloff = saturate(1.0 - len * len / (radius * radius * 1.6));
            best = max(best, saturate(dot(N, v / max(len, 1e-4)) - 0.12) * falloff);
        }
        occlusion += best;
    }
    float visibility = pow(saturate(1.0 - occlusion / float(dirs)), 1.6);
    imageStore(outImage, id, vec4(visibility, -P.z, 0.0, 1.0));
}
)glsl";

// Bloom chain (Jimenez, "Next Generation Post Processing in Call of Duty:
// Advanced Warfare"): 13-tap downsamples (the first with a soft threshold
// and Karis average against fireflies), tent upsamples adding each level.
// effects.x: 0 first downsample, 1 downsample, 2 upsample (+ raysTexture).
inline constexpr const char* kBloom = R"glsl(
layout(local_size_x = 8, local_size_y = 8) in;
layout(binding = 10, rgba16f) uniform writeonly image2D outImage;
layout(binding = 13) uniform sampler2D currentLevel;
vec3 tap(vec2 uv) { return textureLod(sceneColor, uv, 0.0).rgb; }
float karis(vec3 c) { return 1.0 / (1.0 + luminance(c)); }
vec3 threshold(vec3 c) {
    float y = luminance(c), knee = 0.6, t = 1.1 - 0.35 * saturate(1.0 - p.sun.y * 4.5) * smoothstep(-0.12, 0.0, p.sun.y);   // signs and lamps glow; at golden hour bright edges too
    float soft = clamp(y - t + knee, 0.0, 2.0 * knee);
    soft = soft * soft / (4.0 * knee + 1e-4);
    return c * max(soft, y - t) / max(y, 1e-4);
}
void main() {
    ivec2 id = ivec2(gl_GlobalInvocationID.xy), size = imageSize(outImage);
    if (any(greaterThanEqual(id, size))) return;
    vec2 uv = (vec2(id) + 0.5) / vec2(size);
    vec2 t = 1.0 / vec2(textureSize(sceneColor, 0));
    vec3 result;
    if (p.effects.x < 1.5) {
        vec3 a = tap(uv + t * vec2(-2, -2)), b = tap(uv + t * vec2(0, -2)), c = tap(uv + t * vec2(2, -2));
        vec3 d = tap(uv + t * vec2(-2, 0)), e = tap(uv), f = tap(uv + t * vec2(2, 0));
        vec3 g = tap(uv + t * vec2(-2, 2)), h = tap(uv + t * vec2(0, 2)), i = tap(uv + t * vec2(2, 2));
        vec3 j = tap(uv + t * vec2(-1, -1)), k = tap(uv + t * vec2(1, -1)), l = tap(uv + t * vec2(-1, 1)), m = tap(uv + t * vec2(1, 1));
        if (p.effects.x < 0.5) {
            vec3 g0 = (a + b + d + e) * 0.25, g1 = (b + c + e + f) * 0.25, g2 = (d + e + g + h) * 0.25, g3 = (e + f + h + i) * 0.25, g4 = (j + k + l + m) * 0.25;
            float w0 = karis(g0), w1 = karis(g1), w2 = karis(g2), w3 = karis(g3), w4 = karis(g4);
            result = (g0 * w0 * 0.125 + g1 * w1 * 0.125 + g2 * w2 * 0.125 + g3 * w3 * 0.125 + g4 * w4 * 0.5)
                   / (w0 * 0.125 + w1 * 0.125 + w2 * 0.125 + w3 * 0.125 + w4 * 0.5);
            result = threshold(min(result, vec3(12.0)));
        } else {
            result = e * 0.125 + (a + c + g + i) * 0.03125 + (b + d + f + h) * 0.0625 + (j + k + l + m) * 0.125;
        }
    } else {
        vec3 sum = tap(uv) * 4.0;
        sum += (tap(uv + vec2(-t.x, 0)) + tap(uv + vec2(t.x, 0)) + tap(uv + vec2(0, -t.y)) + tap(uv + vec2(0, t.y))) * 2.0;
        sum += tap(uv + vec2(-t.x, -t.y)) + tap(uv + vec2(t.x, -t.y)) + tap(uv + vec2(-t.x, t.y)) + tap(uv + vec2(t.x, t.y));
        result = sum / 16.0 + textureLod(currentLevel, uv, 0.0).rgb;
    }
    imageStore(outImage, id, vec4(result, 1.0));
}
)glsl";

// Adaptive tone matching: the frame's luma histogram (quarter resolution of
// the game's image) and, from it, a 256-entry curve that maps the frame's
// distribution onto the reference images' (quantile to quantile), blended
// over time so the picture never pumps.
inline constexpr const char* kHistogram = R"glsl(
layout(local_size_x = 8, local_size_y = 8) in;
layout(binding = 12, r32ui) uniform uimage2D histogram;
void main() {
    ivec2 id = ivec2(gl_GlobalInvocationID.xy);
    ivec2 size = textureSize(sceneColor, 0) / 4;
    if (any(greaterThanEqual(id, size))) return;
    vec2 uv = (vec2(id) + 0.5) / vec2(size);
    if (uv.y < 0.17 && uv.x > 0.6) return;                  // the HUD corner
    vec3 c = textureLod(sceneColor, uv, 0.0).rgb;
    float y = dot(displayColor(max(c, vec3(0.0))), vec3(0.299, 0.587, 0.114));
    imageAtomicAdd(histogram, ivec2(min(int(saturate(y) * 128.0), 127), 0), 1u);
}
)glsl";
inline constexpr const char* kToneCurve = R"glsl(
layout(local_size_x = 128, local_size_y = 1) in;
layout(binding = 12, r32ui) uniform uimage2D histogram;
layout(binding = 10, rgba16f) uniform image2D curveImage;
const float refDayQuantiles[33] = float[33](0.0000, 0.0505, 0.0769, 0.1009, 0.1236, 0.1453, 0.1661, 0.1880, 0.2101, 0.2332, 0.2577, 0.2831, 0.3115, 0.3406, 0.3730, 0.4123, 0.4647, 0.5209, 0.5847, 0.6434, 0.6896, 0.7229, 0.7478, 0.7721, 0.7969, 0.8165, 0.8348, 0.8516, 0.8685, 0.8875, 0.9096, 0.9373, 1.0000);
const float refGoldenQuantiles[33] = float[33](0.0016, 0.0676, 0.0945, 0.1206, 0.1467, 0.1701, 0.1916, 0.2121, 0.2318, 0.2522, 0.2748, 0.2983, 0.3208, 0.3425, 0.3659, 0.3949, 0.4381, 0.4995, 0.5570, 0.6114, 0.6633, 0.6982, 0.7262, 0.7516, 0.7766, 0.8038, 0.8273, 0.8511, 0.8761, 0.9055, 0.9295, 0.9538, 1.0000);
shared float cdf[129];
float refQuantile(float f, float golden) {
    float x = saturate(f) * 32.0;
    int i = min(int(x), 31);
    float d = mix(refDayQuantiles[i], refDayQuantiles[i + 1], x - float(i));
    float g = mix(refGoldenQuantiles[i], refGoldenQuantiles[i + 1], x - float(i));
    return mix(d, g, golden);
}
void main() {
    uint i = gl_LocalInvocationID.x;
    float count = float(imageLoad(histogram, ivec2(i, 0)).r);
    cdf[i + 1] = count;
    if (i == 0) cdf[0] = 0.0;
    barrier();
    if (i == 0) { for (int k = 1; k <= 128; ++k) cdf[k] += cdf[k - 1]; }
    barrier();
    imageStore(histogram, ivec2(i, 0), uvec4(0u));
    float total = max(cdf[128], 1.0);
    float golden = saturate(1.0 - p.sun.y * 4.5) * smoothstep(-0.12, 0.0, p.sun.y);
    float strength = p.look.y*smoothstep(-.12,.04,p.sun.y);
    for (uint k = i * 2u; k < i * 2u + 2u; ++k) {
        float y = (float(k) + 0.5) / 256.0;
        float x = y * 128.0;
        int b = min(int(x), 127);
        float f = mix(cdf[b], cdf[b + 1], x - float(b)) / total;
        float target = mix(y, refQuantile(f, golden), strength);
        vec4 prev = imageLoad(curveImage, ivec2(k, 0));
        float outv = p.look.z > 0.5 ? mix(prev.r, target, 0.06) : target;
        imageStore(curveImage, ivec2(k, 0), vec4(outv, 0.0, 0.0, 1.0));
    }
}
)glsl";

// Temporal anti-aliasing after the composite (display space): the previous
// result reprojected through depth and the previous camera, clipped to the
// current neighbourhood (variance clipping in YCoCg) against ghosting.
// shortcut: animated objects lack motion vectors; add native object vectors
// before using this history for DLSS or longer temporal accumulation.
inline constexpr const char* kTemporal = R"glsl(
layout(local_size_x = 8, local_size_y = 8) in;
layout(binding = 10, rgba16f) uniform writeonly image2D outImage;
layout(binding = 12, rgba16f) uniform writeonly image2D nextHistory;
layout(binding = 13) uniform sampler2D historyTexture;
vec3 toYCoCg(vec3 c) { return vec3(0.25 * c.r + 0.5 * c.g + 0.25 * c.b, 0.5 * c.r - 0.5 * c.b, -0.25 * c.r + 0.5 * c.g - 0.25 * c.b); }
vec3 fromYCoCg(vec3 y) { return vec3(y.x + y.y - y.z, y.x + y.z, y.x - y.y - y.z); }
void currentOnly(ivec2 id,vec4 color,float depth) {
    imageStore(outImage,id,color);
    imageStore(nextHistory,id,vec4(color.rgb,depth));
}
void main() {
    ivec2 id = ivec2(gl_GlobalInvocationID.xy), size = imageSize(outImage);
    if (any(greaterThanEqual(id, size))) return;
    vec2 uv = (vec2(id) + 0.5) / vec2(size), px = 1.0 / vec2(size);
    vec4 cur4 = textureLod(sceneColor, uv, 0.0);
    vec3 cur = cur4.rgb;
    float d = textureLod(sceneDepth, uv, 0.0).r;
    bool sky=skyMask(d)>0.5;
    // Log view depth retains useful precision in RGBA16F; sky has a separate tag.
    float historyDepth=sky?-1.0:log2(max(viewDistance(d),0.001));
    if(sky && p.cloudControl.y>.5){currentOnly(id,cur4,historyDepth);return;}
    if (p.taa.x < 0.5) { currentOnly(id,cur4,historyDepth); return; }
    vec3 m1 = vec3(0.0), m2 = vec3(0.0);
    for (int y = -1; y <= 1; ++y) for (int x = -1; x <= 1; ++x) {
        vec3 s = toYCoCg(textureLod(sceneColor, uv + vec2(x, y) * px, 0.0).rgb);
        m1 += s; m2 += s * s;
    }
    m1 /= 9.0; m2 /= 9.0;
    vec3 sigma = sqrt(max(m2 - m1 * m1, vec3(0.0)));
    vec3 dir = normalize(worldDirection(viewDirection(uv)));
    vec3 world = skyMask(d) > 0.5 ? eye() + dir * 1e6 : worldPoint(uv, d);
    vec3 rel = world - vec3(p.prev0.w, p.prev1.w, p.prev2.w);
    vec3 v = vec3(dot(vec3(p.prev0.x, p.prev1.x, p.prev2.x), rel), dot(vec3(p.prev0.y, p.prev1.y, p.prev2.y), rel),
                  dot(vec3(p.prev0.z, p.prev1.z, p.prev2.z), rel));
    vec2 prevUv = vec2(v.x * p.prevProj.x / max(-v.z, 1e-4) * 0.5 + 0.5, 0.5 - v.y * p.prevProj.y / max(-v.z, 1e-4) * 0.5);
    if (v.z >= 0.0 || any(lessThan(prevUv, vec2(0.0))) || any(greaterThan(prevUv, vec2(1.0)))) {
        currentOnly(id,cur4,historyDepth); return;
    }
    ivec2 historySize=textureSize(historyTexture,0);
    ivec2 historyId=clamp(ivec2(prevUv*vec2(historySize)),ivec2(0),historySize-1);
    float previousDepth=texelFetch(historyTexture,historyId,0).a;
    if(sky!=(previousDepth<0.0) || (!sky && abs(exp2(previousDepth)+v.z)>max(.15,-v.z*.008))) {
        currentOnly(id,cur4,historyDepth); return;
    }
    // Camera motion blur (180 degree shutter): the scene smears along the
    // screen motion of the camera since the last frame, as on film.
    vec2 velocity = uv - prevUv;
    float pixels = length(velocity * vec2(size));
    if (p.taa.w > 0.0 && pixels > 1.0) {
        vec2 stepv = velocity * p.taa.w / 8.0;
        vec3 sum = cur; float wsum = 1.0;
        for (int k = 1; k <= 8; ++k) {
            float t = float(k) - 4.5;
            vec2 q = uv + stepv * t;
            if (any(lessThan(q, vec2(0.0))) || any(greaterThan(q, vec2(1.0)))) continue;
            float w = 1.0 - abs(t) / 5.0;
            sum += textureLod(sceneColor, q, 0.0).rgb * w; wsum += w;
        }
        cur = sum / wsum;
    }
    vec3 hist = toYCoCg(textureLod(historyTexture, prevUv, 0.0).rgb);
    hist = clamp(hist, m1 - sigma * 1.1, m1 + sigma * 1.1);
    // Faster response where the history moved a lot (less smearing).
    float motion = length((prevUv - uv) * vec2(size));
    float blend = p.taa.y * mix(1.0, 0.75, saturate(motion / 24.0));
    vec3 outc = fromYCoCg(mix(toYCoCg(cur), hist, blend * (pixels > 1.0 && p.taa.w > 0.0 ? 0.5 : 1.0)));
    imageStore(outImage, id, vec4(max(outc, vec3(0.0)), cur4.a));
    // Camera blur belongs only in the displayed frame, never in accumulated history.
    vec3 historyColor=fromYCoCg(mix(toYCoCg(cur4.rgb),hist,blend*(pixels>1.0 && p.taa.w>0.0?.5:1.0)));
    imageStore(nextHistory,id,vec4(max(historyColor,vec3(0.0)),historyDepth));
}
)glsl";

// Final image before the HUD (display space): ambient occlusion, light
// shafts, wet ground with reflections, bloom from the HDR scene, contrast
// adaptive sharpening and a filmic grade.
inline constexpr const char* kComposite = R"glsl(
layout(local_size_x = 8, local_size_y = 8) in;
layout(binding = 10, rgba16f) uniform writeonly image2D outImage;
layout(binding = 13) uniform sampler2D raysTexture;
layout(binding = 14) uniform sampler2D aoTexture;
layout(binding = 15) uniform sampler2D bloom0;
layout(binding = 16) uniform sampler2D bloom1;
layout(binding = 17) uniform sampler2D bloom2;
layout(binding = 18) uniform sampler2D bloom3;
layout(binding = 19) uniform sampler2D bloom4;
layout(binding = 20) uniform sampler2D toneCurve;
layout(binding = 12, r32ui) uniform uimage2D lookHistogram;   // input of the look, for the next frame's curve
// Sky seen in a reflection (same exposure as the sky draw), display linear.
// The camera's white balance adapts to a low sun: its light reads golden
// white instead of the raw orange-red transmittance (as gx2_env_light.cpp).
vec3 whiteBalance(vec3 t) { return mix(t, luminance(t) * vec3(1.08, 0.96, 0.78), 0.6); }
vec3 skyReflection(vec3 d) {
    d.y = max(d.y, 0.02); d = normalize(d);
    vec3 sunE = sunIlluminance(), moonE = moonIlluminance();
    vec3 sky = skyView(d, false) * sunE + skyView(d, true) * moonE;
    float zenith = luminance(skyView(vec3(0.0, 1.0, 0.0), false) * sunE);
    float scale = p.exposure.x * clamp(pow(p.exposure.y / max(zenith, 1e-7), 0.42), 1.0, 40.0);
    vec4 cl = cloudsAt(d);
    vec3 night = (vec3(0.0026, 0.0038, 0.0085) + cityGlow(d) * 0.35) / scale;
    vec3 c = (sky+night)*cl.a*scale+exposedCloud(cl.rgb,scale);
    return c / (1.0 + c);                             // the game's highlights roll off too
}
vec3 wetReflection(vec3 at, vec3 n, vec3 fallback,float roughness) {
    vec3 direction = reflect(normalize(at - eye()), n), q = at + n * 0.35;
    float stride = 1.5;
    int steps = int(p.exposure.w) <= 1 ? 10 : int(p.exposure.w) == 2 ? 18 : 28;
    for (int i = 0; i < steps; ++i) {
        q += direction * stride; stride *= 1.22;
        vec2 uv = projectDirection(q - eye());
        if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0)))) break;
        float d = textureLod(sceneDepth, uv, 0.0).r;
        float z = -dot(vec3(p.cam0.z, p.cam1.z, p.cam2.z), q - eye());
        float diff = z - viewDistance(d);
        if (skyMask(d) < 0.5 && diff > 0.0 && diff < max(1.5, stride * 0.6)) {
            float edge = smoothstep(0.0, 0.08, min(min(uv.x, uv.y), min(1.0 - uv.x, 1.0 - uv.y)));
            vec3 hit=textureLod(sceneColor,uv,0.0).rgb;
            vec2 radius=min(vec2(.025),roughness*roughness*length(q-at)*p.proj.xy*.5/max(z,p.proj.z));
            if(max(radius.x*p.target.x,radius.y*p.target.y)>.75){
                vec3 sum=hit*2.0;float weight=2.0;
                const vec2 offsets[4]=vec2[4](vec2(1,0),vec2(-1,0),vec2(0,1),vec2(0,-1));
                for(int k=0;k<4;++k){
                    vec2 tap=uv+offsets[k]*radius;
                    if(any(lessThan(tap,vec2(0))) || any(greaterThan(tap,vec2(1))))continue;
                    float td=textureLod(sceneDepth,tap,0.0).r;
                    if(skyMask(td)>.5 || abs(viewDistance(td)-viewDistance(d))>max(2.0,viewDistance(d)*.04))continue;
                    sum+=textureLod(sceneColor,tap,0.0).rgb;weight+=1.0;
                }
                hit=sum/weight;
            }
            return mix(fallback,hit,edge);
        }
    }
    return fallback;
}
vec2 coverUv(vec3 at) { return (at.xz + at.y * vec2(18.0, 5.0) / 140.0 - p.cover.xy) * p.cover.zw; }
float coverHeight(vec3 at) {
    vec2 uv = coverUv(at);
    if (p.cover.z <= 0.0 || any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0)))) return -100000.0;
    return textureLod(rainCover, uv, 0.0).r;
}
// Rings of falling drops on standing water (normal offset in xz).
vec2 rippleNormal(vec2 xz, float t) {
    vec2 n = vec2(0.0);
    for (int k = 0; k < 3; ++k) {
        vec2 g = xz * 0.6 + float(k) * vec2(0.37, 0.61);
        vec2 cell = floor(g), f = fract(g) - 0.5;
        float h = hash12(cell + float(k) * 7.0);
        vec2 c = (vec2(hash12(cell + 3.1), hash12(cell + 5.7)) - 0.5) * 0.5;
        float life = fract(t * (0.8 + 0.4 * h) + h);
        vec2 dv = f - c;
        float r = length(dv), radius = life * 0.42;
        float ring = sin((r - radius) * 55.0) * exp(-pow((r - radius) * 12.0, 2.0)) * (1.0 - life) * (1.0 - life);
        n += (r > 1e-4 ? dv / r : vec2(0.0)) * ring;
    }
    return n;
}
)glsl"
                                          R"glsl(
vec3 srcColor(vec2 uv) { return textureLod(sceneColor, uv, 0.0).rgb; }
float srcLuma(vec2 uv) { return sqrt(luminance(srcColor(uv))); }
// FXAA 3.11 (Lottes), quality search along the edge.
vec3 fxaa(vec2 uv, vec2 px) {
    float lM = srcLuma(uv), lN = srcLuma(uv + vec2(0.0, -px.y)), lS = srcLuma(uv + vec2(0.0, px.y));
    float lW = srcLuma(uv + vec2(-px.x, 0.0)), lE = srcLuma(uv + vec2(px.x, 0.0));
    float lo = min(lM, min(min(lN, lS), min(lW, lE))), hi = max(lM, max(max(lN, lS), max(lW, lE)));
    float range = hi - lo;
    if (range < max(0.0312, hi * 0.125)) return srcColor(uv);
    float lNW = srcLuma(uv + vec2(-px.x, -px.y)), lNE = srcLuma(uv + vec2(px.x, -px.y));
    float lSW = srcLuma(uv + vec2(-px.x, px.y)), lSE = srcLuma(uv + vec2(px.x, px.y));
    float edgeH = abs(lNW + lNE - 2.0 * lN) + 2.0 * abs(lW + lE - 2.0 * lM) + abs(lSW + lSE - 2.0 * lS);
    float edgeV = abs(lNW + lSW - 2.0 * lW) + 2.0 * abs(lN + lS - 2.0 * lM) + abs(lNE + lSE - 2.0 * lE);
    bool horizontal = edgeH >= edgeV;
    float l1 = horizontal ? lN : lW, l2 = horizontal ? lS : lE;
    float g1 = abs(l1 - lM), g2 = abs(l2 - lM);
    bool steeper1 = g1 >= g2;
    float gradient = 0.25 * max(g1, g2);
    float stepLen = horizontal ? px.y : px.x;
    float lAvg;
    if (steeper1) { stepLen = -stepLen; lAvg = 0.5 * (l1 + lM); } else lAvg = 0.5 * (l2 + lM);
    vec2 cur = uv;
    if (horizontal) cur.y += stepLen * 0.5; else cur.x += stepLen * 0.5;
    vec2 off = horizontal ? vec2(px.x, 0.0) : vec2(0.0, px.y);
    vec2 a = cur - off, b = cur + off;
    float ea = srcLuma(a) - lAvg, eb = srcLuma(b) - lAvg;
    bool da = abs(ea) >= gradient, db = abs(eb) >= gradient;
    const float quality[10] = float[10](1.0, 1.0, 1.0, 1.5, 2.0, 2.0, 2.0, 2.0, 4.0, 8.0);
    for (int i = 0; i < 10 && !(da && db); ++i) {
        if (!da) { a -= off * quality[i]; ea = srcLuma(a) - lAvg; da = abs(ea) >= gradient; }
        if (!db) { b += off * quality[i]; eb = srcLuma(b) - lAvg; db = abs(eb) >= gradient; }
    }
    float distA = horizontal ? uv.x - a.x : uv.y - a.y, distB = horizontal ? b.x - uv.x : b.y - uv.y;
    bool closerA = distA < distB;
    float dist = min(distA, distB), len = distA + distB;
    bool smaller = lM < lAvg;
    bool correct = ((closerA ? ea : eb) < 0.0) != smaller;
    float edgeOffset = correct ? -dist / len + 0.5 : 0.0;
    float sub = saturate(abs((2.0 * (lN + lS + lW + lE) + lNW + lNE + lSW + lSE) / 12.0 - lM) / range);
    sub = sub * sub * (3.0 - 2.0 * sub);
    float finalOffset = max(edgeOffset, sub * sub * 0.75);
    vec2 f = uv;
    if (horizontal) f.y += finalOffset * stepLen; else f.x += finalOffset * stepLen;
    return srcColor(f);
}
// The realistic look: cubic colour transforms (display sRGB) fitted by
// tools/perf/fit_look.py to the reference images, per channel 20 terms.
const float lookDay[60] = float[60](0.084267, 0.429047, -0.208368, -0.215271, 0.532113, 0.486266, 0.158992, 0.652351, 0.315545, 0.464119, -0.324607, -0.153441, -0.014157, -0.167470, -0.328197, -0.249346, -0.336312, -0.013733, 0.138353, -0.253558, 0.068004, -0.292685, 0.506597, -0.206369, 0.600792, 0.420971, 0.153349, 0.652480, 0.288395, 0.492011, -0.344974, -0.133961, -0.008397, -0.186213, -0.346183, -0.234286, -0.325493, -0.021244, 0.146707, -0.248093, 0.072784, -0.248120, -0.151445, 0.409487, 0.599414, 0.498433, 0.091729, 0.688067, 0.267166, 0.455439, -0.347133, -0.160566, 0.011810, -0.195567, -0.339152, -0.261418, -0.331698, 0.002754, 0.166090, -0.241690);
const float lookGolden[60] = float[60](0.137630, 0.607103, -0.004655, -0.152533, 0.183198, 0.447645, 0.349009, 0.421806, 0.553698, 0.544158, -0.099940, -0.283707, -0.216050, -0.108398, -0.310533, -0.204772, -0.484454, -0.212125, 0.041747, -0.198751, 0.082287, -0.064107, 0.730814, -0.215074, 0.027550, 0.402505, 0.429709, 0.355535, 0.629754, 0.508405, 0.142256, -0.398480, -0.192284, 0.083474, -0.263238, -0.182263, -0.554821, -0.299429, -0.024778, -0.176715, 0.027277, -0.048519, 0.008002, 0.398604, 0.101259, 0.500048, 0.395102, 0.454345, 0.614590, 0.485529, 0.095603, -0.397476, -0.188378, 0.038466, -0.268665, -0.212249, -0.581901, -0.275688, 0.000650, -0.195579);
const float lookDayCurve[33] = float[33](0.00000, 0.08241, 0.11859, 0.14616, 0.16922, 0.18762, 0.20472, 0.22114, 0.23833, 0.25509, 0.27385, 0.29602, 0.31465, 0.33524, 0.35658, 0.38368, 0.42691, 0.50071, 0.58045, 0.69156, 0.76062, 0.79674, 0.81919, 0.83790, 0.85961, 0.88044, 0.89582, 0.91264, 0.93378, 0.95694, 0.97268, 0.98396, 1.00000);
const mat2 lookDayChroma = mat2(0.52250, 0.04232, 0.04232, 0.59961);
const vec4 lookDayMean = vec4(0.01421, -0.01740, -0.00165, 0.00298);
const float lookGoldenCurve[33] = float[33](0.00235, 0.08044, 0.10803, 0.13844, 0.17300, 0.20076, 0.23036, 0.25815, 0.28947, 0.31936, 0.33954, 0.35611, 0.37086, 0.38863, 0.41233, 0.43999, 0.47878, 0.55911, 0.68317, 0.76862, 0.82795, 0.86484, 0.89491, 0.92328, 0.94431, 0.95652, 0.96616, 0.97030, 0.97324, 0.97588, 0.98371, 0.99187, 1.00000);
const mat2 lookGoldenChroma = mat2(0.48099, 0.08162, 0.08162, 0.62302);
const vec4 lookGoldenMean = vec4(-0.05220, 0.06125, -0.06529, 0.06482);
// The exact transfer: the measured tone curve (quantile to quantile) on
// luma and the chroma transform (mean + covariance) in YCbCr.
vec3 lookExact(vec3 s, bool golden) {
    float y = dot(s, vec3(0.299, 0.587, 0.114));
    float cb = dot(s, vec3(-0.168736, -0.331264, 0.5)), cr = dot(s, vec3(0.5, -0.418688, -0.081312));
    // The adaptive curve (kToneCurve) when it is running, the fixed fit otherwise.
    float f = saturate(y) * 32.0;
    int i = min(int(f), 31);
    float y2 = p.look.y > 0.0 ? textureLod(toneCurve, vec2(saturate(y) * (255.0 / 256.0) + 0.5 / 256.0, 0.5), 0.0).r
             : golden ? mix(lookGoldenCurve[i], lookGoldenCurve[i + 1], f - float(i)) : mix(lookDayCurve[i], lookDayCurve[i + 1], f - float(i));
    // Depth: shadows and midtones sink like in the reference (0.85 stays put).
    float depthGamma = golden ? 1.16 : 1.07;   // evenings: deeper blacks, a little more contrast
    y2 = pow(max(y2, 0.0) / 0.85, depthGamma) * 0.85;
    // Film shoulder: highlights roll off instead of glaring.
    float knee = 0.45, roll = golden ? 0.92 : 0.84;
    if (y2 > knee) y2 = knee + (y2 - knee) * roll;
    vec4 m = golden ? lookGoldenMean : lookDayMean;
    vec2 ch = (golden ? lookGoldenChroma : lookDayChroma) * (vec2(cb, cr) - m.xy) + (golden ? m.xy : m.zw);   // no colour cast at golden hour
    // Neon yellow-green paint reads olive in daylight like in the reference.
    {
        float dang = abs(mod(atan(cr, cb) + 3.0 + PI, 2.0 * PI) - PI);     // distance to yellow-green (-3.0 rad in CbCr)
        float hueYG = 1.0-smoothstep(0.0,0.35,dang);
        float chroma = length(vec2(cb, cr));
        float k = 1.0 - 0.28 * hueYG * smoothstep(0.08, 0.2, chroma) * (golden ? 0.3 : 1.0);
        cb *= k; cr *= k;
    }
    // By day only the saturation changes: shifting the colour balance is what makes a filter.
    if (!golden) ch = (golden ? lookGoldenChroma : lookDayChroma) * vec2(cb, cr);
    ch = mix(vec2(cb, cr), ch, 0.9) * (golden ? 0.8 : 1.02);
    return saturate(vec3(y2 + 1.402 * ch.y, y2 - 0.344136 * ch.x - 0.714136 * ch.y, y2 + 1.772 * ch.x));
}
vec3 lookTransform(vec3 s, bool golden) {
    float r = s.r, g = s.g, b = s.b;
    float t[20] = float[20](1.0, r, g, b, r * r, g * g, b * b, r * g, g * b, b * r,
                            r * r * r, g * g * g, b * b * b, r * r * g, r * r * b, g * g * r, g * g * b, b * b * r, b * b * g, r * g * b);
    vec3 o = vec3(0.0);
    for (int i = 0; i < 20; ++i) {
        o.r += t[i] * (golden ? lookGolden[i] : lookDay[i]);
        o.g += t[i] * (golden ? lookGolden[20 + i] : lookDay[20 + i]);
        o.b += t[i] * (golden ? lookGolden[40 + i] : lookDay[40 + i]);
    }
    return clamp(o, vec3(0.0), vec3(1.0));
}
float aoAt(vec2 uv, float viewZ) {
    // Depth-aware 4-tap upsample of the half resolution occlusion.
    vec2 t = 1.0 / vec2(textureSize(aoTexture, 0));
    float sum = 0.0, weight = 0.0;
    for (int i = 0; i < 4; ++i) {
        vec2 o = (vec2(i & 1, i >> 1) - 0.5) * t * 1.5;
        vec2 s = textureLod(aoTexture, uv + o, 0.0).rg;
        float w = 1.0 / (0.02 + abs(s.g - viewZ) / max(viewZ, 1.0) * 40.0);
        sum += s.r * w; weight += w;
    }
    return sum / max(weight, 1e-4);
}
)glsl"
                                          R"glsl(
void main() {
    ivec2 id = ivec2(gl_GlobalInvocationID.xy), size = imageSize(outImage);
    if (any(greaterThanEqual(id, size))) return;
    vec2 uv = (vec2(id) + 0.5) / vec2(size), px = 1.0 / vec2(size);
    // Hand-held sway: the whole image drifts, rolls and breathes a little.
    if (p.sway.w > 0.0) {
        vec2 q = (uv - 0.5) * vec2(float(size.x) / float(size.y), 1.0);
        float cr = cos(p.sway.z), sr = sin(p.sway.z);
        q = vec2(cr * q.x - sr * q.y, sr * q.x + cr * q.y) / p.sway.w;
        uv = q / vec2(float(size.x) / float(size.y), 1.0) + 0.5 + p.sway.xy;
    }
    vec4 source = textureLod(sceneColor, uv, 0.0);
    // Contrast adaptive sharpening (AMD FidelityFX CAS, simplified).
    vec3 a = textureLod(sceneColor, uv + vec2(0.0, -px.y), 0.0).rgb;
    vec3 b = textureLod(sceneColor, uv + vec2(-px.x, 0.0), 0.0).rgb;
    vec3 c = p.post.x > 0.0 ? fxaa(uv, px) : source.rgb;
    vec3 e = textureLod(sceneColor, uv + vec2(px.x, 0.0), 0.0).rgb;
    vec3 f = textureLod(sceneColor, uv + vec2(0.0, px.y), 0.0).rgb;
    vec3 mn = min(min(min(a, b), min(c, e)), f), mx = max(max(max(a, b), max(c, e)), f);
    vec3 amp = sqrt(saturate(min(mn, 1.0 - mx) / max(mx, vec3(1e-4))));
    vec3 wgt = -amp / mix(8.0, 5.0, 0.55) * (p.post.x > 0.0 ? 0.18 : 0.6)*p.gradeTools.w;
    c = max((c + (a + b + e + f) * wgt) / (1.0 + 4.0 * wgt), vec3(0.0));
    // Lens fringes: red and blue drift apart towards the corners.
    if (p.post.z > 0.0) {
        vec2 dir = uv - 0.5;
        vec2 shift = dir * dot(dir, dir) * p.post.z;
        c.r = mix(c.r, srcColor(uv + shift).r, 0.85);
        c.b = mix(c.b, srcColor(uv - shift).b, 0.85);
    }
    // Clarity: medium-radius local contrast on luminance brings out the
    // texture detail of walls, asphalt and metal (not the sky).
    float d = textureLod(sceneDepth, uv, 0.0).r;
    bool sky = skyMask(d) > 0.5;
    if(sky && p.cloudControl.y>.5){imageStore(outImage,id,source);return;}
    if (!sky && p.look.x > 0.0) {
        float around = 0.0;
        for (int k = 0; k < 8; ++k) {
            float a = float(k) * 0.785398;
            around += luminance(srcColor(uv + vec2(cos(a), sin(a)) * px * 3.0));
        }
        around /= 8.0;
        float yc = luminance(c);
        float detail = clamp(yc - around, -0.12, 0.12);
        c *= 1.0 + detail*.3*p.cloudControl.z/max(yc,.04)*smoothstep(.02,.15,yc);
    }
    float aoVis = 1.0;
    if (!sky && p.depthRange.w > 0.0) {
        aoVis = aoAt(uv, viewDistance(d));
        float y = luminance(c);
        c *= mix(1.0, aoVis, p.depthRange.w * (1.0 - 0.6 * saturate(y * 1.5)));
    }
    // Weathering: dirt gathers in corners and joints, large surfaces vary
    // a little in tone (world-space noise, so it sticks to the surfaces).
    if (!sky && p.look.x > 0.0) {
        float dist = viewDistance(d);
        vec3 wp = worldPoint(uv, d);
        float n1 = textureLod(detailNoise, wp * 0.021, 0.0).r;
        float n2 = textureLod(shapeNoise, wp * 0.0042, 0.0).g;
        float fade = 1.0 - smoothstep(600.0, 2500.0, dist);
        float crevice = saturate((1.0 - aoVis) * 2.2) * smoothstep(0.35, 0.75, n1);
        float blotch = (n2 - 0.5) * 0.16 + (n1 - 0.5) * 0.06;
        vec3 dirt = vec3(0.86, 0.80, 0.70);
        c *= mix(vec3(1.0), dirt, crevice * 0.55 * fade);
        c *= 1.0 + blotch * fade * (1.0 - smoothstep(0.6, 1.2, luminance(c)));
    }
    // Depth of field: far buildings soften like through a real lens
    // (the blurred-minus-sharp difference keeps every other effect).
    if ((p.look.x > 0.0 && p.look.w > 0.0) || p.cinema.x > 0.0) {
        float dist = sky ? 1e5 : viewDistance(d);
        float coc = p.look.x > 0.0 ? smoothstep(900.0, 4500.0, dist) * p.look.w : 0.0;     // radius in pixels
        // Focus blur (menu): sharp at the distance of what the screen centre
        // shows (the character), softer in front and behind like a big lens.
        if (p.cinema.x > 0.0) {
            float focus = viewDistance(textureLod(sceneDepth, vec2(0.5, 0.58), 0.0).r);
            coc = max(coc, p.cinema.x * saturate(abs(dist - focus) / max(focus, 1.0)));
        }
        if (coc > 0.3) {
            vec3 sharp = srcColor(uv), blur = vec3(0.0);
            for (int k = 0; k < 8; ++k) {
                float a = float(k) * 0.785398 + 0.39;
                blur += srcColor(uv + vec2(cos(a), sin(a)) * px * coc);
                blur += srcColor(uv + vec2(cos(a + 0.39), sin(a + 0.39)) * px * coc * 0.5);
            }
            blur = (blur / 16.0) * 0.75 + sharp * 0.25;
            c = max(c + (blur - sharp), vec3(0.0));
        }
    }
    float wet = p.effects.y;
    vec3 at = vec3(0.0), n = vec3(0.0, 1.0, 0.0);
    float surfaceFootprint=0.0;
    if (!sky) {
        at = worldPoint(uv, d);
        vec2 dp = 1.0 / vec2(textureSize(sceneDepth, 0));
        float zl = textureLod(sceneDepth, uv - vec2(dp.x, 0.0), 0.0).r, zr = textureLod(sceneDepth, uv + vec2(dp.x, 0.0), 0.0).r;
        float zu = textureLod(sceneDepth, uv - vec2(0.0, dp.y), 0.0).r, zd = textureLod(sceneDepth, uv + vec2(0.0, dp.y), 0.0).r;
        vec3 dx = abs(zl - d) < abs(zr - d) ? at - worldPoint(uv - vec2(dp.x, 0.0), zl) : worldPoint(uv + vec2(dp.x, 0.0), zr) - at;
        vec3 dy = abs(zu - d) < abs(zd - d) ? at - worldPoint(uv - vec2(0.0, dp.y), zu) : worldPoint(uv + vec2(0.0, dp.y), zd) - at;
        surfaceFootprint=max(length(dx),length(dy));
        n = cross(dy, dx);
        n = dot(n, n) > 1e-12 ? normalize(n) : vec3(0.0, 1.0, 0.0);
        if (dot(n, eye() - at) < 0.0) n = -n;
    }
    // Contact shadows: small occluders (feet, bollards, kerbs, props) that
    // the game's shadow map is too coarse for. March towards the sun in
    // world space and test the depth buffer.
    if (!sky && p.look.x > 0.0 && p.sun.y > 0.02 && p.wind.w > 0.05) {
        float lit = dot(n, p.sun.xyz);
        if (lit > 0.0) {
            float dist0 = viewDistance(d);
            float stepLen = clamp(dist0 * 0.004, 0.25, 2.0);
            float noise = hash12(vec2(id) + p.clouds.z * 3.1);
            float shadow = 0.0;
            for (int k = 1; k <= 14; ++k) {
                vec3 q = at + n * 0.15 + p.sun.xyz * stepLen * (float(k) + noise);
                vec2 quv = projectDirection(q - eye());
                if (any(lessThan(quv, vec2(0.0))) || any(greaterThan(quv, vec2(1.0)))) break;
                float sd = textureLod(sceneDepth, quv, 0.0).r;
                if (skyMask(sd) > 0.5) continue;
                float rayZ = -dot(vec3(p.cam0.z, p.cam1.z, p.cam2.z), q - eye());
                float diff = rayZ - viewDistance(sd);
                if (diff > 0.05 && diff < stepLen * 6.0) { shadow = 1.0 - float(k) / 16.0; break; }
            }
            float daylight = smoothstep(0.02, 0.2, p.sun.y) * p.wind.w;
            c *= 1.0 - shadow * 0.6 * daylight * saturate(lit * 3.0) * (1.0 - smoothstep(400.0, 1200.0, dist0));
        }
    }
    // Dry ground still has a sheen: brighter towards grazing angles and a
    // broad glow towards a low sun.
    if (!sky && p.look.x > 0.0 && wet < 0.6) {
        float floorish = smoothstep(0.75, 0.95, n.y);
        if (floorish > 0.0) {
            vec3 view = normalize(at - eye());
            float cosv = max(dot(n, -view), 0.0);
            float fres = 0.04 + 0.96 * pow(1.0 - cosv, 5.0);
            vec3 r = reflect(view, n);
            float dry = floorish * (1.0 - wet / 0.6);
            // (no sky mix: it washed dry asphalt out to a milky grey)
            vec3 sunCol = whiteBalance(transmittance(Rg + viewHeight, max(p.sun.y, 0.0))) * sunIlluminance() * p.wind.w;
            c += sunCol * pow(saturate(dot(r, p.sun.xyz)), 32.0) * fres * 0.45 * dry;
        }
    }
    if (wet > 0.001 && !sky) {
        float roof = coverHeight(at);
        float exposed = roof > -99999.0 ? step(roof - 1.5, at.y) : 1.0;
        // Puddles collect in a slow noise pattern; elsewhere the ground darkens.
        // Puddles only where the noise dips deepest: small, soft-edged pools.
        float puddleNoise = textureLod(shapeNoise, vec3(at.xz * 0.004, 0.5), 0.0).r * 0.7 + textureLod(detailNoise, vec3(at.xz * 0.03, 0.3), 0.0).g * 0.3;
        float puddle = smoothstep(0.66, 0.72, puddleNoise + wet * 0.04) * wet;
        // Paint and tiles shed water sooner than porous asphalt.
        float albedo = luminance(c);
        float w = wet * exposed * smoothstep(0.6, 0.9, n.y) * mix(1.0, 0.55, smoothstep(0.25, 0.6, albedo));
        if (w > 0.001) {
            float t = p.sun.w;
            vec2 ripple = rippleNormal(at.xz, t) * p.clouds.y;
            // Wet asphalt keeps its rough micro surface: blurred, broken reflections.
            vec2 grain = vec2(textureLod(detailNoise, vec3(at.xz * 0.35, 0.6), 0.0).r, textureLod(detailNoise, vec3(at.xz * 0.35 + 0.37, 0.2), 0.0).g) - 0.5;
            float rough = mix(.45,.06,puddle);
            vec3 nn = wetSurfaceNormal(n,grain,ripple,puddle,surfaceFootprint);
            vec3 view = normalize(at - eye());
            float cosv = max(dot(nn, -view), 0.0);
            float fresnel = 0.02 + 0.98 * pow(1.0 - cosv, 5.0);
            fresnel *= mix(0.45, 1.0, puddle);                 // rough surfaces lose the grazing mirror
            vec3 reflection = wetReflection(at, nn, skyReflection(reflect(view, nn)),rough);
            // Darker and slightly richer when soaked.
            vec3 soaked = c * c * 1.8;
            c = mix(c, mix(c * 0.72, soaked, 0.35), w);
            float amount = wetReflectionAmount(w,fresnel,puddle);
            c = mix(c, reflection, amount);
            // The sun's glints: wide and soft on asphalt, sharp on puddles.
            vec3 sunCol = whiteBalance(transmittance(Rg + viewHeight, max(p.sun.y, 0.0))) * sunIlluminance() * p.wind.w;
            float spec = pow(saturate(dot(reflect(view, nn), p.sun.xyz)), mix(60.0, 700.0, puddle));
            c += sunCol * spec * w * fresnel * mix(1.2, 5.0, puddle);
        }
    }
    float warm = saturate(1.0 - p.sun.y * 3.0) * smoothstep(-0.1, 0.02, p.sun.y);
    vec3 rays = textureLod(raysTexture, uv, 0.0).rgb;
    vec3 sunColor = whiteBalance(transmittance(Rg + viewHeight, max(p.sun.y, 0.0))) * sunIlluminance() * p.wind.w;
    if (p.effects.x > 0.0) {
        // Shafts are seen near the sun and mostly when it is low; elsewhere
        // they would only veil the picture.
        float toward = pow(saturate(dot(normalize(worldDirection(viewDirection(uv))), p.sun.xyz)), mix(8.0, 3.0, saturate(p.extra.x / 1.5)));
        vec3 sunThrough = whiteBalance(transmittance(Rg + viewHeight, max(p.sun.y, 0.0))) * sunIlluminance();   // gaps let the full sun through
        vec3 shafts = sunThrough * rays.r * rays.r * toward * p.effects.x * (0.2 + 2.2 * warm) * (1.0 + p.extra.x);
        float distance=sky?6000.0:length(at-eye());
        // Crepuscular rays across the sky: with broken clouds the lit
        // shafts fan out from the sun and the shadowed air between them
        // darkens, so they read as rays, not as a glow.
        float broken = saturate(p.extra.x / 1.5);
        if (sky && broken > 0.0) {
            float fan = pow(saturate(dot(normalize(worldDirection(viewDirection(uv))), p.sun.xyz)), 2.0);
            // Relative to the sky's own brightness: never washes it out.
            float k = fan * broken * p.effects.x * 2.0;
            c *= max(1.0 + (rays.r * rays.r * 1.1 - (1.0 - rays.r) * 0.45) * k, 0.4);
        }
        // Volumetric beams (rays pass g/b): lit air brightens, shaded air
        // darkens, strongest looking towards the sun (forward scattering).
        if (p.clouds.x > 0.03) {
            float cosV = dot(normalize(worldDirection(viewDirection(uv))), p.sun.xyz);
            float forward = hgPhase(cosV, 0.72) / hgPhase(1.0, 0.72);
            float beams = (rays.g - 0.55) * rays.b * p.effects.x * (0.5 + broken) * (0.35 + 4.0 * forward) * (1.0 + 1.5 * warm);
            c *= max(1.0 + beams * 1.6, 0.35);
        }
        c += shafts * airOpacity(distance,mix(.000003,.000018,warm)+p.picture.w*.00009) * (1.0-c*.5);
    }
)glsl"
                                          R"glsl(
    // Air: aerial perspective and, at a low sun, golden light scattered in
    // the haze; distant buildings fade into it and become silhouettes.
    vec3 vdir = normalize(worldDirection(viewDirection(uv)));
    float golden = saturate(1.0 - p.sun.y * 4.5) * smoothstep(-0.12, 0.0, p.sun.y);
    if (!sky && p.taa.z < 0.5) {
        float dist = length(at-eye());
        float density = mix(0.000003, 0.000018, golden) * (1.0 + 1.5 * p.clouds.y);   // distant buildings only
        float cosS = dot(vdir, p.sun.xyz);
        float towardSun = pow(saturate(cosS), 4.0);
        // Haze only in the distance: nothing hangs in the air near the camera.
        float haze = airOpacity(dist,density*(1.0+2.5*golden*towardSun));
        vec3 ambientAir = skyReflection(normalize(vec3(vdir.x, 0.12, vdir.z)));
        vec3 sunAir = sunColor * (hgPhase(cosS, 0.72) * 3.0 + hgPhase(cosS, 0.92) * 3.0 * golden + 0.04) * (0.3 + 0.7 * rays.r);
        c = mix(c, ambientAir * mix(0.8, 0.45, golden) + sunAir, haze * 0.6);
    }
    // A low sun: veiling glare over everything, diffraction spikes, ghosts
    // along the line through the centre, dust drifting in the light.
    vec2 sunUv = projectDirection(p.sun.xyz);
    if (golden > 0.0 && p.effects.x > 0.0 && sunUv.x > -0.9 && sunUv.x < 1.9 && sunUv.y > -0.9 && sunUv.y < 1.9) {
        float vis=textureLod(raysTexture,vec2(.5),0.0).a;
        vec2 aspect = vec2(p.target.x / p.target.y, 1.0);
        vec2 dv = (uv - sunUv) * aspect;
        float r = length(dv), ang = atan(dv.y, dv.x);
        float veil = exp(-r * 18.0) * 0.08 + exp(-r * 55.0) * 0.3;
        float star = pow(abs(cos(ang * 3.0 + 0.4)), 80.0) * exp(-r * 6.0) * 0.9 + pow(abs(cos(ang * 5.0 + 1.1)), 160.0) * exp(-r * 9.0) * 0.5;
        vec3 lightNow = sunColor * vis * golden;
        c += lightNow * (veil + star) * 1.1;
        const float gpos[4] = float[4](0.35, 0.7, 1.15, 1.6);
        const float gsize[4] = float[4](0.045, 0.085, 0.03, 0.13);
        const vec3 gtint[4] = vec3[4](vec3(1.0, 0.7, 0.35), vec3(0.45, 0.8, 1.0), vec3(1.0, 0.9, 0.6), vec3(0.6, 0.45, 1.0));
        for (int k = 0; k < 4; ++k) {
            vec2 gp = sunUv + (vec2(0.5) - sunUv) * gpos[k];
            float gd = length((uv - gp) * aspect);
            float ghost = (1.0-smoothstep(gsize[k]*.55,gsize[k],gd)) * (0.5 + 0.5 * smoothstep(gsize[k] * 0.5, gsize[k], gd));
            c += lightNow * gtint[k] * ghost * 0.05;
        }
        // Dust motes: tiny specks in view-angle cells, lit only inside the shafts.
        float pixelAngle = 2.0 / (p.proj.y * p.target.y);
        float az = atan(vdir.z, vdir.x), el = asin(clamp(vdir.y, -1.0, 1.0));
        for (int layer = 0; layer < 2; ++layer) {
            float K = 140.0 + 160.0 * float(layer);
            vec2 q = vec2(az, el) * K + vec2(p.sun.w * 0.15, sin(p.sun.w * 0.2 + float(layer)) * 0.3);
            vec2 cell = floor(q), f = fract(q);
            float h = hash12(cell + float(layer) * 31.0);
            if (h > 0.93) {
                vec2 center = vec2(hash12(cell + 2.3), hash12(cell + 5.9)) * 0.8 + 0.1;
                float dd = length(f - center) / K / pixelAngle;      // pixels
                float speck = exp(-dd * dd / (layer == 0 ? 2.5 : 1.2));
                float forward = pow(saturate(dot(vdir, p.sun.xyz)), 4.0);
                c += lightNow * speck * rays.r * (0.25 + forward) * 0.35;
            }
        }
    }
    // Fog: exponential height fog thickest near the ground, lit by the sky
    // and by the sun through it (shafts show inside it).
    if (p.picture.w > 0.0) {
        float dist = sky ? 6000.0 : length(at-eye());
        vec3 at2 = eye() + vdir * dist;
        float groundY = 0.0; // plaza world datum: the fog must not move up and down with the camera
        float h0 = max(eye().y - groundY, 0.0), h1 = max(at2.y - groundY, 0.0);
        const float H = 60.0;
        // Integral of exp(-h/H) along the ray.
        float dh = h1 - h0;
        float integral = abs(dh) > 0.01 ? H * (exp(-h0 / H) - exp(-h1 / H)) / dh : exp(-h0 / H);
        float optical = p.picture.w * 0.004 * max(dist - 120.0, 0.0) * integral;   // clear right around the player
        float fogT = exp(-optical);
        float cosS = dot(vdir, p.sun.xyz);
        vec3 fogSun = whiteBalance(transmittance(Rg + viewHeight, max(p.sun.y, 0.0))) * sunIlluminance() * p.wind.w
                    * (hgPhase(cosS, 0.6) * 2.5 + 0.08) * (0.35 + 0.65 * rays.r);
        vec3 fogSky = skyReflection(normalize(vec3(vdir.x, 0.15, vdir.z))) * 0.75;
        c = c * fogT + (fogSky + fogSun) * (1.0 - fogT);
    }
    // Bloom from the HDR scene: energy above the display range spreads.
    if (p.depthRange.z > 0.0) {
        vec3 bright = textureLod(bloom0, uv, 0.0).rgb;
        c += bright * p.depthRange.z * 0.45 * (1.0 + 1.8 * golden);
    }
    // Filmic grade: gentle S-curve around mid grey, vibrance, warm light and
    // cool shadows following the sun, soft vignette.
    float night = 1.0 - smoothstep(-0.12, 0.05, p.sun.y);
    c = max(c, vec3(0.0));
    vec3 graded = 0.18 * pow(c / 0.18 + 1e-5, vec3(1.12));
    graded = mix(c, graded, saturate(1.0 - luminance(c)));       // curve in the darks and mids only
    float y = luminance(graded);
    float sat = (max(graded.r, max(graded.g, graded.b)) - min(graded.r, min(graded.g, graded.b))) / max(max(graded.r, max(graded.g, graded.b)), 1e-4);
    graded = mix(vec3(y), graded, 1.0 + 0.34 * (1.0 - sat) + 0.06);
    vec3 shadowTint = mix(mix(vec3(0.99, 1.0, 1.02), vec3(0.95, 0.98, 1.05), warm), vec3(0.93, 0.97, 1.08), night);
    vec3 lightTint = mix(vec3(1.01, 1.0, 0.99), vec3(1.06, 1.0, 0.93), warm);
    graded *= mix(shadowTint, lightTint, smoothstep(0.05, 0.6, y));
    float vig = 1.0 - mix(0.03, 0.14, warm) * pow(saturate(length((uv - 0.5) * vec2(1.0, 0.8)) * 1.35), 2.4);
    vig *= 1.0 - p.cinema.y * 0.5 * pow(saturate(length((uv - 0.5) * vec2(1.0, 0.8)) * 1.35), 2.0);   // menu vignette
    c = graded * vig;
    if (p.look.x > 0.0) {
        vec3 s = displayColor(saturate(c));
        if (p.look.y > 0.0 && (id.x & 3) == 0 && (id.y & 3) == 0 && !(uv.y < 0.17 && uv.x > 0.6))
            imageAtomicAdd(lookHistogram, ivec2(min(int(dot(s, vec3(0.299, 0.587, 0.114)) * 128.0), 127), 0), 1u);
        float golden = saturate(1.0 - p.sun.y * 4.5) * smoothstep(-0.12, 0.0, p.sun.y);
        vec3 graded2 = mix(lookExact(s, false), lookExact(s, true), golden);
        // Subtle by day; the golden look carries the sunset (warm, calmer colours).
        float strength = p.look.x*smoothstep(-.12,.04,p.sun.y);
        c = linearColor(mix(s, graded2, strength));
    }
    c += vec3(0.75, 0.8, 1.0) * p.storm.x * 0.08;          // the flash reaches everything
    // Player picture settings: exposure, contrast about mid grey, saturation.
    c *= exp2(p.picture.z);
    c = 0.18 * pow(max(c, vec3(0.0)) / 0.18 + 1e-6, vec3(p.picture.y));
    c = max(mix(vec3(luminance(c)), c, p.picture.x), vec3(0.0));
    if (p.cinema.z != 0.0) {   // white balance: warm/cool at the same brightness
        vec3 wb = vec3(1.0 + 0.16 * p.cinema.z, 1.0, 1.0 - 0.16 * p.cinema.z);
        c *= wb / luminance(wb);
    }
    // Halation: low sun light bleeds warm around bright areas.
    if (p.depthRange.z > 0.0) c += textureLod(bloom0, uv, 0.0).rgb * vec3(1.0, 0.7, 0.45) * warm * 0.03;
    // Film grain, stronger in the darks, new every frame.
    if (p.post.y > 0.0) {
        float g = hash12(vec2(id) + p.post.w * 113.0) + hash12(vec2(id.yx) * 1.31 + p.post.w * 71.0) - 1.0;
        c += c * g * p.post.y * (1.0 - saturate(luminance(c) * 1.5)) + g * p.post.y * 0.004;
    }
    imageStore(outImage, id, vec4(pictureTools(max(c,vec3(0)),p.gradeTools), source.a));
}
)glsl";

// Falling rain and splashes on the collision cover (world space).
inline constexpr const char* kRainVertex = R"glsl(
layout(location = 0) out vec2 dropUV;
layout(location = 1) out vec3 dropWorld;
layout(location = 2) flat out float roofHeight;
layout(location = 3) flat out float dropAlpha;
layout(location = 4) flat out int splash;
vec2 coverUv(vec3 at) { return (at.xz + at.y * vec2(18.0, 5.0) / 140.0 - p.cover.xy) * p.cover.zw; }
float coverHeight(vec3 at) {
    vec2 uv = coverUv(at);
    if (p.cover.z <= 0.0 || any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0)))) return -100000.0;
    return textureLod(rainCover, uv, 0.0).r;
}
void main() {
    const vec2 corners[6] = vec2[6](vec2(-1, -1), vec2(1, -1), vec2(1, 1), vec2(-1, -1), vec2(1, 1), vec2(-1, 1));
    vec2 corner = corners[gl_VertexIndex % 6]; splash = gl_VertexIndex >= 6 ? 1 : 0;
    int grid = int(p.effects.z), index = gl_InstanceIndex;
    vec2 wind = vec2(18, 5), drift = wind * p.sun.w;
    vec2 cell = floor((eye().xz - drift) / 6.0) + vec2(index % grid, index / grid) - float(grid / 2);
    float seed = hash13(vec3(cell, 19.3));
    vec2 jitter = vec2(hash13(vec3(cell, 2.1)), hash13(vec3(cell, 7.3))) * 6.0;
    float y = floor(eye().y / 128.0) * 128.0 - 128.0 + mod(seed * 512.0 - p.sun.w * 140.0, 512.0);
    vec3 at = vec3(cell.x * 6.0 + jitter.x + drift.x, y, cell.y * 6.0 + jitter.y + drift.y);
    roofHeight = coverHeight(at);
    vec3 velocity = vec3(18, -140, 5), axis = normalize(velocity);
    vec3 right = cross(axis, normalize(eye() - at));
    right = length(right) > 0.001 ? normalize(right) : vec3(1, 0, 0);
    float opacity = smoothstep(0.0, 0.15, p.clouds.y - seed * 0.65);
    if (splash == 0) {
        dropWorld = at + right * corner.x * (0.018 + seed * 0.015) + axis * corner.y * (0.5 + seed * 0.4);
        opacity *= float(dropWorld.y >= roofHeight && roofHeight > -99999.0);
    } else {
        float age = (roofHeight - y) / 140.0;
        vec2 invariant = at.xz + wind * y / 140.0;
        vec2 impact = invariant - wind * roofHeight / 140.0;
        float radius = 0.15 + clamp(age, 0.0, 0.18) * 5.0;
        dropWorld = vec3(impact.x + corner.x * radius, roofHeight + 0.04, impact.y + corner.y * radius);
        opacity *= float(age >= 0.0 && age < 0.18 && roofHeight > -99999.0) * (1.0 - clamp(age / 0.18, 0.0, 1.0));
    }
    dropUV = corner; dropAlpha = opacity;
    vec3 q = dropWorld - eye();
    vec3 v = vec3(dot(vec3(p.cam0.x, p.cam1.x, p.cam2.x), q), dot(vec3(p.cam0.y, p.cam1.y, p.cam2.y), q),
                  dot(vec3(p.cam0.z, p.cam1.z, p.cam2.z), q));
    float z = -v.z, a = p.proj.w / (p.proj.w - p.proj.z);
    gl_Position = vec4(v.x * p.proj.x, -v.y * p.proj.y, a * z - a * p.proj.z, z);
    if (opacity <= 0.001 || z <= p.proj.z) gl_Position = vec4(2, 2, 2, 1);
}
)glsl";
inline constexpr const char* kRainFragment = R"glsl(
layout(location = 0) in vec2 dropUV;
layout(location = 1) in vec3 dropWorld;
layout(location = 2) flat in float roofHeight;
layout(location = 3) flat in float dropAlpha;
layout(location = 4) flat in int splash;
layout(location = 0) out vec4 outColor;
void main() {
    if (dropAlpha <= 0.001 || dropWorld.y < roofHeight - 0.01) discard;
    vec2 uv = gl_FragCoord.xy / vec2(textureSize(sceneDepth, 0));
    float d = textureLod(sceneDepth, uv, 0.0).r;
    float z = -dot(vec3(p.cam0.z, p.cam1.z, p.cam2.z), dropWorld - eye());
    if (z > viewDistance(d) + 0.03) discard;
    float opacity;
    if (splash == 0) opacity = (1.0 - smoothstep(0.1, 1.0, abs(dropUV.x))) * (1.0 - smoothstep(0.45, 1.0, abs(dropUV.y))) * 0.34;
    else { float r = length(dropUV), width = max(fwidth(r), 0.10); opacity = (1.0 - smoothstep(width, width * 2.0, abs(r - 0.72))) * 0.26; }
    float daylight = smoothstep(-0.1, 0.25, p.sun.y);
    vec3 light = mix(vec3(0.02, 0.03, 0.05) + cityGlow(vec3(0.0)) * 0.6, vec3(0.42, 0.5, 0.62), daylight);
    light += transmittance(Rg + viewHeight, max(p.sun.y, 0.0)) * sunIlluminance()
           * hgPhase(dot(normalize(dropWorld - eye()), p.sun.xyz), 0.65) * 1.5 * p.wind.w;
    outColor = vec4(light, opacity * dropAlpha);   // linear target
}
)glsl";
} // namespace gx2weather
