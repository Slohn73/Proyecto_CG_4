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
   --vista N                 vista inicial (1-8)
   --camara x y z yaw pitch  posición y orientación inicial de la cámara
   --depuracion              muestra cuadrícula, ejes y cajas envolventes
   --captura archivo.png     guarda una imagen del primer cuadro estable y termina
   --captura-todas prefijo   guarda una imagen por cada vista (prefijo_1.png, prefijo_2.png, ...) y termina
   --segundos N              termina después de N segundos (para medir el rendimiento)
   --prueba-arbitro prefijo  ejecuta dos veces la animación del árbitro (T), guarda capturas de sus
                             fases (prefijo_1_inicio.png, ...), mide holguras y termina
   --limitar-fps N           limita los cuadros por segundo (para comprobar la independencia de los FPS)
   --prueba-combate prefijo  ejecuta dos veces el combate (Space), guarda capturas de sus fases y mide
                             holguras con la malla deformada real; la segunda vez pide T y Space juntos
   --video-combate carpeta   graba el combate cuadro por cuadro a 30 cuadros por segundo de animación
   --congelar-combate t      muestra el combate detenido en el instante t (para revisar poses)
   --video-teclas sec dur carpeta  pulsa teclas en instantes exactos ("B@0,Z@0.5,X@4") con paso fijo
                             de 1/30 s y guarda cada cuadro durante dur segundos (revisión visual)
*/

#include <iostream>
#include <cmath>
#include <vector>
#include <string>
#include <sstream>
#include <iomanip>
#include <thread>
#include <chrono>
#include <unordered_map>

// Librería GLEW
#include <GL/glew.h>

// Librería GLFW
#include <GLFW/glfw3.h>

// Matemáticas GLM
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/euler_angles.hpp>   // extractEulerAngleYXZ: rotación compuesta -> ángulos del motor

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
#include "AnimacionArbitro.h"
#include "AnimacionCombate.h"

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
void PruebaArbitro(const std::string& prefijo, GLFWwindow* window);
void PruebaCombate(const std::string& prefijo, GLFWwindow* window);

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

// Objetos lanzados (burrito y cerveza): tiro parabólico con la gravedad real. Al caer se quedan
// acostados sobre la lona y después se encogen hasta desaparecer (no se esfuman de golpe).
const float GRAVEDAD = 9.81f;
const float REPOSO_EN_LONA = 1.2f;                   // Segundos que el objeto queda sobre la lona
const float ENCOGER = 0.4f;                          // Segundos en que se encoge hasta desaparecer

// Burrito volador (B): sale de las manos de un espectador de las gradas norte. Con la parábola
// mix(inicio, fin, u) + 4h·u(1-u) la aceleración vertical es -8h/T², así que T = sqrt(8h/g).
bool burritoLanzado = false;
float tiempoVueloBurrito = 0.0f;
float apoyoBurrito = 0.05f;                          // Altura del centro del burrito acostado (de su caja)

// Animación del espectador E (Z: lanza la cerveza izquierda, X: bebe la derecha).
// Cada brazo es una cadena hombro -> codo: el brazo gira sobre el hombro y el antebrazo, la mano y
// la botella giran además sobre el codo (que se mueve con el brazo):
//   pieza del antebrazo = T(cuerpo) · R(yaw) · T(hombro) · Rh · T(codo - hombro) · Rc · T(-codo)
// La rotación total se convierte a los ángulos de Euler del motor (orden Y, X, Z).
// Puntos medidos en los OBJ del espectador (metros, coordenadas del modelo; frente +Z, izquierda +X).
SceneObject *objE_cuerpo = nullptr, *objE_brazoIzq = nullptr, *objE_antebrazoIzq = nullptr, *objE_manoIzq = nullptr;
SceneObject *objE_brazoDer = nullptr, *objE_antebrazoDer = nullptr, *objE_manoDer = nullptr;
SceneObject *objCervezaIzq = nullptr, *objCervezaDer = nullptr;
const glm::vec3 HOMBRO_IZQ(0.26f, 0.95f, -0.03f), HOMBRO_DER(-0.26f, 0.95f, -0.03f);
const glm::vec3 CODO_IZQ(0.27f, 0.70f, 0.0f), CODO_DER(-0.27f, 0.70f, 0.0f);
const glm::vec3 MANO_DER(-0.14f, 0.665f, 0.12f);    // Centro de la mano derecha
const glm::vec3 AGARRE_IZQ(0.14f, 0.57f, 0.12f);    // Base de la botella: la mano la toma a ~10 cm
const glm::vec3 AGARRE_DER(-0.14f, 0.57f, 0.12f);
const glm::vec3 CENTRO_BOTELLA(0.0f, 0.124f, 0.0f); // La botella mide 24.5 cm
const float RADIO_BOTELLA = 0.033f;

// Pose de un brazo: (flexión al frente, abducción, giro sobre el eje del brazo, codo), en grados.
// Cada clave lleva la curva del tramo que termina en ella: 's' suave, 'a' acelera, 'f' frena.
struct ClaveBrazoE { float t; glm::vec4 pose; char curva; };

// Z: lanzamiento por encima de la cabeza. Hombro y codo giran solo en el plano sagital (eje X):
// el vecino de la izquierda está a 0.6 m y su brazo toca el de E, así que el brazo no se abre de lado.
// Con la botella rígida en la mano el codo no puede flexionarse más de ~40° sin que la botella toque
// el pecho; por eso el brazo se arma arriba (180°) y el latigazo es del codo, que se extiende.
const ClaveBrazoE CLAVES_LANZAR[] = {
	{ 0.00f, glm::vec4(0.0f), 's' },
	{ 0.28f, glm::vec4(90.0f, 0.0f, 0.0f, 0.0f), 's' },     // Sube el brazo al frente
	{ 0.55f, glm::vec4(180.0f, 0.0f, 0.0f, 30.0f), 's' },   // Arma: brazo arriba, botella atrás
	{ 0.65f, glm::vec4(180.0f, 0.0f, 0.0f, 30.0f), 's' },   // Apunta
	{ 0.73f, glm::vec4(175.0f, 0.0f, 0.0f, -20.0f), 'a' },  // Latigazo hasta soltar
	{ 0.88f, glm::vec4(90.0f, 0.0f, 0.0f, -40.0f), 'f' },   // Seguimiento: el brazo baja al frente
	{ 1.60f, glm::vec4(0.0f), 's' },                         // Regreso al reposo
};
const float SUELTA_CERVEZA = 0.73f;                  // Segundo en que la mano suelta la botella
const glm::vec2 CAIDA_CERVEZA(0.0f, 1.4f);           // (dx, z) del punto de la lona donde cae
const float ELEVACION_MIN = 45.0f, ELEVACION_MAX = 70.0f; // Debe librar las cuerdas del ring

