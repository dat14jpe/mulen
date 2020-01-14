#pragma once
#include "GLObject.hpp"

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

		void Bind(GLenum target)
		{
			glBindFramebuffer(target, id);
		}
	};
}
