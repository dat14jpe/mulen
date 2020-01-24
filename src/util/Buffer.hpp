#pragma once
#include "GLObject.hpp"

namespace Util {
    class Buffer : public GLObject {
    protected:
        void GLDestroy() 
        { 
            size = 0u;
            glDeleteBuffers(1, &id); 
        }

        typedef GLsizeiptr Size;
        Size size = 0u;

    public:
        Size GetSize() const { return size; }

        void Create(Size size, GLbitfield flags)
        {
            Destroy();
            glCreateBuffers(1, &id);
            glNamedBufferStorage(id, size, nullptr, flags);
            this->size = size;
        }

        void Upload(GLintptr offset, Size size, const void* data)
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

        void BindRange(GLenum target, GLuint index, GLintptr offset, Size size)
        {
            glBindBufferRange(target, index, id, offset, size);
        }
    };
}
