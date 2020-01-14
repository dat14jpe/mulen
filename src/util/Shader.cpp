#include "Shader.hpp"
#include <functional>
#include <sstream>

namespace Util {
    bool Shader::Create(const FileNames& files)
    {
        Destroy();
        id = glCreateProgram();
        GLint status, logLength;
        std::string info;

        std::function<void(const std::string&, std::string&, unsigned)> appendSource
            = [&](const std::string& name, std::string& src, unsigned level)
        {
            std::string line;
            std::ifstream file(name);
            if (!file.is_open())
            {
                std::cerr << "Could not open include file " << name << "\n";
                return;
            }
            auto nextLineNumber = 1u;
            while (std::getline(file, line))
            {
                ++nextLineNumber;
                static const std::string includeStart("#include");
                if (line.size() > includeStart.size() && line.find(includeStart) == 0)
                {
                    std::istringstream iss(line);
                    std::string preproc, include;
                    iss >> preproc >> include;
                    if (include.size() > 2 && include[0] == include.back() && include[0] == '"')
                    {
                        include = include.substr(1, include.size() - 2);
                        const auto stop = name.find_last_of('/');
                        const auto dir = name.substr(0, stop == std::string::npos ? 0 : stop + 1);
                        include = dir + include;

                        src += "#line 1\n";
                        appendSource(include, src, level + 1u);
                        src += "#line " + std::to_string(nextLineNumber) + "\n";
                    }
                    else std::cerr << "Invalid include \"" << include << "\" in " << name << "\n";
                    continue;
                }
                src += line + '\n';
            }
        };

        auto createShader = [&](GLenum type, const std::string& name) -> GLuint
        {
            //const auto src = GetFileContents(name);
            std::string src;
            appendSource(name, src, 0);
            if (!src.size())
            {
                std::cerr << "Error: empty shader source file " << name << "\n";
            }
            const GLint srcSize = (GLint)src.size();
            const auto srcPointer = src.c_str();
            auto shader = glCreateShader(type);
            glShaderSource(shader, 1, (const GLchar**)&srcPointer, &srcSize);
            glCompileShader(shader);
            glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
            if (GL_FALSE == status)
            {
                glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLength);
                info.resize(logLength);
                glGetShaderInfoLog(shader, logLength, &logLength, info.data());
                std::cerr << "Error: unable to compile shader " << name << "\n" << info << "\n";
                return 0;
            }
            glAttachShader(id, shader);
            return shader;
        };
        GLuint vert = 0u, frag = 0u, compute = 0u;
        const bool hasFrag = files.frag.size(), hasCompute = files.compute.size();
        if (hasCompute) // only compute
        {
            compute = createShader(GL_COMPUTE_SHADER, files.compute);
        }
        else // vertex and fragment
        {
            vert = createShader(GL_VERTEX_SHADER, files.vert);
            if (hasFrag) frag = createShader(GL_FRAGMENT_SHADER, files.frag);
            //if (hasGeom) geom = createShader(GL_GEOMETRY_SHADER, files.geom);
            //if (!vert || (hasFrag && !frag) || (hasGeom && !geom)) return false;
        }

        glLinkProgram(id);
        glGetProgramiv(id, GL_LINK_STATUS, &status);
        if (GL_FALSE == status)
        {
            glGetProgramiv(id, GL_INFO_LOG_LENGTH, &logLength);
            info.resize(logLength);
            glGetProgramInfoLog(id, (GLsizei)info.size(), &logLength, info.data());
            std::cerr << "Error: unable to link shader program (" << files.vert << ")\n" << info << "\n";
            return false;
        }

        // - to do: destroy shader objects

        return true;
    }
}