// X: beber. Hombro con 3 giros y codo con su bisagra anatómica; las poses se resolvieron para que la
// boca de la botella quede en los labios (y = 1.115, z = 0.068) sin tocar la cabeza.
const glm::vec4 POSE_ACERCAR(121.0f, 36.6f, 10.5f, -28.1f);  // Botella en la boca, inclinada 16°
const glm::vec4 POSE_TRAGO(125.0f, 26.8f, -4.6f, -28.1f);    // Levanta el codo: inclinada 24°
const ClaveBrazoE CLAVES_BEBER[] = {
	{ 0.0f, glm::vec4(0.0f), 's' },
	{ 0.9f, POSE_ACERCAR, 's' },
	{ 1.4f, POSE_TRAGO, 's' },
	{ 1.9f, POSE_ACERCAR, 's' },
	{ 2.8f, glm::vec4(0.0f), 's' },
};

bool lanzandoCerveza = false;
float tiempoLanzamiento = 0.0f;
bool bebiendo = false;
float tiempoBebida = 0.0f;

// Cerveza extra sobre la mesa de comentaristas, junto al pan: mira al ring y brinca
SceneObject* objCervezaMesa = nullptr;
glm::vec3 baseCervezaMesa(0.0f);                     // Punto de la mesa donde se apoya

// Estados de animación por teclado / input
bool animacionPantalla = false;

// Árbitro (T): se acerca, observa a los luchadores, da la señal de inicio y regresa (AnimacionArbitro.h)
AnimacionArbitro arbitro;

