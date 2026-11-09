#pragma once

// =====================================================================================
//  ANIMACIÓN DE COMBATE (barra espaciadora), generada por código
//
//  Los dos luchadores tienen esqueleto Mixamo (huesos + pesos por vértice). El motor solo dibujaba
//  la pose de enlace; ahora Model::ComputePose recibe los giros de cada articulación calculados
//  aquí y lighting.vs deforma la malla (skinning). Las animaciones que traen los FBX no se usan.
//
//  Secuencia (atacante = "luchador", rival = "penta"):
//    1. Guardia: ambos flexionan las rodillas y suben los brazos.
//    2. El rival provoca abriendo los brazos ("¡pégame!"); el atacante camina hacia él.
//    3. El atacante prepara y lanza un golpe de mano abierta al pecho (chop).
//    4. El rival recibe el impacto: se echa hacia atrás, retrocede tambaleándose y se dobla
//       sujetándose el pecho; el atacante celebra con el puño en alto.
//    5. Ambos recuperan la guardia.
//    6. Regresan caminando a sus posiciones y vuelven a su pose original.
//
//  Técnicas:
//    - Cinemática directa sobre la jerarquía de huesos: G_i = G_padre * bindLocal_i * giro_i.
//    - Pose semántica por personaje (rodillas, torso, cabeza, dirección del brazo y codo) que se
//      traduce a giros de huesos; los brazos se orientan con la rotación mínima entre su dirección
//      de enlace y la deseada.
//    - Claves de pose interpoladas con curvas (suave, aceleración del golpe, frenado del impacto).
//    - Ciclo de pasos procedural según la distancia recorrida (muslos, rodillas y braceo).
//    - Contacto con la lona: cada cuadro se coloca el cuerpo para que el punto más bajo de las
//      suelas (vértices reales del pie, deformados con la pose) quede sobre la lona.
//    - Coordinación con la geometría real: el atacante se detiene a DISTANCIA_PECHOS del rival
//      (medida con los vértices de ambos torsos) y la pose de impacto es el primer instante del
//      barrido del golpe en que algún vértice de su brazo llega a HOLGURA_GOLPE del cuerpo del
//      rival (búsqueda en pasos de 1 % y bisección). Así el brazo se detiene al tocarlo y no lo
//      atraviesa. La reacción del rival empieza en ese instante y su intensidad depende de la
//      velocidad de la mano en el impacto.
//
//  La pose de cada personaje es una función del tiempo de la secuencia y de su transformación
//  inicial guardada: no se acumulan transformaciones de un cuadro a otro.
// =====================================================================================

#include <vector>
#include <string>
#include <cmath>
#include <cfloat>
#include <algorithm>
#include <unordered_map>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "Scene.h"

// ---------------- Parámetros (todos los ajustes de la animación están aquí)
namespace CombateParams
{
	// Momentos clave (segundos desde que inicia la secuencia)
	const float T_GUARDIA = 0.7f;          // Ambos terminan de ponerse en guardia
	const float T_PROVOCA = 1.4f;          // El rival termina de abrir los brazos
	const float T_INICIO_CAMINAR = 1.0f;   // El atacante empieza a caminar
	const float T_LLEGA = 2.4f;            // El atacante llega frente al rival
	const float T_PREPARA = 2.75f;         // Brazo atrás, listo para golpear
	const float T_IMPACTO = 3.05f;         // El brazo toca al rival (empieza su reacción)
	const float T_RECUPERA = 5.0f;         // Ambos de nuevo en guardia
	const float T_INICIO_REGRESO = 5.2f;
	const float T_FIN_REGRESO = 6.8f;
	const float T_FIN = 7.6f;              // Pose original (se relajan los brazos y las rodillas)

	// Distancias
	const float PASO = 0.45f;              // Longitud aproximada de un paso (m)
	const float DISTANCIA_PECHOS = 0.40f;  // Del frente del torso del atacante al del rival al golpear (m)
	const float HOLGURA_GOLPE = 0.02f;     // Separación mínima entre vértices del brazo y del rival al impactar (m)
	const float RETROCESO_RIVAL = 0.45f;   // Metros que retrocede el rival (con intensidad 1)
	const float RETROCESO_ATACANTE = 0.25f;

	// Amplitudes de los pasos
	const float MUSLO_PASO = 22.0f;        // Grados que avanza/retrocede cada muslo
	const float RODILLA_PASO = 38.0f;      // Flexión de la rodilla de la pierna en el aire
	const float BRACEO = 12.0f;            // Braceo al caminar

	// Reacción del rival
	const float VELOCIDAD_REFERENCIA = 10.0f; // m/s de la mano que dan intensidad 1
	const float INTENSIDAD_MIN = 0.7f, INTENSIDAD_MAX = 1.3f;

	// Paso máximo de tiempo por cuadro (ver AnimacionArbitro.h)
	const float PASO_MAXIMO = 0.1f;
}

