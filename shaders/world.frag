#version 450
layout(location = 0) in vec3 vNormal;
layout(location = 1) in vec2 vUV;
layout(location = 2) in vec3 vWorldPos;
layout(set = 0, binding = 0) uniform UBO {
    mat4 uViewProj;
    vec4 uCamPos;
    vec4 uFogColor;
    vec4 uFogParams;
} ubo;
layout(set = 0, binding = 1) uniform sampler2D uAtlas;
layout(location = 0) out vec4 outColor;
void main() {
    vec3 L = normalize(vec3(0.55, 1.0, 0.35));
    float bright = 0.42 + 0.58 * max(dot(normalize(vNormal), L), 0.0);
    vec4 tex = texture(uAtlas, vUV);
    if (tex.a < 0.5) discard;
    vec3 color = tex.rgb * bright;
    float dist = length(vWorldPos - ubo.uCamPos.xyz);
    float fog = clamp((dist - ubo.uFogParams.x) / (ubo.uFogParams.y - ubo.uFogParams.x), 0.0, 1.0);
    color = mix(color, ubo.uFogColor.xyz, fog);
    // Fade the alpha to solid as fog takes over: translucent water must not let
    // the world behind it keep showing through at distance (that ghosted
    // through-water edges and caused the shifting alpha-flicker).
    outColor = vec4(color, mix(tex.a, 1.0, fog));
}

