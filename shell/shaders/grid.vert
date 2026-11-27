#version 440
layout(location = 0) out vec2 uv;
layout(std140, binding = 0) uniform View {
    vec4 destination;
    vec4 source;
    vec4 options; // x: render-to-texture Y correction
    vec4 sampling; // xy: Normal physical scale, zw: texture size
};
void main() {
    vec2 p = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2));
    uv = vec2(p.x, 1.0 - p.y);
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
    if (options.x > 0.5) gl_Position.y = -gl_Position.y;
}
