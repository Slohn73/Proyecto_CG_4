#pragma once

// Penta: máquina de estados de sus dos animaciones esqueléticas.
//
//   Reposo ──F5──> Entrada ──(termina)──> EntradaFin
//     │                                       │
//     └──F6──> Caida ──(termina)──> CaidaFin <─┘ (F6 desde cualquier estado)
//   F7 (desde cualquier estado) -> Reposo
//
// - Reposo: de pie en el ring con el primer cuadro de la caída (así la caída arranca sin salto).
// - Entrada: se coloca al salir del arco y reproduce el clip de la entrada; la altura del objeto sigue
//   la pendiente de la pasarela bajo la cadera (el clip se animó sobre un piso plano). El avance
//   horizontal es el root motion del clip: no se agrega ningún desplazamiento por código.
// - Caida: se coloca en el ring y reproduce el clip de la caída.
// - Al terminar un clip (no se repite) se conserva su último cuadro hasta otra orden.
// Transiciones: dentro del ring, si la cadera de la pose actual está cerca de la del primer cuadro de
// la caída (de pie), la pose cambia con una mezcla suave de FUNDIDO segundos. Si el personaje cambia de
// lugar (rampa <-> ring) o está tumbado, el cambio es directo: mezclar la traslación de la cadera lo
// haría deslizarse por la lona.

#include <string>
#include <sstream>
#include <iomanip>
#include <iostream>

#include "Scene.h"
#include "ArenaLayout.h"
#include "DebugDraw.h"

class PentaAnimado
{
public:
	enum class Estado { Reposo, Entrada, EntradaFin, Caida, CaidaFin };

	static constexpr float FUNDIDO = 0.4f;            // Segundos de transición dentro del ring
	static constexpr float FIN_CAIDA = 89.0f / 24.0f; // La escena de Blender de la caída termina en el cuadro 90
	static constexpr float DISTANCIA_FUNDIDO = 0.3f;  // m: separación máxima de la cadera para mezclar poses

	bool Init(Scene& scene)
	{
		this->obj = scene.Find("penta");
		if (!this->obj || !this->obj->asset || !this->obj->asset->IsReady() || !this->obj->asset->model->HasAnimations())
		{
			std::cout << "[Penta] AVISO: no hay modelo animado de Penta; sus controles no harán nada" << std::endl;
			this->obj = nullptr;
			return false;
		}
		this->model = this->obj->asset->model.get();
		this->clipEntrada = this->model->FindAnimation("Armature|mixamo.com");   // Clip de Penta_Entrada.fbx
		this->clipCaida = this->model->FindAnimation("Armature|Caida");          // Clip de Penta_Caida.fbx
		const SkeletonData& sk = this->model->GetSkeleton();
		auto hips = sk.nodeByName.find("mixamorig:Hips");
		this->nodoCadera = hips != sk.nodeByName.end() ? hips->second : -1;
		if (this->clipEntrada < 0 || this->clipCaida < 0 || this->nodoCadera < 0)
		{
			std::cout << "[Penta] AVISO: faltan los clips de entrada/caida o el hueso Hips" << std::endl;
			this->obj = nullptr;
			return false;
		}
		// Lugar en el ring (BuildArena lo deja sobre la lona) y cadera del primer cuadro de la caída
		this->posRing = this->obj->position;
		std::vector<glm::mat4> globales, huesos;
		sk.Evaluate(&sk.clips[this->clipCaida], 0.0f, globales, huesos);
		this->caderaReposo = glm::vec3(globales[this->nodoCadera][3]);
		this->Reiniciar(false);
		return true;
	}

	bool Activo() const { return this->obj != nullptr; }

	void Entrada()
	{
		if (!this->obj) return;
		this->colocar(PENTA_RAMPA, PENTA_RAMPA_GIRO);
		this->model->SetAnimationRange(0.0f);
		this->model->TriggerAnimation(this->clipEntrada, false);
		this->cambiar(Estado::Entrada);
		this->seguirPasarela();
	}

	void Caida()
	{
		if (!this->obj) return;
		float fundido = this->fundidoEnRing();
		this->colocar(this->posRing, PENTA_RING_GIRO);
		this->model->SetAnimationRange(0.0f, FIN_CAIDA);
		this->model->TriggerAnimation(this->clipCaida, false, fundido);
		this->cambiar(Estado::Caida, fundido);
	}

	// Vuelve a la pose de espera en el ring (suave si ya estaba en el ring)
	void Reiniciar(bool suave = true)
	{
		if (!this->obj) return;
		float fundido = suave ? this->fundidoEnRing() : 0.0f;
		this->colocar(this->posRing, PENTA_RING_GIRO);
		this->model->SetAnimationRange(0.0f, FIN_CAIDA);
		this->model->ShowAnimationFrame(this->clipCaida, 0.0f, fundido);
		this->cambiar(Estado::Reposo, fundido);
	}

