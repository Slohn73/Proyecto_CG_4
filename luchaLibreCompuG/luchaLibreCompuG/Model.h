#pragma once

#include <string>
#include <iostream>
#include <map>
#include <unordered_map>
#include <vector>
#include <cmath>
#include <cfloat>
#include <filesystem>
#include <sstream>
#include <memory>
#include <algorithm>
#include <cctype>

// Shape keys (morph targets). Los objetivos (aiMesh::mAnimMeshes con sus vértices) existen desde Assimp 4.x.
// Lo SIGUIENTE solo existe en Assimp >= 5.0: nombre y peso por defecto de cada objetivo (aiAnimMesh::mName/mWeight)
// y los canales de shape keys animadas dentro de los clips (aiMeshMorphAnim). Pon 1 solo si tu Assimp es 5.x.
// Con 0 (Assimp 4.x) las shape keys se cargan igual, pero los pesos se controlan por código con
// Model::SetMorphWeights(...) (ver ArenaLayout/main); no se leen de los clips del archivo.
#define ANIM_USE_ASSIMP_MORPH 1

#include <GL/glew.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#include "Mesh.h"
#include "Shader.h"
#include "Texture.h"
#include "Animation.h"

using namespace std;

// Opciones de carga por modelo (se definen en ArenaLayout.h)
struct ModelOptions
{
	// Prefijos de nodos o mallas que no se cargan (p.ej. pisos de referencia que vienen en el archivo)
	vector<string> excludeNodes;
	// Textura difusa a usar para un material: nombre de material -> ruta relativa al archivo del modelo.
	// La clave "*" se aplica a todos los materiales que no traigan mapa difuso propio.
	map<string, string> diffuseOverrides;
	// Opacidad por material (nombre -> 0..1) para superficies que deben ser translúcidas
	// aunque el archivo no lo indique (p.ej. el haz de luz de un reflector)
	map<string, float> opacityOverrides;
	// Color emisivo por material (superficies que deben verse encendidas, como un haz de luz)
	map<string, glm::vec3> emissiveOverrides;
	// Materiales que se dibujan como haz de luz (sin iluminación, ver Material::glow)
	vector<string> glowMaterials;
	// Activa el descarte por alfa en todos los materiales del modelo
	bool alphaTest = false;
	// Blender y la mayoría de exportadores guardan los colores de material en espacio lineal,
	// mientras que el shader trabaja con valores como los de las texturas (sRGB). Se convierten
	// para que un color de material se vea igual que en Blender.
	bool linearColors = true;
	// Une las mallas que comparten material en una sola (menos llamadas de dibujo). Los colores de
	// materiales sin textura pasan a color por vértice, así que la apariencia no cambia.
	bool mergeMeshes = true;
	// Archivos extra (rutas relativas al del modelo) de los que se toman SOLO las animaciones.
	// Típico de Mixamo: un FBX "With Skin" del personaje + un FBX "Without Skin" por animación.
	// Los nombres de los nodos/huesos deben coincidir con los del modelo.
	vector<string> animationFiles;
	// Reflectividad por material (nombre -> 0..1) para el modelo de Fresnel + mapa de entorno (cubemap).
	// 0 = Phong puro; ~0.3 plástico/barniz; ~0.6-0.9 metal. La clave "*" aplica a todos los materiales.
	map<string, float> reflectivityOverrides;
	// Clip cuyo primer cuadro se usa como pose de referencia (cajas envolventes, normalización y pose
	// sin animación). -1 = transformaciones de los nodos del archivo, que en un FBX exportado desde
	// Blender son la pose del cuadro en que se exportó, no necesariamente una pose de pie.
	int restPoseClip = -1;
};

// Parte original del archivo (nodo/malla) con su caja envolvente; se conserva aunque las
// mallas se unan, para calcular pivotes y medidas a partir de partes concretas.
struct MeshPart
{
	string name;
	glm::vec3 boundsMin;
	glm::vec3 boundsMax;
};

enum class RenderPass { All, Opaque, Transparent };

class Model
{
public:
	/*  Functions   */
	// Constructor, expects a filepath to a 3D model.
	Model(GLchar *path) : Model(string(path)) {}

	explicit Model(const string& path, const ModelOptions& options = ModelOptions())
		: path(path), options(options)
	{
		this->loadModel(path);
	}

	// Modelo generado por código (p.ej. el piso de la arena)
	Model(const string& name, const vector<Vertex>& vertices, const vector<GLuint>& indices, const Material& material)
		: path(name)
	{
		this->meshes.emplace_back(name, vertices, indices, material);
		this->parts.push_back({ name, this->meshes.back().boundsMin, this->meshes.back().boundsMax });
		this->triangleCount = indices.size() / 3;
		this->computeBounds();
	}

	// Draws the model, and thus all its meshes. El shader ya debe tener la matriz "model"
	// (y, si el modelo está animado, las matrices de los huesos: las sube Scene::drawPass al bloque "Bones").
	void Draw(Shader& shader, RenderPass pass = RenderPass::All) const
	{
		const MaterialUniforms& uniforms = MaterialUniforms::For(shader.Program);
		for (const Mesh& mesh : this->meshes)
		{
			bool transparent = mesh.material.IsTransparent();
			if (pass == RenderPass::Opaque && transparent) continue;
			if (pass == RenderPass::Transparent && !transparent) continue;
			mesh.Draw(uniforms);
		}
	}

	bool IsLoaded() const { return !this->meshes.empty(); }
	bool HasTransparency() const { return this->hasTransparency; }
	const string& GetPath() const { return this->path; }
	const vector<Mesh>& GetMeshes() const { return this->meshes; }
	size_t GetTriangleCount() const { return this->triangleCount; }

	// ---- Animación ----
	// El estado de reproducción vive en el modelo: todas las instancias (SceneObject) del mismo modelo
	// comparten la misma pose. Para poses independientes, registra el recurso con otro id.

	// true si el archivo trae clips (huesos, nodos o shape keys)
	bool HasAnimations() const { return !this->skeleton.clips.empty(); }
	int GetAnimationCount() const { return static_cast<int>(this->skeleton.clips.size()); }
	// true si trae huesos (o mallas rígidas animables): Scene sube sus matrices al bloque "Bones"
	bool HasBones() const { return this->skeleton.IsAnimated(); }
	const SkeletonData& GetSkeleton() const { return this->skeleton; }
	const vector<glm::mat4>& GetRestBones() const { return this->restBones; }
	const vector<glm::mat4>& GetBoneMatrices() const { return this->animator ? this->animator->GetBoneMatrices() : this->restBones; }
	// Matriz global (espacio del archivo) de cada nodo de SkeletonData::nodes en la pose actual
	const vector<glm::mat4>& GetNodeGlobals() const { return this->animator ? this->animator->GetNodeGlobals() : this->restGlobals; }
	Animator* GetAnimator() { return this->HasAnimations() ? &this->anim() : nullptr; }
	int FindAnimation(const string& name) const { return this->skeleton.FindClip(name); }

