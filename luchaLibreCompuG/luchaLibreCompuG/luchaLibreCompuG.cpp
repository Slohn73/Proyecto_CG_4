/*
 PROYECTO FINAL: Lucha Libre Mexicana - "Little Endians"
 Computación Gráfica e Interacción Humano-Computadora, Grupo 04

 Organización:
   ArenaLayout.h  -> qué modelos se cargan y dónde se colocan (posiciones, rotaciones, escalas),
					 luces y vistas de cámara. Es el archivo a editar para acomodar la escena.
   Scene.h        -> recursos (modelo cargado una vez + normalización) e instancias del escenario.
   Model.h/Mesh.h -> carga con Assimp, materiales, huesos/animaciones y dibujo.
   Lights.h       -> parámetros de las luces OpenGL.
   DebugDraw.h    -> cuadrícula, ejes, cajas envolventes y esqueletos (solo depuración).
   Animation.h    -> esqueleto, clips e interpolación de claves (animación esquelética).
   PentaAnimado.h -> máquina de estados de las animaciones de Penta (entrada y caída).
*/

#include <iostream>
#include <cmath>
#include <vector>
#include <string>
#include <sstream>
#include <iomanip>
#include <algorithm>

// Librería GLEW
#include <GL/glew.h>

// Librería GLFW
#include <GLFW/glfw3.h>

// Matemáticas GLM
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

// Librería de carga de imágenes
#include "SOIL2/SOIL2.h"

// Versión de assimp
#include <assimp/version.h>

// Módulos internos
#include "Shader.h"
#include "Camera.h"
#include "Model.h"
#include "Texture.h"
#include "Scene.h"
#include "Lights.h"
#include "DebugDraw.h"
#include "ArenaLayout.h"
#include "PentaAnimado.h"

// Prototipos de funciones
void KeyCallback(GLFWwindow* window, int key, int scancode, int action, int mode);
void MouseCallback(GLFWwindow* window, double xPos, double yPos);
void ScrollCallback(GLFWwindow* window, double xOffset, double yOffset);
void DoMovement();
void Animation();
void ApplyCameraView(int index);
void SelectObject(int direction);
void UpdateWindowTitle(GLFWwindow* window);
void SaveScreenshot(const std::string& file);
void PrintGLLimits();

// Dimensiones de la ventana
const GLuint WIDTH = 1280, HEIGHT = 720;
int SCREEN_WIDTH, SCREEN_HEIGHT;

// Configuración de la Cámara (la pose inicial se toma de GetCameraViews())
Camera camera(glm::vec3(0.0f, 2.0f, 10.0f));
std::vector<CameraView> cameraViews;
GLfloat lastX = WIDTH / 2.0f;
GLfloat lastY = HEIGHT / 2.0f;
bool keys[1024];
bool firstMouse = true;
bool mouseCaptured = true;

// DeltaTime para renderizado independiente de los FPS
GLfloat deltaTime = 0.0f;
GLfloat lastFrame = 0.0f;

// Escena e iluminación
Scene scene;
LightRig lightRig;
std::vector<LightFixture> lightFixtures;
bool luzEscenarioActiva = true;
glm::vec3 colorLuzSpot(1.0f, 1.0f, 1.0f);

// Herramientas de depuración
bool showDebugGrid = false;
bool showSkeleton = false;     // F8: esqueleto de Penta
int glErrorsReported = 0;
bool showAllBounds = false;
int selectedObject = -1;
int screenshotCounter = 0;
bool screenshotRequested = false;

// Objetos animados (se buscan una sola vez después de construir la escena)
SceneObject* objCinturon = nullptr;
SceneObject* objBurrito = nullptr;
std::vector<SceneObject*> objBocinas;

// Bandera animada (GLB) y modelos con esqueleto/animación (FBX / GLB)
std::vector<SceneObject*> objBanderasAnimadas;
Model* modeloBanderaAnimada = nullptr;
Model* modeloArbitro = nullptr;
Model* modeloLuchador = nullptr;
std::vector<Model*> modelosAnimados;

//minion
Model* modeloMinion = nullptr;
int clipMinion = 0;


Model* modeloReflector = nullptr;

// Variables para animaciones por código y procedurales
float rotacionCinturon = 0.0f;
float escalaBocina = 1.0f;
bool animacionBandera = false;
float tiempoBandera = 0.0f;

// Variables para la animación del Burrito Volador (Idea del equipo)
bool burritoLanzado = false;
float tiempoVueloBurrito = 0.0f;

// Animación del espectador E (Z: lanza la cerveza izquierda, X: bebe la derecha).
SceneObject* objE_cuerpo = nullptr, * objE_antebrazoIzq = nullptr, * objE_manoIzq = nullptr;
SceneObject* objE_brazoDer = nullptr, * objE_antebrazoDer = nullptr, * objE_manoDer = nullptr;
SceneObject* objCervezaIzq = nullptr, * objCervezaDer = nullptr;
const glm::vec3 CODO_IZQ(0.27f, 0.70f, 0.0f);
const glm::vec3 HOMBRO_DER(-0.26f, 0.95f, -0.03f);
const glm::vec3 AGARRE_IZQ(0.14f, 0.57f, 0.12f);
const glm::vec3 AGARRE_DER(-0.14f, 0.57f, 0.12f);
const glm::vec3 CENTRO_BOTELLA(0.0f, 0.12f, 0.0f);
const float SUELTA_CERVEZA = 0.70f;
const float ALTURA_CERVEZA = 2.5f;
bool lanzandoCerveza = false;
bool cervezaEnVuelo = false;
float tiempoLanzamiento = 0.0f;
glm::vec3 inicioCerveza(0.0f);
glm::vec3 giroSuelta(0.0f);
bool bebiendo = false;
float tiempoBebida = 0.0f;

