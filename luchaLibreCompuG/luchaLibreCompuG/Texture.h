#pragma once

// Carga de texturas 2D y cubemaps con SOIL2.
// - Caché global: una misma imagen solo se lee del disco y se sube a la GPU una vez,
//   aunque la usen varios modelos o varias instancias.
// - Formatos: 1, 2, 3 o 4 canales (GL_RED, GL_RG, GL_RGB, GL_RGBA) con alineación de 1 byte,
//   de modo que las imágenes con anchos no múltiplos de 4 no se corrompen.
// - Errores: si una imagen no existe o no se puede decodificar se imprime un diagnóstico y se
//   devuelve 0; quien la usa debe recurrir al color del material en lugar de muestrear basura.

#include <string>
#include <vector>
#include <map>
#include <iostream>
#include <algorithm>
#include <filesystem>

#include <GL/glew.h>
#include "SOIL2/SOIL2.h"

namespace TextureLoader
{
	inline std::map<std::string, GLuint>& Cache()
	{
		static std::map<std::string, GLuint> cache;
		return cache;
	}

	inline std::string NormalizePath(std::string path)
	{
		std::replace(path.begin(), path.end(), '\\', '/');
		return path;
	}

	inline bool FileExists(const std::string& path)
	{
		std::error_code ec;
		return std::filesystem::is_regular_file(std::filesystem::path(path), ec);
	}

	// Sube a la GPU una imagen ya decodificada y configura filtrado con mipmaps
	inline GLuint UploadTexture(const unsigned char* data, int width, int height, int channels)
	{
		GLenum format = GL_RGB;
		if (channels == 1) format = GL_RED;
		else if (channels == 2) format = GL_RG;
		else if (channels == 4) format = GL_RGBA;

		GLuint textureID;
		glGenTextures(1, &textureID);
		glBindTexture(GL_TEXTURE_2D, textureID);
		glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
		glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0, format, GL_UNSIGNED_BYTE, data);
		glPixelStorei(GL_UNPACK_ALIGNMENT, 4);

