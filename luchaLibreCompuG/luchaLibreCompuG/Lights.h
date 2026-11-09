#pragma once

// Luces OpenGL que realmente iluminan la escena (Shaders/lighting.frag).
// Son independientes de los MODELOS de reflectores/luminarias: un reflector dibujado no
// ilumina nada por sí mismo. Para que "encienda", se coloca una SpotLight con la misma
// posición y dirección (ver ArenaLayout.h -> BuildLightRig). Los reflectores de la rampa
// actualizan la posición y dirección de su SpotLight cada cuadro (UpdateLightFixtures).

#include <string>
#include <vector>
#include <GL/glew.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "Shader.h"

const int NUMBER_OF_POINT_LIGHTS = 4; // Debe coincidir con el #define del shader
const int MAX_SPOT_LIGHTS = 8;   // 4 luminarias del ring + 2 reflectores de la rampa (+2 libres)

struct DirLightParams
{
	glm::vec3 direction = glm::vec3(-0.2f, -1.0f, -0.3f);
	glm::vec3 ambient = glm::vec3(0.2f);
	glm::vec3 diffuse = glm::vec3(0.5f);
	glm::vec3 specular = glm::vec3(0.3f);
};

struct PointLightParams
{
	std::string id;
	glm::vec3 position = glm::vec3(0.0f);
	glm::vec3 ambient = glm::vec3(0.0f);
	glm::vec3 diffuse = glm::vec3(1.0f);
	glm::vec3 specular = glm::vec3(1.0f);
	float constant = 1.0f;
	float linear = 0.09f;
	float quadratic = 0.032f;
	bool enabled = true;
};

struct SpotLightParams
{
	std::string id;
	glm::vec3 position = glm::vec3(0.0f);
	glm::vec3 direction = glm::vec3(0.0f, -1.0f, 0.0f);
	float cutOffDeg = 15.0f;       // Cono interior (intensidad completa)
	float outerCutOffDeg = 22.0f;  // Cono exterior (difuminado)
	glm::vec3 ambient = glm::vec3(0.0f);
	glm::vec3 diffuse = glm::vec3(1.0f);
	glm::vec3 specular = glm::vec3(1.0f);
	float constant = 1.0f;
	float linear = 0.045f;
	float quadratic = 0.0075f;
	bool enabled = true;
};

struct LightRig
{
	DirLightParams dirLight;
	PointLightParams pointLights[NUMBER_OF_POINT_LIGHTS];
	std::vector<SpotLightParams> spotLights;

	void Apply(Shader& shader) const
	{
		GLuint p = shader.Program;
		auto vec3 = [p](const std::string& name, const glm::vec3& v) { glUniform3fv(glGetUniformLocation(p, name.c_str()), 1, glm::value_ptr(v)); };
		auto flt = [p](const std::string& name, float v) { glUniform1f(glGetUniformLocation(p, name.c_str()), v); };

		vec3("dirLight.direction", this->dirLight.direction);
		vec3("dirLight.ambient", this->dirLight.ambient);
		vec3("dirLight.diffuse", this->dirLight.diffuse);
		vec3("dirLight.specular", this->dirLight.specular);

		// Una luz apagada se envía con color negro (el shader siempre recorre las 4)
		for (int i = 0; i < NUMBER_OF_POINT_LIGHTS; i++)
		{
			const PointLightParams& l = this->pointLights[i];
			std::string base = "pointLights[" + std::to_string(i) + "].";
			glm::vec3 on = glm::vec3(l.enabled ? 1.0f : 0.0f);
			vec3(base + "position", l.position);
			vec3(base + "ambient", l.ambient * on);
			vec3(base + "diffuse", l.diffuse * on);
			vec3(base + "specular", l.specular * on);
			flt(base + "constant", l.constant);
			flt(base + "linear", l.linear);
			flt(base + "quadratic", l.quadratic);
		}

		int count = 0;
		for (const SpotLightParams& l : this->spotLights)
		{
			if (!l.enabled || count >= MAX_SPOT_LIGHTS) continue;
			std::string base = "spotLights[" + std::to_string(count++) + "].";
			vec3(base + "position", l.position);
			vec3(base + "direction", glm::normalize(l.direction));
			flt(base + "cutOff", cos(glm::radians(l.cutOffDeg)));
			flt(base + "outerCutOff", cos(glm::radians(l.outerCutOffDeg)));
			vec3(base + "ambient", l.ambient);
			vec3(base + "diffuse", l.diffuse);
			vec3(base + "specular", l.specular);
			flt(base + "constant", l.constant);
			flt(base + "linear", l.linear);
			flt(base + "quadratic", l.quadratic);
		}
		glUniform1i(glGetUniformLocation(p, "numSpotLights"), count);
	}
};
