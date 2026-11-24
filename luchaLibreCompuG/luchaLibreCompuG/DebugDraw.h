#pragma once

// Ayudas visuales de depuración: cuadrícula del suelo, ejes del mundo, cajas envolventes y esqueletos.
// Solo se dibujan cuando se activan con el teclado; no forman parte del render final.

#include <vector>
#include <string>
#include <algorithm>
#include <GL/glew.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "Shader.h"
#include "Animation.h"

class DebugDraw
{
public:
	DebugDraw() : shader("Shaders/debug.vs", "Shaders/debug.frag")
	{
		// Cuadrícula de 1 m (líneas cada 5 m más claras), centrada en el origen
		std::vector<float> grid;
		const int half = 40;
		for (int i = -half; i <= half; i++)
		{
			glm::vec3 c = (i == 0) ? glm::vec3(0.55f) : (i % 5 == 0 ? glm::vec3(0.4f) : glm::vec3(0.22f));
			pushLine(grid, glm::vec3((float)i, 0.0f, (float)-half), glm::vec3((float)i, 0.0f, (float)half), c);
			pushLine(grid, glm::vec3((float)-half, 0.0f, (float)i), glm::vec3((float)half, 0.0f, (float)i), c);
		}
		this->gridVertexCount = createVAO(grid, this->gridVAO);

		// Ejes: X rojo, Y verde, Z azul (1 m)
		std::vector<float> axes;
		pushLine(axes, glm::vec3(0.0f), glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(1.0f, 0.1f, 0.1f));
		pushLine(axes, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.1f, 1.0f, 0.1f));
		pushLine(axes, glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 1.0f), glm::vec3(0.2f, 0.4f, 1.0f));
		this->axesVertexCount = createVAO(axes, this->axesVAO);

		// Cubo unitario [0,1]^3 en líneas, se escala a cada caja envolvente
		std::vector<float> box;
		glm::vec3 white(1.0f);
		for (int i = 0; i < 8; i++)
		{
			glm::vec3 a((float)(i & 1), (float)((i >> 1) & 1), (float)((i >> 2) & 1));
			for (int axis = 0; axis < 3; axis++)
			{
				if (((i >> axis) & 1) == 0)
				{
					glm::vec3 b = a;
					b[axis] = 1.0f;
					pushLine(box, a, b, white);
				}
			}
		}
		this->boxVertexCount = createVAO(box, this->boxVAO);
	}

	void Begin(const glm::mat4& view, const glm::mat4& projection)
	{
		this->shader.Use();
		glUniformMatrix4fv(glGetUniformLocation(this->shader.Program, "view"), 1, GL_FALSE, glm::value_ptr(view));
		glUniformMatrix4fv(glGetUniformLocation(this->shader.Program, "projection"), 1, GL_FALSE, glm::value_ptr(projection));
	}

	void DrawGrid()
	{
		// Ligeramente sobre el suelo para no parpadear con él
		draw(this->gridVAO, this->gridVertexCount, glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.01f, 0.0f)), glm::vec3(1.0f));
	}

	void DrawAxes(const glm::mat4& transform, float length)
	{
		draw(this->axesVAO, this->axesVertexCount, glm::scale(transform, glm::vec3(length)), glm::vec3(1.0f));
	}

	// Caja (mn, mx) expresada en el espacio de 'transform' (orientada con el objeto)
	void DrawBox(const glm::mat4& transform, const glm::vec3& mn, const glm::vec3& mx, const glm::vec3& color)
	{
		glm::mat4 m = glm::translate(transform, mn);
		m = glm::scale(m, glm::max(mx - mn, glm::vec3(1e-4f)));
		draw(this->boxVAO, this->boxVertexCount, m, color);
	}

	// Esqueleto en su pose actual: una línea de cada articulación a la de su padre y una cruz en cada
	// articulación. La posición de una articulación es la columna de traslación de su matriz global
	// (global * (0,0,0,1)), llevada al mundo con la matriz del objeto. Se dibuja sin prueba de
	// profundidad para verlo a través de la malla. Lado izquierdo verde, derecho rojo, centro amarillo.
	void DrawSkeleton(const glm::mat4& model, const SkeletonData& skeleton, const std::vector<glm::mat4>& globals, float jointSize)
	{
		const size_t n = std::min(skeleton.nodes.size(), globals.size());
		if (n == 0) return;

		// Raíz del esqueleto: el ancestro común más bajo de todos los huesos (en Mixamo, Hips).
		// Se dibuja todo su subárbol, incluidos los extremos "_end" que no deforman.
		int root = -1;
		for (const BoneSlot& bone : skeleton.bones)
		{
			if (bone.node < 0 || bone.name.rfind("__rigido__", 0) == 0) continue;
			if (root < 0) { root = bone.node; continue; }
			std::vector<int> path;   // Ancestros del hueso
			for (int a = bone.node; a >= 0; a = skeleton.nodes[a].parent) path.push_back(a);
			while (root >= 0 && std::find(path.begin(), path.end(), root) == path.end())
				root = skeleton.nodes[root].parent;
		}
		if (root < 0) return;

		std::vector<char> inSkeleton(n, 0);
		inSkeleton[root] = 1;
		for (size_t i = root + 1; i < n; i++)   // Los padres siempre van antes que sus hijos
			inSkeleton[i] = skeleton.nodes[i].parent >= 0 && inSkeleton[skeleton.nodes[i].parent];

		std::vector<float> lines;
		auto joint = [&](size_t i) { return glm::vec3(model * globals[i] * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f)); };
		for (size_t i = 0; i < n; i++)
		{
			if (!inSkeleton[i]) continue;
			const std::string& name = skeleton.nodes[i].name;
			glm::vec3 color = name.find("Left") != std::string::npos ? glm::vec3(0.2f, 1.0f, 0.3f)
				: name.find("Right") != std::string::npos ? glm::vec3(1.0f, 0.3f, 0.3f) : glm::vec3(1.0f, 0.85f, 0.1f);
			glm::vec3 p = joint(i);
			int parent = skeleton.nodes[i].parent;
			if ((int)i != root && parent >= 0)
				pushLine(lines, joint(parent), p, color);
			for (int axis = 0; axis < 3; axis++)
			{
				glm::vec3 d(0.0f);
				d[axis] = jointSize;
				pushLine(lines, p - d, p + d, glm::vec3(1.0f));
			}
		}

		if (!this->dynamicVAO)
		{
			glGenVertexArrays(1, &this->dynamicVAO);
			glGenBuffers(1, &this->dynamicVBO);
			glBindVertexArray(this->dynamicVAO);
			glBindBuffer(GL_ARRAY_BUFFER, this->dynamicVBO);
			glEnableVertexAttribArray(0);
			glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (GLvoid*)0);
			glEnableVertexAttribArray(1);
			glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (GLvoid*)(3 * sizeof(float)));
			glBindVertexArray(0);
		}
		glBindBuffer(GL_ARRAY_BUFFER, this->dynamicVBO);
		glBufferData(GL_ARRAY_BUFFER, lines.size() * sizeof(float), lines.data(), GL_STREAM_DRAW);
		glBindBuffer(GL_ARRAY_BUFFER, 0);

		glDisable(GL_DEPTH_TEST);
		draw(this->dynamicVAO, static_cast<GLsizei>(lines.size() / 6), glm::mat4(1.0f), glm::vec3(1.0f));
		glEnable(GL_DEPTH_TEST);
	}

