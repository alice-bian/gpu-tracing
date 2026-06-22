#version 450
layout(location = 0) out vec4 outColor;

void main() {
    vec2 uv = gl_FragCoord.xy / vec2(800.0, 600.0);
    outColor = vec4(uv.x, 1.0 - uv.y, 0.0, 1.0);
}
