#version 330 core
layout (location = 0) in vec3 position;
layout (location = 1) in vec3 normal;
layout (location = 2) in vec2 texCoords;
layout (location = 3) in vec3 vertexColor;
layout (location = 4) in ivec4 boneIds;       // Hasta 4 huesos por vértice (-> aiProcess_LimitBoneWeights)
layout (location = 5) in vec4 boneWeights;

out vec3 Normal;
out vec3 FragPos;
out vec2 TexCoords;
out vec3 VertexColor;

const int MAX_BONES = 100;                    // Debe coincidir con MAX_BONES de Animation.h

uniform mat4 model;
uniform mat3 normalMatrix;   // transpose(inverse(mat3(model))), calculada una vez por objeto en la CPU
uniform mat4 view;
uniform mat4 projection;

uniform bool useSkinning;                     // Lo fija Mesh::Draw (solo las mallas animadas)
uniform mat4 finalBonesMatrices[MAX_BONES];   // Lo sube Scene::drawPass una vez por objeto

void main()
{
    vec4 localPos = vec4(position, 1.0f);
    vec3 localNormal = normal;

    if (useSkinning)
    {
        // Skinning lineal (LBS): mezcla de las matrices finales de los huesos que afectan al vértice
        mat4 skin = mat4(0.0);
        float total = 0.0;
        for (int i = 0; i < 4; i++)
        {
            if (boneWeights[i] > 0.0)
            {
                skin += finalBonesMatrices[boneIds[i]] * boneWeights[i];
                total += boneWeights[i];
            }
        }
        if (total < 1e-4)
            skin = mat4(1.0);                 // Vértice sin pesos: se queda en su sitio

        localPos = skin * localPos;
        localNormal = normalize(mat3(skin) * normal);
    }

    gl_Position = projection * view *  model * localPos;
    FragPos = vec3(model * localPos);
    Normal = normalMatrix * localNormal;
    TexCoords = texCoords;
    VertexColor = vertexColor;
}
