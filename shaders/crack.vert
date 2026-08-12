#version 450
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;
layout(set = 0, binding = 0) uniform UBO {
    mat4 uViewProj;
    vec4 uCamPos;
    vec4 uFogColor;
    vec4 uFogParams;
} ubo;
layout(push_constant) uniform PC {
    mat4 uModel;
    vec4 uUVRect;
} pc;
layout(location = 0) out vec2 vUV;
layout(location = 1) out vec3 vNormal;
void main() {
    vUV = pc.uUVRect.xy + aUV * (pc.uUVRect.zw - pc.uUVRect.xy);
    vNormal = aNormal;
    gl_Position = ubo.uViewProj * pc.uModel * vec4(aPos, 1.0);
}
