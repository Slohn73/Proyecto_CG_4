/*
 PROYECTO FINAL: Lucha Libre Mexicana - "Little Endians"
 Computación Gráfica e Interacción Humano-Computadora, Grupo 04
*/

#include <iostream>
#include <cmath>
#include <vector>

// Librería GLEW
#include <GL/glew.h>

// Librería GLFW
#include <GLFW/glfw3.h>

// Carga de imágenes / texturas
#include "stb_image.h"

// Matemáticas GLM
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

// Librería de carga de imágenes alternativa
#include "SOIL2/SOIL2.h"

// Módulos internos
#include "Shader.h"
#include "Camera.h"
#include "Model.h"

// Prototipos de funciones
void KeyCallback(GLFWwindow* window, int key, int scancode, int action, int mode);
void MouseCallback(GLFWwindow* window, double xPos, double yPos);
void DoMovement();
void Animation();
GLuint LoadCubemap(std::vector<std::string> faces); // Función para cargar las 6 caras del Cubemap

// Dimensiones de la ventana
const GLuint WIDTH = 1280, HEIGHT = 720;
int SCREEN_WIDTH, SCREEN_HEIGHT;

// Configuración de la Cámara
Camera camera(glm::vec3(0.0f, 2.0f, 10.0f));
GLfloat lastX = WIDTH / 2.0f;
GLfloat lastY = HEIGHT / 2.0f;
bool keys[1024];
bool firstMouse = true;

// DeltaTime para renderizado independiente de los FPS
GLfloat deltaTime = 0.0f;
GLfloat lastFrame = 0.0f;

// Variables de iluminación y control en tiempo real
glm::vec3 lightPos(0.0f, 5.0f, 0.0f);
bool luzEscenarioActiva = true;
glm::vec3 colorLuzSpot(1.0f, 1.0f, 1.0f);

// Posiciones de luces puntuales (Reflectores del Ring)
glm::vec3 pointLightPositions[] = {
	glm::vec3(0.0f, 8.0f, 0.0f),
	glm::vec3(-5.0f, 4.0f, 5.0f),
	glm::vec3(5.0f, 4.0f, 5.0f),
	glm::vec3(0.0f, 4.0f, -5.0f)
};

// Variables para animaciones por código y procedurales
float rotacionCinturon = 0.0f;
float movimientoReflector = 0.0f;
float escalaBocina = 1.0f;

// Variables para la animación del Burrito Volador (Idea del equipo)
bool burritoLanzado = false;
glm::vec3 posicionBurrito(-6.0f, 1.5f, 6.0f);
float tiempoVueloBurrito = 0.0f;

// Estados de animación por teclado / input
bool animacionLuchador1 = false;
bool animacionArbitro = false;
bool animacionPantalla = false;