	// Pose fija en un instante de un clip (para capturas reproducibles desde la línea de comandos)
	void Congelar(bool entrada, float segundos)
	{
		if (!this->obj) return;
		if (entrada) this->colocar(PENTA_RAMPA, PENTA_RAMPA_GIRO);
		else this->colocar(this->posRing, PENTA_RING_GIRO);
		this->model->SetAnimationRange(0.0f);
		this->model->ShowAnimationFrame(entrada ? this->clipEntrada : this->clipCaida, segundos);
		this->cambiar(entrada ? Estado::Entrada : Estado::Caida);
		this->congelado = true;
		if (entrada) this->seguirPasarela();
	}

	// Una vez por cuadro, después de Model::UpdateAnimation
	void Update(float deltaTime)
	{
		if (!this->obj) return;
		this->tiempoEstado += deltaTime;
		Animator* a = this->model->GetAnimator();
		if (this->estado == Estado::Entrada)
		{
			this->seguirPasarela();
			if (a->HasFinished()) this->cambiar(Estado::EntradaFin);
		}
		else if (this->estado == Estado::Caida && a->HasFinished())
			this->cambiar(Estado::CaidaFin);
	}

	void DibujarEsqueleto(DebugDraw& debugDraw) const
	{
		if (!this->obj || !this->obj->visible) return;
		debugDraw.DrawSkeleton(this->obj->GetMatrix(), this->model->GetSkeleton(), this->model->GetNodeGlobals(), 0.025f);
	}

	std::string Descripcion() const
	{
		if (!this->obj) return "";
		std::ostringstream s;
		s << "Penta: " << nombre(this->estado);
		const Animator* a = this->model->GetAnimator();
		if (a && (this->estado == Estado::Entrada || this->estado == Estado::Caida))
		{
			float total = this->estado == Estado::Caida ? FIN_CAIDA : this->model->GetSkeleton().clips[this->clipEntrada].DurationSeconds();
			s << std::fixed << std::setprecision(1) << " " << a->GetTime() << "/" << total << " s" << (this->congelado ? " (congelado)" : "");
		}
		return s.str();
	}

private:
	SceneObject* obj = nullptr;
	Model* model = nullptr;
	int clipEntrada = -1, clipCaida = -1, nodoCadera = -1;
	Estado estado = Estado::Reposo;
	bool congelado = false;
	float tiempoEstado = 0.0f;   // Segundos reales en el estado actual (para el registro en consola)

	static const char* nombre(Estado e)
	{
		static const char* nombres[] = { "reposo", "entrada", "entrada terminada", "caida", "caida terminada" };
		return nombres[static_cast<int>(e)];
	}

	void cambiar(Estado nuevo, float fundido = 0.0f)
	{
		if (nuevo != this->estado || this->tiempoEstado > 0.0f)
			std::cout << "[Penta] " << nombre(this->estado) << " -> " << nombre(nuevo) << " (" << std::fixed << std::setprecision(2)
				<< this->tiempoEstado << " s en el estado anterior; " << (fundido > 0.0f ? "transicion suave" : "cambio directo") << ")"
				<< std::defaultfloat << std::endl;
		this->estado = nuevo;
		this->tiempoEstado = 0.0f;
	}

	glm::vec3 posRing = glm::vec3(0.0f);
	glm::vec3 caderaReposo = glm::vec3(0.0f);   // Hips en el primer cuadro de la caída (espacio del archivo)

	bool enRing() const { return this->estado == Estado::Reposo || this->estado == Estado::Caida || this->estado == Estado::CaidaFin; }

	// Duración de la transición hacia la pose de pie del ring: FUNDIDO si la cadera actual está cerca de
	// la de esa pose (las dos medidas en el mundo con la matriz del objeto en el ring); 0 si no
	float fundidoEnRing() const
	{
		if (!this->enRing()) return 0.0f;
		glm::mat4 m = this->obj->GetMatrix();
		glm::vec3 actual = glm::vec3(m * this->model->GetNodeGlobals()[this->nodoCadera] * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
		glm::vec3 destino = glm::vec3(m * glm::vec4(this->caderaReposo, 1.0f));
		return glm::distance(actual, destino) <= DISTANCIA_FUNDIDO ? FUNDIDO : 0.0f;
	}

	void colocar(const glm::vec3& posicion, float giro)
	{
		this->obj->position = posicion;
		this->obj->rotation = glm::vec3(0.0f, giro, 0.0f);
		this->congelado = false;
	}

	// La entrada se animó sobre un piso plano y la pasarela baja hacia el ring: el objeto toma la altura
	// de la pasarela bajo la cadera actual (posición de la cadera = matriz del objeto * global de Hips)
	void seguirPasarela()
	{
		glm::vec3 cadera = glm::vec3(this->obj->GetMatrix() * this->model->GetNodeGlobals()[this->nodoCadera] * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
		this->obj->position.y = AlturaPasarela(cadera.z);
	}
};
