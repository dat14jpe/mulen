#pragma once
#include "GLObject.hpp"

namespace Util {
    class Buffer : public GLObject {
    protected:
        void GLDestroy() { glDeleteBuffers(1, &id); }

    public:
        void Create(GLsizeiptr size, GLbitfield flags)
        {
            Destroy();
            glCreateBuffers(1, &id);
            glNamedBufferStorage(id, size, nullptr, flags);
        }

        void Upload(GLintptr offset, GLsizeiptr size, const void* data)
        {
            glNamedBufferSubData(id, offset, size, data);
        }

        void Bind(GLenum target)
        {
            glBindBuffer(target, id);
        }

        void BindBase(GLenum target, GLuint index)
        {
            glBindBufferBase(target, index, id);
        }

        void BindRange(GLenum target, GLuint index, GLintptr offset, GLsizeiptr size)
        {
            glBindBufferRange(target, index, id, offset, size);
        }
    };
}
