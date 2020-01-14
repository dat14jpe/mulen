#pragma once
#include "GLObject.hpp"
#include "Files.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <string>
#include <iostream>

namespace Util {
    class Shader : public GLObject {
    protected:
        void GLDestroy() override {	glDeleteProgram(id); }
        typedef const GLchar* Name;

    public:
        Shader() {}
        Shader(Shader&& o) noexcept : GLObject(std::move(o)) {}

        struct FileNames
        {
            std::string vert, frag, compute;
        };
        bool Create(const FileNames&);

        void Bind()
        {
            glUseProgram(id);
        }

        GLuint GetUniformLocation(Name name) 
        {
            return glGetUniformLocation(id, name); 
        }
        void Uniform1f(Name name, const glm::vec1& v) { glProgramUniform1f(id, GetUniformLocation(name), v.x); }
        void Uniform2f(Name name, const glm::vec2& v) { glProgramUniform2f(id, GetUniformLocation(name), v.x, v.y); }
        void Uniform3f(Name name, const glm::vec3& v) { glProgramUniform3f(id, GetUniformLocation(name), v.x, v.y, v.z); }
        void Uniform4f(Name name, const glm::vec4& v) { glProgramUniform4f(id, GetUniformLocation(name), v.x, v.y, v.z, v.w); }
        void Uniform1i(Name name, const glm::ivec1& v) { glProgramUniform1i(id, GetUniformLocation(name), v.x); }
        void Uniform2i(Name name, const glm::ivec2& v) { glProgramUniform2i(id, GetUniformLocation(name), v.x, v.y); }
        void Uniform3i(Name name, const glm::ivec3& v) { glProgramUniform3i(id, GetUniformLocation(name), v.x, v.y, v.z); }
        void Uniform4i(Name name, const glm::ivec4& v) { glProgramUniform4i(id, GetUniformLocation(name), v.x, v.y, v.z, v.w); }
        void Uniform1u(Name name, const glm::uvec1& v) { glProgramUniform1ui(id, GetUniformLocation(name), v.x); }
        void Uniform2u(Name name, const glm::uvec2& v) { glProgramUniform2ui(id, GetUniformLocation(name), v.x, v.y); }
        void Uniform3u(Name name, const glm::uvec3& v) { glProgramUniform3ui(id, GetUniformLocation(name), v.x, v.y, v.z); }
        void Uniform4u(Name name, const glm::uvec4& v) { glProgramUniform4ui(id, GetUniformLocation(name), v.x, v.y, v.z, v.w); }
        void UniformMat4(Name name, const glm::mat4& v) {
            glProgramUniformMatrix4fv(id, GetUniformLocation(name), 1u, false, glm::value_ptr(v));
        }
    };
}
