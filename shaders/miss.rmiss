#version 460
#extension GL_EXT_ray_tracing : enable
#extension GL_GOOGLE_include_directive : enable

#include "common.glsl"

layout(location = 0) rayPayloadInEXT PathPayload payload;

void main() {
    payload.radiance = skyColor(gl_WorldRayDirectionEXT);
    payload.state = 0u;
}
