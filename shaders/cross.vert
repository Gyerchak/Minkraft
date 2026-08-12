#version 450
layout(location = 0) in vec3 aPos;
layout(push_constant) uniform PC {
    vec4 uColor;
} pc;
void main() {
    gl_Position = vec4(aPos, 1.0);
}

