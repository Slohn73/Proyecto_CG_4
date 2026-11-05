#pragma once

// Estructura ligera de la escena:
//  - ModelAsset: un archivo 3D cargado UNA vez, con su normalización (unidades -> metros,
//    corrección de orientación y pivote calculado a partir de su caja envolvente).
//  - SceneObject: una instancia colocada en el mundo (posición, rotación, escala, visibilidad).
//    Varias instancias pueden compartir el mismo ModelAsset.
//
// Matriz final de un objeto:
//   T(position) * R(rotation) * S(scale) * T(-pivotOffset) * normalization
// Para animar un objeto basta con modificar position/rotation/scale cada cuadro;
// la forma de dibujarlo no cambia.

#include <string>
#include <vector>
#include <deque>
#include <map>
#include <memory>
#include <iostream>
#include <iomanip>
#include <chrono>
#include <cfloat>

#include <GL/glew.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "Model.h"
#include "Shader.h"

// Punto del modelo que se coloca en SceneObject::position
enum class Pivot
{
	Original,    // Se respeta el origen del archivo (cuando ya es el adecuado)
	BaseCenter,  // Centro de la caja en X/Z y su base en Y: el objeto se apoya en position.y
	Center       // Centro de la caja envolvente (objetos colgantes o flotantes)
};

// Descripción de un recurso: archivo + cómo normalizarlo
struct AssetDesc
{
	std::string id;
	std::string path;
	float unitScale = 1.0f;                     // Unidades del archivo -> metros (no cambia proporciones)
	float targetHeight = 0.0f;                  // Si es > 0, la escala uniforme se calcula para esta altura (m);
	                                            // para modelos cuyas unidades de origen no son confiables
	glm::vec3 baseRotation = glm::vec3(0.0f);   // Grados (X, Y, Z) para dejarlo con Y arriba y el frente hacia +Z
	Pivot pivot = Pivot::BaseCenter;
	std::string pivotPart;                      // Si no está vacío, el pivote se calcula con la caja de esa parte
	ModelOptions options;
	bool load = true;                           // false = no se carga (modelos pesados o pendientes)
};

struct ModelAsset
{
	AssetDesc desc;
	std::unique_ptr<Model> model;
	glm::mat4 normalization = glm::mat4(1.0f);  // Archivo -> espacio normalizado (metros, Y arriba, pivote en el origen)
	glm::vec3 localMin = glm::vec3(0.0f);       // Caja envolvente en espacio normalizado
	glm::vec3 localMax = glm::vec3(0.0f);
	double loadSeconds = 0.0;

	bool IsReady() const { return this->model && this->model->IsLoaded(); }
	glm::vec3 GetSize() const { return this->localMax - this->localMin; }
};

inline glm::mat4 EulerRotation(const glm::vec3& degrees)
{
	// Orden: giro (Y), cabeceo (X), alabeo (Z)
	glm::mat4 r(1.0f);
	r = glm::rotate(r, glm::radians(degrees.y), glm::vec3(0.0f, 1.0f, 0.0f));
	r = glm::rotate(r, glm::radians(degrees.x), glm::vec3(1.0f, 0.0f, 0.0f));
	r = glm::rotate(r, glm::radians(degrees.z), glm::vec3(0.0f, 0.0f, 1.0f));
	return r;
}

// Caja alineada a los ejes que contiene la caja (mn, mx) transformada por m
inline void TransformBounds(const glm::mat4& m, const glm::vec3& mn, const glm::vec3& mx, glm::vec3& outMin, glm::vec3& outMax)
{
	outMin = glm::vec3(FLT_MAX);
	outMax = glm::vec3(-FLT_MAX);
	for (int i = 0; i < 8; i++)
	{
		glm::vec3 corner((i & 1) ? mx.x : mn.x, (i & 2) ? mx.y : mn.y, (i & 4) ? mx.z : mn.z);
		glm::vec3 p = glm::vec3(m * glm::vec4(corner, 1.0f));
		outMin = glm::min(outMin, p);
		outMax = glm::max(outMax, p);
	}
}

struct SceneObject
{
	std::string id;
	std::string assetId;
	std::string group;                           // Zona o categoría (para mostrar/ocultar en bloque)
	glm::vec3 position = glm::vec3(0.0f);        // Metros, espacio del mundo
	glm::vec3 rotation = glm::vec3(0.0f);        // Grados (X, Y, Z)
	glm::vec3 scale = glm::vec3(1.0f);           // Escala adicional a la normalización del recurso
	glm::vec3 pivotOffset = glm::vec3(0.0f);     // Punto local (m) que se coloca en position
	bool visible = true;
	ModelAsset* asset = nullptr;

	glm::mat4 GetMatrix() const
	{
		glm::mat4 m = glm::translate(glm::mat4(1.0f), this->position);
		m = m * EulerRotation(this->rotation);
		m = glm::scale(m, this->scale);
		m = glm::translate(m, -this->pivotOffset);
		if (this->asset)
			m = m * this->asset->normalization;
		return m;
	}

