// Frontier Engine - GLSL program wrapper with cached uniform locations.
#pragma once

#include "math/Math.h"
#include "render/GL.h"

#include <string>
#include <unordered_map>

namespace frontier::render {

class Shader {
public:
    Shader() = default;
    ~Shader();

    Shader(const Shader&) = delete;
    Shader& operator=(const Shader&) = delete;
    Shader(Shader&& other) noexcept;
    Shader& operator=(Shader&& other) noexcept;

    bool LoadFromFiles(const std::string& vertexPath, const std::string& fragmentPath,
                       const std::string& debugName);
    bool LoadFromSource(const std::string& vertexSource, const std::string& fragmentSource,
                        const std::string& debugName);

    void Destroy();
    void Bind() const;
    void Unbind() const;
    bool Valid() const { return m_program != 0; }
    GLuint Id() const { return m_program; }
    const std::string& Name() const { return m_name; }

    // Uniform setters silently ignore missing uniforms (they may be optimised out).
    void SetInt(const char* name, int value);
    void SetFloat(const char* name, float value);
    void SetVec2(const char* name, const vec2& value);
    void SetVec3(const char* name, const vec3& value);
    void SetVec4(const char* name, const vec4& value);
    void SetMat3(const char* name, const mat3& value);
    void SetMat4(const char* name, const mat4& value);
    void SetFloatArray(const char* name, const float* values, int count);

    int UniformLocation(const char* name);

private:
    GLuint CompileStage(GLenum type, const std::string& source, const std::string& stageName);
    void DestroyProgram();

    GLuint m_program = 0;
    std::string m_name;
    std::unordered_map<std::string, int> m_uniformCache;
};

// Reads a text file; returns false on failure.
bool ReadTextFile(const std::string& path, std::string& outContents);

} // namespace frontier::render
