#pragma once

#include <string>
#include <iostream>
#include <vector>
#include <map>
#include <cfloat>

#include <GL/glew.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "Shader.h"
#include "Texture.h"

using namespace std;

struct Vertex
{
	// Position
	glm::vec3 Position;
	// Normal
	glm::vec3 Normal;
	// TexCoords
	glm::vec2 TexCoords;
	// Color por vértice: multiplica al color base (lo usan las mallas unidas de varios materiales)
	glm::vec3 Color = glm::vec3(1.0f);
};

// Propiedades del material que usa Shaders/lighting.frag (struct Material).
// Si un mapa no existe su id es 0 y el shader usa el color correspondiente.
struct Material
{
	string name;
	glm::vec3 diffuseColor = glm::vec3(0.8f);
	glm::vec3 specularColor = glm::vec3(0.5f);
	glm::vec3 emissiveColor = glm::vec3(0.0f);
	float shininess = 32.0f;
	float opacity = 1.0f;
	GLuint diffuseMap = 0;
	GLuint specularMap = 0;
	bool alphaTest = false;   // Descarta fragmentos con alfa < 0.1 (recortes como hojas o rejillas)

	bool IsTransparent() const { return opacity < 0.999f; }
};

// Ubicaciones de los uniforms del material, consultadas una sola vez por programa
// (evita cientos de glGetUniformLocation por cuadro cuando hay muchas instancias).
struct MaterialUniforms
{
	GLint diffuseMap, specularMap, hasDiffuseMap, hasSpecularMap;
	GLint diffuseColor, specularColor, emissiveColor, shininess, opacity, alphaTest;

	static const MaterialUniforms& For(GLuint program)
	{
		static map<GLuint, MaterialUniforms> cache;
		auto it = cache.find(program);
		if (it != cache.end())
			return it->second;

		MaterialUniforms u;
		u.diffuseMap = glGetUniformLocation(program, "material.diffuse");
		u.specularMap = glGetUniformLocation(program, "material.specular");
		u.hasDiffuseMap = glGetUniformLocation(program, "material.hasDiffuseMap");
		u.hasSpecularMap = glGetUniformLocation(program, "material.hasSpecularMap");
		u.diffuseColor = glGetUniformLocation(program, "material.diffuseColor");
		u.specularColor = glGetUniformLocation(program, "material.specularColor");
		u.emissiveColor = glGetUniformLocation(program, "material.emissive");
		u.shininess = glGetUniformLocation(program, "material.shininess");
		u.opacity = glGetUniformLocation(program, "material.opacity");
		u.alphaTest = glGetUniformLocation(program, "material.alphaTest");
		return cache[program] = u;
	}
};

class Mesh
{
public:
	/*  Mesh Data  */
	string name;             // Nombre del nodo de Assimp al que pertenece (útil para depurar y ocultar partes)
	Material material;
	glm::vec3 boundsMin;     // Caja envolvente en el espacio del modelo (ya con las transformaciones de nodos)
	glm::vec3 boundsMax;
	GLsizei indexCount = 0;

	/*  Functions  */
	// Constructor
	Mesh(const string& name, const vector<Vertex>& vertices, const vector<GLuint>& indices, const Material& material)
		: name(name), material(material)
	{
		this->boundsMin = glm::vec3(FLT_MAX);
		this->boundsMax = glm::vec3(-FLT_MAX);
		for (const Vertex& v : vertices)
		{
			this->boundsMin = glm::min(this->boundsMin, v.Position);
			this->boundsMax = glm::max(this->boundsMax, v.Position);
		}
		this->indexCount = static_cast<GLsizei>(indices.size());

		// Now that we have all the required data, set the vertex buffers and its attribute pointers.
		// Los datos quedan en la GPU; no se conserva copia en CPU.
		this->setupMesh(vertices, indices);
	}

	// Render the mesh
	void Draw(Shader& shader) const
	{
		this->Draw(MaterialUniforms::For(shader.Program));
	}

	void Draw(const MaterialUniforms& u) const
	{
		const Material& m = this->material;

		// Unidad 0: difusa, unidad 1: especular. Si no hay mapa se enlaza una textura blanca.
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, m.diffuseMap ? m.diffuseMap : TextureLoader::White());
		glUniform1i(u.diffuseMap, 0);
		glUniform1i(u.hasDiffuseMap, m.diffuseMap != 0);

		glActiveTexture(GL_TEXTURE1);
		glBindTexture(GL_TEXTURE_2D, m.specularMap ? m.specularMap : TextureLoader::White());
		glUniform1i(u.specularMap, 1);
		glUniform1i(u.hasSpecularMap, m.specularMap != 0);

		glUniform3fv(u.diffuseColor, 1, glm::value_ptr(m.diffuseColor));
		glUniform3fv(u.specularColor, 1, glm::value_ptr(m.specularColor));
		glUniform3fv(u.emissiveColor, 1, glm::value_ptr(m.emissiveColor));
		glUniform1f(u.shininess, m.shininess);
		glUniform1f(u.opacity, m.opacity);
		glUniform1i(u.alphaTest, m.alphaTest);

		// Draw mesh
		glBindVertexArray(this->VAO);
		glDrawElements(GL_TRIANGLES, this->indexCount, GL_UNSIGNED_INT, 0);
		glBindVertexArray(0);
		glActiveTexture(GL_TEXTURE0);
	}

private:
	/*  Render data  */
	GLuint VAO = 0, VBO = 0, EBO = 0;

	/*  Functions    */
	// Initializes all the buffer objects/arrays
	void setupMesh(const vector<Vertex>& vertices, const vector<GLuint>& indices)
	{
		if (vertices.empty() || indices.empty())
			return;

		// Create buffers/arrays
		glGenVertexArrays(1, &this->VAO);
		glGenBuffers(1, &this->VBO);
		glGenBuffers(1, &this->EBO);

		glBindVertexArray(this->VAO);
		// Load data into vertex buffers
		glBindBuffer(GL_ARRAY_BUFFER, this->VBO);
		// A great thing about structs is that their memory layout is sequential for all its items.
		// The effect is that we can simply pass a pointer to the struct and it translates perfectly to a glm::vec3/2 array which
		// again translates to 3/2 floats which translates to a byte array.
		glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(Vertex), &vertices[0], GL_STATIC_DRAW);

		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, this->EBO);
		glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(GLuint), &indices[0], GL_STATIC_DRAW);

		// Set the vertex attribute pointers
		// Vertex Positions
		glEnableVertexAttribArray(0);
		glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (GLvoid *)0);
		// Vertex Normals
		glEnableVertexAttribArray(1);
		glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (GLvoid *)offsetof(Vertex, Normal));
		// Vertex Texture Coords
		glEnableVertexAttribArray(2);
		glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (GLvoid *)offsetof(Vertex, TexCoords));
		// Vertex Colors
		glEnableVertexAttribArray(3);
		glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (GLvoid *)offsetof(Vertex, Color));

		glBindVertexArray(0);
	}
};
