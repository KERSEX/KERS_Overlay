#version 440
layout(location = 0) in vec3 v_color;
layout(location = 1) in vec3 v_normal;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 viewProj;
    vec4 lightDir;
    vec4 light;
};

void main()
{
    float l = max(dot(normalize(v_normal), normalize(-lightDir.xyz)), 0.0);
    fragColor = vec4(v_color * (light.x + light.y * l), 1.0);
}
