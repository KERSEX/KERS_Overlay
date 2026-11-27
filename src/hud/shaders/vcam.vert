#version 440
// VirtualCam: Ecke = Position, Normale, Farbe (a = 1 -> Teamfarbe der Instanz).
// Instanz = Modellmatrix (vier Spalten) + Farbe.
layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec4 color;
layout(location = 3) in vec4 m0;
layout(location = 4) in vec4 m1;
layout(location = 5) in vec4 m2;
layout(location = 6) in vec4 m3;
layout(location = 7) in vec4 tint;

layout(location = 0) out vec3 v_color;
layout(location = 1) out vec3 v_normal;

layout(std140, binding = 0) uniform buf {
    mat4 viewProj;
    vec4 lightDir;
    vec4 light;     // x = Umgebung, y = Diffus
};

void main()
{
    mat4 model = mat4(m0, m1, m2, m3);
    v_color = color.a > 0.5 ? color.rgb * tint.rgb : color.rgb;
    v_normal = mat3(model) * normal;
    gl_Position = viewProj * model * vec4(position, 1.0);
}