	// Avanzar (una vez por cuadro, con el deltaTime)
	void UpdateAnimation(float deltaTime)
	{
		if (!this->HasAnimations()) return;
		this->anim().Update(deltaTime);
		this->applyMorphs();
	}

	// Reproduce el clip desde el inicio (loop = false: una sola vez y se queda en el último fotograma).
	// fadeSeconds > 0: transición suave desde la pose actual.
	void TriggerAnimation(int clip, bool loop = false, float fadeSeconds = 0.0f)
	{
		if (!this->HasAnimations()) return;
		this->anim().Play(clip, loop, fadeSeconds);
		this->applyMorphs();
	}

	// Pose fija de un instante del clip (sin reproducir)
	void ShowAnimationFrame(int clip, float seconds, float fadeSeconds = 0.0f)
	{
		if (!this->HasAnimations()) return;
		this->anim().ShowFrame(clip, seconds, fadeSeconds);
		this->applyMorphs();
	}

	// Mismo clip: pausa/reanuda. Otro clip: lo inicia.
	void ToggleAnimation(int clip, bool loop = true)
	{
		if (!this->HasAnimations()) return;
		Animator& a = this->anim();
		if (a.GetClipIndex() != clip) a.Play(clip, loop);
		else a.Toggle();
		this->applyMorphs();
	}

	void StopAnimation()
	{
		if (!this->HasAnimations()) return;
		this->anim().Stop();
		this->applyMorphs();
	}

	// Control manual de shape keys por código (único modo con Assimp 4.x): pesos de los objetivos 0..N-1,
	// normalmente de 0 a 1. Se aplican a todas las mallas con shape keys y tienen prioridad sobre los clips.
	bool HasMorphTargets() const { return !this->morphBindings.empty(); }
	void SetMorphWeights(const vector<float>& weights)
	{
		this->manualMorph = weights;
		this->applyMorphs();
	}
	void ClearMorphWeights()
	{
		this->manualMorph.clear();
		this->applyMorphs();
	}

	// Reproduce solo el tramo [inicio, fin] (segundos) de los clips: recorta tiempo muerto al principio/final
	void SetAnimationRange(float startSeconds, float endSeconds = -1.0f)
	{
		if (this->HasAnimations()) this->anim().SetRange(startSeconds, endSeconds);
	}

	void SetAnimationSpeed(float speed) { if (this->HasAnimations()) this->anim().SetSpeed(speed); }

	// Caja envolvente del modelo completo en sus unidades originales
	// (en modelos animados, calculada con la pose de reposo)
	glm::vec3 GetBoundsMin() const { return this->boundsMin; }
	glm::vec3 GetBoundsMax() const { return this->boundsMax; }
	glm::vec3 GetSize() const { return this->boundsMax - this->boundsMin; }
	glm::vec3 GetCenter() const { return (this->boundsMax + this->boundsMin) * 0.5f; }

	// Caja envolvente de las mallas cuyo nombre empieza con 'prefix' (p.ej. "Piso_Ring")
	bool GetPartBounds(const string& prefix, glm::vec3& outMin, glm::vec3& outMax) const
	{
		outMin = glm::vec3(FLT_MAX);
		outMax = glm::vec3(-FLT_MAX);
		bool found = false;
		for (const MeshPart& part : this->parts)
		{
			if (part.name.rfind(prefix, 0) != 0) continue;
			outMin = glm::min(outMin, part.boundsMin);
			outMax = glm::max(outMax, part.boundsMax);
			found = true;
		}
		return found;
	}

	void PrintSummary() const
	{
		glm::vec3 s = this->GetSize(), c = this->GetCenter();
		cout << "  " << this->parts.size() << " partes en " << this->meshes.size() << " mallas de dibujo, " << this->triangleCount << " triangulos";
		if (this->excludedNodes > 0) cout << ", " << this->excludedNodes << " nodos excluidos";
		cout << "\n  tamano (" << s.x << ", " << s.y << ", " << s.z << ")  centro (" << c.x << ", " << c.y << ", " << c.z << ")"
			<< "  minY " << this->boundsMin.y << endl;
		this->PrintAnimations();
	}

	void PrintAnimations() const
	{
		if (!this->morphBindings.empty())
			cout << "  shape keys: " << this->morphBindings.size() << " mallas con objetivos de morph" << endl;
		if (!this->HasAnimations() && !this->skeleton.IsAnimated()) return;
		cout << "  esqueleto: " << this->skeleton.bones.size() << " huesos, " << this->skeleton.nodes.size() << " nodos, "
			<< this->skeleton.clips.size() << " animaciones" << endl;
		if (this->skinnedVertices + this->unweightedVertices > 0)
			cout << "  skinning: " << this->skinnedVertices << " vertices con pesos (max. 4 por vertice, suma 1), "
				<< this->unweightedVertices << " sin pesos (siguen al nodo de su malla)" << endl;
		for (size_t i = 0; i < this->skeleton.clips.size(); i++)
		{
			const AnimationClip& c = this->skeleton.clips[i];
			cout << "    [" << i << "] '" << c.name << "' " << c.DurationSeconds() << " s (" << c.duration << " ticks a "
				<< c.ticksPerSecond << " ticks/s), " << c.channels.size() << " canales, " << c.morphChannels.size() << " canales de shape keys" << endl;
		}
	}

	void PrintMaterials() const
	{
		// Materiales tal como vienen del archivo (antes de unir mallas)
		for (auto& kv : this->materialCache)
		{
			const Material& m = kv.second;
			cout << "    material '" << m.name << "': " << (m.diffuseMap ? "textura difusa" : "color")
				<< " (" << m.diffuseColor.r << ", " << m.diffuseColor.g << ", " << m.diffuseColor.b << ")"
				<< (m.specularMap ? " +especular" : "") << " brillo " << m.shininess;
			if (m.IsTransparent()) cout << " opacidad " << m.opacity;
			cout << endl;
		}
	}

private:
	/*  Model Data  */
	string path;
	ModelOptions options;
	vector<Mesh> meshes;
	vector<MeshPart> parts;
	string directory;
	glm::vec3 boundsMin = glm::vec3(0.0f);
	glm::vec3 boundsMax = glm::vec3(0.0f);
	size_t triangleCount = 0;
	int excludedNodes = 0;
	bool hasTransparency = false;
	map<unsigned int, Material> materialCache; // Un material de Assimp se convierte una sola vez

	/*  Animación (solo se llena si el archivo trae huesos o animaciones)  */
	SkeletonData skeleton;                            // Compartido por todas las instancias del modelo
	vector<glm::mat4> restBones;                      // Matrices finales en pose de reposo
	vector<glm::mat4> restGlobals;                    // Globales de los nodos en pose de reposo
	bool animated = false;                            // Todas sus mallas usan (o pueden usar) skinning
	bool bakeSkinnedNodes = false;                    // FBX: la malla con piel lleva horneada la transformación de su nodo (ver processMesh)
	bool warnedBoneLimit = false;
	size_t skinnedVertices = 0;                       // Vértices con pesos de huesos (renormalizados a suma 1)
	size_t unweightedVertices = 0;                    // Vértices de mallas con huesos que no traían pesos
	unique_ptr<Animator> animator;                    // Se crea la primera vez que se usa

