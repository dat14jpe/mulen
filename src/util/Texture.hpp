#pragma once
#include "GLObject.hpp"

namespace Util {
	class Texture : public GLObject {
	public:
		typedef uint32_t Dim;

	protected:
		void GLDestroy() override {	glDeleteTextures(1, &id); }
		Dim width, height, depth;

	public:
		Dim GetWidth () const { return width; }
		Dim GetHeight() const { return height; }
		Dim GetDepth () const { return depth; }

		void Create(GLenum target, Dim levels, GLenum internalformat, Dim width, Dim height = 1u, Dim depth = 1u)
		{
			Destroy();
			this->width = width;
			this->height = height;
			this->depth = depth;
			glCreateTextures(target, 1, &id);
			switch (target) {
			case GL_TEXTURE_1D: glTextureStorage1D(id, levels, internalformat, width); break;
			case GL_TEXTURE_2D: glTextureStorage2D(id, levels, internalformat, width, height); break;
			case GL_TEXTURE_3D: glTextureStorage3D(id, levels, internalformat, width, height, depth); break;
			}
		}

		void Bind(GLuint unit)
		{
			glBindTextureUnit(unit, id);
		}
	};
}
