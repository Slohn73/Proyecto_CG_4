#pragma once

// Datos de esqueleto y animación + reproductor (Animator).
//
//  - SkeletonData: se construye UNA vez por modelo (lo llena Model.h al cargar con Assimp) y se
//    comparte entre todas las instancias. Contiene el árbol de nodos COMPLETO del archivo (incluye
//    los nodos "$AssimpFbx$" de FBX), los huesos (nodo + offsetMatrix) y los clips de animación.
//  - Animator: estado de reproducción (tiempo, clip, velocidad, transición) y matrices finales de los
//    huesos. Hay uno por modelo (Model::anim), así que todas las instancias de un modelo comparten pose.
//
// Matriz final de un hueso:  global(nodo del hueso) * offsetMatrix
// El vertex shader la aplica a la posición del archivo (espacio del modelo) y después multiplica
// por la matriz "model" del objeto, igual que en la ruta estática.
//
// Cálculo por cuadro (Animator::evaluate):
//   SamplePose  -> pose local de cada nodo: interpolación de claves (lineal en posición y escala,
//                  SLERP en rotación); si hay transición, mezcla con la pose anterior
//   Compose     -> globales recorriendo la jerarquía padre -> hijo, y matrices finales de los huesos
// Explicación completa: ANIMACION_ESQUELETICA.md (raíz del repositorio).

#include <string>
#include <vector>
#include <map>
#include <algorithm>
#include <cmath>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>

// Debe coincidir con MAX_BONES de Shaders/lighting.vs
constexpr int MAX_BONES = 100;

// Nodo del árbol del archivo. Se guardan en orden: un padre siempre va antes que sus hijos.
struct SkelNode
{
	std::string name;
	int parent = -1;
	glm::mat4 local = glm::mat4(1.0f);               // Transformación de reposo (aiNode::mTransformation)
	glm::vec3 restPos = glm::vec3(0.0f);             // Descomposición de la de reposo: sirve de valor por
	glm::quat restRot = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);   // defecto cuando un canal no trae alguna de las
	glm::vec3 restScale = glm::vec3(1.0f);           // tres pistas (posición, rotación, escala)
};

// Hueso: nodo al que sigue + matriz que lleva un vértice del espacio de la malla al espacio del hueso.
// Los huesos "__rigido__*" son sintéticos (offset identidad): permiten animar mallas sin skin
// que cuelgan de un nodo animado (animación por transformación de nodo, típica de glTF).
struct BoneSlot
{
	std::string name;
	int node = -1;
	glm::mat4 offset = glm::mat4(1.0f);
};

struct KeyVec3 { float time; glm::vec3 value; };
struct KeyQuat { float time; glm::quat value; };

// Shape keys (morph targets): pesos por objetivo en cada instante. Se guardan densos (un peso por objetivo).
struct MorphKey { float time; std::vector<float> weights; };

namespace AnimMath
{
	// Índice k con keys[k].time <= t (0 si t es anterior a la primera clave)
	template <class K>
	inline int FindKey(const std::vector<K>& keys, float t)
	{
		auto it = std::upper_bound(keys.begin(), keys.end(), t, [](float v, const K& k) { return v < k.time; });
		return std::max(static_cast<int>(it - keys.begin()) - 1, 0);
	}

	inline float Factor(float a, float b, float t)
	{
		float d = b - a;
		return d > 1e-6f ? glm::clamp((t - a) / d, 0.0f, 1.0f) : 0.0f;
	}

	inline glm::vec3 Sample(const std::vector<KeyVec3>& keys, float t, const glm::vec3& fallback)
	{
		if (keys.empty()) return fallback;
		if (keys.size() == 1) return keys[0].value;
		int i = FindKey(keys, t);
		if (i >= static_cast<int>(keys.size()) - 1) return keys.back().value;
		return glm::mix(keys[i].value, keys[i + 1].value, Factor(keys[i].time, keys[i + 1].time, t));
	}

	inline glm::quat Sample(const std::vector<KeyQuat>& keys, float t, const glm::quat& fallback)
	{
		if (keys.empty()) return fallback;
		if (keys.size() == 1) return keys[0].value;
		int i = FindKey(keys, t);
		if (i >= static_cast<int>(keys.size()) - 1) return keys.back().value;
		return glm::normalize(glm::slerp(keys[i].value, keys[i + 1].value, Factor(keys[i].time, keys[i + 1].time, t)));
	}
}

// Transformación local de un nodo en un instante, descompuesta en traslación, rotación y escala.
// Se guarda así (y no como matriz) para poder mezclar dos poses en una transición suave.
struct NodePose
{
	glm::vec3 position = glm::vec3(0.0f);
	glm::quat rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
	glm::vec3 scale = glm::vec3(1.0f);
	bool animated = false;   // false: el nodo conserva su matriz de reposo (SkelNode::local) tal cual