int main()
{
	// Inicialización de GLFW
	glfwInit();
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
	glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
	glfwWindowHint(GLFW_RESIZABLE, GL_FALSE);

	GLFWwindow* window = glfwCreateWindow(WIDTH, HEIGHT, "Proyecto Final CG: Lucha Libre Mexicana", nullptr, nullptr);

	if (nullptr == window)
	{
		std::cout << "Error al crear la ventana de GLFW" << std::endl;
		glfwTerminate();
		return EXIT_FAILURE;
	}

	glfwMakeContextCurrent(window);
	glfwGetFramebufferSize(window, &SCREEN_WIDTH, &SCREEN_HEIGHT);

	glfwSetKeyCallback(window, KeyCallback);
	glfwSetCursorPosCallback(window, MouseCallback);
	glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

	glewExperimental = GL_TRUE;
	if (GLEW_OK != glewInit())
	{
		std::cout << "Error al inicializar GLEW" << std::endl;
		return EXIT_FAILURE;
	}

	glViewport(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
	glEnable(GL_DEPTH_TEST);

	// Carga de Shaders del proyecto
	Shader lightingShader("Shaders/lighting.vs", "Shaders/lighting.frag");
	Shader lampShader("Shaders/lamp.vs", "Shaders/lamp.frag");
	Shader skyboxShader("Shaders/skybox.vs", "Shaders/skybox.frag");

	// Vértices del cubo para el Skybox
	float skyboxVertices[] = {
		// Positions          
		-1.0f,  1.0f, -1.0f,
		-1.0f, -1.0f, -1.0f,
		 1.0f, -1.0f, -1.0f,
		 1.0f, -1.0f, -1.0f,
		 1.0f,  1.0f, -1.0f,
		-1.0f,  1.0f, -1.0f,

		-1.0f, -1.0f,  1.0f,
		-1.0f, -1.0f, -1.0f,
		-1.0f,  1.0f, -1.0f,
		-1.0f,  1.0f, -1.0f,
		-1.0f,  1.0f,  1.0f,
		-1.0f, -1.0f,  1.0f,

		 1.0f, -1.0f, -1.0f,
		 1.0f, -1.0f,  1.0f,
		 1.0f,  1.0f,  1.0f,
		 1.0f,  1.0f,  1.0f,
		 1.0f,  1.0f, -1.0f,
		 1.0f, -1.0f, -1.0f,

		-1.0f, -1.0f,  1.0f,
		-1.0f,  1.0f,  1.0f,
		 1.0f,  1.0f,  1.0f,
		 1.0f,  1.0f,  1.0f,
		 1.0f, -1.0f,  1.0f,
		-1.0f, -1.0f,  1.0f,

		-1.0f,  1.0f, -1.0f,
		 1.0f,  1.0f, -1.0f,
		 1.0f,  1.0f,  1.0f,
		 1.0f,  1.0f,  1.0f,
		-1.0f,  1.0f,  1.0f,
		-1.0f,  1.0f, -1.0f,

		-1.0f, -1.0f, -1.0f,
		-1.0f, -1.0f,  1.0f,
		 1.0f, -1.0f, -1.0f,
		 1.0f, -1.0f, -1.0f,
		-1.0f, -1.0f,  1.0f,
		 1.0f, -1.0f,  1.0f
	};

	// Configuración de VAO y VBO para el Skybox
	GLuint skyboxVAO, skyboxVBO;
	glGenVertexArrays(1, &skyboxVAO);
	glGenBuffers(1, &skyboxVBO);
	glBindVertexArray(skyboxVAO);
	glBindBuffer(GL_ARRAY_BUFFER, skyboxVBO);
	glBufferData(GL_ARRAY_BUFFER, sizeof(skyboxVertices), &skyboxVertices, GL_STATIC_DRAW);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(GLfloat), (GLvoid*)0);
	glBindVertexArray(0);

	// Carga de las 6 imágenes de la Skybox
	std::vector<std::string> faces;
	faces.push_back("Skybox/skybox_right.png");
	faces.push_back("Skybox/skybox_left.png");
	faces.push_back("Skybox/skybox_top.png");
	faces.push_back("Skybox/skybox_bottom.png");
	faces.push_back("Skybox/skybox_back.png");
	faces.push_back("Skybox/skybox_front.png");

	GLuint cubemapTexture = LoadCubemap(faces);

	// Carga de modelos 3D
	std::cout << "Cargando modelos 3D del escenario..." << std::endl;

	// Modelos propios
	Model Ring((char*)"modelos/modelos_propios/Ring/Ring.obj");
	Model Gradas((char*)"modelos/modelos_propios/Gradas/Gradas.obj");
	Model Rampa((char*)"modelos/modelos_propios/rampa_arco/rampa_lucha_libre_texturizada.obj"); //Incluye rampa y arco ?
	Model Barrera((char*)"modelos/modelos_propios/barrera/barrera.obj");
	Model Marcador((char*)"modelos/modelos_propios/marcador/marcadorBueno.obj");
	Model Bocina((char*)"modelos/modelos_propios/bocina/bocina.obj");
	Model Silla((char*)"modelos/modelos_propios/silla_metalica/silla_metalica.fbx");

	// Modelos importados
	//Model Luchador1((char*)"modelos/modelos_importados/luchador/source/Luchador_SKETCHFAB/Luchador_001.fbx");
	//Model Luchador2((char*)"modelos/modelos_importados/penta/source/Taunt.fbx");
	//Model Arbitro((char*)"modelos/modelos_importados/arbitro-ruper-design/source/manga ralgla_cuello en v.fbx");
	////Model Espectadores((char*)"modelos/modelos_importados/??????.obj");
	////Model Minion((char*)"modelos/modelos_importados/?????.obj");
	//Model Mascara((char*)"modelos/modelos_importados/lucha-libre-mask/source/LuchaLibreOptimized/LuchaLibreOptimized.obj");
	////Model Cinturon((char*)"modelos/modelos_importados/???????.obj");
	//Model LucesSpot((char*)"modelos/modelos_importados/ceiling-lights/source/PTRCUNWM24VXBNKZ/ASSET.fbx");
	//Model Spotlight((char*)"modelos/modelos_importados/simple-spotlight/source/Spotlight/source/Spotlight.fbx");
	//Model CamarasTV((char*)"modelos/modelos_importados/set-of-four-4-low-poly-cameras/source/allCameras-Avatars-merged.glb");
	////Model Mesa((char*)"modelos/modelos_importados/mesas/scene.gltf"); falta separar mesas
	//Model BanderaMex((char*)"modelos/modelos_importados/mexican-flag/source/FB SP MexFlag.fbx");
	//Model Pan((char*)"modelos/modelos_importados/pan-de-muerto/source/Pan_De_Muerto_.obj");
	//Model Burrito((char*)"modelos/modelos_importados/burrito/source/LP Burrito.obj");

	lightingShader.Use();
	glUniform1i(glGetUniformLocation(lightingShader.Program, "Material.diffuse"), 0);
	glUniform1i(glGetUniformLocation(lightingShader.Program, "Material.specular"), 1);

	glm::mat4 projection = glm::perspective(camera.GetZoom(), (GLfloat)SCREEN_WIDTH / (GLfloat)SCREEN_HEIGHT, 0.1f, 100.0f);

	// Bucle principal de renderizado
	while (!glfwWindowShouldClose(window))
	{
		GLfloat currentFrame = glfwGetTime();
		deltaTime = currentFrame - lastFrame;
		lastFrame = currentFrame;

		glfwPollEvents();
		DoMovement();
		Animation();

		glClearColor(0.05f, 0.05f, 0.08f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		lightingShader.Use();

		GLint viewPosLoc = glGetUniformLocation(lightingShader.Program, "viewPos");
		glUniform3f(viewPosLoc, camera.GetPosition().x, camera.GetPosition().y, camera.GetPosition().z);

		// Configuración de iluminación direccional
		glUniform3f(glGetUniformLocation(lightingShader.Program, "dirLight.direction"), -0.2f, -1.0f, -0.3f);
		glUniform3f(glGetUniformLocation(lightingShader.Program, "dirLight.ambient"), 0.2f, 0.2f, 0.25f);
		glUniform3f(glGetUniformLocation(lightingShader.Program, "dirLight.diffuse"), 0.5f, 0.5f, 0.5f);
		glUniform3f(glGetUniformLocation(lightingShader.Program, "dirLight.specular"), 0.3f, 0.3f, 0.3f);

		// Configuración de luces puntuales (reflectores)
		for (int i = 0; i < 4; i++) {
			std::string baseName = "pointLights[" + std::to_string(i) + "].";
			glUniform3f(glGetUniformLocation(lightingShader.Program, (baseName + "position").c_str()), pointLightPositions[i].x, pointLightPositions[i].y, pointLightPositions[i].z);
			glUniform3f(glGetUniformLocation(lightingShader.Program, (baseName + "ambient").c_str()), 0.1f, 0.1f, 0.1f);
			glUniform3f(glGetUniformLocation(lightingShader.Program, (baseName + "diffuse").c_str()), colorLuzSpot.r, colorLuzSpot.g, colorLuzSpot.b);
			glUniform3f(glGetUniformLocation(lightingShader.Program, (baseName + "specular").c_str()), 1.0f, 1.0f, 1.0f);
			glUniform1f(glGetUniformLocation(lightingShader.Program, (baseName + "constant").c_str()), 1.0f);
			glUniform1f(glGetUniformLocation(lightingShader.Program, (baseName + "linear").c_str()), 0.09f);
			glUniform1f(glGetUniformLocation(lightingShader.Program, (baseName + "quadratic").c_str()), 0.032f);
		}

		glUniform1f(glGetUniformLocation(lightingShader.Program, "material.shininess"), 32.0f);

		glm::mat4 view = camera.GetViewMatrix();
		GLint modelLoc = glGetUniformLocation(lightingShader.Program, "model");
		GLint viewLoc = glGetUniformLocation(lightingShader.Program, "view");
		GLint projLoc = glGetUniformLocation(lightingShader.Program, "projection");

		glUniformMatrix4fv(viewLoc, 1, GL_FALSE, glm::value_ptr(view));
		glUniformMatrix4fv(projLoc, 1, GL_FALSE, glm::value_ptr(projection));

		glm::mat4 model(1.0f);

		// Renderizado del Ring
		model = glm::mat4(1.0f);
		glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(model));
		Ring.Draw(lightingShader);

		// Renderizado de las Gradas
		model = glm::mat4(1.0f);
		model = glm::translate(model, glm::vec3(-10.0f, 0.0f, 0.0f));
		glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(model));
		Gradas.Draw(lightingShader);

		// Renderizado de las Bocinas con animación procedural de escala
		model = glm::mat4(1.0f);
		model = glm::translate(model, glm::vec3(5.0f, 0.0f, -8.0f));
		model = glm::scale(model, glm::vec3(escalaBocina));
		glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(model));
		Bocina.Draw(lightingShader);

		// Renderizado del Cinturón con animación por código de rotación
		model = glm::mat4(1.0f);
		model = glm::translate(model, glm::vec3(0.0f, 3.5f, 0.0f));
		model = glm::rotate(model, glm::radians(rotacionCinturon), glm::vec3(0.0f, 1.0f, 0.0f));
		glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(model));
		// Cinturon.Draw(lightingShader); // Descomentar al habilitar el modelo

		// Renderizado condicional del Burrito Volador cuando el usuario presiona la tecla B
		if (burritoLanzado) {
			model = glm::mat4(1.0f);
			model = glm::translate(model, posicionBurrito);
			model = glm::scale(model, glm::vec3(0.3f));
			glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(model));
			// Burrito.Draw(lightingShader); // Descomentar al habilitar el modelo
		}

		// RENDERIZADO DEL SKYBOX 
		glDepthFunc(GL_LEQUAL);  // Permite que el skybox se dibuje al fondo (profundidad máxima)

		glDisable(GL_CULL_FACE); // Desactiva el corte de caras para ver el interior del cubo

		skyboxShader.Use();

		glm::mat4 viewSkybox = glm::mat4(glm::mat3(camera.GetViewMatrix()));
		glUniformMatrix4fv(glGetUniformLocation(skyboxShader.Program, "view"), 1, GL_FALSE, glm::value_ptr(viewSkybox));
		glUniformMatrix4fv(glGetUniformLocation(skyboxShader.Program, "projection"), 1, GL_FALSE, glm::value_ptr(projection));

		glBindVertexArray(skyboxVAO);
		glActiveTexture(GL_TEXTURE0);
		glUniform1i(glGetUniformLocation(skyboxShader.Program, "skybox"), 0);
		glBindTexture(GL_TEXTURE_CUBE_MAP, cubemapTexture);
		glDrawArrays(GL_TRIANGLES, 0, 36);
		glBindVertexArray(0);

		glEnable(GL_CULL_FACE);  // Activar el culling para los demás modelos 3D
		glDepthFunc(GL_LESS);    // Restaura la función de profundidad estándar

		glfwSwapBuffers(window);
	}

	glfwTerminate();
	return 0;
}

