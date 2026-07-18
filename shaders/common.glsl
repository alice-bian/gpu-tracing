#ifndef COMMON_GLSL_INCLUDED
#define COMMON_GLSL_INCLUDED

struct PathPayload {
    vec3 radiance;
    uint seed;
    vec3 attenuation;
    uint state;
    vec3 scatterOrigin;
    float pad0;
    vec3 scatterDirection;
    float pad1;
};

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
    float sphereFuzz;
    float materialType;
    float sphereIor;
    float hollowShell;
    float fov;
    float aspect;
    float width;
    float height;
    float frame;
} pc;

uint wangHash(uint seed) {
    seed ^= seed << 13;
    seed ^= seed >> 17;
    seed ^= seed << 5;
    return seed;
}

float randomFloat(inout uint seed) {
    seed = wangHash(seed + 0x9E3779B9u);
    return float(seed) / 4294967296.0;
}

vec3 randomInUnitSphere(inout uint seed) {
    for (int i = 0; i < 16; i++) {
        vec3 p = 2.0 * vec3(randomFloat(seed), randomFloat(seed), randomFloat(seed)) - vec3(1.0);
        float len2 = dot(p, p);
        if (len2 < 1.0 && len2 >= 1e-8) {
            return p;
        }
    }
    return vec3(0.0, 0.0, 1.0);
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

vec3 skyColor(vec3 direction) {
    float len2 = dot(direction, direction);
    vec3 unitDir = len2 > 1e-10 ? direction * inversesqrt(len2) : vec3(0.0, 1.0, 0.0);

    if (unitDir.y > 0.35) {
        return vec3(0.15, 0.25, 0.8);
    }
    if (unitDir.y > -0.1) {
        return vec3(0.9, 0.85, 0.25);
    }
    return vec3(0.1, 0.2, 0.08);
}

vec3 reflectDirection(vec3 incoming, vec3 normal) {
    return incoming - 2.0 * dot(incoming, normal) * normal;
}

float schlick(float cosine, float ref_idx) {
    if (abs(ref_idx - 1.0) < 1e-6) {
        return 0.0;
    }
    float r0 = (1.0 - ref_idx) / (1.0 + ref_idx);
    r0 = r0 * r0;
    return r0 + (1.0 - r0) * pow(1.0 - cosine, 5.0);
}

bool nearZero(vec3 v) {
    return dot(v, v) < 1e-8;
}

#endif