	// Local = T * R * S (primero escala, luego rotación, al final traslación)
	glm::mat4 ToMatrix() const
	{
		return glm::translate(glm::mat4(1.0f), this->position) * glm::mat4_cast(this->rotation) * glm::scale(glm::mat4(1.0f), this->scale);
	}

	// Mezcla de dos poses: lineal en traslación y escala, SLERP (camino corto) en la rotación
	static NodePose Blend(const NodePose& a, const NodePose& b, float t)
	{
		NodePose r;
		r.position = glm::mix(a.position, b.position, t);
		r.rotation = glm::normalize(glm::slerp(a.rotation, b.rotation, t));
		r.scale = glm::mix(a.scale, b.scale, t);
		r.animated = a.animated || b.animated;
		return r;
	}
};

// Pistas de un nodo dentro de un clip (aiNodeAnim)
struct NodeChannel
{
	int node = -1;                       // Índice en SkeletonData::nodes
	std::vector<KeyVec3> positions;
	std::vector<KeyQuat> rotations;
	std::vector<KeyVec3> scales;

	// Una pista ausente (p.ej. un canal sin claves de escala) conserva el valor de reposo del nodo
	NodePose Sample(float tick, const SkelNode& rest) const
	{
		NodePose p;
		p.position = AnimMath::Sample(this->positions, tick, rest.restPos);
		p.rotation = AnimMath::Sample(this->rotations, tick, rest.restRot);
		p.scale = AnimMath::Sample(this->scales, tick, rest.restScale);
		p.animated = true;
		return p;
	}
};

// Pista de shape keys de una malla (aiMeshMorphAnim). meshName puede ser el nombre de la malla o del nodo.
struct MorphChannel
{
	std::string meshName;
	int numTargets = 0;
	std::vector<MorphKey> keys;

	void Sample(float tick, std::vector<float>& out) const
	{
		out.assign(this->numTargets, 0.0f);
		if (this->keys.empty()) return;
		if (this->keys.size() == 1) { out = this->keys[0].weights; return; }
		int i = AnimMath::FindKey(this->keys, tick);
		if (i >= static_cast<int>(this->keys.size()) - 1) { out = this->keys.back().weights; return; }
		const MorphKey& a = this->keys[i];
		const MorphKey& b = this->keys[i + 1];
		float f = AnimMath::Factor(a.time, b.time, tick);
		for (int k = 0; k < this->numTargets; k++)
			out[k] = glm::mix(a.weights[k], b.weights[k], f);
	}
};

struct AnimationClip
{
	std::string name;
	std::vector<MorphChannel> morphChannels;
	float duration = 0.0f;                    // En ticks
	float ticksPerSecond = 25.0f;
	std::vector<NodeChannel> channels;
	std::vector<int> channelOfNode;           // nodo -> índice en channels (-1 si el clip no lo anima)

	float DurationSeconds() const { return this->ticksPerSecond > 0.0f ? this->duration / this->ticksPerSecond : 0.0f; }
};

struct SkeletonData
{
	std::vector<SkelNode> nodes;
	std::map<std::string, int> nodeByName;
	std::vector<BoneSlot> bones;
	std::vector<AnimationClip> clips;

	bool IsAnimated() const { return !this->bones.empty(); }

	// Clip por nombre exacto o, si no hay, por coincidencia parcial (los nombres de Mixamo/Blender
	// suelen ser "Armature|mixamo.com|Layer0", "Take 001", "Armature|Taunt"...). -1 si no existe.
	int FindClip(const std::string& name) const
	{
		for (size_t i = 0; i < this->clips.size(); i++)
			if (this->clips[i].name == name) return static_cast<int>(i);
		for (size_t i = 0; i < this->clips.size(); i++)
			if (this->clips[i].name.find(name) != std::string::npos) return static_cast<int>(i);
		return -1;
	}

	// Pesos de shape keys de cada canal del clip en el instante 'tick' (out[canal][objetivo])
	void EvaluateMorph(const AnimationClip* clip, float tick, std::vector<std::vector<float>>& out) const
	{
		out.resize(clip ? clip->morphChannels.size() : 0);
		for (size_t i = 0; i < out.size(); i++)
			clip->morphChannels[i].Sample(tick, out[i]);
	}

	// 1) Transformación local de cada nodo en el instante 'tick' (interpolando las claves del clip).
	//    Los nodos que el clip no anima (o clip = nullptr) quedan en su pose de reposo.
	void SamplePose(const AnimationClip* clip, float tick, std::vector<NodePose>& pose) const
	{
		pose.resize(this->nodes.size());
		for (size_t i = 0; i < this->nodes.size(); i++)
		{
			const SkelNode& n = this->nodes[i];
			if (clip && i < clip->channelOfNode.size() && clip->channelOfNode[i] >= 0)
				pose[i] = clip->channels[clip->channelOfNode[i]].Sample(tick, n);
			else
				pose[i] = NodePose{ n.restPos, n.restRot, n.restScale, false };
		}
	}

