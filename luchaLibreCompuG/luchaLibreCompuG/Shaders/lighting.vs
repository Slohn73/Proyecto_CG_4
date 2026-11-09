#version 330 core
layout (location = 0) in vec3 position;
layout (location = 1) in vec3 normal;
layout (location = 2) in vec2 texCoords;
layout (location = 3) in vec3 vertexColor;
// Solo en mallas con esqueleto (luchadores): hasta 4 huesos por vertice y sus pesos
layout (location = 4) in ivec4 boneIds;
layout (location = 5) in vec4 boneWeights;

out vec3 Normal;
out vec3 FragPos;
out vec2 TexCoords;
out vec3 VertexColor;

uniform mat4 model;
uniform mat3 normalMatrix;   // transpose(inverse(mat3(model))), calculada una vez por objeto en la CPU
uniform mat4 view;
uniform mat4 projection;

// Deformacion por esqueleto (skinning). bones[i] lleva el vertice de la pose de enlace a la pose
// actual del hueso i, en el espacio del modelo; la pose la calcula el programa (AnimacionCombate.h).
const int MAX_BONES = 56;          // Igual que MAX_SKIN_BONES en Mesh.h
uniform mat4 bones[MAX_BONES];
uniform bool skinned;

void main()
{
    vec4 localPos = vec4(position, 1.0f);
    vec3 localNormal = normal;
    if (skinned)
    {
        float total = boneWeights.x + boneWeights.y + boneWeights.z + boneWeights.w;
        if (total > 0.001f)
        {
            mat4 skin = boneWeights.x * bones[boneIds.x] + boneWeights.y * bones[boneIds.y]
                      + boneWeights.z * bones[boneIds.z] + boneWeights.w * bones[boneIds.w];
            localPos = skin * localPos;
            localNormal = mat3(skin) * normal;
        }
    }
    gl_Position = projection * view * model * localPos;
    FragPos = vec3(model * localPos);
    Normal = normalMatrix * localNormal;
    TexCoords = texCoords;
    VertexColor = vertexColor;
}