		// Escala de grises: se replica el canal para que el shader lea (L, L, L, A)
		if (channels == 1)
		{
			GLint swizzle[] = { GL_RED, GL_RED, GL_RED, GL_ONE };
			glTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_RGBA, swizzle);
		}
		else if (channels == 2)
		{
			GLint swizzle[] = { GL_RED, GL_RED, GL_RED, GL_GREEN };
			glTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_RGBA, swizzle);
		}

		glGenerateMipmap(GL_TEXTURE_2D);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		if (GLEW_EXT_texture_filter_anisotropic)
		{
			GLfloat maxAniso = 1.0f;
			glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &maxAniso);
			glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, std::min(8.0f, maxAniso));
		}
		glBindTexture(GL_TEXTURE_2D, 0);
		return textureID;
	}

	// Carga una textura desde archivo. Devuelve 0 (y lo informa) si falla.
	inline GLuint LoadFromFile(const std::string& rawPath)
	{
		std::string path = NormalizePath(rawPath);
		auto it = Cache().find(path);
		if (it != Cache().end())
			return it->second;

		GLuint id = 0;
		int width = 0, height = 0, channels = 0;
		unsigned char* image = SOIL_load_image(path.c_str(), &width, &height, &channels, SOIL_LOAD_AUTO);
		if (image == nullptr || width <= 0 || height <= 0)
		{
			std::cout << "  [Textura] ERROR: no se pudo cargar '" << path << "': " << SOIL_last_result() << std::endl;
		}
		else
		{
			id = UploadTexture(image, width, height, channels);
		}
		if (image)
			SOIL_free_image_data(image);

		Cache()[path] = id; // También se guardan los fallos para no reintentar ni repetir el aviso
		return id;
	}

	// Carga una textura comprimida (PNG/JPG...) incrustada en memoria, p.ej. dentro de un GLB o FBX.
	inline GLuint LoadFromMemory(const std::string& cacheKey, const unsigned char* data, int size)
	{
		auto it = Cache().find(cacheKey);
		if (it != Cache().end())
			return it->second;

		GLuint id = 0;
		int width = 0, height = 0, channels = 0;
		unsigned char* image = SOIL_load_image_from_memory(data, size, &width, &height, &channels, SOIL_LOAD_AUTO);
		if (image == nullptr)
			std::cout << "  [Textura] ERROR: no se pudo decodificar la textura incrustada '" << cacheKey << "': " << SOIL_last_result() << std::endl;
		else
			id = UploadTexture(image, width, height, channels);
		if (image)
			SOIL_free_image_data(image);

		Cache()[cacheKey] = id;
		return id;
	}

	// Textura sin comprimir (texels BGRA de Assimp)
	inline GLuint LoadFromTexels(const std::string& cacheKey, const unsigned char* bgra, int width, int height)
	{
		auto it = Cache().find(cacheKey);
		if (it != Cache().end())
			return it->second;

		std::vector<unsigned char> rgba(static_cast<size_t>(width) * height * 4);
		for (size_t i = 0; i < rgba.size(); i += 4)
		{
			rgba[i + 0] = bgra[i + 2];
			rgba[i + 1] = bgra[i + 1];
			rgba[i + 2] = bgra[i + 0];
			rgba[i + 3] = bgra[i + 3];
		}
		GLuint id = UploadTexture(rgba.data(), width, height, 4);
		Cache()[cacheKey] = id;
		return id;
	}

	// Gira una imagen cuadrada en múltiplos de 90° (sentido antihorario), en la CPU
	inline void RotateSquareImage(unsigned char* data, int size, int channels, int quarterTurns)
	{
		quarterTurns = ((quarterTurns % 4) + 4) % 4;
		if (quarterTurns == 0) return;
		std::vector<unsigned char> src(data, data + static_cast<size_t>(size) * size * channels);
		for (int i = 0; i < size; i++)
		{
			for (int j = 0; j < size; j++)
			{
				int si = i, sj = j;
				if (quarterTurns == 1) { si = j; sj = size - 1 - i; }
				else if (quarterTurns == 2) { si = size - 1 - i; sj = size - 1 - j; }
				else { si = size - 1 - j; sj = i; }
				for (int c = 0; c < channels; c++)
					data[(static_cast<size_t>(i) * size + j) * channels + c] = src[(static_cast<size_t>(si) * size + sj) * channels + c];
			}
		}
	}

	// Cubemap: caras en el orden +X, -X, +Y, -Y, +Z, -Z.
	// 'quarterTurns' permite corregir caras exportadas con otra orientación sin editar las imágenes.
	inline GLuint LoadCubemap(const std::vector<std::string>& faces, const std::vector<int>& quarterTurns = {})
	{
		GLuint textureID;
		glGenTextures(1, &textureID);
		glBindTexture(GL_TEXTURE_CUBE_MAP, textureID);
		glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

		for (unsigned int i = 0; i < faces.size(); i++)
		{
			int width = 0, height = 0, channels = 0;
			// Se fuerza RGB: el cubemap se sube como GL_RGB aunque el PNG tenga canal alfa
			unsigned char* data = SOIL_load_image(faces[i].c_str(), &width, &height, &channels, SOIL_LOAD_RGB);
			if (data)
			{
				if (i < quarterTurns.size() && width == height)
					RotateSquareImage(data, width, 3, quarterTurns[i]);
				glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, data);
				SOIL_free_image_data(data);
			}
			else
			{
				std::cout << "[Skybox] ERROR: no se pudo cargar la cara '" << faces[i] << "': " << SOIL_last_result() << std::endl;
				const unsigned char black[3] = { 0, 0, 0 };
				glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGB, 1, 1, 0, GL_RGB, GL_UNSIGNED_BYTE, black);
			}
		}
		glPixelStorei(GL_UNPACK_ALIGNMENT, 4);

		glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
		glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
		return textureID;
	}

	// Textura blanca de 1x1: se enlaza cuando un material no tiene mapa, para que el sampler
	// nunca apunte a una unidad vacía (comportamiento indefinido en algunos drivers).
	inline GLuint White()
	{
		static GLuint white = 0;
		if (white == 0)
		{
			const unsigned char pixel[4] = { 255, 255, 255, 255 };
			glGenTextures(1, &white);
			glBindTexture(GL_TEXTURE_2D, white);
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
			glBindTexture(GL_TEXTURE_2D, 0);
		}
		return white;
	}
}