// Huesos que se controlan (nombres del esqueleto Mixamo, iguales en ambos modelos)
enum HuesoCombate
{
	H_COLUMNA, H_COLUMNA1, H_COLUMNA2, H_CUELLO, H_CABEZA,
	H_BRAZO_I, H_ANTEBRAZO_I, H_MANO_I, H_BRAZO_D, H_ANTEBRAZO_D, H_MANO_D,
	H_MUSLO_I, H_PIERNA_I, H_PIE_I, H_DEDOS_I, H_MUSLO_D, H_PIERNA_D, H_PIE_D, H_DEDOS_D,
	NUM_HUESOS_COMBATE
};
static const char* NOMBRES_HUESOS_COMBATE[NUM_HUESOS_COMBATE] = {
	"mixamorig:Spine", "mixamorig:Spine1", "mixamorig:Spine2", "mixamorig:Neck", "mixamorig:Head",
	"mixamorig:LeftArm", "mixamorig:LeftForeArm", "mixamorig:LeftHand",
	"mixamorig:RightArm", "mixamorig:RightForeArm", "mixamorig:RightHand",
	"mixamorig:LeftUpLeg", "mixamorig:LeftLeg", "mixamorig:LeftFoot", "mixamorig:LeftToeBase",
	"mixamorig:RightUpLeg", "mixamorig:RightLeg", "mixamorig:RightFoot", "mixamorig:RightToeBase" };

// Brazo: dirección del brazo (elevación sobre la horizontal y azimut: 0 = hacia el costado,
// +90 = al frente, -90 = atrás), plano del codo y flexión del codo. Grados.
// giro = hacia dónde se dobla el antebrazo alrededor del brazo: 0 = hacia arriba (puño frente a la
// cara en la guardia); valores positivos lo llevan hacia el centro del cuerpo.
struct PoseBrazo
{
	float elevacion = 0.0f, azimut = 0.0f, giro = 0.0f, codo = 0.0f;
};

// Pose semántica de un luchador (grados y metros). La pose de enlace usa los ángulos de brazo
// medidos en su esqueleto y todo lo demás en cero.
struct PoseCombate
{
	float avance = 0.0f;        // Metros hacia el frente original
	float yaw = 0.0f;           // Giro del cuerpo completo
	float inclinacion = 0.0f;   // Cuerpo completo hacia adelante (+) / atrás (-), apoyado en los pies
	float agachar = 0.0f;       // Flexión de cadera; las rodillas se doblan el doble y los pies quedan planos
	glm::vec3 torso = glm::vec3(0.0f);   // x: adelante (+), y: giro hacia su izquierda (+), z: ladeo
	glm::vec2 cabeza = glm::vec2(0.0f);  // x: abajo (+), y: giro hacia su izquierda (+)
	PoseBrazo brazoI, brazoD;
	float caminar = 0.0f;       // 1 = el tramo que termina en esta clave es una caminata (ciclo de pasos)
	// Capa procedural del ciclo de pasos (la calcula Evaluar; no se interpola entre claves)
	float musloI = 0.0f, musloD = 0.0f, rodillaI = 0.0f, rodillaD = 0.0f;
};

inline PoseCombate MezclarPoses(const PoseCombate& a, const PoseCombate& b, float s)
{
	auto m = [s](float x, float y) { return x + (y - x) * s; };
	auto mb = [&](const PoseBrazo& x, const PoseBrazo& y) {
		PoseBrazo r; r.elevacion = m(x.elevacion, y.elevacion); r.azimut = m(x.azimut, y.azimut);
		r.giro = m(x.giro, y.giro); r.codo = m(x.codo, y.codo); return r; };
	PoseCombate p;
	p.avance = m(a.avance, b.avance); p.yaw = m(a.yaw, b.yaw); p.inclinacion = m(a.inclinacion, b.inclinacion);
	p.agachar = m(a.agachar, b.agachar);
	p.torso = a.torso + (b.torso - a.torso) * s;
	p.cabeza = a.cabeza + (b.cabeza - a.cabeza) * s;
	p.brazoI = mb(a.brazoI, b.brazoI);
	p.brazoD = mb(a.brazoD, b.brazoD);
	p.caminar = b.caminar;
	return p;
}

// Curvas de interpolación del tramo que termina en una clave
enum class Curva { Suave, Acelera, Frena };

inline float AplicarCurva(Curva c, float u)
{
	u = glm::clamp(u, 0.0f, 1.0f);
	switch (c)
	{
	case Curva::Acelera: return u * u;                                  // El golpe llega a toda velocidad
	case Curva::Frena: return 1.0f - (1.0f - u) * (1.0f - u) * (1.0f - u); // Sale disparado y se frena
	default: return u * u * (3.0f - 2.0f * u);                         // Arranca y frena suave
	}
}

struct ClaveCombate
{
	float t;
	PoseCombate pose;
	Curva curva;
	const char* fase;   // Nombre de la fase que termina en esta clave (para depurar y probar)
};

// ---------------- Un luchador: esqueleto, pose inicial, claves y colocación sobre la lona
class LuchadorAnimado
{
public:
	SceneObject* obj = nullptr;
	const Model* modelo = nullptr;
	glm::vec3 posInicial = glm::vec3(0.0f), rotInicial = glm::vec3(0.0f);
	PoseCombate reposo;                 // Pose de enlace expresada como PoseCombate
	std::vector<ClaveCombate> claves;
	int hueso[NUM_HUESOS_COMBATE];
	glm::vec3 dirBrazoI = glm::vec3(1, 0, 0), dirBrazoD = glm::vec3(-1, 0, 0);   // Dirección de enlace (brazo)
	glm::vec3 bisagraI = glm::vec3(0, 1, 0), bisagraD = glm::vec3(0, 1, 0);      // Eje del codo
	glm::vec3 flexionI = glm::vec3(0, 0, 1), flexionD = glm::vec3(0, 0, 1);      // Hacia dónde se dobla el antebrazo (enlace)
	std::vector<size_t> suela;          // Vértices de las suelas (contacto con la lona)
	std::vector<size_t> palmaD;         // Vértices de la mano derecha (incluye dedos)
	std::vector<size_t> pecho;          // Vértices del pecho (Spine1/Spine2)
	std::vector<size_t> brazoDerecho;   // Brazo, antebrazo y mano derechos (lo que golpea)