	// 2) Recorre la jerarquía (los padres van antes que los hijos) y calcula
	//      global(nodo) = global(padre) * local(nodo)
	//    y la matriz final de cada hueso:
	//      final(hueso) = global(nodo del hueso) * offsetMatrix
	//    La offsetMatrix lleva el vértice del espacio de la malla al espacio del hueso en la pose de
	//    enlace (bind pose); global() lo regresa al espacio del modelo con la pose actual.
	void Compose(const std::vector<NodePose>& pose, std::vector<glm::mat4>& globals, std::vector<glm::mat4>& out) const
	{
		globals.resize(this->nodes.size());
		out.resize(this->bones.size());
		for (size_t i = 0; i < this->nodes.size(); i++)
		{
			const SkelNode& n = this->nodes[i];
			glm::mat4 local = pose[i].animated ? pose[i].ToMatrix() : n.local;
			globals[i] = n.parent >= 0 ? globals[n.parent] * local : local;
		}
		for (size_t b = 0; b < this->bones.size(); b++)
			out[b] = globals[this->bones[b].node] * this->bones[b].offset;
	}

	// Calcula las matrices finales de los huesos. clip = nullptr -> pose de reposo.
	// 'globals' recibe la matriz global de cada nodo (sirve para dibujar el esqueleto).
	void Evaluate(const AnimationClip* clip, float tick, std::vector<glm::mat4>& globals, std::vector<glm::mat4>& out) const
	{
		std::vector<NodePose> pose;
		this->SamplePose(clip, tick, pose);
		this->Compose(pose, globals, out);
	}
};

// Reproductor de animación de una instancia
class Animator
{
public:
	explicit Animator(const SkeletonData* skeleton) : skeleton(skeleton)
	{
		this->evaluate();   // Pose de reposo inicial: las matrices siempre son válidas
	}

	// Reproduce un clip desde el principio. fadeSeconds > 0: transición suave desde la pose visible
	// (durante ese tiempo se mezcla con el clip nuevo, que ya avanza)
	bool Play(int clipIndex, bool loop = true, float fadeSeconds = 0.0f)
	{
		if (!this->validClip(clipIndex))
			return false;
		this->startFade(fadeSeconds);
		this->clip = clipIndex;
		this->loop = loop;
		this->time = this->rangeStart;
		this->playing = true;
		this->finished = false;
		this->evaluate();
		return true;
	}

	bool Play(const std::string& name, bool loop = true)
	{
		return this->skeleton && this->Play(this->skeleton->FindClip(name), loop);
	}

	// Deja la pose de un instante fijo del clip, sin reproducir (p.ej. el primer cuadro como pose de espera)
	bool ShowFrame(int clipIndex, float seconds, float fadeSeconds = 0.0f)
	{
		if (!this->validClip(clipIndex))
			return false;
		this->startFade(fadeSeconds);
		this->clip = clipIndex;
		this->time = std::max(seconds, 0.0f);
		this->playing = false;
		this->finished = false;
		this->evaluate();
		return true;
	}

	void Pause() { this->playing = false; }

	void Resume()
	{
		if (this->clip < 0) { this->Play(0, this->loop); return; }
		if (this->finished) { this->Play(this->clip, this->loop); return; }
		this->playing = true;
	}

	void Toggle() { if (this->playing) this->Pause(); else this->Resume(); }

	// Detiene y vuelve a la pose de reposo del modelo
	void Stop()
	{
		this->clip = -1;
		this->playing = false;
		this->finished = false;
		this->time = 0.0f;
		this->fadeDuration = 0.0f;
		this->evaluate();
	}

	// Avanza con el tiempo real transcurrido (deltaTime en segundos), no con el número de cuadros:
	// la animación dura lo mismo a 30 o a 144 fps
	void Update(float deltaTime)
	{
		bool fading = this->isFading();
		if (fading)
			this->fadeTime = std::min(this->fadeTime + deltaTime, this->fadeDuration);
		if (!this->playing || this->clip < 0)
		{
			if (fading) this->evaluate();   // Transición hacia una pose fija (ShowFrame)
			return;
		}
		float clipDuration = this->skeleton->clips[this->clip].DurationSeconds();
		// Rango de reproducción: por defecto el clip completo
		float start = glm::clamp(this->rangeStart, 0.0f, clipDuration);
		float end = (this->rangeEnd > start && this->rangeEnd < clipDuration) ? this->rangeEnd : clipDuration;
		float span = end - start;

		this->time += deltaTime * this->speed;
		if (span > 0.0f)
		{
			if (this->loop)
			{
				this->time = start + std::fmod(this->time - start, span);
				if (this->time < start) this->time += span;
			}
			else if (this->time >= end)
			{
				this->time = end;            // Se queda en el último fotograma
				this->playing = false;
				this->finished = true;
			}
			else if (this->time < start)
			{
				this->time = start;
				this->playing = false;
			}
		}
		else if (!this->loop)
		{
			this->playing = false;   // Clip de un solo cuadro: termina de inmediato
			this->finished = true;
		}
		this->evaluate();
	}

