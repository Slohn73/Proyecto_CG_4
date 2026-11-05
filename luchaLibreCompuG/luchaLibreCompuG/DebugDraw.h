#pragma once

// Ayudas visuales de depuración: cuadrícula del suelo, ejes del mundo y cajas envolventes.
// Solo se dibujan cuando se activan con el teclado; no forman parte del render final.

#include <vector>
#include <GL/glew.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "Shader.h"

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

private:
	Shader shader;
	GLuint gridVAO = 0, axesVAO = 0, boxVAO = 0;
	GLsizei gridVertexCount = 0, axesVertexCount = 0, boxVertexCount = 0;

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