	// Resultado de la última pose
	std::vector<glm::mat4> globales, paleta;

	bool Inicializar(SceneObject* o)
	{
		this->obj = o;
		if (!o || !o->asset || !o->asset->IsReady() || !o->asset->model->HasSkeleton())
			return false;
		this->modelo = o->asset->model.get();
		this->posInicial = o->position;
		this->rotInicial = o->rotation;
		for (int i = 0; i < NUM_HUESOS_COMBATE; i++)
		{
			this->hueso[i] = this->modelo->FindBone(NOMBRES_HUESOS_COMBATE[i]);
			if (this->hueso[i] < 0)
			{
				std::cout << "[Combate] AVISO: '" << o->id << "' no tiene el hueso " << NOMBRES_HUESOS_COMBATE[i] << std::endl;
				return false;
			}
		}
		const std::vector<SkeletonBone>& b = this->modelo->GetBones();
		auto pos = [&](int h) { return glm::vec3(b[this->hueso[h]].bindGlobal[3]); };
		this->dirBrazoI = glm::normalize(pos(H_ANTEBRAZO_I) - pos(H_BRAZO_I));
		this->dirBrazoD = glm::normalize(pos(H_ANTEBRAZO_D) - pos(H_BRAZO_D));
		// El codo se flexiona hacia el frente: eje perpendicular al antebrazo y al frente (+Z)
		this->bisagraI = glm::normalize(glm::cross(glm::normalize(pos(H_MANO_I) - pos(H_ANTEBRAZO_I)), glm::vec3(0, 0, 1)));
		this->bisagraD = glm::normalize(glm::cross(glm::normalize(pos(H_MANO_D) - pos(H_ANTEBRAZO_D)), glm::vec3(0, 0, 1)));
		this->flexionI = glm::normalize(glm::cross(this->bisagraI, this->dirBrazoI));
		this->flexionD = glm::normalize(glm::cross(this->bisagraD, this->dirBrazoD));
		this->reposo = PoseCombate();
		this->reposo.brazoI = AngulosBrazo(this->dirBrazoI, 1.0f);
		this->reposo.brazoD = AngulosBrazo(this->dirBrazoD, -1.0f);
		// Plano del codo de la pose de enlace (así la pose de reposo no gira el brazo)
		this->reposo.brazoI.giro = glm::degrees(AnguloFirmado(ReferenciaCodo(this->dirBrazoI), this->flexionI, this->dirBrazoI));
		this->reposo.brazoD.giro = -glm::degrees(AnguloFirmado(ReferenciaCodo(this->dirBrazoD), this->flexionD, this->dirBrazoD));

		// Vértices de referencia agrupados por el hueso que más los mueve
		auto descendiente = [&](int bone, int ancestro) {
			for (int k = bone; k >= 0; k = b[k].parent) if (k == ancestro) return true;
			return false; };
		std::vector<glm::vec3> enlace;
		this->modelo->SkinPositions(std::vector<glm::mat4>(), enlace);
		float minY = FLT_MAX;
		for (const glm::vec3& p : enlace) minY = std::min(minY, p.y);
		float alto = 0.0f;
		for (const glm::vec3& p : enlace) alto = std::max(alto, p.y - minY);
		for (size_t v = 0; v < enlace.size(); v++)
		{
			int d = this->modelo->DominantBone(v);
			if (d < 0) continue;
			bool pie = descendiente(d, this->hueso[H_PIE_I]) || descendiente(d, this->hueso[H_PIE_D]);
			if (pie && enlace[v].y < minY + 0.04f * alto) this->suela.push_back(v);   // 4 % inferior de la altura
			if (descendiente(d, this->hueso[H_MANO_D])) this->palmaD.push_back(v);
			if (d == this->hueso[H_COLUMNA1] || d == this->hueso[H_COLUMNA2]) this->pecho.push_back(v);
			if (descendiente(d, this->hueso[H_BRAZO_D])) this->brazoDerecho.push_back(v);
		}
		return !this->suela.empty() && !this->palmaD.empty() && !this->pecho.empty();
	}

	// Elevación y azimut de una dirección de brazo (lado: +1 izquierdo, -1 derecho)
	static PoseBrazo AngulosBrazo(const glm::vec3& d, float lado)
	{
		PoseBrazo a;
		a.elevacion = glm::degrees(asinf(glm::clamp(d.y, -1.0f, 1.0f)));
		a.azimut = glm::degrees(atan2f(d.z, lado * d.x));
		return a;
	}

