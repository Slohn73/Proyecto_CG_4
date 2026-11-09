/*
 PROYECTO FINAL: Lucha Libre Mexicana - "Little Endians"
 Computación Gráfica e Interacción Humano-Computadora, Grupo 04

 Organización:
   ArenaLayout.h  -> qué modelos se cargan y dónde se colocan (posiciones, rotaciones, escalas),
                     luces y vistas de cámara. Es el archivo a editar para acomodar la escena.
   Scene.h        -> recursos (modelo cargado una vez + normalización) e instancias del escenario.
   Model.h/Mesh.h -> carga con Assimp, materiales y dibujo.
   Lights.h       -> parámetros de las luces OpenGL.
   DebugDraw.h    -> cuadrícula, ejes y cajas envolventes (solo depuración).

 Opciones de línea de comandos (útiles para revisar la escena sin interacción):
   --galeria                 una instancia de cada modelo en fila, en lugar de la arena
   --vista N                 vista inicial (1-6)
   --camara x y z yaw pitch  posición y orientación inicial de la cámara
   --depuracion              muestra cuadrícula, ejes y cajas envolventes
   --captura archivo.png     guarda una imagen del primer cuadro estable y termina
   --captura-todas prefijo   guarda una imagen por cada vista (prefijo_1.png, prefijo_2.png, ...) y termina
   --segundos N              termina después de N segundos (para medir el rendimiento)
*/

#include <iostream>
#include <cmath>
#include <vector>
#include <string>
#include <sstream>
#include <iomanip>

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

// Módulos internos
#include "Shader.h"
#include "Camera.h"
#include "Model.h"
#include "Texture.h"
#include "Scene.h"
#include "Lights.h"
#include "DebugDraw.h"
#include "ArenaLayout.h"

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
std::vector<LightFixture> lightFixtures;   // Luces de la rampa ligadas a sus modelos de reflector
bool luzEscenarioActiva = true;
glm::vec3 colorLuzSpot(1.0f, 1.0f, 1.0f);

// Herramientas de depuración
bool showDebugGrid = false;
bool showAllBounds = false;
int selectedObject = -1;
int screenshotCounter = 0;
bool screenshotRequested = false;

// Objetos animados (se buscan una sola vez después de construir la escena)
SceneObject* objCinturon = nullptr;
SceneObject* objBurrito = nullptr;
std::vector<SceneObject*> objBocinas;

// Variables para animaciones por código y procedurales
float rotacionCinturon = 0.0f;
float escalaBocina = 1.0f;

// Variables para la animación del Burrito Volador (Idea del equipo)
bool burritoLanzado = false;
float tiempoVueloBurrito = 0.0f;

// Animación del espectador E (Z: lanza la cerveza izquierda, X: bebe la derecha).
// Cada pieza gira alrededor de su articulación con position + rotation + pivotOffset:
//   T(cuerpo + codo) * R(giro) * T(-codo)  ->  la pieza rota sobre el codo / hombro.
// Puntos medidos en los OBJ del espectador (metros, coordenadas del modelo).
SceneObject *objE_cuerpo = nullptr, *objE_antebrazoIzq = nullptr, *objE_manoIzq = nullptr;
SceneObject *objE_brazoDer = nullptr, *objE_antebrazoDer = nullptr, *objE_manoDer = nullptr;
SceneObject *objCervezaIzq = nullptr, *objCervezaDer = nullptr;
const glm::vec3 CODO_IZQ(0.27f, 0.70f, 0.0f);
const glm::vec3 HOMBRO_DER(-0.26f, 0.95f, -0.03f);
const glm::vec3 AGARRE_IZQ(0.14f, 0.57f, 0.12f);    // Base de la botella: la mano la toma a ~10 cm
const glm::vec3 AGARRE_DER(-0.14f, 0.57f, 0.12f);
const glm::vec3 CENTRO_BOTELLA(0.0f, 0.12f, 0.0f);  // La botella mide 24.5 cm
const float SUELTA_CERVEZA = 0.70f;                  // Segundo en que la mano suelta la botella
const float ALTURA_CERVEZA = 2.5f;                   // Altura extra del arco parabólico
bool lanzandoCerveza = false;
bool cervezaEnVuelo = false;
float tiempoLanzamiento = 0.0f;
glm::vec3 inicioCerveza(0.0f);
glm::vec3 giroSuelta(0.0f);
bool bebiendo = false;
float tiempoBebida = 0.0f;