	// Malla de dibujo con shape keys (no se une con otras) y a qué canales de clip responde
	struct MorphBinding { size_t meshIndex; string nodeName; string meshName; int numTargets; };
	vector<MorphBinding> morphBindings;
	vector<float> manualMorph;                        // Pesos puestos por código (vacío = usar clips / valores del archivo)
	vector<vector<vector<int>>> morphTargetOf;        // [clip][canal] -> índices en morphBindings

	Animator& anim()
	{
		if (!this->animator)
		{
			this->animator = make_unique<Animator>(&this->skeleton);
			// Sin clip activo se muestra la misma pose de referencia que se usó para las cajas
			if (this->options.restPoseClip >= 0)
				this->animator->ShowFrame(this->options.restPoseClip, 0.0f);
		}
		return *this->animator;
	}
	unordered_map<const aiNode*, int> nodeIndexOf;    // Solo durante la carga: aiNode -> índice en skeleton.nodes
	map<string, vector<int>> boneSlotsByName;         // Huesos reales por nombre (varios si el offset difiere)
	map<int, int> rigidSlotOfNode;                    // Hueso sintético por nodo para mallas sin skin (-1 = no cupo)

	// Geometría ya transformada, antes de subirla a la GPU
	struct MeshData
	{
		string name;
		vector<Vertex> vertices;
		vector<GLuint> indices;
		Material material;
		vector<MorphTarget> morph;                // Shape keys de la malla (vacío si no trae)
		string meshName;                          // Nombre de la aiMesh (los canales de morph se enlazan por él)
		bool skinned = false;
		glm::vec3 restMin = glm::vec3(FLT_MAX);   // Caja en pose de reposo (con skinning aplicado en CPU)
		glm::vec3 restMax = glm::vec3(-FLT_MAX);
	};
	vector<MeshData> pending;

	/*  Functions   */
	// Loads a model with supported ASSIMP extensions from file and stores the resulting meshes in the meshes vector.
	void loadModel(const string& path)
	{
		// Read file via ASSIMP
		Assimp::Importer importer;
		// Se descartan puntos y líneas sueltas: este cargador solo dibuja triángulos
		importer.SetPropertyInteger(AI_CONFIG_PP_SBP_REMOVE, aiPrimitiveType_POINT | aiPrimitiveType_LINE);
		// LimitBoneWeights: como máximo 4 huesos por vértice (lo que cabe en ivec4/vec4) y pesos renormalizados.
		// NO usar aiProcess_PreTransformVertices ni aiProcess_OptimizeGraph/OptimizeMeshes: destruyen la jerarquía.
		const aiScene *scene = importer.ReadFile(path,
			aiProcess_Triangulate | aiProcess_FlipUVs | aiProcess_GenSmoothNormals |
			aiProcess_JoinIdenticalVertices | aiProcess_SortByPType | aiProcess_ImproveCacheLocality |
			aiProcess_LimitBoneWeights);

		// Check for errors
		if (!scene || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) || !scene->mRootNode)
		{
			cout << "  ERROR::ASSIMP:: no se pudo cargar '" << path << "': " << importer.GetErrorString() << endl;
			return;
		}
		// Retrieve the directory path of the filepath
		string normalized = TextureLoader::NormalizePath(path);
		size_t slash = normalized.find_last_of('/');
		this->directory = (slash == string::npos) ? "." : normalized.substr(0, slash);
		string ext = std::filesystem::path(normalized).extension().string();
		std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
		this->bakeSkinnedNodes = ext == ".fbx";

		// ¿Trae esqueleto o animaciones? Entonces el modelo entero se trata como animado
		this->animated = !this->options.animationFiles.empty() || scene->mNumAnimations > 0;
		for (unsigned int i = 0; i < scene->mNumMeshes && !this->animated; i++)
			this->animated = scene->mMeshes[i]->HasBones();
		if (this->animated)
			this->prepareSkeleton(scene);

		// Process ASSIMP's root node recursively, acumulando las transformaciones de cada nodo
		this->processNode(scene->mRootNode, scene, glm::mat4(1.0f));
		this->buildMeshes();
		this->computeBounds();
		this->resolveMorphBindings();
		this->applyMorphs();   // Deja puesto el peso por defecto de cada shape key

