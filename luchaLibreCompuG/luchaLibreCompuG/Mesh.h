#pragma once

#include <string>
#include <iostream>
#include <vector>
#include <utility>
#include <cmath>
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
	// Skinning: hasta 4 huesos por vértice (índice en SkeletonData::bones y peso). Sin animar: pesos 0.
	glm::ivec4 BoneIDs = glm::ivec4(0);
	glm::vec4 Weights = glm::vec4(0.0f);
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
	bool skinned = false;     // Malla animada: el vertex shader aplica finalBonesMatrices (lo fija Model al cargar)
	bool glow = false;        // Haz de luz: sin iluminación, color emisivo * objectGlow, alfa de la textura

	bool IsTransparent() const { return opacity < 0.999f; }
};

// Ubicaciones de los uniforms del material, consultadas una sola vez por programa
// (evita cientos de glGetUniformLocation por cuadro cuando hay muchas instancias).
struct MaterialUniforms
{
	GLint diffuseMap, specularMap, hasDiffuseMap, hasSpecularMap;
	GLint diffuseColor, specularColor, emissiveColor, shininess, opacity, alphaTest, glow, useSkinning;

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
		u.glow = glGetUniformLocation(program, "material.glow");
			u.useSkinning = glGetUniformLocation(program, "useSkinning");   // -1 en shaders sin skinning: glUniform lo ignora
		return cache[program] = u;
	}
};

// Shape key (morph target): desplazamientos respecto a la malla base, por vértice
struct MorphTarget
{
	std::string name;
	float defaultWeight = 0.0f;
	std::vector<glm::vec3> dPos;      // Un valor por vértice
	std::vector<glm::vec3> dNormal;   // Vacío si el archivo no trae normales del objetivo
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

	// Sustituye la caja envolvente (en mallas animadas se calcula con la pose de reposo, no con los
	// vértices crudos, que están en el espacio local de la malla)
	void SetBounds(const glm::vec3& mn, const glm::vec3& mx)
	{
		this->boundsMin = mn;
		this->boundsMax = mx;
	}

	// ---- Shape keys ----
	// Se morfea en CPU (posición y normal) y se re-sube el VBO solo cuando cambian los pesos.
	// Si la malla además tiene huesos, primero se morfea y luego el shader aplica el skinning (orden de glTF).
	void SetMorph(std::vector<MorphTarget> targets, const std::vector<Vertex>& vertices)
	{
		if (targets.empty() || this->VBO == 0) return;
		this->morphTargets = std::move(targets);
		this->morphBase = vertices;
		this->morphWork = vertices;
		this->morphApplied.clear();
		glBindBuffer(GL_ARRAY_BUFFER, this->VBO);
		glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(Vertex), vertices.data(), GL_DYNAMIC_DRAW);
		glBindBuffer(GL_ARRAY_BUFFER, 0);
	}

	bool HasMorph() const { return !this->morphTargets.empty(); }

	std::vector<float> GetMorphDefaults() const
	{
		std::vector<float> w;
		for (const MorphTarget& t : this->morphTargets) w.push_back(t.defaultWeight);
		return w;
	}

	void ApplyMorph(const std::vector<float>& w)
	{
		if (this->morphTargets.empty() || this->VBO == 0 || w == this->morphApplied) return;
		this->morphApplied = w;

		// Solo los objetivos con peso: en una simulación horneada cuadro a cuadro (un objetivo por
		// cuadro) hay cientos de objetivos pero solo 2 activos a la vez
		std::vector<std::pair<const MorphTarget*, float>> active;
		for (size_t t = 0; t < this->morphTargets.size() && t < w.size(); t++)
			if (fabsf(w[t]) > 1e-5f)
				active.emplace_back(&this->morphTargets[t], w[t]);

		for (size_t v = 0; v < this->morphBase.size(); v++)
		{
			glm::vec3 p = this->morphBase[v].Position, n = this->morphBase[v].Normal;
			for (const auto& a : active)
			{
				p += a.second * a.first->dPos[v];
				if (!a.first->dNormal.empty())
					n += a.second * a.first->dNormal[v];
			}
			this->morphWork[v].Position = p;
			float len = glm::length(n);
			this->morphWork[v].Normal = len > 1e-8f ? n / len : this->morphBase[v].Normal;
		}
		glBindBuffer(GL_ARRAY_BUFFER, this->VBO);
		glBufferSubData(GL_ARRAY_BUFFER, 0, this->morphWork.size() * sizeof(Vertex), this->morphWork.data());
		glBindBuffer(GL_ARRAY_BUFFER, 0);
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
		glUniform1i(u.glow, m.glow);
		glUniform1i(u.useSkinning, m.skinned);

		// Draw mesh
		glBindVertexArray(this->VAO);
		glDrawElements(GL_TRIANGLES, this->indexCount, GL_UNSIGNED_INT, 0);
		glBindVertexArray(0);
		glActiveTexture(GL_TEXTURE0);
	}

private:
	/*  Render data  */
	GLuint VAO = 0, VBO = 0, EBO = 0;
	std::vector<MorphTarget> morphTargets;
	std::vector<Vertex> morphBase, morphWork;
	std::vector<float> morphApplied;

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
		// Bone IDs (enteros: glVertexAttribIPointer, no se convierten a float) y pesos
		glEnableVertexAttribArray(4);
		glVertexAttribIPointer(4, 4, GL_INT, sizeof(Vertex), (GLvoid *)offsetof(Vertex, BoneIDs));
		glEnableVertexAttribArray(5);
		glVertexAttribPointer(5, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), (GLvoid *)offsetof(Vertex, Weights));

		glBindVertexArray(0);
	}
};
