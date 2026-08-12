#version 450
layout(location = 0) in vec2 vUV;
layout(location = 1) in vec3 vNormal;
layout(set = 0, binding = 1) uniform sampler2D uAtlas;
layout(location = 0) out vec4 outColor;
void main() {
    float a = texture(uAtlas, vUV).a;
    if (a < 0.5) discard;
    float shade = 0.85 + 0.15 * abs(vNormal.y);
    outColor = vec4(vec3(0.04) * shade, a);
}
