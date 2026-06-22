#version 450

layout(push_constant) uniform PushConstants {
    vec3 origin;
    float pad0;
    vec3 u;
    float pad1;
    vec3 v;
    float pad2;
    vec3 w;
    float pad3;
    vec3 sphereCenter;
    float pad4;
    float fov;
    float aspect;
    float width;
    float height;
    float frame;
} pc;

layout(binding = 0, rgba32f) uniform readonly image2D prevAccum;
layout(binding = 1, rgba32f) uniform writeonly image2D outAccum;
layout(location = 0) out vec4 outColor;

uint wangHash(uint seed) {
    seed ^= seed << 13;
    seed ^= seed >> 17;
    seed ^= seed << 5;
    return seed;
}

float randomFloat(inout uint seed) {
    seed = wangHash(seed);
    return float(seed) / 4294967296.0;
}

vec3 randomInUnitSphere(inout uint seed) {
    vec3 p;
    do {
        p = 2.0 * vec3(randomFloat(seed), randomFloat(seed), randomFloat(seed)) - vec3(1.0);
    } while (dot(p, p) >= 1.0);
    return p;
}

vec3 randomCosineDirection(inout uint seed) {
    float u = randomFloat(seed);
    float v = randomFloat(seed);
    float r = sqrt(u);
    float theta = 2.0 * 3.14159265359 * v;
    return vec3(r * cos(theta), r * sin(theta), sqrt(max(0.0, 1.0 - u)));
}

vec3 randomHemisphere(vec3 normal, inout uint seed) {
    vec3 direction = randomCosineDirection(seed);
    vec3 helper = abs(normal.x) > 0.1 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    vec3 tangent = normalize(cross(helper, normal));
    vec3 bitangent = cross(normal, tangent);
    return direction.x * tangent + direction.y * bitangent + direction.z * normal;
}

vec3 rayDirection(vec2 fragCoord) {
    vec2 uv = (fragCoord.xy / vec2(pc.width, pc.height)) * 2.0 - vec2(1.0);
    uv.y = -uv.y;

    float theta = radians(pc.fov);
    float halfHeight = tan(theta * 0.5);
    float halfWidth = pc.aspect * halfHeight;

    return normalize(uv.x * halfWidth * pc.u + uv.y * halfHeight * pc.v - pc.w);
}

float intersectSphere(vec3 ro, vec3 rd, vec3 center, float radius) {
    vec3 oc = ro - center;
    float a = dot(rd, rd);
    float b = 2.0 * dot(oc, rd);
    float c = dot(oc, oc) - radius * radius;
    float discriminant = b * b - 4.0 * a * c;
    if (discriminant < 0.0) {
        return -1.0;
    }
    float sqrtDisc = sqrt(discriminant);
    float t0 = (-b - sqrtDisc) / (2.0 * a);
    float t1 = (-b + sqrtDisc) / (2.0 * a);
    if (t0 > 0.001) return t0;
    if (t1 > 0.001) return t1;
    return -1.0;
}

vec3 skyColor(vec3 direction) {
    vec3 unitDir = normalize(direction);
    float t = smoothstep(-0.2, 0.8, unitDir.y);
    vec3 horizon = vec3(0.05, 0.1, 0.25);
    vec3 zenith = vec3(0.3, 0.6, 1.0);
    return mix(horizon, zenith, t * t);
}

vec3 traceRay(vec3 origin, vec3 direction, inout uint seed) {
    vec3 sphereCenter = pc.sphereCenter;
    float sphereRadius = 0.5;
    float t = intersectSphere(origin, direction, sphereCenter, sphereRadius);
    if (t > 0.0) {
        vec3 hit = origin + t * direction;
        vec3 normal = normalize(hit - sphereCenter);
        vec3 target = hit + randomHemisphere(normal, seed);
        vec3 lambert = vec3(0.6, 0.6, 0.7);
        float cosine = max(dot(normal, normalize(target - hit)), 0.0);
        return lambert * cosine;
    }
    return skyColor(direction);
}

void main() {
    uvec2 pixel = uvec2(gl_FragCoord.xy);
    uint seed = pixel.x * 1973u + pixel.y * 9277u + uint(pc.frame) * 26699u;

    vec3 direction = rayDirection(gl_FragCoord.xy);
    vec3 color = traceRay(pc.origin, direction, seed);

    vec4 prev = imageLoad(prevAccum, ivec2(gl_FragCoord.xy));
    float weight = 1.0 / (pc.frame + 1.0);
    vec4 accumulated = mix(prev, vec4(color, 1.0), weight);

    imageStore(outAccum, ivec2(gl_FragCoord.xy), accumulated);
    outColor = accumulated;
}