	// Métodos encadenables para describir la escena de forma compacta
	SceneObject& At(float x, float y, float z) { this->position = glm::vec3(x, y, z); return *this; }
	SceneObject& At(const glm::vec3& p) { this->position = p; return *this; }
	SceneObject& RotY(float degrees) { this->rotation.y = degrees; return *this; }
	SceneObject& Rot(float x, float y, float z) { this->rotation = glm::vec3(x, y, z); return *this; }
	SceneObject& Scale(float s) { this->scale = glm::vec3(s); return *this; }
	SceneObject& Hidden() { this->visible = false; return *this; }
};

class Scene
{
public:
	void RegisterAsset(const AssetDesc& desc)
	{
		ModelAsset& asset = this->assets[desc.id];
		asset.desc = desc;
	}

	// Recurso cuyo modelo se generó por código (no se lee de disco)
	void RegisterAsset(const AssetDesc& desc, std::unique_ptr<Model> model)
	{
		ModelAsset& asset = this->assets[desc.id];
		asset.desc = desc;
		asset.model = std::move(model);
		if (asset.IsReady())
			this->computeNormalization(asset);
	}

	// Carga (una sola vez) todos los recursos registrados con load = true
	void LoadAssets()
	{
		for (auto& kv : this->assets)
		{
			ModelAsset& asset = kv.second;
			if (!asset.desc.load || asset.model)
				continue;
			std::cout << "[Modelo] " << asset.desc.id << " <- " << asset.desc.path << std::endl;
			auto t0 = std::chrono::steady_clock::now();
			asset.model = std::make_unique<Model>(asset.desc.path, asset.desc.options);
			asset.loadSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
			if (!asset.IsReady())
			{
				std::cout << "  ERROR: el modelo no se pudo cargar; sus instancias no se dibujaran" << std::endl;
				continue;
			}
			this->computeNormalization(asset);
			asset.model->PrintSummary();
			glm::vec3 s = asset.GetSize();
			std::cout << "  normalizado: " << s.x << " x " << s.y << " x " << s.z << " m (escala " << asset.desc.unitScale
				<< ", " << asset.loadSeconds << " s)" << std::endl;
		}
	}

	ModelAsset* GetAsset(const std::string& id)
	{
		auto it = this->assets.find(id);
		return it == this->assets.end() ? nullptr : &it->second;
	}

	// Tamaño normalizado (metros) de un recurso; (0,0,0) si no está cargado
	glm::vec3 GetAssetSize(const std::string& id)
	{
		ModelAsset* a = this->GetAsset(id);
		return (a && a->IsReady()) ? a->GetSize() : glm::vec3(0.0f);
	}

	SceneObject& Add(const std::string& id, const std::string& assetId, const std::string& group)
	{
		SceneObject obj;
		obj.id = id;
		obj.assetId = assetId;
		obj.group = group;
		obj.asset = this->GetAsset(assetId);
		if (!obj.asset)
			std::cout << "[Escena] AVISO: el objeto '" << id << "' usa el recurso no registrado '" << assetId << "'" << std::endl;
		this->objects.push_back(obj);
		return this->objects.back();
	}

	SceneObject* Find(const std::string& id)
	{
		for (SceneObject& obj : this->objects)
			if (obj.id == id) return &obj;
		return nullptr;
	}

	void SetGroupVisible(const std::string& group, bool visible)
	{
		for (SceneObject& obj : this->objects)
			if (obj.group == group) obj.visible = visible;
	}

	std::deque<SceneObject>& Objects() { return this->objects; }

	// Caja envolvente en el mundo (alineada a los ejes) de un objeto
	bool GetWorldBounds(const SceneObject& obj, glm::vec3& outMin, glm::vec3& outMax) const
	{
		if (!obj.asset || !obj.asset->IsReady()) return false;
		TransformBounds(obj.GetMatrix(), obj.asset->model->GetBoundsMin(), obj.asset->model->GetBoundsMax(), outMin, outMax);
		return true;
	}

	// Orden de dibujo en cada cuadro: DrawOpaque -> skybox -> DrawTransparent.
	// Las mallas translúcidas no escriben profundidad, por eso van después del skybox
	// (si fueran antes, el skybox las cubriría al dibujarse donde la profundidad sigue en 1.0).
	void DrawOpaque(Shader& shader)
	{
		this->drawPass(shader, RenderPass::Opaque);
	}

	void DrawTransparent(Shader& shader)
	{
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		glDepthMask(GL_FALSE);
		this->drawPass(shader, RenderPass::Transparent);
		glDepthMask(GL_TRUE);
		glDisable(GL_BLEND);
	}