	PoseCombate Evaluar(float t, const char** fase = nullptr) const
	{
		if (this->claves.empty()) return this->reposo;
		if (t <= this->claves.front().t) { if (fase) *fase = this->claves.front().fase; return this->claves.front().pose; }
		for (size_t k = 1; k < this->claves.size(); k++)
		{
			const ClaveCombate& a = this->claves[k - 1];
			const ClaveCombate& b = this->claves[k];
			if (t <= b.t)
			{
				if (fase) *fase = b.fase;
				float u = (b.t > a.t) ? (t - a.t) / (b.t - a.t) : 1.0f;
				PoseCombate p = MezclarPoses(a.pose, b.pose, AplicarCurva(b.curva, u));
				if (b.pose.caminar > 0.0f)
					this->agregarPasos(p, a.pose.avance, b.pose.avance);
				return p;
			}
		}
		if (fase) *fase = this->claves.back().fase;
		return this->claves.back().pose;
	}

	// Calcula la paleta de huesos y coloca el objeto para la pose dada
	void Aplicar(const PoseCombate& p)
	{
		using namespace CombateParams;
		std::vector<glm::mat3> giros(this->modelo->GetBones().size(), glm::mat3(1.0f));
		auto G = [&](int h) -> glm::mat3& { return giros[this->hueso[h]]; };

		// Torso repartido en las tres vértebras y cabeza en cuello + cabeza
		const float reparto[3] = { 0.3f, 0.35f, 0.35f };
		for (int i = 0; i < 3; i++)
			G(H_COLUMNA + i) = Euler(p.torso * reparto[i]);
		G(H_CUELLO) = Euler(glm::vec3(p.cabeza.x * 0.4f, p.cabeza.y * 0.4f, 0.0f));
		G(H_CABEZA) = Euler(glm::vec3(p.cabeza.x * 0.6f, p.cabeza.y * 0.6f, 0.0f));

		// Brazos: orientar el brazo (rotación mínima desde la dirección de enlace), girar sobre su eje y doblar el codo
		// El plano del codo se fija explícitamente: tras orientar el brazo se gira sobre su eje hasta que
		// la dirección de flexión del enlace coincida con la pedida (ReferenciaCodo girada 'giro').
		auto brazo = [&](const PoseBrazo& a, float lado, const glm::vec3& enlace, const glm::vec3& bisagra,
			const glm::vec3& flexion, int hBrazo, int hAntebrazo)
		{
			float el = glm::radians(a.elevacion), az = glm::radians(a.azimut);
			glm::vec3 d(lado * cosf(el) * cosf(az), sinf(el), cosf(el) * sinf(az));
			glm::mat3 orientar = RotacionMinima(enlace, d);
			glm::vec3 deseada = Rot(lado * a.giro, d) * ReferenciaCodo(d);
			float ajuste = glm::degrees(AnguloFirmado(orientar * flexion, deseada, d));
			G(hBrazo) = Rot(ajuste, d) * orientar;
			G(hAntebrazo) = Rot(a.codo, bisagra);
		};
		brazo(p.brazoI, 1.0f, this->dirBrazoI, this->bisagraI, this->flexionI, H_BRAZO_I, H_ANTEBRAZO_I);
		brazo(p.brazoD, -1.0f, this->dirBrazoD, this->bisagraD, this->flexionD, H_BRAZO_D, H_ANTEBRAZO_D);

		// Piernas: muslo adelante = Rx(-), rodilla atrás = Rx(+), el pie compensa para quedar plano
		auto pierna = [&](float muslo, float rodilla, int hMuslo, int hPierna, int hPie)
		{
			G(hMuslo) = Rot(-muslo, glm::vec3(1, 0, 0));
			G(hPierna) = Rot(rodilla, glm::vec3(1, 0, 0));
			G(hPie) = Rot(muslo - rodilla, glm::vec3(1, 0, 0));
		};
		pierna(p.agachar + p.musloI, 2.0f * p.agachar + p.rodillaI, H_MUSLO_I, H_PIERNA_I, H_PIE_I);
		pierna(p.agachar + p.musloD, 2.0f * p.agachar + p.rodillaD, H_MUSLO_D, H_PIERNA_D, H_PIE_D);

		this->modelo->ComputePose(giros, this->globales, this->paleta);

		// Cuerpo completo: avance en su dirección original, giro e inclinación
		float yaw0 = glm::radians(this->rotInicial.y);
		glm::vec3 frente(sinf(yaw0), 0.0f, cosf(yaw0));
		this->obj->position = this->posInicial + frente * p.avance;
		this->obj->rotation = this->rotInicial + glm::vec3(p.inclinacion, p.yaw, 0.0f);
		this->obj->skinPalette = this->paleta;

		// Contacto con la lona: el punto más bajo de las suelas queda a la altura original
		glm::mat4 M = this->obj->GetMatrix();
		float minY = FLT_MAX;
		for (size_t v : this->suela)
			minY = std::min(minY, (M * glm::vec4(this->PuntoPosado(v), 1.0f)).y);
		this->obj->position.y += this->posInicial.y - minY;
	}

	// Vértice deformado con la pose actual (espacio del modelo)
	glm::vec3 PuntoPosado(size_t v) const
	{
		return this->modelo->SkinnedPosition(v, this->paleta);
	}

