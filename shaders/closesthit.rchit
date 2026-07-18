#version 460
#extension GL_EXT_ray_tracing : enable
#extension GL_GOOGLE_include_directive : enable

#include "common.glsl"

layout(location = 0) rayPayloadInEXT PathPayload payload;
hitAttributeEXT vec2 hitAttributes;

void main() {
    uint seed = payload.seed;
    vec3 rayDir = normalize(gl_WorldRayDirectionEXT);
    vec3 hitPos = gl_WorldRayOriginEXT + gl_WorldRayDirectionEXT * gl_HitTEXT;
    vec3 normal = normalize(hitPos - pc.sphereCenter);
    if (dot(rayDir, normal) > 0.0) {
        normal = -normal;
    }

    if (pc.materialType < 0.5) {
        vec3 reflected = normalize(reflectDirection(rayDir, normal));
        vec3 fuzzVector = pc.sphereFuzz * randomInUnitSphere(seed);
        vec3 candidate = reflected + fuzzVector;
        if (nearZero(candidate) || dot(candidate, normal) <= 0.0) {
            payload.seed = seed;
            payload.radiance = skyColor(reflected);
            payload.state = 0u;
            return;
        }

        payload.seed = seed;
        payload.attenuation = vec3(1.0, 1.0, 1.0);
        payload.scatterOrigin = hitPos + normal * 0.001;
        payload.scatterDirection = normalize(candidate);
        payload.state = 1u;
        return;
    }

    if (pc.materialType < 1.5) {
        vec3 scatter = randomHemisphere(normal, seed);
        if (nearZero(scatter)) {
            scatter = normal;
        }

        payload.seed = seed;
        payload.attenuation = vec3(0.85, 0.85, 0.85);
        payload.scatterOrigin = hitPos + normal * 0.001;
        payload.scatterDirection = normalize(scatter);
        payload.state = 1u;
        return;
    }

    bool frontFace = dot(rayDir, normal) < 0.0;
    vec3 outwardNormal = frontFace ? normal : -normal;
    float ratio = frontFace ? (1.0 / pc.sphereIor) : pc.sphereIor;
    float cosTheta = min(dot(-rayDir, outwardNormal), 1.0);
    float sinTheta = sqrt(max(0.0, 1.0 - cosTheta * cosTheta));
    bool cannotRefract = ratio * sinTheta > 1.0;
    float reflectProbability = schlick(cosTheta, pc.sphereIor);

    vec3 rayOut;
    if (cannotRefract || reflectProbability > randomFloat(seed)) {
        rayOut = reflectDirection(rayDir, normal);
    } else {
        vec3 refracted = refract(rayDir, outwardNormal, ratio);
        vec3 insideOrigin = hitPos + refracted * 0.001;

        vec3 toCenter = insideOrigin - pc.sphereCenter;
        float a = dot(refracted, refracted);
        float b = 2.0 * dot(toCenter, refracted);
        float c = dot(toCenter, toCenter) - 0.25;
        float disc = b * b - 4.0 * a * c;
        if (disc < 0.0) {
            rayOut = reflectDirection(rayDir, normal);
        } else {
            float tInside = (-b + sqrt(disc)) / (2.0 * a);
            vec3 exitHit = insideOrigin + refracted * tInside;
            vec3 exitNormal = normalize(exitHit - pc.sphereCenter);
            if (dot(refracted, exitNormal) > 0.0) {
                exitNormal = -exitNormal;
            }

            if (pc.hollowShell > 0.5) {
                float innerRadius = 0.45;
                vec3 innerToCenter = insideOrigin - pc.sphereCenter;
                float aInner = dot(refracted, refracted);
                float bInner = 2.0 * dot(innerToCenter, refracted);
                float cInner = dot(innerToCenter, innerToCenter) - innerRadius * innerRadius;
                float discInner = bInner * bInner - 4.0 * aInner * cInner;
                float tInner = -1.0;
                if (discInner >= 0.0) {
                    float sqrtDiscInner = sqrt(discInner);
                    float t0Inner = (-bInner - sqrtDiscInner) / (2.0 * aInner);
                    float t1Inner = (-bInner + sqrtDiscInner) / (2.0 * aInner);
                    if (t0Inner > 0.0) {
                        tInner = t0Inner;
                    } else if (t1Inner > 0.0) {
                        tInner = t1Inner;
                    }
                }

                if (tInner > 0.0) {
                    vec3 innerHit = insideOrigin + refracted * tInner;
                    vec3 innerNormal = normalize(innerHit - pc.sphereCenter);
                    float cosThetaInner = min(dot(-refracted, innerNormal), 1.0);
                    float sinThetaInner = sqrt(max(0.0, 1.0 - cosThetaInner * cosThetaInner));
                    bool cannotRefractInner = pc.sphereIor * sinThetaInner > 1.0;
                    if (cannotRefractInner) {
                        rayOut = reflectDirection(refracted, innerNormal);
                    } else {
                        vec3 innerRefracted = refract(refracted, innerNormal, 1.0 / pc.sphereIor);
                        if (nearZero(innerRefracted)) {
                            rayOut = reflectDirection(refracted, innerNormal);
                        } else {
                            rayOut = innerRefracted;
                        }
                    }
                } else {
                    vec3 exitRefracted = refract(refracted, exitNormal, pc.sphereIor);
                    if (nearZero(exitRefracted)) {
                        rayOut = reflectDirection(rayDir, normal);
                    } else {
                        rayOut = exitRefracted;
                    }
                }
            } else {
                vec3 exitRefracted = refract(refracted, exitNormal, pc.sphereIor);
                if (nearZero(exitRefracted)) {
                    rayOut = reflectDirection(rayDir, normal);
                } else {
                    rayOut = exitRefracted;
                }
            }
        }
    }

    payload.seed = seed;
    payload.attenuation = vec3(1.0, 1.0, 1.0);
    payload.scatterOrigin = hitPos + rayOut * 0.001;
    payload.scatterDirection = normalize(rayOut);
    payload.state = 1u;
}
