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
    float fov;
    float aspect;
    float width;
    float height;
} pc;

layout(location = 0) out vec4 outColor;

vec3 rayDirection(vec2 fragCoord) {
    vec2 uv = (fragCoord.xy / vec2(pc.width, pc.height)) * 2.0 - vec2(1.0);
    uv.y = -uv.y;

    float theta = radians(pc.fov);
    float halfHeight = tan(theta * 0.5);
    float halfWidth = pc.aspect * halfHeight;

    vec3 dir = normalize(uv.x * halfWidth * pc.u + uv.y * halfHeight * pc.v - pc.w);
    return dir;
}

vec3 skyColor(vec3 direction) {
    vec3 unitDir = normalize(direction);
    float t = smoothstep(-0.2, 0.8, unitDir.y);
    vec3 horizon = vec3(0.05, 0.1, 0.25);
    vec3 zenith = vec3(0.3, 0.6, 1.0);
    return mix(horizon, zenith, t * t);
}

void main() {
    vec3 direction = rayDirection(gl_FragCoord.xy);
    outColor = vec4(skyColor(direction), 1.0);
}