	// Reporte de recursos; con includeObjects también lista la posición y caja de cada objeto
	void PrintReport(bool includeObjects = true)
	{
		std::cout << "\n=========== RECURSOS ===========" << std::endl;
		for (auto& kv : this->assets)
		{
			ModelAsset& a = kv.second;
			std::cout << std::left << std::setw(22) << a.desc.id;
			if (!a.desc.load) { std::cout << "DESACTIVADO" << std::endl; continue; }
			if (!a.IsReady()) { std::cout << "ERROR DE CARGA (" << a.desc.path << ")" << std::endl; continue; }
			glm::vec3 s = a.GetSize();
			std::cout << a.model->GetTriangleCount() << " tris, " << std::fixed << std::setprecision(2)
				<< s.x << " x " << s.y << " x " << s.z << " m" << std::defaultfloat << std::endl;
			a.model->PrintMaterials();
		}
		int visibles = 0;
		for (SceneObject& obj : this->objects)
			visibles += obj.visible ? 1 : 0;
		std::cout << "\n=========== OBJETOS: " << this->objects.size() << " (" << visibles << " visibles) ===========" << std::endl;
		if (!includeObjects)
		{
			std::cout << "(F3 imprime la posicion y caja de cada objeto)" << std::endl;
			return;
		}
		for (SceneObject& obj : this->objects)
			this->PrintObject(obj);
		std::cout << std::endl;
	}

	void PrintObject(const SceneObject& obj) const
	{
		std::cout << std::left << std::setw(24) << obj.id << std::setw(14) << obj.group << std::fixed << std::setprecision(2)
			<< " pos (" << obj.position.x << ", " << obj.position.y << ", " << obj.position.z << ")"
			<< " rotY " << obj.rotation.y;
		glm::vec3 mn, mx;
		if (this->GetWorldBounds(obj, mn, mx))
			std::cout << "  caja [" << mn.x << ", " << mn.y << ", " << mn.z << "] - [" << mx.x << ", " << mx.y << ", " << mx.z << "]";
		else
			std::cout << "  (sin modelo)";
		if (!obj.visible) std::cout << " OCULTO";
		std::cout << std::defaultfloat << std::endl;
	}

private:
	std::map<std::string, ModelAsset> assets;
	std::deque<SceneObject> objects;  // deque: las referencias a objetos no se invalidan al agregar más

	void computeNormalization(ModelAsset& asset)
	{
		AssetDesc& d = asset.desc;
		glm::vec3 mn, mx;
		if (d.targetHeight > 0.0f)
		{
			TransformBounds(EulerRotation(d.baseRotation), asset.model->GetBoundsMin(), asset.model->GetBoundsMax(), mn, mx);
			if (mx.y - mn.y > 1e-6f)
				d.unitScale = d.targetHeight / (mx.y - mn.y);
		}
		glm::mat4 base = glm::scale(glm::mat4(1.0f), glm::vec3(d.unitScale)) * EulerRotation(d.baseRotation);

		glm::vec3 partMin = asset.model->GetBoundsMin(), partMax = asset.model->GetBoundsMax();
		if (!d.pivotPart.empty() && !asset.model->GetPartBounds(d.pivotPart, partMin, partMax))
		{
			std::cout << "  AVISO: la parte de pivote '" << d.pivotPart << "' no existe; se usa el modelo completo" << std::endl;
			partMin = asset.model->GetBoundsMin();
			partMax = asset.model->GetBoundsMax();
		}

		TransformBounds(base, partMin, partMax, mn, mx);
		glm::vec3 pivot(0.0f);
		if (d.pivot == Pivot::BaseCenter) pivot = glm::vec3((mn.x + mx.x) * 0.5f, mn.y, (mn.z + mx.z) * 0.5f);
		else if (d.pivot == Pivot::Center) pivot = (mn + mx) * 0.5f;

		asset.normalization = glm::translate(glm::mat4(1.0f), -pivot) * base;
		TransformBounds(asset.normalization, asset.model->GetBoundsMin(), asset.model->GetBoundsMax(), asset.localMin, asset.localMax);
	}

	void drawPass(Shader& shader, RenderPass pass)
	{
		GLint modelLoc = glGetUniformLocation(shader.Program, "model");
		GLint normalLoc = glGetUniformLocation(shader.Program, "normalMatrix");
		for (const SceneObject& obj : this->objects)
		{
			if (!obj.visible || !obj.asset || !obj.asset->IsReady()) continue;
			if (pass == RenderPass::Transparent && !obj.asset->model->HasTransparency()) continue;
			glm::mat4 model = obj.GetMatrix();
			glm::mat3 normalMatrix = glm::transpose(glm::inverse(glm::mat3(model)));
			glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(model));
			glUniformMatrix3fv(normalLoc, 1, GL_FALSE, glm::value_ptr(normalMatrix));
			obj.asset->model->Draw(shader, pass);
		}
	}
};