// Cerveza extra sobre la mesa de comentaristas, junto al pan: mira al ring y brinca
SceneObject* objCervezaMesa = nullptr;
glm::vec3 baseCervezaMesa(0.0f);

// Penta con animación esquelética (F5 entrada, F6 caída, F7 reiniciar)
PentaAnimado penta;

// Estados de animación por teclado / input
bool animacionLuchador1 = false;
bool animacionArbitro = false;
bool animacionPantalla = false;

int main(int argc, char** argv)
{
	std::cout << "Assimp " << aiGetVersionMajor() << "." << aiGetVersionMinor() << "." << aiGetVersionPatch() << std::endl;
	bool galleryMode = false;
	bool customCamera = false;
	int initialView = 0;
	glm::vec3 camPos(0.0f);
	float camYaw = 0.0f, camPitch = 0.0f;
	std::string captureFile;
	std::string captureAllPrefix;
	double runSeconds = 0.0;
	double captureAfter = -1.0;      // --captura-tras s: la captura se toma tras s segundos en lugar del cuadro 3
	std::string pentaClip;           // --penta entrada|caida [segundos]
	float pentaSeconds = -1.0f;      // < 0: reproducir desde el inicio; >= 0: pose congelada en ese instante
	for (int i = 1; i < argc; i++)
	{
		std::string arg = argv[i];
		if (arg == "--galeria") galleryMode = true;
		else if (arg == "--esqueleto") showSkeleton = true;
		else if (arg == "--penta" && i + 1 < argc)
		{
			pentaClip = argv[++i];
			if (i + 1 < argc && argv[i + 1][0] != '-')
				pentaSeconds = (float)atof(argv[++i]);
		}
		else if (arg == "--depuracion") showDebugGrid = showAllBounds = true;
		else if (arg == "--vista" && i + 1 < argc) initialView = atoi(argv[++i]) - 1;
		else if (arg == "--captura" && i + 1 < argc) captureFile = argv[++i];
		else if (arg == "--captura-tras" && i + 1 < argc) captureAfter = atof(argv[++i]);
		else if (arg == "--captura-todas" && i + 1 < argc) captureAllPrefix = argv[++i];
		else if (arg == "--segundos" && i + 1 < argc) runSeconds = atof(argv[++i]);
		else if (arg == "--camara" && i + 5 < argc)
		{
			customCamera = true;
			camPos = glm::vec3((float)atof(argv[i + 1]), (float)atof(argv[i + 2]), (float)atof(argv[i + 3]));
			camYaw = (float)atof(argv[i + 4]);
			camPitch = (float)atof(argv[i + 5]);
			i += 5;
		}
	}

	glfwInit();
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
	glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
	glfwWindowHint(GLFW_RESIZABLE, GL_FALSE);
	glfwWindowHint(GLFW_SAMPLES, 4);

	GLFWwindow* window = glfwCreateWindow(WIDTH, HEIGHT, "Proyecto Final CG: Lucha Libre Mexicana", nullptr, nullptr);
	if (nullptr == window)
	{
		std::cout << "Error al crear la ventana de GLFW" << std::endl;
		glfwTerminate();
		return EXIT_FAILURE;
	}

	glfwMakeContextCurrent(window);
	glfwSwapInterval(1);   // sincroniza con el monitor (60 fps típicamente)
	glfwGetFramebufferSize(window, &SCREEN_WIDTH, &SCREEN_HEIGHT);

	glfwSetKeyCallback(window, KeyCallback);
	glfwSetCursorPosCallback(window, MouseCallback);
	glfwSetScrollCallback(window, ScrollCallback);
	glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

	glewExperimental = GL_TRUE;
	if (GLEW_OK != glewInit())
	{
		std::cout << "Error al inicializar GLEW" << std::endl;
		return EXIT_FAILURE;
	}

	PrintGLLimits();
	glViewport(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
	glEnable(GL_DEPTH_TEST);
	glEnable(GL_MULTISAMPLE);
	glDisable(GL_CULL_FACE);

	Shader lightingShader("Shaders/lighting.vs", "Shaders/lighting.frag");
	Shader skyboxShader("Shaders/skybox.vs", "Shaders/skybox.frag");
	DebugDraw debugDraw;

	float skyboxVertices[] = {
		-1.0f,  1.0f, -1.0f, -1.0f, -1.0f, -1.0f,  1.0f, -1.0f, -1.0f,
		 1.0f, -1.0f, -1.0f,  1.0f,  1.0f, -1.0f, -1.0f,  1.0f, -1.0f,
		-1.0f, -1.0f,  1.0f, -1.0f, -1.0f, -1.0f, -1.0f,  1.0f, -1.0f,
		-1.0f,  1.0f, -1.0f, -1.0f,  1.0f,  1.0f, -1.0f, -1.0f,  1.0f,
		 1.0f, -1.0f, -1.0f,  1.0f, -1.0f,  1.0f,  1.0f,  1.0f,  1.0f,
		 1.0f,  1.0f,  1.0f,  1.0f,  1.0f, -1.0f,  1.0f, -1.0f, -1.0f,
		-1.0f, -1.0f,  1.0f, -1.0f,  1.0f,  1.0f,  1.0f,  1.0f,  1.0f,
		 1.0f,  1.0f,  1.0f,  1.0f, -1.0f,  1.0f, -1.0f, -1.0f,  1.0f,
		-1.0f,  1.0f, -1.0f,  1.0f,  1.0f, -1.0f,  1.0f,  1.0f,  1.0f,
		 1.0f,  1.0f,  1.0f, -1.0f,  1.0f,  1.0f, -1.0f,  1.0f, -1.0f,
		-1.0f, -1.0f, -1.0f, -1.0f, -1.0f,  1.0f,  1.0f, -1.0f, -1.0f,
		 1.0f, -1.0f, -1.0f, -1.0f, -1.0f,  1.0f,  1.0f, -1.0f,  1.0f
	};

	GLuint skyboxVAO, skyboxVBO;
	glGenVertexArrays(1, &skyboxVAO);
	glGenBuffers(1, &skyboxVBO);
	glBindVertexArray(skyboxVAO);
	glBindBuffer(GL_ARRAY_BUFFER, skyboxVBO);
	glBufferData(GL_ARRAY_BUFFER, sizeof(skyboxVertices), &skyboxVertices, GL_STATIC_DRAW);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(GLfloat), (GLvoid*)0);
	glBindVertexArray(0);

	std::vector<std::string> faces = {
		"Skybox/skybox_right.png", "Skybox/skybox_left.png",
		"Skybox/skybox_top.png",   "Skybox/skybox_bottom.png",
		"Skybox/skybox_back.png",  "Skybox/skybox_front.png"
	};
	GLuint cubemapTexture = TextureLoader::LoadCubemap(faces, { 0, 0, 2, 2, 0, 0 });

	std::cout << "Cargando modelos 3D del escenario..." << std::endl;
	double loadStart = glfwGetTime();
	RegisterArenaAssets(scene);
	scene.LoadAssets();
	std::cout << "Modelos cargados en " << glfwGetTime() - loadStart << " s" << std::endl;

	if (galleryMode)
		BuildGallery(scene, GetGalleryOrder());
	else
		BuildArena(scene);
	BuildLightRig(lightRig, scene, lightFixtures);
	scene.PrintReport(galleryMode);

	objCinturon = scene.Find("cinturon");
	objBurrito = scene.Find("burrito");
	objE_cuerpo = scene.Find("E_anim_cuerpo");
	objE_antebrazoIzq = scene.Find("E_anim_antebrazo_izquierdo");
	objE_manoIzq = scene.Find("E_anim_mano_izquierda");
	objE_brazoDer = scene.Find("E_anim_brazo_derecho");
	objE_antebrazoDer = scene.Find("E_anim_antebrazo_derecho");
	objE_manoDer = scene.Find("E_anim_mano_derecha");
	objCervezaIzq = scene.Find("E_cerveza_izquierda");
	objCervezaDer = scene.Find("E_cerveza_derecha");

	if (SceneObject* pan = scene.Find("pan_de_muerto"))
	{
		baseCervezaMesa = pan->position + glm::vec3(-0.1f, 0.0f, -1.5f);
		objCervezaMesa = &scene.Add("cerveza_mesa", "cerveza_E", "mesa").At(baseCervezaMesa).RotY(90.0f);
	}

	for (SceneObject& obj : scene.Objects())
	{
		if (obj.assetId == "bocina")
			objBocinas.push_back(&obj);

		// Solo la bandera animada GLB (las estáticas "bandera_entrada" y "bandera_norte" quedan fijas)
		if (obj.assetId == "bandera_animada")
		{
			objBanderasAnimadas.push_back(&obj);
			if (obj.asset && obj.asset->model)
				modeloBanderaAnimada = obj.asset->model.get();
		}

		if (obj.assetId == "minion" && obj.asset && obj.asset->model)
			modeloMinion = obj.asset->model.get();

		if (obj.assetId == "arbitro" && obj.asset && obj.asset->model)
			modeloArbitro = obj.asset->model.get();

		if (obj.assetId == "reflector" && !modeloReflector && obj.asset && obj.asset->model && obj.asset->model->HasAnimations())
			modeloReflector = obj.asset->model.get();

		if (obj.assetId == "luchador" && !modeloLuchador && obj.asset && obj.asset->model && obj.asset->model->HasAnimations())
			modeloLuchador = obj.asset->model.get();

		if (obj.asset && obj.asset->model && obj.asset->model->HasAnimations())
		{
			Model* m = obj.asset->model.get();
			if (std::find(modelosAnimados.begin(), modelosAnimados.end(), m) == modelosAnimados.end())
				modelosAnimados.push_back(m);
		}
	}

	// Penta: solo en la arena (en la galería el objeto se llama distinto y queda en su pose de referencia)
	if (!galleryMode && penta.Init(scene) && !pentaClip.empty())
	{
		bool entrada = pentaClip == "entrada";
		if (!entrada && pentaClip != "caida")
			std::cout << "[Penta] AVISO: --penta '" << pentaClip << "' no existe (usa entrada o caida); se usa caida" << std::endl;
		if (pentaSeconds >= 0.0f) penta.Congelar(entrada, pentaSeconds);
		else if (entrada) penta.Entrada();
		else penta.Caida();
	}

	if (modeloMinion)
	{
		modeloMinion->PrintAnimations();   // imprime los nombres de los clips
		clipMinion = std::max(modeloMinion->FindAnimation("Armature"), 0); // o parte del nombre de tu clip
		// Sin esto el minion aparece en pose base y la silla se vería a tamaño completo
		modeloMinion->ShowAnimationFrame(clipMinion, 0.0f);
	}

	// Recorte de la animación de la bandera (segundos, no frames)
	if (modeloBanderaAnimada)
	{
		modeloBanderaAnimada->PrintAnimations();                    // imprime clips y duración
		const float fpsBlender = 24.0f;                             // pon los fps de tu escena de Blender
		modeloBanderaAnimada->SetAnimationRange(0.0f, 89.0f / fpsBlender);   // frames 1..90
	}

	cameraViews = GetCameraViews();
	if (galleryMode)
		cameraViews = GetGalleryViews(scene);
	ApplyCameraView(initialView);
	if (customCamera)
		camera.SetPose(camPos, camYaw, camPitch);

	int framesRendered = 0;
	int captureView = initialView < 0 ? 0 : initialView;
	double titleTimer = 0.0;
	double loopStart = glfwGetTime();
	lastFrame = (GLfloat)loopStart;   // Si no, el primer deltaTime incluiría toda la carga de modelos
	while (!glfwWindowShouldClose(window))
	{
		GLfloat currentFrame = (GLfloat)glfwGetTime();
		// Tiempo real entre cuadros; se acota para que un bloqueo (arrastrar la ventana, un punto de
		// interrupción) no adelante de golpe las animaciones
		deltaTime = std::min(currentFrame - lastFrame, 0.25f);
		lastFrame = currentFrame;

		glfwPollEvents();
		DoMovement();
		Animation();
		UpdateLightFixtures(lightRig, lightFixtures);

		titleTimer += deltaTime;
		if (titleTimer > 0.25)
		{
			UpdateWindowTitle(window);
			titleTimer = 0.0;
		}

		glClearColor(0.05f, 0.05f, 0.08f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		glm::mat4 projection = glm::perspective(glm::radians(camera.GetZoom()),
			(GLfloat)SCREEN_WIDTH / (GLfloat)std::max(SCREEN_HEIGHT, 1), 0.1f, 400.0f);
		glm::mat4 view = camera.GetViewMatrix();

		lightingShader.Use();
		glm::vec3 camPosition = camera.GetPosition();
		glUniform3fv(glGetUniformLocation(lightingShader.Program, "viewPos"), 1, glm::value_ptr(camPosition));
		glUniformMatrix4fv(glGetUniformLocation(lightingShader.Program, "view"), 1, GL_FALSE, glm::value_ptr(view));
		glUniformMatrix4fv(glGetUniformLocation(lightingShader.Program, "projection"), 1, GL_FALSE, glm::value_ptr(projection));
		lightRig.Apply(lightingShader);

		// Mapa de entorno (cube map) para el reflejo de Fresnel: unidad 2 (0 y 1 son difusa y especular)
		glActiveTexture(GL_TEXTURE2);
		glBindTexture(GL_TEXTURE_CUBE_MAP, cubemapTexture);
		glActiveTexture(GL_TEXTURE0);
		glUniform1i(glGetUniformLocation(lightingShader.Program, "envMap"), 2);

		scene.DrawOpaque(lightingShader);

		if (showDebugGrid || showAllBounds || selectedObject >= 0)
		{
			debugDraw.Begin(view, projection);
			if (showDebugGrid)
			{
				debugDraw.DrawGrid();
				debugDraw.DrawAxes(glm::mat4(1.0f), 2.0f);
			}
			int index = 0;
			for (SceneObject& obj : scene.Objects())
			{
				bool selected = (index++ == selectedObject);
				if (!obj.asset || !obj.asset->IsReady() || (!selected && (!showAllBounds || !obj.visible)))
					continue;
				glm::vec3 color = selected ? glm::vec3(1.0f, 1.0f, 0.0f) : glm::vec3(0.0f, 0.9f, 1.0f);
				debugDraw.DrawBox(obj.GetMatrix(), obj.asset->model->GetBoundsMin(), obj.asset->model->GetBoundsMax(), color);
				if (selected || showDebugGrid)
				{
					glm::mat4 pivot = glm::translate(glm::mat4(1.0f), obj.position) * EulerRotation(obj.rotation);
					debugDraw.DrawAxes(pivot, 0.6f);
				}
			}
		}

		glDepthFunc(GL_LEQUAL);
		skyboxShader.Use();

		glm::mat4 viewSkybox = glm::mat4(glm::mat3(view));
		glUniformMatrix4fv(glGetUniformLocation(skyboxShader.Program, "view"), 1, GL_FALSE, glm::value_ptr(viewSkybox));
		glUniformMatrix4fv(glGetUniformLocation(skyboxShader.Program, "projection"), 1, GL_FALSE, glm::value_ptr(projection));

		glBindVertexArray(skyboxVAO);
		glActiveTexture(GL_TEXTURE0);
		glUniform1i(glGetUniformLocation(skyboxShader.Program, "skybox"), 0);
		glBindTexture(GL_TEXTURE_CUBE_MAP, cubemapTexture);
		glDrawArrays(GL_TRIANGLES, 0, 36);
		glBindVertexArray(0);

		glDepthFunc(GL_LESS);

		lightingShader.Use();
		scene.DrawTransparent(lightingShader);

		// Esqueleto de Penta encima de todo (sin prueba de profundidad)
		if (showSkeleton)
		{
			debugDraw.Begin(view, projection);
			penta.DibujarEsqueleto(debugDraw);
		}

		// Errores de OpenGL: en Debug cada cuadro; en Release una vez por segundo (glGetError puede forzar
		// una sincronización con el driver). Los indicadores de error se conservan hasta consultarlos.
#ifdef _DEBUG
		const bool checkGL = true;
#else
		const bool checkGL = framesRendered % 60 == 0;
#endif
		if (checkGL)
			for (GLenum err = glGetError(); err != GL_NO_ERROR; err = glGetError())
				if (glErrorsReported++ < 10)
					std::cout << "[OpenGL] ERROR 0x" << std::hex << err << std::dec << " cerca del cuadro " << framesRendered << std::endl;

		framesRendered++;
		if (screenshotRequested)
		{
			SaveScreenshot("captura_" + std::to_string(++screenshotCounter) + ".png");
			screenshotRequested = false;
		}
		bool captureNow = captureAfter >= 0.0 ? glfwGetTime() - loopStart >= captureAfter : framesRendered == 3;
		if (!captureFile.empty() && captureNow)
		{
			SaveScreenshot(captureFile);
			glfwSetWindowShouldClose(window, GL_TRUE);
		}
		if (!captureAllPrefix.empty() && framesRendered >= 3)
		{
			SaveScreenshot(captureAllPrefix + "_" + std::to_string(captureView + 1) + ".png");
			if (++captureView >= (int)cameraViews.size())
				glfwSetWindowShouldClose(window, GL_TRUE);
			else
				ApplyCameraView(captureView);
		}

		glfwSwapBuffers(window);
		if (runSeconds > 0.0 && glfwGetTime() - loopStart > runSeconds)
			glfwSetWindowShouldClose(window, GL_TRUE);
	}

	double elapsed = glfwGetTime() - loopStart;
	if (framesRendered > 0 && elapsed > 0.0)
		std::cout << "Rendimiento: " << framesRendered << " cuadros en " << elapsed << " s (" << framesRendered / elapsed << " fps promedio)" << std::endl;
	for (GLenum err = glGetError(); err != GL_NO_ERROR; err = glGetError())
		glErrorsReported++;
	std::cout << "Errores de OpenGL: " << glErrorsReported << std::endl;

	glfwTerminate();
	return 0;
}

void SaveScreenshot(const std::string& file)
{
	glReadBuffer(GL_BACK);
	if (SOIL_save_screenshot(file.c_str(), SOIL_SAVE_TYPE_PNG, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT))
		std::cout << "Captura guardada en " << file << std::endl;
	else
		std::cout << "No se pudo guardar la captura " << file << std::endl;
}

// Límites de la GPU relevantes para el skinning (ver BoneBuffer en Scene.h)
void PrintGLLimits()
{
	GLint uniformComponents = 0, blockSize = 0, vertexBlocks = 0;
	glGetIntegerv(GL_MAX_VERTEX_UNIFORM_COMPONENTS, &uniformComponents);
	glGetIntegerv(GL_MAX_UNIFORM_BLOCK_SIZE, &blockSize);
	glGetIntegerv(GL_MAX_VERTEX_UNIFORM_BLOCKS, &vertexBlocks);
	std::cout << "OpenGL " << glGetString(GL_VERSION) << " | " << glGetString(GL_RENDERER) << std::endl;
	std::cout << "  uniforms del vertex shader: " << uniformComponents << " componentes; bloque uniforme: " << blockSize
		<< " bytes (" << blockSize / (int)sizeof(glm::mat4) << " mat4), " << vertexBlocks << " bloques por vertex shader" << std::endl;
	if (blockSize < MAX_BONES * (int)sizeof(glm::mat4))
		std::cout << "  AVISO: MAX_BONES (" << MAX_BONES << ") no cabe en un bloque uniforme de esta GPU" << std::endl;
}

void ApplyCameraView(int index)
{
	if (cameraViews.empty()) return;
	if (index < 0 || index >= (int)cameraViews.size()) index = 0;
	const CameraView& v = cameraViews[index];
	camera.SetPose(v.position, 0.0f, 0.0f);
	camera.LookAt(v.target);
	std::cout << "Vista " << index + 1 << ": " << v.name << std::endl;
}

void SelectObject(int direction)
{
	int count = (int)scene.Objects().size();
	if (count == 0) return;
	selectedObject = (selectedObject + direction + count + 1) % (count + 1) - 1;
	if (selectedObject >= 0)
	{
		std::cout << "Seleccionado: ";
		scene.PrintObject(scene.Objects()[selectedObject]);
	}
}

void UpdateWindowTitle(GLFWwindow* window)
{
	glm::vec3 p = camera.GetPosition();
	std::ostringstream title;
	title << std::fixed << std::setprecision(1) << "Lucha Libre CG | camara (" << p.x << ", " << p.y << ", " << p.z
		<< ") yaw " << camera.GetYaw() << " pitch " << camera.GetPitch();
	if (selectedObject >= 0)
	{
		const SceneObject& obj = scene.Objects()[selectedObject];
		title << " | " << obj.id << " (" << obj.position.x << ", " << obj.position.y << ", " << obj.position.z << ") rotY "
			<< obj.rotation.y << (obj.visible ? "" : " [oculto]");
	}
	if (penta.Activo())
		title << " | " << penta.Descripcion();
	title << " | " << (int)(1.0f / std::max(deltaTime, 0.0001f)) << " fps";
	glfwSetWindowTitle(window, title.str().c_str());
}

void DoMovement()
{
	GLfloat speedFactor = (keys[GLFW_KEY_LEFT_SHIFT] || keys[GLFW_KEY_RIGHT_SHIFT]) ? 4.0f : 1.0f;

	if (keys[GLFW_KEY_W] || keys[GLFW_KEY_UP])
		camera.ProcessKeyboard(FORWARD, deltaTime, speedFactor);

	if (keys[GLFW_KEY_S] || keys[GLFW_KEY_DOWN])
		camera.ProcessKeyboard(BACKWARD, deltaTime, speedFactor);

	if (keys[GLFW_KEY_A] || keys[GLFW_KEY_LEFT])
		camera.ProcessKeyboard(LEFT, deltaTime, speedFactor);

	if (keys[GLFW_KEY_D] || keys[GLFW_KEY_RIGHT])
		camera.ProcessKeyboard(RIGHT, deltaTime, speedFactor);

	if (keys[GLFW_KEY_E])
		camera.ProcessKeyboard(UP, deltaTime, speedFactor);

	if (keys[GLFW_KEY_Q])
		camera.ProcessKeyboard(DOWN, deltaTime, speedFactor);
}

void Animation()
{
	// 1. Actualizar animaciones de huesos/nodos de Assimp en modelos animados
	for (Model* m : modelosAnimados)
		m->UpdateAnimation(deltaTime);
	penta.Update(deltaTime);   // Cambios de estado y altura sobre la pasarela (después de avanzar su clip)

	// 2. Bandera animada GLB (tecla 'G'): si el GLB no trae animación de Assimp, aplica ondeo por código
	if (animacionBandera)
	{
		tiempoBandera += deltaTime;
		if (!modeloBanderaAnimada || !modeloBanderaAnimada->HasAnimations())
		{
			for (SceneObject* bandera : objBanderasAnimadas)
			{
				bandera->rotation.z = 6.0f * sinf(tiempoBandera * 3.5f);
				bandera->rotation.y += 12.0f * cosf(tiempoBandera * 2.5f) * deltaTime;
			}
		}
	}

	// Rotación continua del cinturón de campeonato
	rotacionCinturon += 45.0f * deltaTime;
	if (rotacionCinturon >= 360.0f) rotacionCinturon -= 360.0f;
	if (objCinturon)
		objCinturon->rotation.y = rotacionCinturon;

	// Vibración procedural de las bocinas
	escalaBocina = 1.0f + 0.05f * (float)sin(glfwGetTime() * 10.0f);
	for (SceneObject* bocina : objBocinas)
		bocina->scale = glm::vec3(escalaBocina);

	// Cerveza de la mesa brincando
	if (objCervezaMesa)
	{
		float salto = fabsf((float)sin(glfwGetTime() * 4.0));
		objCervezaMesa->position = baseCervezaMesa + glm::vec3(0.0f, 0.12f * salto, 0.0f);
		float golpe = powf(1.0f - salto, 6.0f);
		objCervezaMesa->scale = glm::vec3(1.0f + 0.06f * golpe, 1.0f - 0.12f * golpe, 1.0f + 0.06f * golpe);
	}

	// Trayectoria parabólica del burrito volador
	if (SceneObject* burrito = objBurrito)
	{
		burrito->visible = burritoLanzado;
		if (burritoLanzado)
		{
			tiempoVueloBurrito += deltaTime * 0.8f;
			float t = std::min(tiempoVueloBurrito, 1.0f);
			burrito->position = glm::mix(BURRITO_INICIO, BURRITO_FIN, t);
			burrito->position.y += 4.0f * BURRITO_ALTURA * t * (1.0f - t);
			burrito->rotation.x = 540.0f * t;

			if (tiempoVueloBurrito >= 1.0f)
			{
				burritoLanzado = false;
				tiempoVueloBurrito = 0.0f;
			}
		}
	}

	// ---------------- Espectador E: lanzar cerveza (Z) y beber (X)
	if (objE_cuerpo && objE_antebrazoIzq && objE_manoIzq && objE_brazoDer && objE_antebrazoDer
		&& objE_manoDer && objCervezaIzq && objCervezaDer)
	{
		auto suave = [](float a) { a = glm::clamp(a, 0.0f, 1.0f); return a * a * (3.0f - 2.0f * a); };

		auto articular = [](SceneObject* pieza, glm::vec3 articulacion, glm::vec3 giro, glm::vec3 agarre)
			{
				float yaw = objE_cuerpo->rotation.y;
				pieza->position = objE_cuerpo->position + glm::vec3(EulerRotation(glm::vec3(0.0f, yaw, 0.0f)) * glm::vec4(articulacion, 0.0f));
				pieza->rotation = glm::vec3(giro.x, yaw + giro.y, giro.z);
				pieza->pivotOffset = articulacion - agarre;
			};

		auto poseLanzamiento = [&](float t) -> glm::vec2
			{
				const glm::vec2 reposo(0.0f), atras(-105.0f, 35.0f), suelta(-35.0f, -10.0f), remate(5.0f, -15.0f);
				if (t < 0.45f)
					return glm::mix(reposo, atras, suave(t / 0.45f));
				if (t < SUELTA_CERVEZA) {
					float a = (t - 0.45f) / (SUELTA_CERVEZA - 0.45f);
					return glm::mix(atras, suelta, a * a);
				}
				if (t < 0.85f) {
					float a = (t - SUELTA_CERVEZA) / (0.85f - SUELTA_CERVEZA);
					return glm::mix(suelta, remate, 1.0f - (1.0f - a) * (1.0f - a));
				}
				return glm::mix(remate, reposo, suave((t - 0.85f) / 0.65f));
			};

		if (lanzandoCerveza)
			tiempoLanzamiento += deltaTime;
		glm::vec2 g = lanzandoCerveza ? poseLanzamiento(tiempoLanzamiento) : glm::vec2(0.0f);
		glm::vec3 giroIzq(g.x, g.y, 0.0f);
		articular(objE_antebrazoIzq, CODO_IZQ, giroIzq, glm::vec3(0.0f));
		articular(objE_manoIzq, CODO_IZQ, giroIzq, glm::vec3(0.0f));

		if (!cervezaEnVuelo)
		{
			if (lanzandoCerveza && tiempoLanzamiento >= SUELTA_CERVEZA)
			{
				glm::vec2 s = poseLanzamiento(SUELTA_CERVEZA);
				articular(objCervezaIzq, CODO_IZQ, glm::vec3(s.x, s.y, 0.0f), AGARRE_IZQ);
				inicioCerveza = objCervezaIzq->position + glm::vec3(EulerRotation(objCervezaIzq->rotation)
					* glm::vec4(CENTRO_BOTELLA - objCervezaIzq->pivotOffset, 0.0f));
				giroSuelta = objCervezaIzq->rotation;
				objCervezaIzq->pivotOffset = CENTRO_BOTELLA;
				cervezaEnVuelo = true;
			}
			else
				articular(objCervezaIzq, CODO_IZQ, giroIzq, AGARRE_IZQ);
		}
		if (cervezaEnVuelo)
		{
			float t = std::min((tiempoLanzamiento - SUELTA_CERVEZA) * 0.8f, 1.0f);
			objCervezaIzq->position = glm::mix(inicioCerveza, BURRITO_FIN, t);
			objCervezaIzq->position.y += 4.0f * ALTURA_CERVEZA * t * (1.0f - t);
			objCervezaIzq->rotation = giroSuelta + glm::vec3(540.0f * t, 0.0f, 0.0f);
			objCervezaIzq->visible = t < 1.0f;
		}
		if (lanzandoCerveza && tiempoLanzamiento >= SUELTA_CERVEZA + 1.25f + 0.3f)
		{
			lanzandoCerveza = false;
			cervezaEnVuelo = false;
			objCervezaIzq->visible = true;
			articular(objCervezaIzq, CODO_IZQ, glm::vec3(0.0f), AGARRE_IZQ);
		}

		float levantar = 0.0f, sorbo = 0.0f;
		if (bebiendo)
		{
			tiempoBebida += deltaTime;
			if (tiempoBebida < 0.9f)
				levantar = suave(tiempoBebida / 0.9f);
			else if (tiempoBebida < 1.9f) {
				levantar = 1.0f;
				sorbo = 10.0f * (float)sin(glm::pi<float>() * (tiempoBebida - 0.9f));
			}
			else
				levantar = 1.0f - suave((tiempoBebida - 1.9f) / 0.9f);
			if (tiempoBebida >= 2.8f) {
				bebiendo = false;
				levantar = 0.0f;
			}
		}
		glm::vec3 giroDer(-110.0f * levantar - sorbo, 0.0f, 12.0f * levantar);
		articular(objE_brazoDer, HOMBRO_DER, giroDer, glm::vec3(0.0f));
		articular(objE_antebrazoDer, HOMBRO_DER, giroDer, glm::vec3(0.0f));
		articular(objE_manoDer, HOMBRO_DER, giroDer, glm::vec3(0.0f));
		articular(objCervezaDer, HOMBRO_DER, giroDer, AGARRE_DER);
	}
}

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
		// ---- Cámara ----
		if (key == GLFW_KEY_R)
			ApplyCameraView(0);
		if (key >= GLFW_KEY_1 && key <= GLFW_KEY_9 && key - GLFW_KEY_1 < (int)cameraViews.size())
			ApplyCameraView(key - GLFW_KEY_1);

		if (key == GLFW_KEY_C) {
			mouseCaptured = !mouseCaptured;
			glfwSetInputMode(window, GLFW_CURSOR, mouseCaptured ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
			firstMouse = true;
		}

		// ---- Escena ----
		// G: Activar / pausar animación de la bandera animada GLB
		if (key == GLFW_KEY_G) {
			animacionBandera = !animacionBandera;
			if (modeloBanderaAnimada)
				modeloBanderaAnimada->ToggleAnimation(0, true);
			std::cout << "Animacion Bandera GLB: " << (animacionBandera ? "ACTIVADA" : "PAUSADA") << std::endl;
		}

		// B: Lanzar el burrito desde el público hacia el ring
		if (key == GLFW_KEY_B) {
			burritoLanzado = true;
			tiempoVueloBurrito = 0.0f;
		}

		// Z: el espectador E lanza la cerveza con el antebrazo izquierdo
		if (key == GLFW_KEY_Z && !lanzandoCerveza) {
			lanzandoCerveza = true;
			tiempoLanzamiento = 0.0f;
		}

		// X: el espectador E levanta el brazo derecho para beber
		if (key == GLFW_KEY_X && !bebiendo) {
			bebiendo = true;
			tiempoBebida = 0.0f;
		}

		// L: Cambiar color de luces en tiempo real
		if (key == GLFW_KEY_L) {
			luzEscenarioActiva = !luzEscenarioActiva;
			colorLuzSpot = luzEscenarioActiva ? glm::vec3(1.0f, 0.2f, 0.2f) : glm::vec3(0.2f, 0.2f, 1.0f);
			for (PointLightParams& light : lightRig.pointLights)
				light.diffuse = colorLuzSpot;
		}

		// T: Activar / pausar animación del árbitro
		if (key == GLFW_KEY_T) {
			animacionArbitro = !animacionArbitro;
			if (modeloArbitro)
				modeloArbitro->ToggleAnimation(0, true);
		}

		// SPACE: Disparar animación del luchador una vez
		if (key == GLFW_KEY_SPACE) {
			animacionLuchador1 = true;
			if (modeloLuchador)
				modeloLuchador->TriggerAnimation(0, false);
		}

		// ---- Penta (animación esquelética) ----
		// Solo en GLFW_PRESS: mantener la tecla no reinicia el clip
		if (key == GLFW_KEY_F5) penta.Entrada();
		if (key == GLFW_KEY_F6) penta.Caida();
		if (key == GLFW_KEY_F7) penta.Reiniciar();
		if (key == GLFW_KEY_F8) showSkeleton = !showSkeleton;

		// ---- Depuración ----
		if (key == GLFW_KEY_F1) showDebugGrid = !showDebugGrid;
		if (key == GLFW_KEY_F2) showAllBounds = !showAllBounds;
		if (key == GLFW_KEY_F3) scene.PrintReport();
		if (key == GLFW_KEY_F4) {
			for (SceneObject& obj : scene.Objects()) obj.visible = true;
		}
		if (key == GLFW_KEY_TAB) SelectObject((mode & GLFW_MOD_SHIFT) ? -1 : 1);
		if (key == GLFW_KEY_H && selectedObject >= 0) {
			SceneObject& obj = scene.Objects()[selectedObject];
			obj.visible = !obj.visible;
			std::cout << obj.id << (obj.visible ? " visible" : " oculto") << std::endl;
		}

		// minion alza los brazos y lanza silla
		if (key == GLFW_KEY_M && modeloMinion) {
			// modeloMinion->SetAnimationRange(0.0f, 89.0f / 24.0f);  // si debes recortar cuadros (fps de Blender)
			modeloMinion->TriggerAnimation(clipMinion, false);
		}

		// pausar la animación de los reflectores del marcador
		if (key == GLFW_KEY_K && modeloReflector)
			modeloReflector->ToggleAnimation(0, true);

		if (key == GLFW_KEY_F12) screenshotRequested = true;
	}
}

void MouseCallback(GLFWwindow* window, double xPos, double yPos)
{
	if (firstMouse)
	{
		lastX = (GLfloat)xPos;
		lastY = (GLfloat)yPos;
		firstMouse = false;
	}

	GLfloat xOffset = (GLfloat)xPos - lastX;
	GLfloat yOffset = lastY - (GLfloat)yPos;

	lastX = (GLfloat)xPos;
	lastY = (GLfloat)yPos;

	if (mouseCaptured)
		camera.ProcessMouseMovement(xOffset, yOffset);
}

void ScrollCallback(GLFWwindow* window, double xOffset, double yOffset)
{
	camera.ProcessMouseScroll((GLfloat)yOffset);
}