#version 440
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 color;
layout(std140, binding = 0) uniform View {
    vec4 destination; // normalized output rectangle (centered fit or crop)
    vec4 source; // normalized texture rectangle
    vec4 options;
    vec4 sampling; // xy: Normal physical scale (zero disables), zw: texture size
};
layout(binding = 1) uniform sampler2D scene;
void main() {
    vec2 position = (uv - destination.xy) / destination.zw;
    if (any(lessThan(position, vec2(0))) || any(greaterThanEqual(position, vec2(1)))) {
        color = vec4(0, 0, 0, 1);
        return;
    }
    vec2 coord = source.xy + position * source.zw;
    // Stock Normal magnification: constant 0.75-output-pixel edge transition.
    // Verified against stripes, checkers and composed translucent pixels at
    // explicit and automatic scales. Zero disables this for terrain/Blur;
    // sub-unit scales retain ordinary linear minification.
    vec2 scale = sampling.xy;
    if (any(greaterThanEqual(scale, vec2(1)))) {
        vec2 texels = sampling.zw;
        vec2 p = coord * texels;
        vec2 fraction = fract(p);
        vec2 k = max(scale / 0.75, vec2(1));
        vec2 radius = vec2(0.5) - vec2(0.5) / k;
        vec2 adjusted = (fraction - vec2(0.5) - clamp(fraction - vec2(0.5), -radius, radius)) * k + vec2(0.5);
        coord = (floor(p) + mix(fraction, adjusted, step(vec2(1), scale))) / texels;
    }
    color = texture(scene, coord);
}