		this->nodeIndexOf.clear();
		this->boneSlotsByName.clear();
		this->rigidSlotOfNode.clear();
	}

	// ------------------------------------------------------------------------------------
	//  Esqueleto y animaciones
	// ------------------------------------------------------------------------------------

	// Copia el árbol de nodos COMPLETO (incluidos los "$AssimpFbx$" de FBX: las animaciones de FBX
	// apuntan a ellos por nombre), registra los huesos reales y lee los clips.
	void prepareSkeleton(const aiScene* scene)
	{
		this->addSkelNode(scene->mRootNode, -1);

		// Los huesos reales se registran primero para que los huesos sintéticos de mallas rígidas
		// nunca les quiten lugar dentro de MAX_BONES
		for (unsigned int m = 0; m < scene->mNumMeshes; m++)
		{
			const aiMesh* mesh = scene->mMeshes[m];
			for (unsigned int b = 0; b < mesh->mNumBones; b++)
			{
				const aiBone* bone = mesh->mBones[b];
				auto node = this->skeleton.nodeByName.find(bone->mName.C_Str());
				if (node != this->skeleton.nodeByName.end())
					this->findOrAddBone(node->first, node->second, toGlm(bone->mOffsetMatrix));
			}
		}

		this->readClips(scene);
		for (const string& file : this->options.animationFiles)
			this->loadExtraAnimations(this->directory + "/" + file);
	}

	int addSkelNode(const aiNode* node, int parent)
	{
		SkelNode n;
		n.name = node->mName.C_Str();
		n.parent = parent;
		n.local = toGlm(node->mTransformation);
		aiVector3D s, p;
		aiQuaternion q;
		node->mTransformation.Decompose(s, q, p);
		n.restScale = glm::vec3(s.x, s.y, s.z);
		n.restPos = glm::vec3(p.x, p.y, p.z);
		n.restRot = glm::quat(q.w, q.x, q.y, q.z);

		int index = static_cast<int>(this->skeleton.nodes.size());
		this->skeleton.nodes.push_back(n);
		this->skeleton.nodeByName.emplace(n.name, index);   // Si hay nombres repetidos gana el primero
		this->nodeIndexOf[node] = index;
		for (unsigned int i = 0; i < node->mNumChildren; i++)
			this->addSkelNode(node->mChildren[i], index);
		return index;
	}

	void readClips(const aiScene* scene)
	{
		for (unsigned int a = 0; a < scene->mNumAnimations; a++)
		{
			const aiAnimation* src = scene->mAnimations[a];
			AnimationClip clip;
			clip.name = src->mName.length > 0 ? string(src->mName.C_Str()) : "clip_" + to_string(this->skeleton.clips.size());
			clip.ticksPerSecond = src->mTicksPerSecond > 0.0 ? static_cast<float>(src->mTicksPerSecond) : 25.0f;
			clip.channelOfNode.assign(this->skeleton.nodes.size(), -1);

			float lastKey = 0.0f;
			unsigned int missing = 0;
			for (unsigned int c = 0; c < src->mNumChannels; c++)
			{
				const aiNodeAnim* ch = src->mChannels[c];
				auto node = this->skeleton.nodeByName.find(ch->mNodeName.C_Str());
				if (node == this->skeleton.nodeByName.end()) { missing++; continue; }

				NodeChannel nc;
				nc.node = node->second;
				for (unsigned int k = 0; k < ch->mNumPositionKeys; k++)
				{
					const aiVectorKey& key = ch->mPositionKeys[k];
					nc.positions.push_back({ static_cast<float>(key.mTime), glm::vec3(key.mValue.x, key.mValue.y, key.mValue.z) });
					lastKey = std::max(lastKey, static_cast<float>(key.mTime));
				}
				for (unsigned int k = 0; k < ch->mNumRotationKeys; k++)
				{
					const aiQuatKey& key = ch->mRotationKeys[k];
					nc.rotations.push_back({ static_cast<float>(key.mTime), glm::quat(key.mValue.w, key.mValue.x, key.mValue.y, key.mValue.z) });
					lastKey = std::max(lastKey, static_cast<float>(key.mTime));
				}
				for (unsigned int k = 0; k < ch->mNumScalingKeys; k++)
				{
					const aiVectorKey& key = ch->mScalingKeys[k];
					nc.scales.push_back({ static_cast<float>(key.mTime), glm::vec3(key.mValue.x, key.mValue.y, key.mValue.z) });
					lastKey = std::max(lastKey, static_cast<float>(key.mTime));
				}
				clip.channelOfNode[nc.node] = static_cast<int>(clip.channels.size());
				clip.channels.push_back(std::move(nc));
			}
#if ANIM_USE_ASSIMP_MORPH
			// Shape keys animadas: por cada malla, los pesos de cada objetivo a lo largo del tiempo
			for (unsigned int c = 0; c < src->mNumMorphMeshChannels; c++)
			{
				const aiMeshMorphAnim* ch = src->mMorphMeshChannels[c];
				MorphChannel mc;
				mc.meshName = ch->mName.C_Str();
				for (unsigned int k = 0; k < ch->mNumKeys; k++)
					for (unsigned int v = 0; v < ch->mKeys[k].mNumValuesAndWeights; v++)
						mc.numTargets = std::max(mc.numTargets, static_cast<int>(ch->mKeys[k].mValues[v]) + 1);
				for (unsigned int k = 0; k < ch->mNumKeys; k++)
				{
					const aiMeshMorphKey& key = ch->mKeys[k];
					MorphKey mk;
					mk.time = static_cast<float>(key.mTime);
					mk.weights.assign(mc.numTargets, 0.0f);
					for (unsigned int v = 0; v < key.mNumValuesAndWeights; v++)
						mk.weights[key.mValues[v]] = static_cast<float>(key.mWeights[v]);
					lastKey = std::max(lastKey, mk.time);
					mc.keys.push_back(std::move(mk));
				}
				clip.morphChannels.push_back(std::move(mc));
			}
#endif
			// Algunos exportadores dejan mDuration en 0: se toma la última clave
			clip.duration = src->mDuration > 0.0 ? static_cast<float>(src->mDuration) : lastKey;

			if (missing > 0)
				cout << "  [Anim] AVISO: '" << clip.name << "' tiene " << missing << " canales para nodos que no existen en el modelo" << endl;
			if (clip.channels.empty() && clip.morphChannels.empty())
				cout << "  [Anim] AVISO: '" << clip.name << "' no tiene canales utilizables" << endl;
			this->skeleton.clips.push_back(std::move(clip));
		}
	}

	// Lee solo las animaciones de otro archivo (p.ej. Mixamo "Without Skin")
	void loadExtraAnimations(const string& file)
	{
		Assimp::Importer importer;
		const aiScene* extra = importer.ReadFile(file, 0);
		if (!extra || extra->mNumAnimations == 0)
		{
			cout << "  [Anim] AVISO: '" << file << "' no se pudo leer o no trae animaciones" << endl;
			return;
		}
		this->readClips(extra);
	}

	static bool sameMatrix(const glm::mat4& a, const glm::mat4& b)
	{
		for (int c = 0; c < 4; c++)
			for (int r = 0; r < 4; r++)
				if (fabsf(a[c][r] - b[c][r]) > 1e-4f) return false;
		return true;
	}

	// Un hueso real se identifica por nombre + offsetMatrix (en glTF dos "skins" pueden compartir
	// el nodo con distinto offset). Devuelve -1 si se supera MAX_BONES.
	int findOrAddBone(const string& name, int node, const glm::mat4& offset)
	{
		vector<int>& slots = this->boneSlotsByName[name];
		for (int s : slots)
			if (sameMatrix(this->skeleton.bones[s].offset, offset)) return s;

		if (this->skeleton.bones.size() >= static_cast<size_t>(MAX_BONES))
		{
			if (!this->warnedBoneLimit)
				cout << "  [Anim] AVISO: '" << this->path << "' supera " << MAX_BONES << " huesos; los sobrantes no se animan (sube MAX_BONES en Animation.h y lighting.vs)" << endl;
			this->warnedBoneLimit = true;
			return -1;
		}
		int index = static_cast<int>(this->skeleton.bones.size());
		this->skeleton.bones.push_back({ name, node, offset });
		slots.push_back(index);
		return index;
	}

	// Hueso sintético (offset identidad) para una malla sin skin colgada del nodo 'node'.
	// Los vértices quedan en el espacio local del nodo y el hueso aporta la transformación global
	// (animada). Devuelve -1 si no hay lugar: esa malla se queda estática con el nodo "horneado".
	// Con 'offset' distinto de la identidad se crea un hueso propio (no se comparte por nodo).
	int rigidSlot(int node, const glm::mat4& offset = glm::mat4(1.0f))
	{
		bool shared = sameMatrix(offset, glm::mat4(1.0f));
		auto it = this->rigidSlotOfNode.find(node);
		if (shared && it != this->rigidSlotOfNode.end()) return it->second;

		int slot = -1;
		if (this->skeleton.bones.size() < static_cast<size_t>(MAX_BONES))
		{
			slot = static_cast<int>(this->skeleton.bones.size());
			this->skeleton.bones.push_back({ "__rigido__" + this->skeleton.nodes[node].name, node, offset });
		}
		if (shared) this->rigidSlotOfNode[node] = slot;
		return slot;
	}

	static void addInfluence(Vertex& v, int slot, float weight)
	{
		int weakest = 0;
		for (int k = 0; k < 4; k++)
		{
			if (v.Weights[k] == 0.0f) { v.BoneIDs[k] = slot; v.Weights[k] = weight; return; }
			if (v.Weights[k] < v.Weights[weakest]) weakest = k;
		}
		if (weight > v.Weights[weakest]) { v.BoneIDs[weakest] = slot; v.Weights[weakest] = weight; }
	}

	// Reparte los pesos de los huesos de una malla a sus vértices
	// 'baked' es la transformación ya aplicada a los vértices (identidad salvo en FBX, ver processMesh)
	void fillBoneWeights(const aiMesh* mesh, vector<Vertex>& vertices, int meshNode, const glm::mat4& baked)
	{
		for (unsigned int b = 0; b < mesh->mNumBones; b++)
		{
			const aiBone* bone = mesh->mBones[b];
			string name = bone->mName.C_Str();
			auto node = this->skeleton.nodeByName.find(name);
			if (node == this->skeleton.nodeByName.end())
			{
				cout << "  [Anim] AVISO: el hueso '" << name << "' no tiene nodo en el arbol del archivo" << endl;
				continue;
			}
			int slot = this->findOrAddBone(name, node->second, toGlm(bone->mOffsetMatrix));
			if (slot < 0) continue;

			for (unsigned int w = 0; w < bone->mNumWeights; w++)
			{
				unsigned int id = bone->mWeights[w].mVertexId;
				float weight = bone->mWeights[w].mWeight;
				if (id < vertices.size() && weight > 0.0f)
					addInfluence(vertices[id], slot, weight);
			}
		}

		int fallback = -2;
		for (Vertex& v : vertices)
		{
			float sum = v.Weights.x + v.Weights.y + v.Weights.z + v.Weights.w;
			if (sum > 1e-5f)
			{
				v.Weights /= sum;
				this->skinnedVertices++;
				continue;
			}
			this->unweightedVertices++;
			// Vértice sin pesos: sigue al nodo de su malla (como una malla rígida). Si la transformación
			// de reposo del nodo ya está horneada, el offset la deshace para no aplicarla dos veces:
			//   global(nodo) * inverse(reposo horneado) * v
			if (fallback == -2)
				fallback = sameMatrix(baked, glm::mat4(1.0f)) ? this->rigidSlot(meshNode) : this->rigidSlot(meshNode, glm::inverse(baked));
			if (fallback >= 0)
			{
				v.BoneIDs = glm::ivec4(fallback, 0, 0, 0);
				v.Weights = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
			}
		}
	}

	// ---- Shape keys ----
	// Entrega los objetivos de morph a la malla de dibujo recién creada y la registra
	void attachMorph(MeshData& data)
	{
		if (data.morph.empty()) return;
		int count = static_cast<int>(data.morph.size());
		this->meshes.back().SetMorph(std::move(data.morph), data.vertices);
		this->morphBindings.push_back({ this->meshes.size() - 1, data.name, data.meshName, count });
	}

	// El nombre de un canal puede ser el de la malla o el del nodo; también se acepta como prefijo
	// (Assimp lo permite como comodín de varias mallas con el mismo montaje de shape keys)
	static bool morphMatches(const MorphBinding& b, const string& name)
	{
		if (name.empty()) return false;
		return b.nodeName == name || b.meshName == name || b.meshName.rfind(name, 0) == 0 || b.nodeName.rfind(name, 0) == 0;
	}

	// Para cada canal de cada clip, qué mallas con shape keys lo reciben. Si ninguna coincide por
	// nombre, se aplica a las mallas que tengan al menos tantos objetivos como el canal.
	void resolveMorphBindings()
	{
		this->morphTargetOf.clear();
		if (this->morphBindings.empty()) return;
		for (const AnimationClip& clip : this->skeleton.clips)
		{
			vector<vector<int>> perChannel;
			for (const MorphChannel& ch : clip.morphChannels)
			{
				vector<int> list;
				for (size_t b = 0; b < this->morphBindings.size(); b++)
					if (morphMatches(this->morphBindings[b], ch.meshName)) list.push_back(static_cast<int>(b));
				if (list.empty())
					for (size_t b = 0; b < this->morphBindings.size(); b++)
						if (this->morphBindings[b].numTargets >= ch.numTargets) list.push_back(static_cast<int>(b));
				perChannel.push_back(list);
			}
			this->morphTargetOf.push_back(perChannel);
		}
	}

	// Pesos por defecto del archivo, sustituidos por los del clip activo (si hay)
	void applyMorphs()
	{
		if (this->morphBindings.empty()) return;
		vector<vector<float>> weights(this->morphBindings.size());
		for (size_t b = 0; b < weights.size(); b++)
			weights[b] = this->meshes[this->morphBindings[b].meshIndex].GetMorphDefaults();

		int clip = this->animator ? this->animator->GetClipIndex() : -1;
		if (clip >= 0 && clip < static_cast<int>(this->morphTargetOf.size()))
		{
			const vector<vector<float>>& ws = this->animator->GetMorphWeights();
			const vector<vector<int>>& map = this->morphTargetOf[clip];
			for (size_t c = 0; c < ws.size() && c < map.size(); c++)
				for (int b : map[c])
				{
					weights[b] = ws[c];
					weights[b].resize(this->morphBindings[b].numTargets, 0.0f);
				}
		}
		if (!this->manualMorph.empty())
			for (size_t b = 0; b < weights.size(); b++)
			{
				weights[b] = this->manualMorph;
				weights[b].resize(this->morphBindings[b].numTargets, 0.0f);
			}
		for (size_t b = 0; b < weights.size(); b++)
			this->meshes[this->morphBindings[b].meshIndex].ApplyMorph(weights[b]);
	}

	// Posición de un vértice animado en pose de reposo (solo para cajas envolventes)
	glm::vec3 skinRest(const Vertex& v) const
	{
		glm::mat4 m(0.0f);
		float total = 0.0f;
		for (int k = 0; k < 4; k++)
		{
			if (v.Weights[k] > 0.0f)
			{
				m += this->restBones[v.BoneIDs[k]] * v.Weights[k];
				total += v.Weights[k];
			}
		}
		if (total < 1e-5f) return v.Position;
		return glm::vec3(m * glm::vec4(v.Position, 1.0f));
	}

	// ------------------------------------------------------------------------------------

	// Registra las partes originales y crea las mallas de dibujo (unidas por material si se pidió)
	void buildMeshes()
	{
		// Pose de reposo: las cajas envolventes (y con ellas la normalización de Scene) salen de ahí
		if (this->animated)
		{
			int c = this->options.restPoseClip;
			const AnimationClip* restClip = (c >= 0 && c < static_cast<int>(this->skeleton.clips.size())) ? &this->skeleton.clips[c] : nullptr;
			if (c >= 0 && !restClip)
				cout << "  [Anim] AVISO: restPoseClip " << c << " no existe; se usa la pose de los nodos" << endl;
			this->skeleton.Evaluate(restClip, 0.0f, this->restGlobals, this->restBones);
		}

		for (MeshData& data : this->pending)
		{
			for (const Vertex& v : data.vertices)
			{
				glm::vec3 p = data.skinned ? this->skinRest(v) : v.Position;
				data.restMin = glm::min(data.restMin, p);
				data.restMax = glm::max(data.restMax, p);
			}
			this->parts.push_back({ data.name, data.restMin, data.restMax });
		}

		if (!this->options.mergeMeshes)
		{
			for (MeshData& data : this->pending)
			{
				this->meshes.emplace_back(data.name, data.vertices, data.indices, data.material);
				this->meshes.back().SetBounds(data.restMin, data.restMax);
				this->attachMorph(data);
			}
			this->pending.clear();
			return;
		}

		// Clave de agrupación: todo lo que el shader usa del material, salvo el color difuso de los
		// materiales sin textura, que se guarda en cada vértice. Las mallas animadas y las estáticas
		// nunca se mezclan (el skinning se activa por malla).
		auto key = [](const Material& m) {
			ostringstream k;
			k << m.diffuseMap << '|' << m.specularMap << '|' << m.specularColor.r << ',' << m.specularColor.g << ',' << m.specularColor.b
				<< '|' << m.emissiveColor.r << ',' << m.emissiveColor.g << ',' << m.emissiveColor.b << '|' << m.shininess
				<< '|' << m.opacity << '|' << m.alphaTest << '|' << m.glow << '|' << m.skinned << '|' << m.reflectivity;
			return k.str();
		};

		map<string, size_t> groupIndex;
		vector<MeshData> groups;
		for (MeshData& data : this->pending)
		{
			bool useVertexColor = data.material.diffuseMap == 0;
			glm::vec3 color = useVertexColor ? data.material.diffuseColor : glm::vec3(1.0f);
			for (Vertex& v : data.vertices)
				v.Color = color;

			// Las mallas con shape keys van solas: sus vértices cambian cada cuadro
			if (!data.morph.empty())
			{
				if (useVertexColor)
					data.material.diffuseColor = glm::vec3(1.0f);
				this->meshes.emplace_back(data.name, data.vertices, data.indices, data.material);
				this->meshes.back().SetBounds(data.restMin, data.restMax);
				this->attachMorph(data);
				continue;
			}

			string k = key(data.material);
			auto it = groupIndex.find(k);
			if (it == groupIndex.end())
			{
				groupIndex[k] = groups.size();
				MeshData g;
				g.name = data.name;
				g.material = data.material;
				g.skinned = data.skinned;
				if (useVertexColor)
					g.material.diffuseColor = glm::vec3(1.0f);
				groups.push_back(std::move(g));
				it = groupIndex.find(k);
			}
			MeshData& g = groups[it->second];
			g.restMin = glm::min(g.restMin, data.restMin);
			g.restMax = glm::max(g.restMax, data.restMax);
			GLuint offset = static_cast<GLuint>(g.vertices.size());
			g.vertices.insert(g.vertices.end(), data.vertices.begin(), data.vertices.end());
			for (GLuint idx : data.indices)
				g.indices.push_back(idx + offset);
		}
		for (MeshData& g : groups)
		{
			this->meshes.emplace_back(g.name, g.vertices, g.indices, g.material);
			this->meshes.back().SetBounds(g.restMin, g.restMax);
		}
		this->pending.clear();
	}

	void computeBounds()
	{
		this->boundsMin = glm::vec3(FLT_MAX);
		this->boundsMax = glm::vec3(-FLT_MAX);
		for (const Mesh& mesh : this->meshes)
		{
			this->boundsMin = glm::min(this->boundsMin, mesh.boundsMin);
			this->boundsMax = glm::max(this->boundsMax, mesh.boundsMax);
			this->hasTransparency = this->hasTransparency || mesh.material.IsTransparent();
		}
		if (this->meshes.empty())
		{
			cout << "  AVISO: '" << path << "' no contiene mallas de triangulos" << endl;
			this->boundsMin = this->boundsMax = glm::vec3(0.0f);
		}
	}

	static glm::mat4 toGlm(const aiMatrix4x4& m)
	{
		// Assimp guarda las matrices por filas; GLM por columnas
		return glm::transpose(glm::make_mat4(&m.a1));
	}

	bool isExcluded(const string& name) const
	{
		for (const string& prefix : this->options.excludeNodes)
		{
			if (!prefix.empty() && name.rfind(prefix, 0) == 0)
				return true;
		}
		return false;
	}

	// Processes a node in a recursive fashion. Processes each individual mesh located at the node and repeats this process on its children nodes (if any).
	// 'parentTransform' es la transformación acumulada de los nodos padre: las mallas de FBX/glTF
	// suelen venir en coordenadas locales de su nodo (rotaciones, escalas de unidades, pivotes).
	void processNode(aiNode* node, const aiScene* scene, const glm::mat4& parentTransform)
	{
		string nodeName = node->mName.C_Str();
		if (this->isExcluded(nodeName))
		{
			this->excludedNodes++;
			return;
		}
		glm::mat4 transform = parentTransform * toGlm(node->mTransformation);

		int nodeIndex = -1;
		if (this->animated)
		{
			auto it = this->nodeIndexOf.find(node);
			if (it != this->nodeIndexOf.end()) nodeIndex = it->second;
		}

		// Process each mesh located at the current node
		for (GLuint i = 0; i < node->mNumMeshes; i++)
		{
			// The node object only contains indices to index the actual objects in the scene.
			// The scene contains all the data, node is just to keep stuff organized (like relations between nodes).
			aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];
			if (!(mesh->mPrimitiveTypes & aiPrimitiveType_TRIANGLE) || this->isExcluded(mesh->mName.C_Str()))
				continue;
			this->pending.push_back(this->processMesh(mesh, scene, transform, nodeName, nodeIndex));
		}

		// After we've processed all of the meshes (if any) we then recursively process each of the children nodes
		for (GLuint i = 0; i < node->mNumChildren; i++)
		{
			this->processNode(node->mChildren[i], scene, transform);
		}
	}

	MeshData processMesh(aiMesh *mesh, const aiScene *scene, const glm::mat4& nodeTransform, const string& nodeName, int nodeIndex)
	{
		// Data to fill
		vector<Vertex> vertices;
		vector<GLuint> indices;
		vertices.reserve(mesh->mNumVertices);

		// Modelos estáticos: la transformación del nodo se aplica una sola vez aquí (se "hornea").
		// Modelos animados:
		//  - Malla con huesos: la posición final sale de  global(hueso) * offsetMatrix * vértice.
		//    · FBX: la offsetMatrix de Assimp lleva del espacio del MODELO en la pose de enlace al espacio
		//      del hueso, así que el vértice debe llevar ya la transformación global de su nodo de malla.
		//      En FBX de Blender ese nodo trae la escala del objeto y de unidades (x2000 en Penta); en FBX
		//      de Mixamo es la identidad, por eso no importaba.
		//    · glTF (y los demás formatos, sin verificar): los vértices se quedan en el espacio de la malla;
		//      glTF ignora por especificación la transformación del nodo de una malla con piel.
		//  - Malla sin huesos: sus vértices quedan en el espacio local de su nodo y un hueso sintético
		//    (offset identidad) le aplica la transformación global del nodo, animada o no.
		bool skinned = false;
		int rigid = -1;
		glm::mat4 transform = nodeTransform;
		if (this->animated && nodeIndex >= 0)
		{
			if (mesh->HasBones())
			{
				skinned = true;
				if (!this->bakeSkinnedNodes)
					transform = glm::mat4(1.0f);
			}
			else
			{
				rigid = this->rigidSlot(nodeIndex);
				skinned = rigid >= 0;
				if (skinned)
					transform = glm::mat4(1.0f);
			}
		}

		// Las normales usan la inversa transpuesta para soportar escalas no uniformes o negativas.
		glm::mat3 normalMatrix = glm::transpose(glm::inverse(glm::mat3(transform)));

		// Walk through each of the mesh's vertices
		for (GLuint i = 0; i < mesh->mNumVertices; i++)
		{
			Vertex vertex;
			const aiVector3D& p = mesh->mVertices[i];
			vertex.Position = glm::vec3(transform * glm::vec4(p.x, p.y, p.z, 1.0f));

			if (mesh->mNormals)
			{
				const aiVector3D& n = mesh->mNormals[i];
				glm::vec3 normal = normalMatrix * glm::vec3(n.x, n.y, n.z);
				float len = glm::length(normal);
				vertex.Normal = len > 1e-8f ? normal / len : glm::vec3(0.0f, 1.0f, 0.0f);
			}
			else
			{
				vertex.Normal = glm::vec3(0.0f, 1.0f, 0.0f);
			}

			// Texture Coordinates
			if (mesh->mTextureCoords[0]) // Does the mesh contain texture coordinates?
			{
				// A vertex can contain up to 8 different texture coordinates. We thus make the assumption that we won't
				// use models where a vertex can have multiple texture coordinates so we always take the first set (0).
				vertex.TexCoords = glm::vec2(mesh->mTextureCoords[0][i].x, mesh->mTextureCoords[0][i].y);
			}
			else
			{
				vertex.TexCoords = glm::vec2(0.0f, 0.0f);
			}

			vertices.push_back(vertex);
		}

		// Pesos de huesos (máx. 4 por vértice, renormalizados)
		if (skinned)
		{
			if (mesh->HasBones())
			{
				this->fillBoneWeights(mesh, vertices, nodeIndex, transform);
			}
			else
			{
				for (Vertex& v : vertices)
				{
					v.BoneIDs = glm::ivec4(rigid, 0, 0, 0);
					v.Weights = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
				}
			}
		}

		// Shape keys: aiAnimMesh guarda posiciones ABSOLUTAS de cada objetivo; se convierten a desplazamientos
		// (objetivo - base), llevados al mismo espacio que los vértices (transform es identidad en mallas animadas)
		vector<MorphTarget> morph;
		for (unsigned int t = 0; t < mesh->mNumAnimMeshes; t++)
		{
			const aiAnimMesh* am = mesh->mAnimMeshes[t];
			MorphTarget mt;   // Se agrega siempre (aunque sea inválido) para no desalinear los índices de objetivo
#if ANIM_USE_ASSIMP_MORPH
			mt.name = am->mName.C_Str();
			mt.defaultWeight = am->mWeight;
#endif
			mt.dPos.assign(mesh->mNumVertices, glm::vec3(0.0f));
			if (am->mVertices && am->mNumVertices == mesh->mNumVertices)
			{
				glm::mat3 linear = glm::mat3(transform);
				bool withNormals = am->mNormals && mesh->mNormals;
				if (withNormals) mt.dNormal.assign(mesh->mNumVertices, glm::vec3(0.0f));
				for (unsigned int i = 0; i < mesh->mNumVertices; i++)
				{
					const aiVector3D& a = am->mVertices[i];
					const aiVector3D& b = mesh->mVertices[i];
					mt.dPos[i] = linear * glm::vec3(a.x - b.x, a.y - b.y, a.z - b.z);
					if (withNormals)
					{
						const aiVector3D& na = am->mNormals[i];
						const aiVector3D& nb = mesh->mNormals[i];
						mt.dNormal[i] = normalMatrix * glm::vec3(na.x - nb.x, na.y - nb.y, na.z - nb.z);
					}
				}
			}
			else
				cout << "  [Anim] AVISO: el objetivo de morph " << t << " de '" << mesh->mName.C_Str() << "' no coincide con la malla" << endl;
			morph.push_back(std::move(mt));
		}

		// Now wak through each of the mesh's faces (a face is a mesh its triangle) and retrieve the corresponding vertex indices.
		indices.reserve(mesh->mNumFaces * 3);
		for (GLuint i = 0; i < mesh->mNumFaces; i++)
		{
			const aiFace& face = mesh->mFaces[i];
			if (face.mNumIndices != 3) continue;
			indices.push_back(face.mIndices[0]);
			indices.push_back(face.mIndices[1]);
			indices.push_back(face.mIndices[2]);
		}
		this->triangleCount += indices.size() / 3;

		// Process materials
		auto cached = this->materialCache.find(mesh->mMaterialIndex);
		if (cached == this->materialCache.end())
			cached = this->materialCache.emplace(mesh->mMaterialIndex, this->processMaterial(scene->mMaterials[mesh->mMaterialIndex], scene)).first;

		// Return a mesh object created from the extracted mesh data
		MeshData data;
		data.name = nodeName.empty() ? string(mesh->mName.C_Str()) : nodeName;
		data.vertices = std::move(vertices);
		data.indices = std::move(indices);
		data.material = cached->second;
		data.morph = std::move(morph);
		data.meshName = mesh->mName.C_Str();
		data.skinned = skinned;
		data.material.skinned = skinned;   // El material de la caché es compartido: el flag se fija en la copia
		return data;
	}

	static float toDisplay(float linear)
	{
		return powf(glm::clamp(linear, 0.0f, 1.0f), 1.0f / 2.2f);
	}

	Material processMaterial(aiMaterial* aimat, const aiScene* scene)
	{
		Material mat;
		aiString name;
		if (aimat->Get(AI_MATKEY_NAME, name) == AI_SUCCESS)
			mat.name = name.C_Str();

		aiColor3D color;
		if (aimat->Get(AI_MATKEY_COLOR_DIFFUSE, color) == AI_SUCCESS)
			mat.diffuseColor = glm::vec3(color.r, color.g, color.b);
		if (aimat->Get(AI_MATKEY_COLOR_SPECULAR, color) == AI_SUCCESS)
			mat.specularColor = glm::vec3(color.r, color.g, color.b);
		if (aimat->Get(AI_MATKEY_COLOR_EMISSIVE, color) == AI_SUCCESS)
			mat.emissiveColor = glm::vec3(color.r, color.g, color.b);

		float value;
		if (aimat->Get(AI_MATKEY_SHININESS, value) == AI_SUCCESS)
			mat.shininess = value;
		if (aimat->Get(AI_MATKEY_OPACITY, value) == AI_SUCCESS)
			mat.opacity = value;

		// Opacidad 0 casi siempre es un error de exportación (el modelo desaparecería por completo)
		if (mat.opacity <= 0.001f)
			mat.opacity = 1.0f;
		mat.opacity = glm::clamp(mat.opacity, 0.0f, 1.0f);

		// Un brillo nulo significa superficie totalmente rugosa: sin reflejo especular
		// (pow(x, 0) = 1 produciría un brillo blanco en toda la superficie)
		if (mat.shininess < 1.0f)
			mat.specularColor = glm::vec3(0.0f);
		mat.shininess = glm::clamp(mat.shininess, 1.0f, 256.0f);

		if (this->options.linearColors)
		{
			mat.diffuseColor = glm::vec3(toDisplay(mat.diffuseColor.r), toDisplay(mat.diffuseColor.g), toDisplay(mat.diffuseColor.b));
			mat.emissiveColor = glm::clamp(mat.emissiveColor, 0.0f, 1.0f);
			mat.emissiveColor = glm::vec3(toDisplay(mat.emissiveColor.r), toDisplay(mat.emissiveColor.g), toDisplay(mat.emissiveColor.b));
		}

		// Mapas de textura (difuso y especular). Los demás mapas (normales, rugosidad, metálico)
		// se ignoran en esta etapa: el shader solo implementa Phong.
		// Una textura asignada manualmente al material (por nombre) tiene prioridad sobre la del archivo;
		// la clave "*" solo cubre materiales que no traen mapa difuso.
		auto ov = this->options.diffuseOverrides.find(mat.name);
		if (ov == this->options.diffuseOverrides.end())
		{
			mat.diffuseMap = this->loadMaterialTexture(aimat, aiTextureType_DIFFUSE, scene, mat.name);
			if (mat.diffuseMap == 0)
				ov = this->options.diffuseOverrides.find("*");
		}
		if (ov != this->options.diffuseOverrides.end())
		{
			string texPath = this->directory + "/" + ov->second;
			GLuint id = TextureLoader::FileExists(texPath) ? TextureLoader::LoadFromFile(texPath) : 0;
			if (id != 0)
				mat.diffuseMap = id;
			else
				cout << "  [Textura] ERROR: la textura asignada '" << texPath << "' para el material '" << mat.name << "' no existe" << endl;
		}
		mat.specularMap = this->loadMaterialTexture(aimat, aiTextureType_SPECULAR, scene, mat.name);

		auto op = this->options.opacityOverrides.find(mat.name);
		if (op != this->options.opacityOverrides.end())
			mat.opacity = glm::clamp(op->second, 0.0f, 1.0f);
		auto em = this->options.emissiveOverrides.find(mat.name);
		if (em != this->options.emissiveOverrides.end())
			mat.emissiveColor = em->second;

		auto rf = this->options.reflectivityOverrides.find(mat.name);
		if (rf == this->options.reflectivityOverrides.end())
			rf = this->options.reflectivityOverrides.find("*");
		if (rf != this->options.reflectivityOverrides.end())
			mat.reflectivity = glm::clamp(rf->second, 0.0f, 1.0f);

		mat.alphaTest = this->options.alphaTest;
		for (const string& glowName : this->options.glowMaterials)
			if (glowName == mat.name) mat.glow = true;
		return mat;
	}

	// Carga el primer mapa del tipo pedido. Devuelve 0 si el material no tiene mapa o si falla.
	GLuint loadMaterialTexture(aiMaterial* mat, aiTextureType type, const aiScene* scene, const string& materialName)
	{
		if (mat->GetTextureCount(type) == 0)
			return 0;

		aiString str;
		if (mat->GetTexture(type, 0, &str) != AI_SUCCESS || str.length == 0)
			return 0;
		string reference = str.C_Str();

		// Texturas incrustadas en el archivo (GLB/FBX): la ruta tiene la forma "*N"
		if (reference[0] == '*')
		{
			unsigned int index = static_cast<unsigned int>(atoi(reference.c_str() + 1));
			if (scene->mTextures && index < scene->mNumTextures)
			{
				const aiTexture* tex = scene->mTextures[index];
				string key = this->path + reference;
				if (tex->mHeight == 0)
					return TextureLoader::LoadFromMemory(key, reinterpret_cast<const unsigned char*>(tex->pcData), static_cast<int>(tex->mWidth));
				return TextureLoader::LoadFromTexels(key, reinterpret_cast<const unsigned char*>(tex->pcData), tex->mWidth, tex->mHeight);
			}
			cout << "  [Textura] ERROR: textura incrustada " << reference << " inexistente en '" << this->path << "'" << endl;
			return 0;
		}

		// 1) El archivo exacto que indica el modelo
		string ref = TextureLoader::NormalizePath(reference);
		string direct = std::filesystem::path(ref).is_absolute() ? ref : this->directory + "/" + ref;
		if (TextureLoader::FileExists(direct))
			return TextureLoader::LoadFromFile(direct);

		// 2) FBX con texturas incrustadas referenciadas por nombre ("Archivo.fbm\0.jpg") en lugar de "*N":
		//    la copia que viene dentro del modelo es la original, antes que un archivo de nombre parecido
		if (const aiTexture* tex = scene->GetEmbeddedTexture(reference.c_str()))
		{
			string key = this->path + "|" + reference;
			if (tex->mHeight == 0)
				return TextureLoader::LoadFromMemory(key, reinterpret_cast<const unsigned char*>(tex->pcData), static_cast<int>(tex->mWidth));
			return TextureLoader::LoadFromTexels(key, reinterpret_cast<const unsigned char*>(tex->pcData), tex->mWidth, tex->mHeight);
		}

		// 3) Búsqueda aproximada (mismo nombre en las carpetas habituales u otra extensión)
		string file = this->findTextureFile(reference);
		if (!file.empty())
			return TextureLoader::LoadFromFile(file);

		cout << "  [Textura] ERROR: el material '" << materialName << "' usa '" << reference
			<< "', que no existe junto al modelo. Se usara el color del material." << endl;
		return 0;
	}

	// Busca el archivo de una textura. Primero la ruta indicada por el modelo; si no existe
	// (rutas absolutas de otra computadora, carpetas renombradas, extensión distinta) se busca
	// el mismo nombre en las carpetas habituales y se informa la sustitución.
	string findTextureFile(const string& reference) const
	{
		namespace fs = std::filesystem;
		string ref = TextureLoader::NormalizePath(reference);
		fs::path refPath = fs::path(ref);

		string direct = refPath.is_absolute() ? ref : this->directory + "/" + ref;
		if (TextureLoader::FileExists(direct))
			return direct;

		vector<string> dirs = { this->directory, this->directory + "/textures", this->directory + "/../textures",
			this->directory + "/../../textures" };
		std::error_code ec;
		for (const auto& entry : fs::directory_iterator(fs::path(this->directory), ec))
		{
			if (entry.is_directory(ec))
				dirs.push_back(TextureLoader::NormalizePath(entry.path().string()));
		}

		string stem = refPath.stem().string();
		vector<string> names = { refPath.filename().string() };
		for (const char* ext : { ".png", ".jpg", ".jpeg", ".tga", ".bmp" })
			names.push_back(stem + ext);

		for (const string& dir : dirs)
		{
			for (const string& name : names)
			{
				string candidate = dir + "/" + name;
				if (TextureLoader::FileExists(candidate))
				{
					cout << "  [Textura] AVISO: '" << reference << "' no existe; se usa '" << candidate << "'" << endl;
					return candidate;
				}
			}
		}
		return "";
	}
};
