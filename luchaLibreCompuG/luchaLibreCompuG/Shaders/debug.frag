#version 330 core
in vec3 LineColor;
out vec4 FragColor;

uniform vec3 tint;

void main()
{
    FragColor = vec4(LineColor * tint, 1.0f);
}
