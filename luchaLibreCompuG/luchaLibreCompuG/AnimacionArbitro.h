#pragma once

// =====================================================================================
//  ANIMACIÓN DEL ÁRBITRO (tecla T), generada por código
//
//  Secuencia: se acerca unos pasos al centro del ring, observa a cada luchador, da la señal de
//  inicio con el brazo derecho (lo levanta y lo baja con un corte), y retrocede hasta recuperar
//  exactamente su posición y orientación iniciales. Se puede repetir al terminar.
//
//  El modelo no tiene esqueleto: el brazo derecho (con su manga) es otra pieza rígida
//  (herramientas/separar_arbitro.py) que gira sobre el hombro, como las piezas del espectador E:
//      M_brazo = M_cuerpo * T(hombro) * Rz(-abducción) * T(-hombro)
//  que con SceneObject se expresa como position + rotation + pivotOffset (ver Aplicar()).
//
//  La pose se calcula en cada cuadro a partir del tiempo transcurrido (no se acumulan
//  transformaciones): cada fase interpola, con arranque y frenado suaves, desde la pose final
//  de la fase anterior hasta la suya. Así el resultado depende del tiempo, no de los FPS.
// =====================================================================================

#include <vector>
#include <cmath>
#include <glm/glm.hpp>

#include "Scene.h"

// ---------------- Parámetros (todos los ajustes de la animación están aquí)
namespace ArbitroParams
{
	// Articulación del hombro derecho en el espacio normalizado del modelo (metros, Y arriba,
	// frente +Z). Medida por herramientas/separar_arbitro.py.
	const glm::vec3 HOMBRO_DER(-0.21f, 1.50f, -0.0455f);

	// Amplitudes
	const float ACERCAMIENTO = 0.70f;    // Metros que avanza hacia el centro del ring
	const int PASOS = 3;                 // Pasos al acercarse (y al retroceder)
	const float REBOTE_PASO = 0.018f;    // Metros que sube el cuerpo a la mitad de cada paso
	const float INCLINACION = 5.0f;      // Grados que se inclina hacia adelante al observar
	const float BRAZO_ARRIBA = 100.0f;   // Abducción del hombro en la señal (más de ~100° abre la manga)
	const float BRAZO_CORTE = 15.0f;     // Ángulo donde termina el corte hacia abajo

	// Duraciones de cada fase (segundos)
	const float T_ACERCARSE = 1.6f;
	const float T_GIRAR_LUCHADOR = 0.9f;
	const float T_OBSERVAR = 0.5f;
	const float T_GIRAR_RIVAL = 1.2f;
	const float T_VOLVER_FRENTE = 0.8f;
	const float T_LEVANTAR_BRAZO = 0.7f;
	const float T_SOSTENER_BRAZO = 0.4f;
	const float T_CORTE = 0.3f;
	const float T_BAJAR_BRAZO = 0.4f;
	const float T_PAUSA = 0.3f;
	const float T_RETROCEDER = 1.6f;

	// Paso máximo de tiempo por cuadro: si un cuadro tarda mucho (guardar una captura, arrastrar la
	// ventana) la secuencia no salta de golpe. No afecta a la velocidad por encima de 10 FPS.
	const float PASO_MAXIMO = 0.1f;
}

// Pose relativa a la transformación original del árbitro
struct PoseArbitro
{
	float avance = 0.0f;       // 0 = posición original, 1 = punto de acercamiento
	float yaw = 0.0f;          // Grados respecto a su orientación original
	float inclinacion = 0.0f;  // Grados hacia adelante
	float brazo = 0.0f;        // Abducción del hombro derecho (grados)
};

// Fase de la secuencia: dura 'duracion' segundos y termina en la pose 'destino'
struct FaseArbitro
{
	const char* nombre;
	float duracion;
	PoseArbitro destino;
};

