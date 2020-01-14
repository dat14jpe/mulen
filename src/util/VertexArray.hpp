#pragma once
#include "GLObject.hpp"

namespace Util {
	class VertexArray : public GLObject {
	protected:
		void GLDestroy() { glDeleteVertexArrays(1, &id); }

	public:
		void Create()
		{
			Destroy();
			glCreateVertexArrays(1, &id);
		}

		void Bind()
		{
			glBindVertexArray(id);
		}
	};
}