private:
	Shader shader;
	GLuint gridVAO = 0, axesVAO = 0, boxVAO = 0;
	GLsizei gridVertexCount = 0, axesVertexCount = 0, boxVertexCount = 0;
	GLuint dynamicVAO = 0, dynamicVBO = 0;   // Líneas que cambian cada cuadro (esqueleto)

	static void pushLine(std::vector<float>& v, const glm::vec3& a, const glm::vec3& b, const glm::vec3& c)
	{
		float data[] = { a.x, a.y, a.z, c.r, c.g, c.b, b.x, b.y, b.z, c.r, c.g, c.b };
		v.insert(v.end(), data, data + 12);
	}

	static GLsizei createVAO(const std::vector<float>& data, GLuint& vao)
	{
		GLuint vbo;
		glGenVertexArrays(1, &vao);
		glGenBuffers(1, &vbo);
		glBindVertexArray(vao);
		glBindBuffer(GL_ARRAY_BUFFER, vbo);
		glBufferData(GL_ARRAY_BUFFER, data.size() * sizeof(float), data.data(), GL_STATIC_DRAW);
		glEnableVertexAttribArray(0);
		glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (GLvoid*)0);
		glEnableVertexAttribArray(1);
		glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (GLvoid*)(3 * sizeof(float)));
		glBindVertexArray(0);
		return static_cast<GLsizei>(data.size() / 6);
	}

	void draw(GLuint vao, GLsizei count, const glm::mat4& model, const glm::vec3& tint)
	{
		glUniformMatrix4fv(glGetUniformLocation(this->shader.Program, "model"), 1, GL_FALSE, glm::value_ptr(model));
		glUniform3fv(glGetUniformLocation(this->shader.Program, "tint"), 1, glm::value_ptr(tint));
		glBindVertexArray(vao);
		glDrawArrays(GL_LINES, 0, count);
		glBindVertexArray(0);
	}
};
