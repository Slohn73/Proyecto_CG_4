#pragma once

// Datos de esqueleto y animación + reproductor (Animator).
//
//  - SkeletonData: se construye UNA vez por modelo (lo llena Model.h al cargar con Assimp) y se
//    comparte entre todas las instancias. Contiene el árbol de nodos COMPLETO del archivo (incluye
//    los nodos "$AssimpFbx$" de FBX), los huesos (nodo + offsetMatrix) y los clips de animación.
//  - Animator: estado de reproducción de UNA instancia (tiempo, clip, velocidad) y sus matrices
//    finales de huesos. Cada SceneObject animado tiene el suyo, así dos instancias del mismo
//    modelo pueden estar en poses distintas.
//
// Matriz final de un hueso:  global(nodo del hueso) * offsetMatrix
// El vertex shader la aplica a la posición del archivo (espacio del modelo) y después multiplica
// por la matriz "model" del objeto, igual que en la ruta estática.

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

// Pistas de un nodo dentro de un clip (aiNodeAnim)
struct NodeChannel
{
	int node = -1;                       // Índice en SkeletonData::nodes
	std::vector<KeyVec3> positions;
	std::vector<KeyQuat> rotations;
	std::vector<KeyVec3> scales;

	glm::mat4 Sample(float tick, const SkelNode& rest) const
	{
		glm::vec3 p = AnimMath::Sample(this->positions, tick, rest.restPos);
		glm::quat r = AnimMath::Sample(this->rotations, tick, rest.restRot);
		glm::vec3 s = AnimMath::Sample(this->scales, tick, rest.restScale);
		return glm::translate(glm::mat4(1.0f), p) * glm::mat4_cast(r) * glm::scale(glm::mat4(1.0f), s);
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

	// Calcula las matrices finales de los huesos. clip = nullptr -> pose de reposo.
	// 'globals' es memoria de trabajo (se reutiliza entre cuadros para no reservar cada vez).
	void Evaluate(const AnimationClip* clip, float tick, std::vector<glm::mat4>& globals, std::vector<glm::mat4>& out) const
	{
		globals.resize(this->nodes.size());
		out.resize(this->bones.size());
		for (size_t i = 0; i < this->nodes.size(); i++)
		{
			const SkelNode& n = this->nodes[i];
			glm::mat4 local = n.local;
			if (clip && i < clip->channelOfNode.size() && clip->channelOfNode[i] >= 0)
				local = clip->channels[clip->channelOfNode[i]].Sample(tick, n);
			globals[i] = n.parent >= 0 ? globals[n.parent] * local : local;
		}
		for (size_t b = 0; b < this->bones.size(); b++)
			out[b] = globals[this->bones[b].node] * this->bones[b].offset;
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

	// Reproduce un clip desde el principio
	bool Play(int clipIndex, bool loop = true)
	{
		if (!this->skeleton || clipIndex < 0 || clipIndex >= static_cast<int>(this->skeleton->clips.size()))
			return false;
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
		this->evaluate();
	}

	void Update(float deltaTime)
	{
		if (!this->playing || this->clip < 0) return;
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
	int GetClipIndex() const { return this->clip; }
	int GetClipCount() const { return this->skeleton ? static_cast<int>(this->skeleton->clips.size()) : 0; }
	std::string GetClipName() const
	{
		return (this->skeleton && this->clip >= 0) ? this->skeleton->clips[this->clip].name : std::string("(pose base)");
	}
	const std::vector<glm::mat4>& GetBoneMatrices() const { return this->bones; }
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

	void evaluate()
	{
		if (!this->skeleton) return;
		const AnimationClip* c = this->clip >= 0 ? &this->skeleton->clips[this->clip] : nullptr;
		float tick = c ? this->time * c->ticksPerSecond : 0.0f;
		this->skeleton->Evaluate(c, tick, this->globals, this->bones);
		this->skeleton->EvaluateMorph(c, tick, this->morphWeights);
	}
};