// ------ FUNCIONES ------

GLuint LoadCubemap(std::vector<std::string> faces)
{
	GLuint textureID;
	glGenTextures(1, &textureID);
	glBindTexture(GL_TEXTURE_CUBE_MAP, textureID);

	int width, height, nrChannels;
	for (unsigned int i = 0; i < faces.size(); i++)
	{
		unsigned char* data = stbi_load(faces[i].c_str(), &width, &height, &nrChannels, 0);
		if (data)
		{
			glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i,
				0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, data
			);
			stbi_image_free(data);
		}
		else
		{
			std::cout << "Fallo al cargar la textura de cubemap en la ruta: " << faces[i] << std::endl;
			stbi_image_free(data);
		}
	}
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

	return textureID;
}

// Función para mover la cámara mediante teclado
void DoMovement()
{
	if (keys[GLFW_KEY_W] || keys[GLFW_KEY_UP])
		camera.ProcessKeyboard(FORWARD, deltaTime);

	if (keys[GLFW_KEY_S] || keys[GLFW_KEY_DOWN])
		camera.ProcessKeyboard(BACKWARD, deltaTime);

	if (keys[GLFW_KEY_A] || keys[GLFW_KEY_LEFT])
		camera.ProcessKeyboard(LEFT, deltaTime);

	if (keys[GLFW_KEY_D] || keys[GLFW_KEY_RIGHT])
		camera.ProcessKeyboard(RIGHT, deltaTime);
}