	// Centro de un grupo de vértices en el mundo con la pose actual
	glm::vec3 CentroMundo(const std::vector<size_t>& grupo) const
	{
		glm::mat4 M = this->obj->GetMatrix();
		glm::vec3 c(0.0f);
		for (size_t v : grupo) c += glm::vec3(M * glm::vec4(this->PuntoPosado(v), 1.0f));
		return c / (float)std::max<size_t>(grupo.size(), 1);
	}

	// Vértices de un grupo en el mundo con la pose actual
	std::vector<glm::vec3> GrupoMundo(const std::vector<size_t>& grupo) const
	{
		glm::mat4 M = this->obj->GetMatrix();
		std::vector<glm::vec3> r;
		r.reserve(grupo.size());
		for (size_t v : grupo) r.push_back(glm::vec3(M * glm::vec4(this->PuntoPosado(v), 1.0f)));
		return r;
	}
	std::vector<glm::vec3> TodoMundo() const
	{
		std::vector<glm::vec3> r;
		this->modelo->SkinPositions(this->paleta, r);
		glm::mat4 M = this->obj->GetMatrix();
		for (glm::vec3& v : r) v = glm::vec3(M * glm::vec4(v, 1.0f));
		return r;
	}

	// Vuelve exactamente a la transformación original y a la pose de enlace (dibujo estático)
	void Restaurar()
	{
		this->obj->position = this->posInicial;
		this->obj->rotation = this->rotInicial;
		this->obj->skinPalette.clear();
	}

	static glm::mat3 Rot(float grados, const glm::vec3& eje)
	{
		return glm::mat3(glm::rotate(glm::mat4(1.0f), glm::radians(grados), eje));
	}
	// Mismo orden que EulerRotation de Scene.h: Y, luego X, luego Z
	static glm::mat3 Euler(const glm::vec3& g)
	{
		return Rot(g.y, glm::vec3(0, 1, 0)) * Rot(g.x, glm::vec3(1, 0, 0)) * Rot(g.z, glm::vec3(0, 0, 1));
	}
	// Dirección "hacia arriba" perpendicular al brazo d (si el brazo es casi vertical, hacia el frente)
	static glm::vec3 ReferenciaCodo(const glm::vec3& d)
	{
		glm::vec3 r = glm::vec3(0, 1, 0) - d * d.y;
		if (glm::length(r) < 0.2f) r = glm::vec3(0, 0, 1) - d * d.z;
		return glm::normalize(r);
	}
	// Ángulo (radianes) de a hacia b alrededor del eje n, usando sus componentes perpendiculares a n
	static float AnguloFirmado(glm::vec3 a, glm::vec3 b, const glm::vec3& n)
	{
		a = a - n * glm::dot(a, n);
		b = b - n * glm::dot(b, n);
		return atan2f(glm::dot(glm::cross(a, b), n), glm::dot(a, b));
	}
	// Rotación más corta que lleva la dirección a hacia b
	static glm::mat3 RotacionMinima(const glm::vec3& a, const glm::vec3& b)
	{
		glm::vec3 eje = glm::cross(a, b);
		float s = glm::length(eje), c = glm::dot(a, b);
		if (s < 1e-6f) return glm::mat3(1.0f);
		return glm::mat3(glm::rotate(glm::mat4(1.0f), atan2f(s, c), eje / s));
	}

private:
	// Ciclo de pasos según la fracción del trayecto recorrida: cada paso es medio ciclo; la
	// envolvente lo apaga al inicio y al final del tramo para empezar y terminar con los pies juntos.
	static void agregarPasos(PoseCombate& p, float desde, float hasta)
	{
		using namespace CombateParams;
		float total = hasta - desde;
		if (fabsf(total) < 1e-4f) return;
		float u = glm::clamp((p.avance - desde) / total, 0.0f, 1.0f);
		int pasos = std::max(1, (int)roundf(fabsf(total) / PASO));
		float fase = glm::pi<float>() * pasos * u;
		float env = AplicarCurva(Curva::Suave, std::min(1.0f, u / 0.15f)) * AplicarCurva(Curva::Suave, std::min(1.0f, (1.0f - u) / 0.15f));
		float sentido = total > 0.0f ? 1.0f : -1.0f;
		float s = sinf(fase), c = cosf(fase);
		p.musloI = sentido * MUSLO_PASO * s * env;
		p.musloD = -sentido * MUSLO_PASO * s * env;
		p.rodillaI = RODILLA_PASO * std::max(0.0f, c) * env;
		p.rodillaD = RODILLA_PASO * std::max(0.0f, -c) * env;
		p.brazoI.azimut -= sentido * BRACEO * s * env;   // Brazo contrario a la pierna
		p.brazoD.azimut += sentido * BRACEO * s * env;
	}
};

// ---------------- Secuencia completa y estados
enum class EstadoCombate { Reposo, EnEspera, Ejecucion, Finalizacion };

class AnimacionCombate
{
public:
	LuchadorAnimado atacante, rival;

	// Prepara ambos luchadores, mide su geometría y construye la coreografía. Devuelve false si
	// algún modelo no tiene esqueleto (entonces la barra espaciadora no tiene efecto).
	bool Inicializar(SceneObject* luchador, SceneObject* contrincante)
	{
		this->listo = this->atacante.Inicializar(luchador) && this->rival.Inicializar(contrincante);
		if (!this->listo) return false;
		this->construirCoreografia();
		this->atacante.Restaurar();
		this->rival.Restaurar();
		return true;
	}