// Combate (Space): el luchador se acerca a Penta y le da un golpe al pecho; Penta reacciona
// (AnimacionCombate.h). Mientras el árbitro está en su secuencia el combate espera, y T se ignora
// durante el combate, para que nunca se crucen en el ring.
AnimacionCombate combate;
float limiteDeltaCombate = FLT_MAX;   // Solo pruebas: hace coincidir un cuadro con un instante exacto
float deltaCombateFijo = 0.0f;        // Solo video: paso fijo de animación por cuadro

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
	std::string pruebaArbitro, pruebaCombate, videoCombate;
	std::string videoTeclas, carpetaVideoTeclas;
	float duracionVideoTeclas = 0.0f;
	float congelarCombate = -1.0f;
	double limiteFps = 0.0;
	for (int i = 1; i < argc; i++)
	{
		std::string arg = argv[i];
		if (arg == "--galeria") galleryMode = true;
		else if (arg == "--depuracion") showDebugGrid = showAllBounds = true;
		else if (arg == "--vista" && i + 1 < argc) initialView = atoi(argv[++i]) - 1;
		else if (arg == "--captura" && i + 1 < argc) captureFile = argv[++i];
		else if (arg == "--captura-todas" && i + 1 < argc) captureAllPrefix = argv[++i];
		else if (arg == "--segundos" && i + 1 < argc) runSeconds = atof(argv[++i]);
		else if (arg == "--prueba-arbitro" && i + 1 < argc) pruebaArbitro = argv[++i];
		else if (arg == "--limitar-fps" && i + 1 < argc) limiteFps = atof(argv[++i]);
		else if (arg == "--prueba-combate" && i + 1 < argc) pruebaCombate = argv[++i];
		else if (arg == "--video-combate" && i + 1 < argc) videoCombate = argv[++i];
		else if (arg == "--congelar-combate" && i + 1 < argc) congelarCombate = (float)atof(argv[++i]);
		else if (arg == "--video-teclas" && i + 3 < argc)
		{
			videoTeclas = argv[++i];
			duracionVideoTeclas = (float)atof(argv[++i]);
			carpetaVideoTeclas = argv[++i];
		}
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
	objE_brazoIzq = scene.Find("E_anim_brazo_izquierdo");
	objE_antebrazoIzq = scene.Find("E_anim_antebrazo_izquierdo");
	objE_manoIzq = scene.Find("E_anim_mano_izquierda");
	objE_brazoDer = scene.Find("E_anim_brazo_derecho");
	objE_antebrazoDer = scene.Find("E_anim_antebrazo_derecho");
	objE_manoDer = scene.Find("E_anim_mano_derecha");
	objCervezaIzq = scene.Find("E_cerveza_izquierda");
	objCervezaDer = scene.Find("E_cerveza_derecha");

	// Burrito: sale frente al pecho del espectador de las gradas más cercano al punto de lanzamiento,
	// y cae acostado (giro final de 90° en X): su centro queda a media altura de la caja girada.
	if (objBurrito)
	{
		SceneObject* lanzador = nullptr;
		float masCerca = FLT_MAX;
		for (SceneObject& o : scene.Objects())
			if (o.id.rfind("publico_", 0) == 0 && glm::length(o.position - BURRITO_INICIO) < masCerca)
			{
				masCerca = glm::length(o.position - BURRITO_INICIO);
				lanzador = &o;
			}
		if (lanzador)
			BURRITO_INICIO = lanzador->position + glm::vec3(EulerRotation(lanzador->rotation) * glm::vec4(0.0f, 1.0f, 0.3f, 0.0f));
		if (objBurrito->asset && objBurrito->asset->IsReady())
		{
			glm::vec3 mn, mx;
			TransformBounds(EulerRotation(glm::vec3(90.0f, 0.0f, 0.0f)), objBurrito->asset->localMin, objBurrito->asset->localMax, mn, mx);
			apoyoBurrito = -mn.y;
		}
		objBurrito->position = BURRITO_INICIO;
		std::cout << "Burrito (B): lo lanza " << (lanzador ? lanzador->id : std::string("(sin espectador)")) << " desde ("
			<< BURRITO_INICIO.x << ", " << BURRITO_INICIO.y << ", " << BURRITO_INICIO.z << "), vuela "
			<< sqrtf(8.0f * BURRITO_ALTURA / GRAVEDAD) << " s" << std::endl;
	}

	// Cerveza junto al pan de muerto (misma altura de la mesa), girada 90° para ver al ring (+X)
	if (SceneObject* pan = scene.Find("pan_de_muerto"))
	{
		baseCervezaMesa = pan->position + glm::vec3(-0.1f, 0.0f, -1.5f);
		objCervezaMesa = &scene.Add("cerveza_mesa", "cerveza_E", "mesa").At(baseCervezaMesa).RotY(90.0f);
	}
	for (SceneObject& obj : scene.Objects())
		if (obj.assetId == "bocina")
			objBocinas.push_back(&obj);
	if (!arbitro.Inicializar(scene.Find("arbitro"), scene.Find("arbitro_brazo_derecho"),
		scene.Find("luchador"), scene.Find("penta"), glm::vec3(0.0f)))
		std::cout << "AVISO: no se encontro el arbitro; la tecla T no tendra efecto" << std::endl;
	if (combate.Inicializar(scene.Find("luchador"), scene.Find("penta")))
		std::cout << "Combate listo: golpe a " << combate.AvanceGolpe() << " m de avance; el brazo toca al rival al "
			<< 100.0f * combate.FraccionImpacto() << " % del barrido (separacion " << combate.SeparacionImpacto() << " m), mano a "
			<< combate.VelocidadImpacto() << " m/s, intensidad de la reaccion " << combate.Intensidad() << std::endl;
	else
		std::cout << "AVISO: los luchadores no tienen esqueleto; la barra espaciadora no tendra efecto" << std::endl;
	if (congelarCombate >= 0.0f)
		combate.Congelar(congelarCombate);

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

		// --video-teclas: paso fijo de 1/30 s y pulsaciones reales (KeyCallback) en instantes exactos.
		// Formato de la secuencia: "B@0,Z@0.5,X@4" (tecla @ segundo de animación).
		if (!carpetaVideoTeclas.empty() && framesRendered >= 3)
		{
			static int cuadroTeclas = 0;
			deltaTime = 1.0f / 30.0f;
			mouseCaptured = false;          // El mouse real no debe mover la cámara durante la grabación
			float ahora = cuadroTeclas * deltaTime, siguiente = ahora + deltaTime;
			std::stringstream lista(videoTeclas);
			std::string evento;
			while (std::getline(lista, evento, ','))
			{
				size_t arroba = evento.find('@');
				if (arroba == std::string::npos || arroba == 0) continue;
				float instante = (float)atof(evento.c_str() + arroba + 1);
				if (instante >= ahora - 1e-4f && instante < siguiente - 1e-4f)
				{
					std::string nombreTecla = evento.substr(0, arroba);
					int tecla = nombreTecla == "SPACE" ? GLFW_KEY_SPACE : toupper((unsigned char)nombreTecla[0]);
					KeyCallback(window, tecla, 0, GLFW_PRESS, 0);
					KeyCallback(window, tecla, 0, GLFW_RELEASE, 0);
					std::cout << "Video: tecla " << nombreTecla << " en t = " << ahora << " s" << std::endl;
				}
			}
			cuadroTeclas++;
		}

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

		if (!pruebaArbitro.empty() && framesRendered >= 3)
			PruebaArbitro(pruebaArbitro, window);
		if (!pruebaCombate.empty() && framesRendered >= 3)
			PruebaCombate(pruebaCombate, window);
		if (!videoCombate.empty() && framesRendered >= 3)
		{
			// Paso fijo de 1/30 s: el video dura lo mismo que la animación aunque guardar cada cuadro sea lento
			static int cuadroVideo = 0, cola = 0;
			if (cuadroVideo == 0) { combate.Solicitar(); deltaCombateFijo = 1.0f / 30.0f; }
			char nombre[64];
			snprintf(nombre, sizeof(nombre), "/cuadro_%04d.png", cuadroVideo++);
			SaveScreenshot(videoCombate + nombre);
			if (!combate.Ocupado() && ++cola > 15)
				glfwSetWindowShouldClose(window, GL_TRUE);
		}
		if (!carpetaVideoTeclas.empty() && framesRendered >= 4)
		{
			// Cuadro n = estado después de n pasos de 1/30 s (el primer paso lleva la pulsación de t = 0)
			static int cuadroGuardado = 0;
			char nombre[64];
			snprintf(nombre, sizeof(nombre), "/cuadro_%04d.png", cuadroGuardado++);
			SaveScreenshot(carpetaVideoTeclas + nombre);
			if (cuadroGuardado / 30.0f > duracionVideoTeclas)
				glfwSetWindowShouldClose(window, GL_TRUE);
		}

		glfwSwapBuffers(window);
		if (limiteFps > 0.0)
		{
			double finCuadro = currentFrame + 1.0 / limiteFps;
			while (glfwGetTime() < finCuadro)
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
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

// --prueba-arbitro: reproduce dos veces la secuencia del árbitro sin interacción. En la primera
// guarda capturas de sus fases; en ambas mide (con cajas envolventes en el mundo) la holgura con
// los luchadores, la distancia a las cuerdas y la altura sobre la lona, y al terminar comprueba que
// recupera exactamente su transformación original. Se llama después de dibujar cada cuadro.
void PruebaArbitro(const std::string& prefijo, GLFWwindow* window)
{
	struct Captura { float t; const char* nombre; };
	static const Captura capturas[] = { { 0.8f, "acercamiento" }, { 2.8f, "observa_luchador" },
		{ 4.5f, "observa_rival" }, { 6.4f, "senal" }, { 8.4f, "retroceso" } };
	static SceneObject* cuerpo = scene.Find("arbitro");
	static SceneObject* brazo = scene.Find("arbitro_brazo_derecho");
	static const SceneObject* luchadores[2] = { scene.Find("luchador"), scene.Find("penta") };
	if (!cuerpo || !brazo) { glfwSetWindowShouldClose(window, GL_TRUE); return; }
	static const glm::vec3 pos0 = cuerpo->position, rot0 = cuerpo->rotation;
	static const glm::vec3 posBrazo0 = brazo->position, rotBrazo0 = brazo->rotation;
	static int repeticion = 0, siguiente = 0, cuadros = 0;
	static bool enCurso = false;
	static double inicio = 0.0;
	static float holgura[2] = { FLT_MAX, FLT_MAX }, tHolgura[2] = { 0.0f, 0.0f };
	static float maxCentro = 0.0f, minAltura = FLT_MAX, maxElevacion = 0.0f, maxVel = 0.0f, maxVelYaw = 0.0f;
	static glm::vec3 posAnterior = pos0;
	static float yawAnterior = rot0.y;

	if (!enCurso)
	{
		if (repeticion == 0)
			SaveScreenshot(prefijo + "_1_inicio.png");
		if (repeticion == 2)
		{
			std::cout << std::fixed << std::setprecision(3)
				<< "[Prueba arbitro] holgura minima (cajas) con el luchador " << holgura[0] << " m (t = " << tHolgura[0]
				<< " s), con el rival " << holgura[1] << " m (t = " << tHolgura[1] << " s)\n"
				<< "[Prueba arbitro] extension maxima desde el centro (|x| o |z|) " << maxCentro
				<< " m; la lona llega a " << Arena::RING_MITAD << " m\n"
				<< "[Prueba arbitro] punto mas bajo respecto a la lona " << minAltura << " m; elevacion maxima al caminar "
				<< maxElevacion << " m\n"
				<< "[Prueba arbitro] velocidad maxima " << maxVel << " m/s, giro maximo " << maxVelYaw << " grados/s"
				<< std::defaultfloat << std::endl;
			glfwSetWindowShouldClose(window, GL_TRUE);
			return;
		}
		arbitro.Iniciar();
		enCurso = true;
		siguiente = cuadros = 0;
		inicio = glfwGetTime();
		std::cout << "[Prueba arbitro] repeticion " << ++repeticion << std::endl;
		return;
	}

	// Mediciones de la pose que se acaba de dibujar
	cuadros++;
	const float lona = pos0.y;
	glm::vec3 mn, mx, amn, amx;
	scene.GetWorldBounds(*cuerpo, mn, mx);
	scene.GetWorldBounds(*brazo, amn, amx);
	mn = glm::min(mn, amn);
	mx = glm::max(mx, amx);
	for (int i = 0; i < 2; i++)
	{
		glm::vec3 lmn, lmx;
		if (!luchadores[i] || !scene.GetWorldBounds(*luchadores[i], lmn, lmx)) continue;
		// Separación entre cajas: positiva si hay espacio libre en algún eje
		glm::vec3 sep = glm::max(lmn - mx, mn - lmx);
		float h = std::max(sep.x, std::max(sep.y, sep.z));
		if (h < holgura[i]) { holgura[i] = h; tHolgura[i] = arbitro.Tiempo(); }
	}
	maxCentro = std::max(maxCentro, std::max(std::max(fabsf(mn.x), fabsf(mx.x)), std::max(fabsf(mn.z), fabsf(mx.z))));
	minAltura = std::min(minAltura, mn.y - lona);
	maxElevacion = std::max(maxElevacion, cuerpo->position.y - lona);
	if (deltaTime > 0.0f)
	{
		maxVel = std::max(maxVel, glm::length(cuerpo->position - posAnterior) / deltaTime);
		maxVelYaw = std::max(maxVelYaw, fabsf(cuerpo->rotation.y - yawAnterior) / deltaTime);
	}
	posAnterior = cuerpo->position;
	yawAnterior = cuerpo->rotation.y;

	if (arbitro.Activa())
	{
		if (repeticion == 1 && siguiente < 5 && arbitro.Tiempo() >= capturas[siguiente].t)
		{
			std::cout << "[Prueba arbitro] t = " << arbitro.Tiempo() << " s, fase: " << arbitro.FaseActual()
				<< ", pos (" << cuerpo->position.x << ", " << cuerpo->position.y << ", " << cuerpo->position.z
				<< "), rot (" << cuerpo->rotation.x << ", " << cuerpo->rotation.y << ", " << brazo->rotation.z << ")" << std::endl;
			SaveScreenshot(prefijo + "_" + std::to_string(siguiente + 2) + "_" + capturas[siguiente].nombre + ".png");
			siguiente++;
		}
		return;
	}

	// Terminó: este cuadro ya muestra la pose final
	float errPos = glm::length(cuerpo->position - pos0) + glm::length(brazo->position - posBrazo0);
	float errRot = glm::length(cuerpo->rotation - rot0) + glm::length(brazo->rotation - rotBrazo0);
	double duracion = glfwGetTime() - inicio;
	std::cout << "[Prueba arbitro] repeticion " << repeticion << " terminada: " << cuadros << " cuadros en " << duracion
		<< " s (" << cuadros / duracion << " fps, secuencia de " << arbitro.Duracion() << " s); error final: posicion "
		<< errPos << " m, rotacion " << errRot << " grados" << std::endl;
	if (repeticion == 1)
		SaveScreenshot(prefijo + "_7_regreso.png");
	enCurso = false;
}

// --prueba-combate: reproduce dos veces el combate sin interacción, usando KeyCallback como si se
// presionaran las teclas. En la primera guarda capturas de sus fases; en la segunda pide T y Space
// a la vez (el combate debe esperar al árbitro) y vuelve a presionar T y Space durante el combate
// (deben ignorarse). En cada cuadro deforma en la CPU todos los vértices de ambos luchadores (la
// misma fórmula que lighting.vs) y mide: altura sobre la lona, distancia a las cuerdas y postes
// reales del ring, distancia entre los dos cuerpos, distancia al árbitro y la separación de la
// brazo del atacante con el rival en el instante del impacto.
void PruebaCombate(const std::string& prefijo, GLFWwindow* window)
{
	using namespace CombateParams;
	struct Captura { float t; const char* nombre; };
	static const Captura capturas[] = { { T_GUARDIA, "guardia" }, { T_PROVOCA, "provoca" }, { 1.9f, "camina" },
		{ T_PREPARA, "prepara" }, { T_IMPACTO, "impacto" }, { T_IMPACTO + 0.15f, "reaccion" },
		{ T_IMPACTO + 0.8f, "dolor_y_celebracion" }, { T_IMPACTO + 1.5f, "sacude_cabeza" }, { 6.0f, "regreso" } };
	const int NUM_CAPTURAS = (int)(sizeof(capturas) / sizeof(capturas[0]));
	static SceneObject* A = scene.Find("luchador");
	static SceneObject* B = scene.Find("penta");
	static SceneObject* ring = scene.Find("ring");
	static SceneObject* arbCuerpo = scene.Find("arbitro");
	static SceneObject* arbBrazo = scene.Find("arbitro_brazo_derecho");
	if (!A || !B || !ring || !A->asset->model->HasSkeleton() || !B->asset->model->HasSkeleton())
	{
		std::cout << "[Prueba combate] faltan los luchadores con esqueleto" << std::endl;
		glfwSetWindowShouldClose(window, GL_TRUE);
		return;
	}
	static const glm::vec3 posA0 = A->position, rotA0 = A->rotation, posB0 = B->position, rotB0 = B->rotation;
	static int repeticion = 0, siguiente = 0, cuadros = 0;
	static bool enCurso = false, pulsoDurante = false;
	static double inicio = 0.0, inicioReal = 0.0;
	static float minPiso = FLT_MAX, minCuerdas = FLT_MAX, minEntre = FLT_MAX, tMinEntre = 0.0f, brechaImpacto = FLT_MAX;
	static float minArbitro = FLT_MAX, maxVel = 0.0f, maxVelVertice = 0.0f, tMaxVelVertice = 0.0f, maxVelFuera = 0.0f, tMaxVelFuera = 0.0f;
	static int dentroPostes = 0, dentroArbitro = 0, verticesDentro = 0;
	static float maxPenetracion = 0.0f, tPenetracion = 0.0f;
	static glm::vec3 antA = posA0, antB = posB0;

	// Cuerdas y postes del ring en el mundo (partes del modelo Ring.obj)
	static std::vector<std::pair<glm::vec3, glm::vec3>> cuerdas, postes;
	static float limiteCuerdas = FLT_MAX;
	if (cuerdas.empty())
	{
		for (const MeshPart& part : ring->asset->model->GetParts())
		{
			glm::vec3 mn, mx;
			TransformBounds(ring->GetMatrix(), part.boundsMin, part.boundsMax, mn, mx);
			if (part.name.rfind("Cylinder", 0) == 0)
			{
				cuerdas.push_back({ mn, mx });
				glm::vec3 d = mx - mn;
				float interior = d.x > d.z ? std::min(fabsf(mn.z), fabsf(mx.z)) : std::min(fabsf(mn.x), fabsf(mx.x));
				limiteCuerdas = std::min(limiteCuerdas, interior);
			}
			else if (part.name.rfind("Poste", 0) == 0)
				postes.push_back({ mn, mx });
		}
		std::cout << "[Prueba combate] " << cuerdas.size() << " cuerdas (cara interior a " << limiteCuerdas << " m del centro), "
			<< postes.size() << " postes" << std::endl;
	}

	auto pulsar = [&](int tecla) { KeyCallback(window, tecla, 0, GLFW_PRESS, 0); KeyCallback(window, tecla, 0, GLFW_RELEASE, 0); };

	if (!enCurso)
	{
		if (repeticion == 0)
			SaveScreenshot(prefijo + "_01_inicio.png");
		if (repeticion == 2)
		{
			std::cout << std::fixed << std::setprecision(3)
				<< "[Prueba combate] punto mas bajo de ambas mallas respecto a la lona: " << minPiso << " m\n"
				<< "[Prueba combate] holgura minima con la cara interior de las cuerdas: " << minCuerdas << " m; vertices dentro de los postes: " << dentroPostes << "\n"
				<< "[Prueba combate] distancia minima entre los cuerpos fuera del contacto: "
				<< (minEntre == FLT_MAX ? std::string("mas de 0.25") : std::to_string(minEntre)) << " m (t = " << tMinEntre << " s)\n"
				<< "[Prueba combate] separacion brazo-rival en el impacto: " << brechaImpacto << " m\n"
				<< "[Prueba combate] vertices de un luchador dentro del otro (> 5 mm): " << verticesDentro << " en total; profundidad maxima "
				<< maxPenetracion << " m (ultimo en t = " << tPenetracion << " s)\n"
				<< "[Prueba combate] distancia minima a la caja del arbitro: " << minArbitro << " m; vertices dentro: " << dentroArbitro << "\n"
				<< "[Prueba combate] velocidad maxima de desplazamiento: " << maxVel << " m/s; de un vertice: " << maxVelVertice
				<< " m/s (t = " << tMaxVelVertice << " s); fuera del golpe: " << maxVelFuera << " m/s (t = " << tMaxVelFuera << " s)"
				<< std::defaultfloat << std::endl;
			glfwSetWindowShouldClose(window, GL_TRUE);
			return;
		}
		repeticion++;
		std::cout << "[Prueba combate] repeticion " << repeticion << std::endl;
		if (repeticion == 2)
			pulsar(GLFW_KEY_T);       // Árbitro y combate a la vez: el combate debe esperar
		pulsar(GLFW_KEY_SPACE);
		enCurso = true;
		pulsoDurante = false;
		siguiente = cuadros = 0;
		inicio = glfwGetTime();
		inicioReal = -1.0;
		return;
	}

	if (combate.Estado() == EstadoCombate::EnEspera)
	{
		if (!arbitro.Activa()) std::cout << "[Prueba combate] ERROR: en espera sin que el arbitro este activo" << std::endl;
		return;
	}
	if (inicioReal < 0.0 && combate.Activa())
	{
		inicioReal = glfwGetTime();
		std::cout << "[Prueba combate] el combate empezo " << inicioReal - inicio << " s despues de pedirlo"
			<< (repeticion == 2 ? " (esperando al arbitro)" : "") << "; arbitro activo: " << arbitro.Activa() << std::endl;
	}

	if (combate.Activa())
	{
		cuadros++;
		float t = combate.Tiempo();
		// Durante el combate: T y Space no deben hacer nada
		if (repeticion == 2 && !pulsoDurante && t > 2.0f)
		{
			pulsar(GLFW_KEY_T);
			pulsar(GLFW_KEY_SPACE);
			pulsoDurante = true;
			std::cout << "[Prueba combate] T y Space durante el combate: arbitro activo " << arbitro.Activa()
				<< ", tiempo del combate " << combate.Tiempo() << " (sigue " << t << ")" << std::endl;
		}

		// Mallas deformadas en el mundo
		static std::vector<glm::vec3> va, vb;
		A->asset->model->SkinPositions(A->skinPalette, va);
		B->asset->model->SkinPositions(B->skinPalette, vb);
		glm::mat4 MA = A->GetMatrix(), MB = B->GetMatrix();
		for (glm::vec3& v : va) v = glm::vec3(MA * glm::vec4(v, 1.0f));
		for (glm::vec3& v : vb) v = glm::vec3(MB * glm::vec4(v, 1.0f));

		glm::vec3 amn, amx, bmn, bmx;
		scene.GetWorldBounds(*arbCuerpo, amn, amx);
		scene.GetWorldBounds(*arbBrazo, bmn, bmx);
		amn = glm::min(amn, bmn); amx = glm::max(amx, bmx);
		for (const std::vector<glm::vec3>* malla : { &va, &vb })
		{
			float lona = (malla == &va ? posA0.y : posB0.y);
			for (const glm::vec3& v : *malla)
			{
				minPiso = std::min(minPiso, v.y - lona);
				minCuerdas = std::min(minCuerdas, limiteCuerdas - std::max(fabsf(v.x), fabsf(v.z)));
				for (auto& p : postes)
					if (glm::all(glm::greaterThanEqual(v, p.first)) && glm::all(glm::lessThanEqual(v, p.second))) dentroPostes++;
				glm::vec3 fuera = glm::max(glm::max(amn - v, v - amx), glm::vec3(0.0f));
				float d = glm::length(fuera);
				minArbitro = std::min(minArbitro, d);
				if (d == 0.0f) dentroArbitro++;
			}
		}

		// Distancia entre los cuerpos: rejilla de 10 cm con los vértices del luchador
		const float CELDA = 0.25f;   // Encuentra cualquier pareja a menos de 25 cm
		std::unordered_map<long long, std::vector<int>> rejilla;
		auto clave = [&](int x, int y, int z) { return ((long long)(x + 1000) << 40) | ((long long)(y + 1000) << 20) | (long long)(z + 1000); };
		for (int i = 0; i < (int)va.size(); i++)
		{
			glm::ivec3 c = glm::ivec3(glm::floor(va[i] / CELDA));
			rejilla[clave(c.x, c.y, c.z)].push_back(i);
		}
		float minimo = FLT_MAX;
		for (const glm::vec3& v : vb)
		{
			glm::ivec3 c = glm::ivec3(glm::floor(v / CELDA));
			for (int dx = -1; dx <= 1; dx++) for (int dy = -1; dy <= 1; dy++) for (int dz = -1; dz <= 1; dz++)
			{
				auto it = rejilla.find(clave(c.x + dx, c.y + dy, c.z + dz));
				if (it == rejilla.end()) continue;
				for (int i : it->second) minimo = std::min(minimo, glm::length(va[i] - v));
			}
		}
		// Penetración: los luchadores se enfrentan a lo largo de X. Para cada columna (y, z) de 2 cm se
		// guarda el intervalo [frente, espalda] en X de cada cuerpo; un vértice del otro cuerpo dentro de
		// ese intervalo está dentro de él (aproximación válida para torso, brazos y piernas).
		{
			const float COL = 0.02f;
			auto columnas = [&](const std::vector<glm::vec3>& vs) {
				std::unordered_map<long long, glm::vec2> c;
				for (const glm::vec3& v : vs)
				{
					long long k = ((long long)(int)floorf(v.y / COL) + 100000) * 1000000LL + ((long long)(int)floorf(v.z / COL) + 100000);
					auto it = c.find(k);
					if (it == c.end()) c[k] = glm::vec2(v.x, v.x);
					else it->second = glm::vec2(std::min(it->second.x, v.x), std::max(it->second.y, v.x));
				}
				return c; };
			auto contar = [&](const std::vector<glm::vec3>& vs, const std::unordered_map<long long, glm::vec2>& c) {
				for (size_t i = 0; i < vs.size(); i++)
				{
					const glm::vec3& v = vs[i];
					long long k = ((long long)(int)floorf(v.y / COL) + 100000) * 1000000LL + ((long long)(int)floorf(v.z / COL) + 100000);
					auto it = c.find(k);
					if (it == c.end() || v.x <= it->second.x || v.x >= it->second.y) continue;
					float prof = std::min(v.x - it->second.x, it->second.y - v.x);
					if (prof > 0.005f) { verticesDentro++; maxPenetracion = std::max(maxPenetracion, prof); tPenetracion = t; }
				} };
			contar(va, columnas(vb));
			contar(vb, columnas(va));
		}

		bool ventanaContacto = t > T_IMPACTO - 0.03f && t < T_IMPACTO + 0.2f;
		if (!ventanaContacto && minimo < minEntre) { minEntre = minimo; tMinEntre = t; }
		if (fabsf(t - T_IMPACTO) < 1e-4f)
			brechaImpacto = minimo;   // En el impacto la pareja más cercana es el brazo con el rival

		// Velocidad de desplazamiento y de cada vértice con el tiempo de la animación (un salto de pose
		// aparecería como una velocidad enorme)
		static std::vector<glm::vec3> pa, pb;
		static float tAnterior = -1.0f;
		float dtAnim = t - tAnterior;
		if (tAnterior >= 0.0f && dtAnim > 1e-4f && pa.size() == va.size() && pb.size() == vb.size())
		{
			maxVel = std::max(maxVel, std::max(glm::length(A->position - antA), glm::length(B->position - antB)) / dtAnim);
			bool golpe = t > T_PREPARA && t < T_IMPACTO + 0.2f;   // Brazo y reacción: rápidos a propósito
			auto revisar = [&](const std::vector<glm::vec3>& ahora, const std::vector<glm::vec3>& antes) {
				for (size_t i = 0; i < ahora.size(); i++)
				{
					float v = glm::length(ahora[i] - antes[i]) / dtAnim;
					if (v > maxVelVertice) { maxVelVertice = v; tMaxVelVertice = t; }
					if (!golpe && v > maxVelFuera) { maxVelFuera = v; tMaxVelFuera = t; }
				} };
			revisar(va, pa);
			revisar(vb, pb);
		}
		pa = va; pb = vb; tAnterior = t;
		antA = A->position; antB = B->position;

		// Capturas en instantes exactos: el siguiente cuadro avanza justo hasta el próximo instante
		if (repeticion == 1 && siguiente < NUM_CAPTURAS && t >= capturas[siguiente].t - 1e-4f)
		{
			std::cout << "[Prueba combate] t = " << t << " s, fases: " << combate.FaseActual()
				<< "; luchador (" << A->position.x << ", " << A->position.y << ", " << A->position.z << ") penta ("
				<< B->position.x << ", " << B->position.y << ", " << B->position.z << "), distancia entre cuerpos "
				<< (minimo == FLT_MAX ? std::string("> 0.25") : std::to_string(minimo)) << " m" << std::endl;
			char nombre[96];
			snprintf(nombre, sizeof(nombre), "_%02d_%s.png", siguiente + 2, capturas[siguiente].nombre);
			SaveScreenshot(prefijo + nombre);
			siguiente++;
		}
		limiteDeltaCombate = (repeticion == 1 && siguiente < NUM_CAPTURAS) ? std::max(capturas[siguiente].t - t, 0.0f)
			: (fabsf(t - T_IMPACTO) > 1e-4f && t < T_IMPACTO ? T_IMPACTO - t : FLT_MAX);
		if (limiteDeltaCombate <= 0.0f) limiteDeltaCombate = FLT_MAX;
		return;
	}

	// Terminó: el cuadro ya muestra el reposo
	limiteDeltaCombate = FLT_MAX;
	float errPos = glm::length(A->position - posA0) + glm::length(B->position - posB0);
	float errRot = glm::length(A->rotation - rotA0) + glm::length(B->rotation - rotB0);
	double duracion = glfwGetTime() - inicioReal;
	std::cout << "[Prueba combate] repeticion " << repeticion << " terminada: " << cuadros << " cuadros en " << duracion << " s ("
		<< cuadros / duracion << " fps, secuencia de " << combate.Duracion() << " s); error final: posicion " << errPos
		<< " m, rotacion " << errRot << " grados, paletas vacias: " << (A->skinPalette.empty() && B->skinPalette.empty()) << std::endl;
	if (repeticion == 1)
		SaveScreenshot(prefijo + "_11_fin.png");
	enCurso = false;
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

// ---------------- Espectador E: cadenas hombro -> codo y tiro de la cerveza

// Interpola las claves en el instante t. La pose depende solo de t (no se acumula nada).
glm::vec4 EvaluarClavesE(const ClaveBrazoE* claves, int n, float t)
{
	for (int i = 1; i < n; i++)
		if (t < claves[i].t)
		{
			float a = glm::clamp((t - claves[i - 1].t) / (claves[i].t - claves[i - 1].t), 0.0f, 1.0f);
			float c = claves[i].curva == 'a' ? a * a                                   // acelera
				: claves[i].curva == 'f' ? 1.0f - (1.0f - a) * (1.0f - a)              // frena
				: a * a * (3.0f - 2.0f * a);                                           // suave
			return glm::mix(claves[i - 1].pose, claves[i].pose, c);
		}
	return claves[n - 1].pose;
}

// Giros de la cadena del brazo (marco del modelo, antes del yaw del cuerpo). lado: +1 izq., -1 der.
struct CadenaBrazoE { glm::mat4 Rh, Rc; glm::vec3 hombro, codo, codoMovido; };
CadenaBrazoE CadenaE(float lado, glm::vec4 p, glm::vec3 ejeCodo)
{
	CadenaBrazoE c;
	c.hombro = lado > 0.0f ? HOMBRO_IZQ : HOMBRO_DER;
	c.codo = lado > 0.0f ? CODO_IZQ : CODO_DER;
	// Rh = Rz(abducción) · Rx(-flexión) · Ry(giro sobre el eje del brazo, que en reposo es vertical)
	c.Rh = glm::rotate(glm::mat4(1.0f), glm::radians(p.y * lado), glm::vec3(0.0f, 0.0f, 1.0f));
	c.Rh = glm::rotate(c.Rh, glm::radians(-p.x), glm::vec3(1.0f, 0.0f, 0.0f));
	c.Rh = glm::rotate(c.Rh, glm::radians(p.z * lado), glm::vec3(0.0f, 1.0f, 0.0f));
	c.Rc = glm::rotate(glm::mat4(1.0f), glm::radians(p.w), ejeCodo);
	c.codoMovido = c.hombro + glm::vec3(c.Rh * glm::vec4(c.codo - c.hombro, 0.0f));
	return c;
}

glm::mat4 YawCuerpoE() { return EulerRotation(glm::vec3(0.0f, objE_cuerpo->rotation.y, 0.0f)); }

// Punto que va con el antebrazo (coordenadas del modelo de E) llevado al mundo
glm::vec3 PuntoAntebrazoE(const CadenaBrazoE& c, glm::vec3 punto)
{
	glm::vec3 p = c.codoMovido + glm::vec3(c.Rh * c.Rc * glm::vec4(punto - c.codo, 0.0f));
	return objE_cuerpo->position + glm::vec3(YawCuerpoE() * glm::vec4(p, 0.0f));
}

// La pieza gira con R (marco del modelo) alrededor de "articulacion", que queda en "destino".
// "agarre" es el punto del cuerpo donde va el origen del modelo de la pieza (la base de la botella).
void ColocarPiezaE(SceneObject* pieza, glm::vec3 articulacion, glm::vec3 destino, const glm::mat4& R, glm::vec3 agarre)
{
	glm::mat4 yaw = YawCuerpoE();
	float ry, rx, rz;
	glm::extractEulerAngleYXZ(yaw * R, ry, rx, rz);              // Mismo orden que EulerRotation
	pieza->position = objE_cuerpo->position + glm::vec3(yaw * glm::vec4(destino, 0.0f));
	pieza->rotation = glm::degrees(glm::vec3(rx, ry, rz));
	pieza->pivotOffset = articulacion - agarre;
	pieza->scale = glm::vec3(1.0f);
}

void ColocarBrazoE(const CadenaBrazoE& c, SceneObject* brazo, SceneObject* antebrazo, SceneObject* mano,
	SceneObject* botella, glm::vec3 agarre)
{
	ColocarPiezaE(brazo, c.hombro, c.hombro, c.Rh, glm::vec3(0.0f));
	ColocarPiezaE(antebrazo, c.codo, c.codoMovido, c.Rh * c.Rc, glm::vec3(0.0f));
	ColocarPiezaE(mano, c.codo, c.codoMovido, c.Rh * c.Rc, glm::vec3(0.0f));
	if (botella)
		ColocarPiezaE(botella, c.codo, c.codoMovido, c.Rh * c.Rc, agarre);
}

// Tiro de la cerveza: se calcula una sola vez a partir de la pose al soltar (no depende de los FPS).
// Dirección de salida = dirección de la mano en ese instante (su elevación, limitada para librar las
// cuerdas); rapidez = la que lleva el tiro parabólico con g = 9.81 m/s² hasta el punto de caída.
struct TiroCerveza
{
	bool listo = false;
	glm::vec3 inicio, velocidad, fin;
	float duracion = 1.0f, elevacion = 0.0f, rapidezMano = 0.0f;
	glm::mat4 giroInicial = glm::mat4(1.0f);     // Rotación de la botella (mundo) al soltarla
	glm::vec3 ejeGiro = glm::vec3(1.0f, 0.0f, 0.0f);
	float vueltas = 0.0f;                        // Radianes que gira en el aire hasta caer acostada
} tiro;

const glm::vec3 EJE_CODO_LANZAR(-1.0f, 0.0f, 0.0f);       // Codo izquierdo: solo plano sagital

void PrepararTiroCerveza()
{
	auto centro = [](float t) {
		return PuntoAntebrazoE(CadenaE(1.0f, EvaluarClavesE(CLAVES_LANZAR, 7, t), EJE_CODO_LANZAR), AGARRE_IZQ + CENTRO_BOTELLA);
	};
	const float e = 0.005f;
	tiro.inicio = centro(SUELTA_CERVEZA);
	glm::vec3 vMano = (tiro.inicio - centro(SUELTA_CERVEZA - e)) / e;
	tiro.rapidezMano = glm::length(vMano);
	tiro.fin = glm::vec3(tiro.inicio.x + CAIDA_CERVEZA.x, BURRITO_FIN.y + RADIO_BOTELLA, CAIDA_CERVEZA.y);

	glm::vec2 horizontal(tiro.fin.x - tiro.inicio.x, tiro.fin.z - tiro.inicio.z);
	float d = glm::length(horizontal), dy = tiro.fin.y - tiro.inicio.y;
	float elev = glm::degrees(atan2f(vMano.y, glm::length(glm::vec2(vMano.x, vMano.z))));
	tiro.elevacion = glm::clamp(elev, ELEVACION_MIN, ELEVACION_MAX);
	float tanA = tanf(glm::radians(tiro.elevacion));
	// d = vh·T  y  dy = vh·tanA·T - g·T²/2   ->   T = sqrt(2·(d·tanA - dy) / g)
	tiro.duracion = sqrtf(std::max(2.0f * (d * tanA - dy) / GRAVEDAD, 0.01f));
	float vh = d / tiro.duracion;
	glm::vec2 dir = horizontal / std::max(d, 1e-4f);
	tiro.velocidad = glm::vec3(dir.x * vh, vh * tanA, dir.y * vh);

	// Gira hacia adelante (como la mano) ~1.5 vueltas y cae acostada: el eje de la botella horizontal
	CadenaBrazoE c = CadenaE(1.0f, EvaluarClavesE(CLAVES_LANZAR, 7, SUELTA_CERVEZA), EJE_CODO_LANZAR);
	tiro.giroInicial = YawCuerpoE() * c.Rh * c.Rc;
	tiro.ejeGiro = glm::normalize(glm::cross(glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(dir.x, 0.0f, dir.y)));
	glm::vec3 a = glm::vec3(tiro.giroInicial * glm::vec4(0.0f, 1.0f, 0.0f, 0.0f));   // Eje de la botella
	glm::vec3 ka = glm::cross(tiro.ejeGiro, a);
	float acostada = atan2f(-a.y, ka.y);                                            // Ángulo con a.y = 0
	float pi = glm::pi<float>();
	tiro.vueltas = acostada + pi * ceilf((glm::radians(450.0f) - acostada) / pi);
	tiro.listo = true;
	std::cout << "Cerveza (Z): sale a " << glm::length(tiro.velocidad) << " m/s con " << tiro.elevacion
		<< " grados (la mano va a " << tiro.rapidezMano << " m/s y " << elev << " grados), vuela "
		<< tiro.duracion << " s y gira " << glm::degrees(tiro.vueltas) << " grados" << std::endl;
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

	// Árbitro (T): la pose se calcula con el tiempo transcurrido de la secuencia
	arbitro.Actualizar(deltaTime);

	// Combate (Space): empieza cuando el árbitro no está en su secuencia
	float dtCombate = deltaCombateFijo > 0.0f ? deltaCombateFijo : std::min(deltaTime, limiteDeltaCombate);
	combate.Actualizar(dtCombate, !arbitro.Activa());

	// ---------------- Burrito volador (B): tiro parabólico desde las gradas hasta la lona
	if (SceneObject* burrito = objBurrito)
	{
		burrito->visible = burritoLanzado;
		if (burritoLanzado)
		{
			tiempoVueloBurrito += deltaTime;
			const float vuelo = sqrtf(8.0f * BURRITO_ALTURA / GRAVEDAD);    // Aceleración vertical = g
			const glm::vec3 fin = BURRITO_FIN + glm::vec3(0.0f, apoyoBurrito, 0.0f);
			float t = tiempoVueloBurrito, escala = 1.0f;
			float u = std::min(t / vuelo, 1.0f);
			burrito->position = glm::mix(BURRITO_INICIO, fin, u);
			burrito->position.y += 4.0f * BURRITO_ALTURA * u * (1.0f - u);
			burrito->rotation = glm::vec3(450.0f * u, 0.0f, 0.0f);         // 1¼ vueltas: cae acostado
			if (t > vuelo + REPOSO_EN_LONA)                                 // Se encoge sobre la lona
			{
				float a = std::min((t - vuelo - REPOSO_EN_LONA) / ENCOGER, 1.0f);
				escala = 1.0f - a * a * (3.0f - 2.0f * a);
				burrito->position.y = BURRITO_FIN.y + apoyoBurrito * escala;
			}
			burrito->scale = glm::vec3(escala);
			if (t >= vuelo + REPOSO_EN_LONA + ENCOGER)
			{
				burritoLanzado = false;
				burrito->visible = false;
				burrito->scale = glm::vec3(1.0f);
			}
		}
	}

	// ---------------- Espectador E: lanzar cerveza (Z) y beber (X)
	if (objE_cuerpo && objE_brazoIzq && objE_antebrazoIzq && objE_manoIzq && objE_brazoDer && objE_antebrazoDer
		&& objE_manoDer && objCervezaIzq && objCervezaDer)
	{
		if (!tiro.listo)
			PrepararTiroCerveza();

		// Brazo izquierdo (Z). Fases de la botella: en la mano, en el aire, sobre la lona, se encoge,
		// y una nueva crece en la mano (con el brazo ya en reposo).
		if (lanzandoCerveza)
			tiempoLanzamiento += deltaTime;
		float t = lanzandoCerveza ? tiempoLanzamiento : 0.0f;
		CadenaBrazoE izq = CadenaE(1.0f, EvaluarClavesE(CLAVES_LANZAR, 7, t), EJE_CODO_LANZAR);
		const float cae = SUELTA_CERVEZA + tiro.duracion;
		const float desaparece = cae + REPOSO_EN_LONA + ENCOGER;
		const float NUEVA = 0.3f;                                          // Crece la nueva botella
		bool enMano = !lanzandoCerveza || t < SUELTA_CERVEZA;
		ColocarBrazoE(izq, objE_brazoIzq, objE_antebrazoIzq, objE_manoIzq, enMano ? objCervezaIzq : nullptr, AGARRE_IZQ);
		objCervezaIzq->visible = true;
		if (!enMano && t < desaparece)
		{
			float s = std::min(t - SUELTA_CERVEZA, tiro.duracion);
			// p(s) = p0 + v·s - g·s²/2 ; gira a velocidad constante alrededor del eje horizontal
			objCervezaIzq->position = tiro.inicio + tiro.velocidad * s + glm::vec3(0.0f, -0.5f * GRAVEDAD * s * s, 0.0f);
			glm::mat4 R = glm::rotate(glm::mat4(1.0f), tiro.vueltas * s / tiro.duracion, tiro.ejeGiro) * tiro.giroInicial;
			float ry, rx, rz;
			glm::extractEulerAngleYXZ(R, ry, rx, rz);
			objCervezaIzq->rotation = glm::degrees(glm::vec3(rx, ry, rz));
			objCervezaIzq->pivotOffset = CENTRO_BOTELLA;                   // Gira sobre su centro
			float escala = 1.0f;
			if (t > cae + REPOSO_EN_LONA)
			{
				float a = std::min((t - cae - REPOSO_EN_LONA) / ENCOGER, 1.0f);
				escala = 1.0f - a * a * (3.0f - 2.0f * a);
				objCervezaIzq->position.y = tiro.fin.y + RADIO_BOTELLA * (escala - 1.0f);
			}
			objCervezaIzq->scale = glm::vec3(escala);
		}
		else if (!enMano)
		{
			// Nueva botella en la mano: crece desde su base, sobre el regazo
			float a = std::min((t - desaparece) / NUEVA, 1.0f);
			objCervezaIzq->position = objE_cuerpo->position + glm::vec3(YawCuerpoE() * glm::vec4(AGARRE_IZQ, 0.0f));
			objCervezaIzq->rotation = glm::vec3(0.0f, objE_cuerpo->rotation.y, 0.0f);
			objCervezaIzq->pivotOffset = glm::vec3(0.0f);
			objCervezaIzq->scale = glm::vec3(a * a * (3.0f - 2.0f * a));
			if (a >= 1.0f)
			{
				lanzandoCerveza = false;
				ColocarBrazoE(izq, objE_brazoIzq, objE_antebrazoIzq, objE_manoIzq, objCervezaIzq, AGARRE_IZQ);
			}
		}

		// Brazo derecho (X): sube la botella a la boca, da el trago levantando el codo y baja
		if (bebiendo)
		{
			tiempoBebida += deltaTime;
			if (tiempoBebida >= CLAVES_BEBER[4].t)
				bebiendo = false;
		}
		glm::vec3 ejeCodoDer = glm::normalize(glm::cross(glm::vec3(0.0f, -1.0f, 0.0f), MANO_DER - CODO_DER));
		CadenaBrazoE der = CadenaE(-1.0f, EvaluarClavesE(CLAVES_BEBER, 5, bebiendo ? tiempoBebida : 0.0f), ejeCodoDer);
		ColocarBrazoE(der, objE_brazoDer, objE_antebrazoDer, objE_manoDer, objCervezaDer, AGARRE_DER);
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
		// R: regresar a la vista inicial; 1-8: vistas predefinidas
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
		// (mientras el burrito está en el aire o sobre la lona, la tecla no lo reinicia)
		if (key == GLFW_KEY_B && !burritoLanzado) {
			burritoLanzado = true;
			tiempoVueloBurrito = 0.0f;
			std::cout << "Burrito: lanzado desde las gradas" << std::endl;
		}

		// Z: el espectador E lanza la cerveza con el brazo izquierdo (no se reinicia a mitad del tiro)
		if (key == GLFW_KEY_Z && !lanzandoCerveza) {
			lanzandoCerveza = true;
			tiempoLanzamiento = 0.0f;
			std::cout << "Espectador E: lanza la cerveza" << std::endl;
		}

		// X: el espectador E sube la botella derecha a la boca y bebe
		if (key == GLFW_KEY_X && !bebiendo) {
			bebiendo = true;
			tiempoBebida = 0.0f;
			std::cout << "Espectador E: bebe" << std::endl;
		}

		// Input 2: Cambiar color de luces en tiempo real al presionar 'L'
		if (key == GLFW_KEY_L) {
			luzEscenarioActiva = !luzEscenarioActiva;
			colorLuzSpot = luzEscenarioActiva ? glm::vec3(1.0f, 0.2f, 0.2f) : glm::vec3(0.2f, 0.2f, 1.0f);
			for (PointLightParams& light : lightRig.pointLights)
				light.diffuse = colorLuzSpot;
		}

		// Input 3: animación del árbitro con la tecla 'T' (antes era 'A', que también mueve la
		// cámara). Si la secuencia está en curso se ignora; al terminar se puede repetir.
		if (key == GLFW_KEY_T)
		{
			if (combate.Ocupado())
				std::cout << "Arbitro: espera a que termine el combate" << std::endl;
			else if (arbitro.Iniciar())
				std::cout << "Arbitro: inicia la secuencia (" << arbitro.Duracion() << " s)" << std::endl;
		}

		// Input 4: combate con la barra espaciadora. Repetir la tecla durante el combate no lo reinicia;
		// si el árbitro está en su secuencia, el combate empieza cuando él termina.
		if (key == GLFW_KEY_SPACE && combate.Solicitar())
			std::cout << (arbitro.Activa() ? "Combate: en espera, inicia cuando el arbitro termine" : "Combate: inicia")
				<< " (" << combate.Duracion() << " s)" << std::endl;

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