// Cerveza extra sobre la mesa de comentaristas, junto al pan: mira al ring y brinca
SceneObject* objCervezaMesa = nullptr;
glm::vec3 baseCervezaMesa(0.0f);                     // Punto de la mesa donde se apoya

// Estados de animación por teclado / input
bool animacionLuchador1 = false;
bool animacionArbitro = false;
bool animacionPantalla = false;

int main(int argc, char** argv)
{
	// Opciones de línea de comandos
	bool galleryMode = false;
	bool customCamera = false;
	int initialView = 0;
	glm::vec3 camPos(0.0f);
	float camYaw = 0.0f, camPitch = 0.0f;
	std::string captureFile;
	std::string captureAllPrefix;
	double runSeconds = 0.0;
	for (int i = 1; i < argc; i++)
	{
		std::string arg = argv[i];
		if (arg == "--galeria") galleryMode = true;
		else if (arg == "--depuracion") showDebugGrid = showAllBounds = true;
		else if (arg == "--vista" && i + 1 < argc) initialView = atoi(argv[++i]) - 1;
		else if (arg == "--captura" && i + 1 < argc) captureFile = argv[++i];
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

	// Inicialización de GLFW
	glfwInit();
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
	glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
	glfwWindowHint(GLFW_RESIZABLE, GL_FALSE);
	glfwWindowHint(GLFW_SAMPLES, 4); // Antialiasing (MSAA x4)

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
	glfwSetScrollCallback(window, ScrollCallback);
	glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

	glewExperimental = GL_TRUE;
	if (GLEW_OK != glewInit())
	{
		std::cout << "Error al inicializar GLEW" << std::endl;
		return EXIT_FAILURE;
	}

	glViewport(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
	glEnable(GL_DEPTH_TEST);
	glEnable(GL_MULTISAMPLE);
	// Sin culling: varios modelos tienen superficies de una sola cara (bandera, letreros, pantallas)
	glDisable(GL_CULL_FACE);

	// ---------------------------------------------------------------- Carga de recursos
	// Carga de Shaders del proyecto
	Shader lightingShader("Shaders/lighting.vs", "Shaders/lighting.frag");
	Shader skyboxShader("Shaders/skybox.vs", "Shaders/skybox.frag");
	DebugDraw debugDraw;

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

	// Carga de las 6 imágenes de la Skybox (orden +X, -X, +Y, -Y, +Z, -Z)
	std::vector<std::string> faces;
	faces.push_back("Skybox/skybox_right.png");
	faces.push_back("Skybox/skybox_left.png");
	faces.push_back("Skybox/skybox_top.png");
	faces.push_back("Skybox/skybox_bottom.png");
	faces.push_back("Skybox/skybox_back.png");
	faces.push_back("Skybox/skybox_front.png");

	// Las caras superior e inferior vienen giradas 180° respecto a la convención de OpenGL
	// (se verificó midiendo la continuidad de color en las 12 aristas del cubo).
	GLuint cubemapTexture = TextureLoader::LoadCubemap(faces, { 0, 0, 2, 2, 0, 0 });

	// Carga de modelos 3D (cada archivo se lee una sola vez)
	std::cout << "Cargando modelos 3D del escenario..." << std::endl;
	double loadStart = glfwGetTime();
	RegisterArenaAssets(scene);
	scene.LoadAssets();
	std::cout << "Modelos cargados en " << glfwGetTime() - loadStart << " s" << std::endl;

	// ---------------------------------------------------------------- Configuración de la escena
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

	// Cerveza junto al pan de muerto (misma altura de la mesa), girada 90° para ver al ring (+X)
	if (SceneObject* pan = scene.Find("pan_de_muerto"))
	{
		baseCervezaMesa = pan->position + glm::vec3(-0.1f, 0.0f, -1.5f);
		objCervezaMesa = &scene.Add("cerveza_mesa", "cerveza_E", "mesa").At(baseCervezaMesa).RotY(90.0f);
	}
	for (SceneObject& obj : scene.Objects())
		if (obj.assetId == "bocina")
			objBocinas.push_back(&obj);

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

	// Bucle principal de renderizado
	while (!glfwWindowShouldClose(window))
	{
		GLfloat currentFrame = (GLfloat)glfwGetTime();
		deltaTime = currentFrame - lastFrame;
		lastFrame = currentFrame;

		// ------------------------------------------------------------ Actualización
		glfwPollEvents();
		DoMovement();
		Animation();
		UpdateLightFixtures(lightRig, lightFixtures);   // Las luces de la rampa siguen a sus reflectores

		titleTimer += deltaTime;
		if (titleTimer > 0.25)
		{
			UpdateWindowTitle(window);
			titleTimer = 0.0;
		}

		// ------------------------------------------------------------ Render
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

		scene.DrawOpaque(lightingShader);

		// Ayudas de depuración (F1: cuadrícula y ejes, F2: cajas, Tab: objeto seleccionado)
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
				// Caja orientada con el objeto y sus ejes locales en el punto de apoyo
				debugDraw.DrawBox(obj.GetMatrix(), obj.asset->model->GetBoundsMin(), obj.asset->model->GetBoundsMax(), color);
				if (selected || showDebugGrid)
				{
					glm::mat4 pivot = glm::translate(glm::mat4(1.0f), obj.position) * EulerRotation(obj.rotation);
					debugDraw.DrawAxes(pivot, 0.6f);
				}
			}
		}

		// RENDERIZADO DEL SKYBOX (al final: solo ocupa los píxeles que la geometría dejó libres)
		glDepthFunc(GL_LEQUAL);  // Permite que el skybox se dibuje al fondo (profundidad máxima)

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

		glDepthFunc(GL_LESS);    // Restaura la función de profundidad estándar

		// Superficies translúcidas (haces de luz) al final, sobre la geometría y el skybox
		lightingShader.Use();
		scene.DrawTransparent(lightingShader);

		// Capturas: F12 en cualquier momento, o --captura (tras unos cuadros para estabilizar)
		framesRendered++;
		if (screenshotRequested)
		{
			SaveScreenshot("captura_" + std::to_string(++screenshotCounter) + ".png");
			screenshotRequested = false;
		}
		if (!captureFile.empty() && framesRendered == 3)
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

	glfwTerminate();
	return 0;
}

// ------ FUNCIONES ------

void SaveScreenshot(const std::string& file)
{
	glReadBuffer(GL_BACK);
	if (SOIL_save_screenshot(file.c_str(), SOIL_SAVE_TYPE_PNG, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT))
		std::cout << "Captura guardada en " << file << std::endl;
	else
		std::cout << "No se pudo guardar la captura " << file << std::endl;
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

// Recorre los objetos de la escena para inspeccionarlos (Tab / Shift+Tab)
void SelectObject(int direction)
{
	int count = (int)scene.Objects().size();
	if (count == 0) return;
	selectedObject = (selectedObject + direction + count + 1) % (count + 1) - 1; // -1 = ninguno
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
	title << " | " << (int)(1.0f / std::max(deltaTime, 0.0001f)) << " fps";
	glfwSetWindowTitle(window, title.str().c_str());
}

// Función para mover la cámara mediante teclado
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

// Lógica de animaciones automáticas y por código.
// Solo modifica transformaciones de objetos de la escena; el dibujo no cambia.
void Animation()
{
	// Rotación continua del cinturón de campeonato
	rotacionCinturon += 45.0f * deltaTime;
	if (rotacionCinturon >= 360.0f) rotacionCinturon -= 360.0f;
	if (objCinturon)
		objCinturon->rotation.y = rotacionCinturon;

	// Vibración procedural de las bocinas utilizando una función senoidal
	escalaBocina = 1.0f + 0.05f * (float)sin(glfwGetTime() * 10.0f);
	for (SceneObject* bocina : objBocinas)
		bocina->scale = glm::vec3(escalaBocina);

	// Cerveza de la mesa brincando: |sen| da saltos que rebotan en la mesa
	if (objCervezaMesa)
	{
		float salto = fabsf((float)sin(glfwGetTime() * 4.0));      // 0 = en la mesa, 1 = punto más alto
		objCervezaMesa->position = baseCervezaMesa + glm::vec3(0.0f, 0.12f * salto, 0.0f);
		// Al tocar la mesa se aplasta un poco y se ensancha (como si rebotara)
		float golpe = powf(1.0f - salto, 6.0f);
		objCervezaMesa->scale = glm::vec3(1.0f + 0.06f * golpe, 1.0f - 0.12f * golpe, 1.0f + 0.06f * golpe);
	}

	// Trayectoria parabólica del burrito volador, desde el público hasta el ring
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
		// Arranque y frenado suaves (0..1 -> 0..1)
		auto suave = [](float a) { a = glm::clamp(a, 0.0f, 1.0f); return a * a * (3.0f - 2.0f * a); };

		// Gira una pieza sobre su articulación. "agarre" es el punto del cuerpo donde
		// se coloca el origen del modelo (la base de la botella en la mano).
		auto articular = [](SceneObject* pieza, glm::vec3 articulacion, glm::vec3 giro, glm::vec3 agarre)
		{
			float yaw = objE_cuerpo->rotation.y;
			pieza->position = objE_cuerpo->position + glm::vec3(EulerRotation(glm::vec3(0.0f, yaw, 0.0f)) * glm::vec4(articulacion, 0.0f));
			pieza->rotation = glm::vec3(giro.x, yaw + giro.y, giro.z);
			pieza->pivotOffset = articulacion - agarre;
		};

		// Antebrazo izquierdo: giro en X (sube / baja) y en Y (hacia afuera / cruzando)
		auto poseLanzamiento = [&](float t) -> glm::vec2
		{
			const glm::vec2 reposo(0.0f), atras(-105.0f, 35.0f), suelta(-35.0f, -10.0f), remate(5.0f, -15.0f);
			if (t < 0.45f)                                       // Preparación: lleva la botella atrás
				return glm::mix(reposo, atras, suave(t / 0.45f));
			if (t < SUELTA_CERVEZA) {                            // Latigazo: acelera hasta soltar
				float a = (t - 0.45f) / (SUELTA_CERVEZA - 0.45f);
				return glm::mix(atras, suelta, a * a);
			}
			if (t < 0.85f) {                                     // Seguimiento: frena después de soltar
				float a = (t - SUELTA_CERVEZA) / (0.85f - SUELTA_CERVEZA);
				return glm::mix(suelta, remate, 1.0f - (1.0f - a) * (1.0f - a));
			}
			return glm::mix(remate, reposo, suave((t - 0.85f) / 0.65f)); // Regreso al reposo
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
				// Suelta: se toma la pose exacta del instante de lanzamiento (no depende de los FPS)
				glm::vec2 s = poseLanzamiento(SUELTA_CERVEZA);
				articular(objCervezaIzq, CODO_IZQ, glm::vec3(s.x, s.y, 0.0f), AGARRE_IZQ);
				inicioCerveza = objCervezaIzq->position + glm::vec3(EulerRotation(objCervezaIzq->rotation)
					* glm::vec4(CENTRO_BOTELLA - objCervezaIzq->pivotOffset, 0.0f));
				giroSuelta = objCervezaIzq->rotation;
				objCervezaIzq->pivotOffset = CENTRO_BOTELLA;     // En el aire gira sobre su centro
				cervezaEnVuelo = true;
			}
			else
				articular(objCervezaIzq, CODO_IZQ, giroIzq, AGARRE_IZQ); // Va en la mano
		}
		if (cervezaEnVuelo)
		{
			// Misma trayectoria parabólica que el burrito: recta + arco 4h·t·(1-t)
			float t = std::min((tiempoLanzamiento - SUELTA_CERVEZA) * 0.8f, 1.0f);
			objCervezaIzq->position = glm::mix(inicioCerveza, BURRITO_FIN, t);
			objCervezaIzq->position.y += 4.0f * ALTURA_CERVEZA * t * (1.0f - t);
			objCervezaIzq->rotation = giroSuelta + glm::vec3(540.0f * t, 0.0f, 0.0f);
			objCervezaIzq->visible = t < 1.0f;
		}
		// Al aterrizar (y con el brazo ya en reposo) aparece otra cerveza en la mano
		if (lanzandoCerveza && tiempoLanzamiento >= SUELTA_CERVEZA + 1.25f + 0.3f)
		{
			lanzandoCerveza = false;
			cervezaEnVuelo = false;
			objCervezaIzq->visible = true;
			articular(objCervezaIzq, CODO_IZQ, glm::vec3(0.0f), AGARRE_IZQ);
		}

		// Brazo derecho completo gira sobre el hombro (antebrazo, mano y botella lo siguen)
		float levantar = 0.0f, sorbo = 0.0f;
		if (bebiendo)
		{
			tiempoBebida += deltaTime;
			if (tiempoBebida < 0.9f)                             // Sube a la boca
				levantar = suave(tiempoBebida / 0.9f);
			else if (tiempoBebida < 1.9f) {                      // Bebe: inclina un poco más la botella
				levantar = 1.0f;
				sorbo = 10.0f * (float)sin(glm::pi<float>() * (tiempoBebida - 0.9f));
			}
			else                                                 // Baja
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
		// ---- Cámara ----
		// R: regresar a la vista inicial; 1-6: vistas predefinidas
		if (key == GLFW_KEY_R)
			ApplyCameraView(0);
		if (key >= GLFW_KEY_1 && key <= GLFW_KEY_9 && key - GLFW_KEY_1 < (int)cameraViews.size())
			ApplyCameraView(key - GLFW_KEY_1);

		// C: liberar / capturar el cursor del mouse
		if (key == GLFW_KEY_C) {
			mouseCaptured = !mouseCaptured;
			glfwSetInputMode(window, GLFW_CURSOR, mouseCaptured ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
			firstMouse = true;
		}

		// ---- Escena ----
		// Input 1: Lanzar el burrito desde el público hacia el ring al presionar 'B'
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

		// Input 2: Cambiar color de luces en tiempo real al presionar 'L'
		if (key == GLFW_KEY_L) {
			luzEscenarioActiva = !luzEscenarioActiva;
			colorLuzSpot = luzEscenarioActiva ? glm::vec3(1.0f, 0.2f, 0.2f) : glm::vec3(0.2f, 0.2f, 1.0f);
			for (PointLightParams& light : lightRig.pointLights)
				light.diffuse = colorLuzSpot;
		}

		// Input 3: Activar animación del árbitro con la tecla 'T'
		// (antes era 'A', que también mueve la cámara a la izquierda)
		if (key == GLFW_KEY_T) {
			animacionArbitro = !animacionArbitro;
		}

		// Input 4: Activar ataque del luchador con la barra espaciadora
		if (key == GLFW_KEY_SPACE) {
			animacionLuchador1 = true;
		}

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
		if (key == GLFW_KEY_F12) screenshotRequested = true;
	}
}

// Callback para el movimiento del mouse (Navegación libre en primera persona)
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

// Rueda del mouse: zoom (campo de visión)
void ScrollCallback(GLFWwindow* window, double xOffset, double yOffset)
{
	camera.ProcessMouseScroll((GLfloat)yOffset);
}
