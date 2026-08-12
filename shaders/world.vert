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
layout(location = 0) out vec3 vNormal;
layout(location = 1) out vec2 vUV;
layout(location = 2) out vec3 vWorldPos;
void main() {
    vNormal = aNormal;
    vUV = aUV;
    vWorldPos = aPos;
    gl_Position = ubo.uViewProj * vec4(aPos, 1.0);
}