class AnimacionArbitro
{
public:
	// Guarda la transformación original y calcula el punto de acercamiento y los giros hacia
	// los luchadores a partir de la escena. Devuelve false si falta alguna pieza.
	bool Inicializar(SceneObject* cuerpo, SceneObject* brazo, const SceneObject* luchador,
		const SceneObject* rival, const glm::vec3& centroRing)
	{
		using namespace ArbitroParams;
		this->cuerpo = cuerpo;
		this->brazo = brazo;
		if (!cuerpo || !brazo)
			return false;
		this->posInicial = cuerpo->position;
		this->rotInicial = cuerpo->rotation;   // Se asume solo giro en Y (como en BuildArena)

		// Punto de acercamiento: ACERCAMIENTO metros en línea recta hacia el centro del ring
		glm::vec3 haciaCentro = centroRing - this->posInicial;
		haciaCentro.y = 0.0f;
		if (glm::length(haciaCentro) > 1e-4f)
			haciaCentro = glm::normalize(haciaCentro);
		this->destino = this->posInicial + haciaCentro * ACERCAMIENTO;

		// Giros para mirar a cada luchador desde el punto de acercamiento
		float yawLuchador = luchador ? this->giroHacia(luchador->position) : -45.0f;
		float yawRival = rival ? this->giroHacia(rival->position) : 45.0f;

		// Pose al terminar cada fase: {avance, yaw, inclinación, brazo}
		this->fases = {
			{ "acercarse",          T_ACERCARSE,      { 1.0f, 0.0f, 0.0f, 0.0f } },
			{ "mirar al luchador",  T_GIRAR_LUCHADOR, { 1.0f, yawLuchador, INCLINACION, 0.0f } },
			{ "observar",           T_OBSERVAR,       { 1.0f, yawLuchador, INCLINACION, 0.0f } },
			{ "mirar al rival",     T_GIRAR_RIVAL,    { 1.0f, yawRival, INCLINACION, 0.0f } },
			{ "observar",           T_OBSERVAR,       { 1.0f, yawRival, INCLINACION, 0.0f } },
			{ "volver al frente",   T_VOLVER_FRENTE,  { 1.0f, 0.0f, 0.0f, 0.0f } },
			{ "levantar el brazo",  T_LEVANTAR_BRAZO, { 1.0f, 0.0f, 0.0f, BRAZO_ARRIBA } },
			{ "sostener la senal",  T_SOSTENER_BRAZO, { 1.0f, 0.0f, 0.0f, BRAZO_ARRIBA } },
			{ "senal de inicio",    T_CORTE,          { 1.0f, 0.0f, 0.0f, BRAZO_CORTE } },
			{ "bajar el brazo",     T_BAJAR_BRAZO,    { 1.0f, 0.0f, 0.0f, 0.0f } },
			{ "pausa",              T_PAUSA,          { 1.0f, 0.0f, 0.0f, 0.0f } },
			{ "retroceder",         T_RETROCEDER,     { 0.0f, 0.0f, 0.0f, 0.0f } },
		};
		this->duracionTotal = 0.0f;
		for (const FaseArbitro& f : this->fases)
			this->duracionTotal += f.duracion;

		this->Aplicar(PoseArbitro());
		return true;
	}

	// Tecla T: inicia la secuencia (si ya está en curso no la reinicia, para no saltar de pose)
	bool Iniciar()
	{
		if (this->activa || !this->cuerpo)
			return false;
		this->activa = true;
		this->recienIniciada = true;
		this->tiempo = 0.0f;
		return true;
	}

	void Actualizar(float deltaTime)
	{
		if (!this->cuerpo || !this->brazo)
			return;
		if (this->activa)
		{
			// El cuadro en que se presiona T muestra t = 0: su deltaTime corresponde a tiempo
			// anterior a la pulsación y no debe adelantar la secuencia
			if (this->recienIniciada)
				this->recienIniciada = false;
			else
				this->tiempo += glm::min(deltaTime, ArbitroParams::PASO_MAXIMO);
			if (this->tiempo >= this->duracionTotal)
			{
				this->activa = false;   // Termina exactamente en la pose original
				this->tiempo = 0.0f;
			}
		}
		this->Aplicar(this->activa ? this->Evaluar(this->tiempo) : PoseArbitro());
	}

