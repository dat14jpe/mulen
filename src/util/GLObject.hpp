#pragma once
#include <glad/glad.h>
#include <cstdint>
#include <utility>

namespace Util {
	class GLObject {
	protected:
		typedef GLuint Id;
		static constexpr Id Invalid = 0u;
		Id id = Invalid;
		virtual void GLDestroy() = 0;

	public:
		GLObject() {}
		GLObject(const GLObject&) = delete;
		GLObject& operator=(const GLObject&) = delete;
		GLObject(GLObject&& o) noexcept : id(std::exchange(o.id, Invalid)) {}
		GLObject& operator=(GLObject&& o) noexcept
		{
			if (this == &o) return *this;
			Destroy();
			id = std::exchange(o.id, Invalid);
		}

		void Destroy()
		{
			if (!id) return;
			//GLDestroy(); // - crashing on destructor. What did we do wrong? Might be that it's too late and the GLFW window is gone already.
			id = Invalid;
		}
		~GLObject() { Destroy(); }
		Id GetId() const { return id; }
	};
}
