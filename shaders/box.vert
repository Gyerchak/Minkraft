#version 450
layout(location = 0) in vec3 aPos;
layout(set = 0, binding = 0) uniform UBO {
    mat4 uViewProj;
    vec4 uCamPos;
    vec4 uFogColor;
    vec4 uFogParams;
} ubo;
layout(push_constant) uniform PC {
    mat4 uModel;
    vec4 uColor;
} pc;
void main() {
    gl_Position = ubo.uViewProj * pc.uModel * vec4(aPos, 1.0);
}