	// Pose en el instante t (s) de la secuencia. Función pura del tiempo.
	PoseArbitro Evaluar(float t, int* indiceFase = nullptr) const
	{
		PoseArbitro anterior;
		float inicio = 0.0f;
		for (size_t i = 0; i < this->fases.size(); i++)
		{
			const FaseArbitro& f = this->fases[i];
			if (t < inicio + f.duracion || i + 1 == this->fases.size())
			{
				if (indiceFase) *indiceFase = (int)i;
				float s = Suave((t - inicio) / f.duracion);
				PoseArbitro p;
				p.avance = glm::mix(anterior.avance, f.destino.avance, s);
				p.yaw = glm::mix(anterior.yaw, f.destino.yaw, s);
				p.inclinacion = glm::mix(anterior.inclinacion, f.destino.inclinacion, s);
				p.brazo = glm::mix(anterior.brazo, f.destino.brazo, s);
				return p;
			}
			anterior = f.destino;
			inicio += f.duracion;
		}
		return PoseArbitro();
	}

	bool Activa() const { return this->activa; }
	float Tiempo() const { return this->tiempo; }
	float Duracion() const { return this->duracionTotal; }
	const char* FaseActual() const
	{
		int i = 0;
		this->Evaluar(this->tiempo, &i);
		return this->activa && !this->fases.empty() ? this->fases[i].nombre : "reposo";
	}

	// Arranque y frenado suaves: 0..1 -> 0..1 con velocidad nula en los extremos
	static float Suave(float a)
	{
		a = glm::clamp(a, 0.0f, 1.0f);
		return a * a * (3.0f - 2.0f * a);
	}

private:
	SceneObject* cuerpo = nullptr;
	SceneObject* brazo = nullptr;
	glm::vec3 posInicial = glm::vec3(0.0f);
	glm::vec3 rotInicial = glm::vec3(0.0f);
	glm::vec3 destino = glm::vec3(0.0f);
	std::vector<FaseArbitro> fases;
	float duracionTotal = 0.0f;
	float tiempo = 0.0f;
	bool activa = false;
	bool recienIniciada = false;

	// Yaw (relativo al original) para que el frente mire hacia 'objetivo' desde el punto de acercamiento
	float giroHacia(const glm::vec3& objetivo) const
	{
		float yaw = glm::degrees(atan2f(objetivo.x - this->destino.x, objetivo.z - this->destino.z)) - this->rotInicial.y;
		while (yaw > 180.0f) yaw -= 360.0f;
		while (yaw < -180.0f) yaw += 360.0f;
		return yaw;
	}

	// Escribe la pose en los dos objetos. Siempre parte de la transformación original guardada.
	void Aplicar(const PoseArbitro& p)
	{
		using namespace ArbitroParams;
		const float escala = this->cuerpo->scale.y;

		// Desplazamiento en línea recta; al caminar sube un poco a la mitad de cada paso
		// (|sen| vale 0 al apoyar el pie, al inicio y al final del trayecto)
		glm::vec3 pos = glm::mix(this->posInicial, this->destino, p.avance);
		pos.y += REBOTE_PASO * fabsf(sinf(glm::pi<float>() * PASOS * p.avance));

		// La inclinación gira sobre la base del modelo; se eleva lo que bajaría el frente de la
		// base para que las puntas de los pies no atraviesen la lona (los talones se despegan)
		float frente = this->cuerpo->asset ? this->cuerpo->asset->localMax.z : 0.0f;
		pos.y += frente * escala * sinf(glm::radians(p.inclinacion));

		this->cuerpo->position = pos;
		this->cuerpo->rotation = this->rotInicial + glm::vec3(p.inclinacion, p.yaw, 0.0f);

		// Brazo: mismo cuerpo + giro sobre el hombro. EulerRotation aplica Y, X y al final Z, así que
		// sumar el giro del hombro en Z equivale a M_cuerpo * Rz(-brazo) alrededor de pivotOffset.
		// Rz negativo separa el brazo derecho (lado -X del modelo) hacia afuera y hacia arriba.
		glm::vec3 hombro = glm::vec3(EulerRotation(this->cuerpo->rotation) * glm::vec4(HOMBRO_DER * escala, 0.0f));
		this->brazo->position = pos + hombro;
		this->brazo->rotation = this->cuerpo->rotation + glm::vec3(0.0f, 0.0f, -p.brazo);
		this->brazo->scale = this->cuerpo->scale;
		this->brazo->pivotOffset = HOMBRO_DER;
		this->brazo->visible = this->cuerpo->visible;
	}
};