	// Barra espaciadora: pide el combate. Se ignora si ya está pedido o en curso.
	bool Solicitar()
	{
		if (!this->listo || this->estado != EstadoCombate::Reposo) return false;
		this->estado = EstadoCombate::EnEspera;
		return true;
	}

	// puedeIniciar: no hay otra animación que use el ring (el árbitro). Mientras sea falso, un
	// combate solicitado espera.
	void Actualizar(float deltaTime, bool puedeIniciar)
	{
		if (!this->listo) return;
		if (this->estado == EstadoCombate::EnEspera)
		{
			if (!puedeIniciar) return;
			this->estado = EstadoCombate::Ejecucion;
			this->tiempo = 0.0f;          // El cuadro de inicio muestra t = 0
		}
		else if (this->congelado)
			return;
		else if (this->estado == EstadoCombate::Ejecucion || this->estado == EstadoCombate::Finalizacion)
			this->tiempo += std::min(deltaTime, CombateParams::PASO_MAXIMO);
		else
			return;

		if (this->tiempo >= CombateParams::T_FIN)
		{
			this->estado = EstadoCombate::Reposo;   // Transformación y pose originales exactas
			this->tiempo = 0.0f;
			this->atacante.Restaurar();
			this->rival.Restaurar();
			return;
		}
		if (this->tiempo >= CombateParams::T_FIN_REGRESO)
			this->estado = EstadoCombate::Finalizacion;
		this->aplicarEn(this->tiempo);
	}

	// Coloca ambos luchadores en el instante t (también lo usan las pruebas)
	void aplicarEn(float t)
	{
		this->atacante.Aplicar(this->atacante.Evaluar(t));
		this->rival.Aplicar(this->rival.Evaluar(t));
	}

	// Depuración (--congelar-combate): deja la secuencia detenida en el instante t
	void Congelar(float t)
	{
		if (!this->listo) return;
		this->estado = EstadoCombate::Ejecucion;
		this->tiempo = t;
		this->congelado = true;
		this->aplicarEn(t);
	}

	EstadoCombate Estado() const { return this->estado; }
	bool Ocupado() const { return this->estado != EstadoCombate::Reposo; }
	bool Activa() const { return this->estado == EstadoCombate::Ejecucion || this->estado == EstadoCombate::Finalizacion; }
	float Tiempo() const { return this->tiempo; }
	float Duracion() const { return CombateParams::T_FIN; }
	float Intensidad() const { return this->intensidad; }
	float VelocidadImpacto() const { return this->velocidadImpacto; }
	float FraccionImpacto() const { return this->fraccionImpacto; }
	float SeparacionImpacto() const { return this->separacionImpacto; }
	float AvanceGolpe() const { return this->avanceGolpe; }
	const char* FaseActual() const
	{
		if (!this->Activa()) return this->estado == EstadoCombate::EnEspera ? "en espera" : "reposo";
		const char* f = "";
		this->atacante.Evaluar(this->tiempo, &f);
		const char* g = "";
		this->rival.Evaluar(this->tiempo, &g);
		this->textoFase = std::string(f) + " / " + g;
		return this->textoFase.c_str();
	}

private:
	bool listo = false;
	bool congelado = false;
	EstadoCombate estado = EstadoCombate::Reposo;
	float tiempo = 0.0f;
	float avanceGolpe = 0.0f;
	float intensidad = 1.0f;
	float velocidadImpacto = 0.0f;
	float fraccionImpacto = 1.0f;     // Fracción del barrido en que el brazo toca al rival
	float separacionImpacto = 0.0f;   // Distancia brazo-rival en la pose de impacto
	mutable std::string textoFase;

