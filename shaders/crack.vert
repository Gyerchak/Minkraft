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
    // Simplest possible crack overlay: one quad billboarded to face the camera.
    vec3 center = vec3(pc.uModel[3][0], pc.uModel[3][1], pc.uModel[3][2]);
    vec3 toCam = normalize(ubo.uCamPos.xyz - center);
    vec3 right = normalize(cross(vec3(0.0, 1.0, 0.0), toCam));
    if (length(right) < 1e-4) right = vec3(1.0, 0.0, 0.0); // looking straight down/up
    vec3 up = normalize(cross(toCam, right));
    vec3 world = center + toCam * 0.51 + aPos.x * right + aPos.y * up;
    vUV = pc.uUVRect.xy + aUV * (pc.uUVRect.zw - pc.uUVRect.xy);
    vNormal = aNormal;
    gl_Position = ubo.uViewProj * vec4(world, 1.0);
}
