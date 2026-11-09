#pragma once

#include <string>
#include <iostream>
#include <map>
#include <vector>
#include <cmath>
#include <cfloat>
#include <filesystem>
#include <sstream>

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

	// Draws the model, and thus all its meshes. El shader ya debe tener la matriz "model".
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

	// Caja envolvente del modelo completo en sus unidades originales
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

	// Geometría ya transformada, antes de subirla a la GPU
	struct MeshData
	{
		string name;
		vector<Vertex> vertices;
		vector<GLuint> indices;
		Material material;
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
		const aiScene *scene = importer.ReadFile(path,
			aiProcess_Triangulate | aiProcess_FlipUVs | aiProcess_GenSmoothNormals |
			aiProcess_JoinIdenticalVertices | aiProcess_SortByPType | aiProcess_ImproveCacheLocality);

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

		// Process ASSIMP's root node recursively, acumulando las transformaciones de cada nodo
		this->processNode(scene->mRootNode, scene, glm::mat4(1.0f));
		this->buildMeshes();
		this->computeBounds();
	}

	// Registra las partes originales y crea las mallas de dibujo (unidas por material si se pidió)
	void buildMeshes()
	{
		for (MeshData& data : this->pending)
		{
			MeshPart part{ data.name, glm::vec3(FLT_MAX), glm::vec3(-FLT_MAX) };
			for (const Vertex& v : data.vertices)
			{
				part.boundsMin = glm::min(part.boundsMin, v.Position);
				part.boundsMax = glm::max(part.boundsMax, v.Position);
			}
			this->parts.push_back(part);
		}

		if (!this->options.mergeMeshes)
		{
			for (MeshData& data : this->pending)
				this->meshes.emplace_back(data.name, data.vertices, data.indices, data.material);
			this->pending.clear();
			return;
		}

		// Clave de agrupación: todo lo que el shader usa del material, salvo el color difuso de los
		// materiales sin textura, que se guarda en cada vértice
		auto key = [](const Material& m) {
			ostringstream k;
			k << m.diffuseMap << '|' << m.specularMap << '|' << m.specularColor.r << ',' << m.specularColor.g << ',' << m.specularColor.b
				<< '|' << m.emissiveColor.r << ',' << m.emissiveColor.g << ',' << m.emissiveColor.b << '|' << m.shininess
				<< '|' << m.opacity << '|' << m.alphaTest << '|' << m.glow;
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

			string k = key(data.material);
			auto it = groupIndex.find(k);
			if (it == groupIndex.end())
			{
				groupIndex[k] = groups.size();
				MeshData g;
				g.name = data.name;
				g.material = data.material;
				if (useVertexColor)
					g.material.diffuseColor = glm::vec3(1.0f);
				groups.push_back(std::move(g));
				it = groupIndex.find(k);
			}
			MeshData& g = groups[it->second];
			GLuint offset = static_cast<GLuint>(g.vertices.size());
			g.vertices.insert(g.vertices.end(), data.vertices.begin(), data.vertices.end());
			for (GLuint idx : data.indices)
				g.indices.push_back(idx + offset);
		}
		for (MeshData& g : groups)
			this->meshes.emplace_back(g.name, g.vertices, g.indices, g.material);
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

		// Process each mesh located at the current node
		for (GLuint i = 0; i < node->mNumMeshes; i++)
		{
			// The node object only contains indices to index the actual objects in the scene.
			// The scene contains all the data, node is just to keep stuff organized (like relations between nodes).
			aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];
			if (!(mesh->mPrimitiveTypes & aiPrimitiveType_TRIANGLE) || this->isExcluded(mesh->mName.C_Str()))
				continue;
			this->pending.push_back(this->processMesh(mesh, scene, transform, nodeName));
		}

		// After we've processed all of the meshes (if any) we then recursively process each of the children nodes
		for (GLuint i = 0; i < node->mNumChildren; i++)
		{
			this->processNode(node->mChildren[i], scene, transform);
		}
	}

	MeshData processMesh(aiMesh *mesh, const aiScene *scene, const glm::mat4& transform, const string& nodeName)
	{
		// Data to fill
		vector<Vertex> vertices;
		vector<GLuint> indices;
		vertices.reserve(mesh->mNumVertices);

		// La transformación del nodo se aplica una sola vez aquí (los modelos del escenario son estáticos);
		// las normales usan la inversa transpuesta para soportar escalas no uniformes o negativas.
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

		string file = this->findTextureFile(reference);
		if (file.empty())
		{
			cout << "  [Textura] ERROR: el material '" << materialName << "' usa '" << reference
				<< "', que no existe junto al modelo. Se usara el color del material." << endl;
			return 0;
		}
		return TextureLoader::LoadFromFile(file);
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