	void construirCoreografia()
	{
		using namespace CombateParams;
		LuchadorAnimado& A = this->atacante;
		LuchadorAnimado& B = this->rival;

		// ---- Poses del atacante
		PoseCombate a0 = A.reposo;
		PoseCombate aGuardia = a0;
		aGuardia.agachar = 16.0f;
		aGuardia.torso = glm::vec3(8.0f, 0.0f, 0.0f);
		aGuardia.brazoI = { -55.0f, 45.0f, 15.0f, 125.0f };
		aGuardia.brazoD = { -55.0f, 45.0f, 15.0f, 125.0f };

		PoseCombate aPrepara = aGuardia;              // Brazo derecho arriba y atrás, torso girado a su derecha
		aPrepara.torso = glm::vec3(2.0f, -30.0f, 0.0f);
		aPrepara.brazoD = { 30.0f, -40.0f, 0.0f, 35.0f };
		aPrepara.agachar = 20.0f;

		PoseCombate aGolpe = aGuardia;                // Barre el brazo hacia el pecho del rival
		aGolpe.torso = glm::vec3(12.0f, 28.0f, 0.0f);
		aGolpe.brazoD = { -20.0f, 95.0f, 0.0f, 15.0f };
		aGolpe.agachar = 22.0f;

		// ---- Poses del rival
		PoseCombate b0 = B.reposo;
		PoseCombate bGuardia = b0;
		bGuardia.agachar = 14.0f;
		bGuardia.torso = glm::vec3(8.0f, 0.0f, 0.0f);
		bGuardia.brazoI = { -55.0f, 45.0f, 15.0f, 125.0f };
		bGuardia.brazoD = { -55.0f, 45.0f, 15.0f, 125.0f };

		PoseCombate bProvoca = b0;                    // Brazos abiertos hacia atrás, pecho al frente
		bProvoca.agachar = 8.0f;
		bProvoca.torso = glm::vec3(-12.0f, 0.0f, 0.0f);
		bProvoca.cabeza = glm::vec2(-12.0f, 0.0f);
		bProvoca.brazoI = { -15.0f, -50.0f, 0.0f, 15.0f };
		bProvoca.brazoD = { -15.0f, -50.0f, 0.0f, 15.0f };

		// ---- Distancia del golpe y pose de impacto, con la geometría real.
		// 1) El atacante se coloca a DISTANCIA_PECHOS del rival (frente del torso a frente del torso).
		// 2) Se barre el golpe desde la preparación hacia aGolpe en pasos de 1 % y, con los vértices
		//    reales del brazo deformados, se busca el primer instante en que cualquier parte del brazo
		//    llega a HOLGURA_GOLPE de la superficie del rival. Esa es la pose de impacto: el brazo se
		//    detiene al tocarlo y nunca lo atraviesa.
		float yaw0 = glm::radians(A.rotInicial.y);
		glm::vec3 frente(sinf(yaw0), 0.0f, cosf(yaw0));
		B.Aplicar(bProvoca);
		std::vector<glm::vec3> cuerpoB = B.TodoMundo();
		float pechoB = FLT_MAX;
		for (const glm::vec3& v : B.GrupoMundo(B.pecho)) pechoB = std::min(pechoB, glm::dot(v, frente));
		A.Aplicar(aPrepara);
		float pechoA = -FLT_MAX;
		for (const glm::vec3& v : A.GrupoMundo(A.pecho)) pechoA = std::max(pechoA, glm::dot(v, frente));
		this->avanceGolpe = (pechoB - pechoA) - DISTANCIA_PECHOS;
		aPrepara.avance = this->avanceGolpe;
		aGolpe.avance = this->avanceGolpe;

		// Rejilla de 5 cm con los vértices del rival (no se mueve durante el golpe)
		const float CELDA = 0.05f;
		std::unordered_map<long long, std::vector<glm::vec3>> rejilla;
		auto clave = [](const glm::ivec3& c) { return ((long long)(c.x + 100000) << 36) ^ ((long long)(c.y + 100000) << 18) ^ (long long)(c.z + 100000); };
		for (const glm::vec3& v : cuerpoB) rejilla[clave(glm::ivec3(glm::floor(v / CELDA)))].push_back(v);
		auto distanciaAlRival = [&](const std::vector<glm::vec3>& puntos) {
			float m = FLT_MAX;
			for (const glm::vec3& p : puntos)
			{
				glm::ivec3 c = glm::ivec3(glm::floor(p / CELDA));
				for (int dx = -1; dx <= 1; dx++) for (int dy = -1; dy <= 1; dy++) for (int dz = -1; dz <= 1; dz++)
				{
					auto it = rejilla.find(clave(c + glm::ivec3(dx, dy, dz)));
					if (it == rejilla.end()) continue;
					for (const glm::vec3& q : it->second) m = std::min(m, glm::length(p - q));
				}
			}
			return m; };
		auto separacion = [&](float b) {
			A.Aplicar(MezclarPoses(aPrepara, aGolpe, b));
			return distanciaAlRival(A.GrupoMundo(A.brazoDerecho)); };
		this->fraccionImpacto = 1.0f;
		for (int k = 1; k <= 100; k++)
		{
			if (separacion(k / 100.0f) > HOLGURA_GOLPE) continue;
			// Bisección entre el último paso libre y el primero en contacto (precisión 0.1 %)
			float libre = (k - 1) / 100.0f, toca = k / 100.0f;
			while (toca - libre > 0.001f)
			{
				float m = 0.5f * (libre + toca);
				if (separacion(m) > HOLGURA_GOLPE) libre = m; else toca = m;
			}
			this->fraccionImpacto = libre;   // Última pose que aún conserva la holgura
			break;
		}
		aGolpe = MezclarPoses(aPrepara, aGolpe, this->fraccionImpacto);
		A.Aplicar(aGolpe);
		this->separacionImpacto = distanciaAlRival(A.GrupoMundo(A.brazoDerecho));

		// Intensidad de la reacción: velocidad de la mano en el último 5 % del golpe (diferencia finita)
		{
			A.Aplicar(MezclarPoses(aPrepara, aGolpe, AplicarCurva(Curva::Acelera, 0.95f)));
			glm::vec3 p0 = A.CentroMundo(A.palmaD);
			A.Aplicar(aGolpe);
			glm::vec3 p1 = A.CentroMundo(A.palmaD);
			this->velocidadImpacto = glm::length(p1 - p0) / (0.05f * (T_IMPACTO - T_PREPARA));
			this->intensidad = glm::clamp(this->velocidadImpacto / VELOCIDAD_REFERENCIA, INTENSIDAD_MIN, INTENSIDAD_MAX);
		}

		// ---- Claves del atacante
		PoseCombate aLlega = aGuardia; aLlega.avance = this->avanceGolpe; aLlega.caminar = 1.0f;
		PoseCombate aRetira = aGolpe;                 // Rebote del brazo después del impacto
		aRetira.brazoD = { 0.0f, 60.0f, 0.0f, 35.0f };
		aRetira.torso = glm::vec3(6.0f, 10.0f, 0.0f);
		PoseCombate aCelebra = aGuardia;              // Retrocede un paso y alza el puño hacia el público
		aCelebra.avance = this->avanceGolpe - RETROCESO_ATACANTE;
		aCelebra.agachar = 6.0f;
		aCelebra.torso = glm::vec3(-6.0f, -25.0f, 0.0f);
		aCelebra.cabeza = glm::vec2(-15.0f, -25.0f);
		aCelebra.brazoD = { 75.0f, 20.0f, 0.0f, 55.0f };
		PoseCombate aGuardia2 = aGuardia; aGuardia2.avance = aCelebra.avance;
		PoseCombate aVuelve = aGuardia; aVuelve.avance = 0.0f; aVuelve.caminar = 1.0f;

		A.claves = {
			{ 0.0f, a0, Curva::Suave, "reposo" },
			{ T_GUARDIA, aGuardia, Curva::Suave, "guardia" },
			{ T_INICIO_CAMINAR, aGuardia, Curva::Suave, "estudia al rival" },
			{ T_LLEGA, aLlega, Curva::Suave, "camina hacia el rival" },
			{ T_PREPARA, aPrepara, Curva::Suave, "prepara el golpe" },
			{ T_IMPACTO, aGolpe, Curva::Acelera, "golpe al pecho" },
			{ T_IMPACTO + 0.25f, aRetira, Curva::Frena, "retira el brazo" },
			{ T_IMPACTO + 0.9f, aCelebra, Curva::Suave, "celebra" },
			{ T_IMPACTO + 1.5f, aCelebra, Curva::Suave, "celebra" },
			{ T_RECUPERA, aGuardia2, Curva::Suave, "vuelve a la guardia" },
			{ T_INICIO_REGRESO, aGuardia2, Curva::Suave, "guardia" },
			{ T_FIN_REGRESO, aVuelve, Curva::Suave, "regresa a su lugar" },
			{ T_FIN, a0, Curva::Suave, "pose original" },
		};

		// ---- Claves del rival (la reacción depende del instante y la intensidad del golpe)
		const float I = this->intensidad;
		PoseCombate bImpacto = bProvoca;              // El golpe lo echa hacia atrás con los brazos al aire
		bImpacto.avance = -0.15f * I;
		bImpacto.inclinacion = -6.0f * I;
		bImpacto.torso = glm::vec3(-26.0f * I, -15.0f * I, 0.0f);
		bImpacto.cabeza = glm::vec2(-28.0f * I, -10.0f);
		bImpacto.brazoI = { 45.0f, -30.0f, 0.0f, 30.0f };
		bImpacto.brazoD = { 45.0f, -30.0f, 0.0f, 30.0f };
		bImpacto.agachar = 4.0f;
		PoseCombate bDolor = bGuardia;                // Retrocede tambaleándose, se dobla y se toma el pecho
		bDolor.avance = -RETROCESO_RIVAL * I;
		bDolor.caminar = 1.0f;
		bDolor.agachar = 24.0f;
		bDolor.torso = glm::vec3(28.0f, 8.0f, 6.0f);
		bDolor.cabeza = glm::vec2(20.0f, 0.0f);
		bDolor.brazoI = { -35.0f, 60.0f, 70.0f, 120.0f };
		bDolor.brazoD = { -35.0f, 60.0f, 70.0f, 120.0f };
		PoseCombate bSacude = bDolor;                 // Se incorpora sacudiendo la cabeza
		bSacude.caminar = 0.0f;
		bSacude.agachar = 18.0f;
		bSacude.torso = glm::vec3(14.0f, -6.0f, -4.0f);
		bSacude.cabeza = glm::vec2(5.0f, 18.0f);
		PoseCombate bSacude2 = bSacude; bSacude2.cabeza = glm::vec2(5.0f, -18.0f);
		PoseCombate bGuardia2 = bGuardia; bGuardia2.avance = bDolor.avance;
		PoseCombate bVuelve = bGuardia; bVuelve.avance = 0.0f; bVuelve.caminar = 1.0f;

		B.claves = {
			{ 0.0f, b0, Curva::Suave, "reposo" },
			{ T_GUARDIA, bGuardia, Curva::Suave, "guardia" },
			{ T_PROVOCA, bProvoca, Curva::Suave, "provoca: abre los brazos" },
			{ T_IMPACTO, bProvoca, Curva::Suave, "espera el golpe" },
			{ T_IMPACTO + 0.15f, bImpacto, Curva::Frena, "recibe el impacto" },
			{ T_IMPACTO + 0.8f, bDolor, Curva::Suave, "retrocede tambaleandose" },
			{ T_IMPACTO + 1.25f, bSacude, Curva::Suave, "se incorpora" },
			{ T_IMPACTO + 1.5f, bSacude2, Curva::Suave, "sacude la cabeza" },
			{ T_RECUPERA, bGuardia2, Curva::Suave, "recupera la guardia" },
			{ T_INICIO_REGRESO, bGuardia2, Curva::Suave, "guardia" },
			{ T_FIN_REGRESO, bVuelve, Curva::Suave, "regresa a su lugar" },
			{ T_FIN, b0, Curva::Suave, "pose original" },
		};
	}
};