// Lógica de animaciones automáticas y por código
void Animation()
{
	// Rotación continua del cinturón de campeonato
	rotacionCinturon += 45.0f * deltaTime;
	if (rotacionCinturon >= 360.0f) rotacionCinturon -= 360.0f;

	// Vibración procedural de las bocinas utilizando una función senoidal
	escalaBocina = 1.0f + 0.05f * sin(glfwGetTime() * 10.0f);

	// Actualización de la trayectoria parabólica del burrito volador
	if (burritoLanzado) {
		tiempoVueloBurrito += deltaTime * 2.0f;

		posicionBurrito.x = -6.0f + 6.0f * tiempoVueloBurrito;
		posicionBurrito.z = 6.0f - 6.0f * tiempoVueloBurrito;
		posicionBurrito.y = 1.5f + 4.0f * tiempoVueloBurrito - 4.9f * pow(tiempoVueloBurrito, 2);

		if (tiempoVueloBurrito >= 1.0f) {
			burritoLanzado = false;
			tiempoVueloBurrito = 0.0f;
			posicionBurrito = glm::vec3(-6.0f, 1.5f, 6.0f);
		}
	}
}

// Función callback para detectar eventos de teclado (Inputs interactivos)
void KeyCallback(GLFWwindow* window, int key, int scancode, int action, int mode)
{
	if (GLFW_KEY_ESCAPE == key && GLFW_PRESS == action)
		glfwSetWindowShouldClose(window, GL_TRUE);

	if (key >= 0 && key < 1024)
	{
		if (action == GLFW_PRESS)
			keys[key] = true;
		else if (action == GLFW_RELEASE)
			keys[key] = false;
	}

	if (action == GLFW_PRESS) {

		// Input 1: Lanzar el burrito desde el público hacia el ring al presionar 'B'
		if (key == GLFW_KEY_B) {
			burritoLanzado = true;
			tiempoVueloBurrito = 0.0f;
		}

		// Input 2: Cambiar color de luces en tiempo real al presionar 'L'
		if (key == GLFW_KEY_L) {
			luzEscenarioActiva = !luzEscenarioActiva;
			colorLuzSpot = luzEscenarioActiva ? glm::vec3(1.0f, 0.2f, 0.2f) : glm::vec3(0.2f, 0.2f, 1.0f);
		}

		// Input 3: Activar animación del árbitro con la tecla 'A'
		if (key == GLFW_KEY_A) {
			animacionArbitro = !animacionArbitro;
		}

		// Input 4: Activar ataque del luchador con la barra espaciadora
		if (key == GLFW_KEY_SPACE) {
			animacionLuchador1 = true;
		}
	}
}

// Callback para el movimiento del mouse (Navegación libre en primera persona)
void MouseCallback(GLFWwindow* window, double xPos, double yPos)
{
	if (firstMouse)
	{
		lastX = xPos;
		lastY = yPos;
		firstMouse = false;
	}

	GLfloat xOffset = xPos - lastX;
	GLfloat yOffset = lastY - yPos;

	lastX = xPos;
	lastY = yPos;

	camera.ProcessMouseMovement(xOffset, yOffset);
}