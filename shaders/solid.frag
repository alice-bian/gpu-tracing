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
    return normalize(p);
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

vec3 traceRay(vec3 origin, vec3 direction, inout uint seed) {
    vec3 sphereCenter = pc.sphereCenter;
    float sphereRadius = 0.5;
    float t = intersectSphere(origin, direction, sphereCenter, sphereRadius);
    if (t > 0.0) {
        vec3 hit = origin + t * direction;
        vec3 normal = normalize(hit - sphereCenter);

        if (pc.materialType < 0.5) {
            vec3 reflected = reflectDirection(normalize(direction), normal);
            vec3 fuzzVector = pc.sphereFuzz * randomInUnitSphere(seed);
            vec3 scattered = normalize(reflected + fuzzVector);
            if (dot(scattered, normal) <= 0.0) {
                return vec3(0.0);
            }
            return skyColor(scattered);
        }

        bool frontFace = dot(direction, normal) < 0.0;
        vec3 outwardNormal = frontFace ? normal : -normal;
        float ratio = frontFace ? (1.0 / pc.sphereIor) : pc.sphereIor;
        float cosTheta = min(dot(-normalize(direction), outwardNormal), 1.0);
        float sinTheta = sqrt(max(0.0, 1.0 - cosTheta * cosTheta));
        bool cannotRefract = ratio * sinTheta > 1.0;
        float reflectProbability = schlick(cosTheta, pc.sphereIor);

        vec3 rayOut;
        if (cannotRefract || reflectProbability > randomFloat(seed)) {
            rayOut = reflectDirection(normalize(direction), normal);
        } else {
            vec3 refracted = refract(normalize(direction), outwardNormal, ratio);
            vec3 insideOrigin = hit + refracted * 0.001;
            float tInside = intersectSphere(insideOrigin, refracted, sphereCenter, sphereRadius);
            if (tInside < 0.0) {
                rayOut = reflectDirection(normalize(direction), normal);
            } else {
                vec3 exitHit = insideOrigin + refracted * tInside;
                vec3 exitNormal = normalize(exitHit - sphereCenter);
                if (dot(refracted, exitNormal) > 0.0) {
                    exitNormal = -exitNormal;
                }

                if (pc.hollowShell > 0.5) {
                    float innerRadius = 0.45;
                    float tInner = intersectSphere(insideOrigin, refracted, sphereCenter, innerRadius);
                    if (tInner > 0.0) {
                        vec3 innerHit = insideOrigin + refracted * tInner;
                        vec3 innerNormal = normalize(innerHit - sphereCenter);
                        float cosThetaInner = min(dot(-refracted, innerNormal), 1.0);
                        float sinThetaInner = sqrt(max(0.0, 1.0 - cosThetaInner * cosThetaInner));
                        bool cannotRefractInner = pc.sphereIor * sinThetaInner > 1.0;
                        if (cannotRefractInner) {
                            rayOut = reflectDirection(refracted, innerNormal);
                        } else {
                            vec3 innerRefracted = refract(refracted, innerNormal, 1.0 / pc.sphereIor);
                            if (length(innerRefracted) == 0.0) {
                                rayOut = reflectDirection(refracted, innerNormal);
                            } else {
                                rayOut = innerRefracted;
                            }
                        }
                    } else {
                        vec3 exitRefracted = refract(refracted, exitNormal, pc.sphereIor);
                        if (length(exitRefracted) == 0.0) {
                            rayOut = reflectDirection(normalize(direction), normal);
                        } else {
                            rayOut = exitRefracted;
                        }
                    }
                } else {
                    vec3 exitRefracted = refract(refracted, exitNormal, pc.sphereIor);
                    if (length(exitRefracted) == 0.0) {
                        rayOut = reflectDirection(normalize(direction), normal);
                    } else {
                        rayOut = exitRefracted;
                    }
                }
            }
        }
        return skyColor(rayOut);
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
