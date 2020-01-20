#pragma once
#include "GLObject.hpp"
#include "Texture.hpp"

namespace Util {
	class Framebuffer : public GLObject {
	protected:
		void GLDestroy() override { glDeleteFramebuffers(1, &id); }

	public:
		void Create()
		{
			Destroy();
			glCreateFramebuffers(1, &id);
		}

		void Bind()
		{
			glBindFramebuffer(GL_FRAMEBUFFER, id);
		}
		static void BindBackbuffer()
		{
			glBindFramebuffer(GL_FRAMEBUFFER, 0u);
		}

		void SetColorBuffer(unsigned index, Texture& tex, unsigned level)
		{
			glNamedFramebufferTexture(id, GL_COLOR_ATTACHMENT0 + index, tex.GetId(), level);
		}

		void SetDepthBuffer(Texture& tex, unsigned level)
		{
			glNamedFramebufferTexture(id, GL_DEPTH_ATTACHMENT, tex.GetId(), level);
		}
	};
}