	// Reproduce solo el tramo [startSeconds, endSeconds] del clip (endSeconds <= 0: hasta el final).
	// Sirve para recortar tiempo muerto al inicio o al final de una animación exportada.
	void SetRange(float startSeconds, float endSeconds = -1.0f)
	{
		this->rangeStart = std::max(startSeconds, 0.0f);
		this->rangeEnd = endSeconds;
		this->time = std::max(this->time, this->rangeStart);
	}

	void SetSpeed(float s) { this->speed = s; }
	void SetLoop(bool l) { this->loop = l; }
	void SetTime(float seconds) { this->time = seconds; this->evaluate(); }

	float GetSpeed() const { return this->speed; }
	float GetTime() const { return this->time; }
	bool IsPlaying() const { return this->playing; }
	bool HasFinished() const { return this->finished; }
	bool IsFading() const { return this->isFading(); }
	int GetClipIndex() const { return this->clip; }
	int GetClipCount() const { return this->skeleton ? static_cast<int>(this->skeleton->clips.size()) : 0; }
	std::string GetClipName() const
	{
		return (this->skeleton && this->clip >= 0) ? this->skeleton->clips[this->clip].name : std::string("(pose base)");
	}
	const std::vector<glm::mat4>& GetBoneMatrices() const { return this->bones; }
	const std::vector<glm::mat4>& GetNodeGlobals() const { return this->globals; }   // Global de cada nodo (espacio del modelo)
	const std::vector<std::vector<float>>& GetMorphWeights() const { return this->morphWeights; }   // [canal][objetivo]
	const AnimationClip* GetClip() const
	{
		return (this->skeleton && this->clip >= 0) ? &this->skeleton->clips[this->clip] : nullptr;
	}

private:
	const SkeletonData* skeleton = nullptr;
	int clip = -1;
	bool playing = false;
	bool loop = true;
	bool finished = false;
	float time = 0.0f;       // Segundos dentro del clip
	float rangeStart = 0.0f;
	float rangeEnd = -1.0f;  // <= rangeStart: hasta el final del clip
	float speed = 1.0f;
	std::vector<glm::mat4> bones;
	std::vector<glm::mat4> globals;
	std::vector<std::vector<float>> morphWeights;

	// Transición suave: pose congelada al iniciar la transición y avance de la mezcla
	std::vector<NodePose> pose;       // Última pose local calculada (la que se ve en pantalla)
	std::vector<NodePose> fadeFrom;
	float fadeTime = 0.0f;
	float fadeDuration = 0.0f;

	bool validClip(int clipIndex) const
	{
		return this->skeleton && clipIndex >= 0 && clipIndex < static_cast<int>(this->skeleton->clips.size());
	}

	bool isFading() const { return this->fadeDuration > 0.0f && this->fadeTime < this->fadeDuration; }

	void startFade(float seconds)
	{
		this->fadeDuration = 0.0f;
		if (seconds <= 0.0f || this->pose.empty()) return;
		this->fadeFrom = this->pose;
		this->fadeTime = 0.0f;
		this->fadeDuration = seconds;
	}

	void evaluate()
	{
		if (!this->skeleton) return;
		const AnimationClip* c = this->clip >= 0 ? &this->skeleton->clips[this->clip] : nullptr;
		// Segundos -> ticks del archivo. Fuera del rango [0, duración] se usa la primera o la última clave.
		float tick = c ? glm::clamp(this->time * c->ticksPerSecond, 0.0f, c->duration) : 0.0f;
		this->skeleton->SamplePose(c, tick, this->pose);
		if (this->fadeDuration > 0.0f && this->fadeFrom.size() == this->pose.size())
		{
			float f = glm::clamp(this->fadeTime / this->fadeDuration, 0.0f, 1.0f);
			f = f * f * (3.0f - 2.0f * f);   // smoothstep: arranca y termina sin brusquedad
			if (f < 1.0f)
				for (size_t i = 0; i < this->pose.size(); i++)
					this->pose[i] = NodePose::Blend(this->fadeFrom[i], this->pose[i], f);
		}
		this->skeleton->Compose(this->pose, this->globals, this->bones);
		this->skeleton->EvaluateMorph(c, tick, this->morphWeights);
	}
};
