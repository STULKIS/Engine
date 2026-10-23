#include "render/Shader.h"

#include "core/Log.h"

#include <fstream>
#include <set>
#include <sstream>
#include <utility>
#include <vector>

namespace derecho::render {
namespace {

// Resolves a shader include relative to the including file. Supports nesting
// with a depth guard and prevents duplicate inclusion of the same path.
bool ResolveIncludes(const std::string& path, std::set<std::string>& included, std::string& output,
                     int depth, std::string& error) {
    if (depth > 16) {
        error = "shader include depth exceeded";
        return false;
    }
    if (included.count(path)) {
        return true;
    }
    included.insert(path);

    std::string source;
    if (!ReadTextFile(path, source)) {
        error = "could not read '" + path + "'";
        return false;
    }

    std::string directory;
    if (const auto slash = path.find_last_of('/'); slash != std::string::npos) {
        directory = path.substr(0, slash + 1);
    }

    std::istringstream stream(source);
    std::string line;
    std::string result;
    while (std::getline(stream, line)) {
        const auto includePos = line.find("#include");
        if (includePos != std::string::npos) {
            const auto quoteStart = line.find('"', includePos);
            const auto quoteEnd = quoteStart == std::string::npos ? std::string::npos
                                                                 : line.find('"', quoteStart + 1);
            if (quoteStart != std::string::npos && quoteEnd != std::string::npos) {
                const std::string child = line.substr(quoteStart + 1, quoteEnd - quoteStart - 1);
                const std::string childPath = directory + child;
                std::string childSource;
                if (!ResolveIncludes(childPath, included, childSource, depth + 1, error)) {
                    return false;
                }
                result += "// ---- " + child + " ----\n";
                result += childSource;
                result += "\n";
                continue;
            }
        }
        result += line;
        result += '\n';
    }
    output += result;
    return true;
}

} // namespace

bool ExpandShaderIncludes(const std::string& path, std::string& outSource, std::string& error) {
    std::set<std::string> included;
    outSource.clear();
    return ResolveIncludes(path, included, outSource, 0, error);
}

bool ReadTextFile(const std::string& path, std::string& outContents) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return false;
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    outContents = buffer.str();
    return !outContents.empty();
}

Shader::~Shader() {
    DestroyProgram();
}

Shader::Shader(Shader&& other) noexcept
    : m_program(other.m_program), m_name(std::move(other.m_name)), m_uniformCache(std::move(other.m_uniformCache)) {
    other.m_program = 0;
}

Shader& Shader::operator=(Shader&& other) noexcept {
    if (this != &other) {
        DestroyProgram();
        m_program = other.m_program;
        m_name = std::move(other.m_name);
        m_uniformCache = std::move(other.m_uniformCache);
        other.m_program = 0;
    }
    return *this;
}

void Shader::DestroyProgram() {
    if (m_program) {
        glDeleteProgram(m_program);
        m_program = 0;
    }
    m_uniformCache.clear();
}

void Shader::Destroy() {
    DestroyProgram();
}

GLuint Shader::CompileStage(GLenum type, const std::string& source, const std::string& stageName) {
    const GLuint shader = glCreateShader(type);
    const char* src = source.c_str();
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);

    GLint success = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        GLint length = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
        std::vector<char> log(static_cast<size_t>(length > 1 ? length : 1));
        glGetShaderInfoLog(shader, length, nullptr, log.data());
        DE_ERROR("[%s] %s shader compilation failed:\n%s", m_name.c_str(), stageName.c_str(), log.data());
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

bool Shader::LoadFromFiles(const std::string& vertexPath, const std::string& fragmentPath,
                           const std::string& debugName) {
    m_name = debugName;
    std::string error;
    std::string vertexSource;
    std::string fragmentSource;
    if (!ExpandShaderIncludes(vertexPath, vertexSource, error) || vertexSource.empty()) {
        DE_ERROR("[%s] Could not load vertex shader '%s': %s", debugName.c_str(), vertexPath.c_str(), error.c_str());
        return false;
    }
    if (!ExpandShaderIncludes(fragmentPath, fragmentSource, error) || fragmentSource.empty()) {
        DE_ERROR("[%s] Could not load fragment shader '%s': %s", debugName.c_str(), fragmentPath.c_str(), error.c_str());
        return false;
    }
    return LoadFromSource(vertexSource, fragmentSource, debugName);
}

bool Shader::LoadFromSource(const std::string& vertexSource, const std::string& fragmentSource,
                            const std::string& debugName) {
    m_name = debugName;
    DestroyProgram();

    const GLuint vs = CompileStage(GL_VERTEX_SHADER, vertexSource, "vertex");
    if (!vs) {
        return false;
    }
    const GLuint fs = CompileStage(GL_FRAGMENT_SHADER, fragmentSource, "fragment");
    if (!fs) {
        glDeleteShader(vs);
        return false;
    }

    const GLuint program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);
    glDeleteShader(vs);
    glDeleteShader(fs);

    GLint success = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success) {
        GLint length = 0;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &length);
        std::vector<char> log(static_cast<size_t>(length > 1 ? length : 1));
        glGetProgramInfoLog(program, length, nullptr, log.data());
        DE_ERROR("[%s] program link failed:\n%s", debugName.c_str(), log.data());
        glDeleteProgram(program);
        return false;
    }

    m_program = program;
    DE_INFO("Shader program '%s' linked (id=%u).", debugName.c_str(), m_program);
    return true;
}

void Shader::Bind() const {
    glUseProgram(m_program);
}

void Shader::Unbind() const {
    glUseProgram(0);
}

int Shader::UniformLocation(const char* name) {
    if (!m_program) {
        return -1;
    }
    const auto it = m_uniformCache.find(name);
    if (it != m_uniformCache.end()) {
        return it->second;
    }
    const int location = glGetUniformLocation(m_program, name);
    m_uniformCache.emplace(name, location);
    return location;
}

void Shader::SetInt(const char* name, int value) {
    const int loc = UniformLocation(name);
    if (loc >= 0) glUniform1i(loc, value);
}

void Shader::SetFloat(const char* name, float value) {
    const int loc = UniformLocation(name);
    if (loc >= 0) glUniform1f(loc, value);
}

void Shader::SetVec2(const char* name, const vec2& value) {
    const int loc = UniformLocation(name);
    if (loc >= 0) glUniform2f(loc, value.x, value.y);
}

void Shader::SetVec3(const char* name, const vec3& value) {
    const int loc = UniformLocation(name);
    if (loc >= 0) glUniform3f(loc, value.x, value.y, value.z);
}

void Shader::SetVec4(const char* name, const vec4& value) {
    const int loc = UniformLocation(name);
    if (loc >= 0) glUniform4f(loc, value.x, value.y, value.z, value.w);
}

void Shader::SetMat3(const char* name, const mat3& value) {
    const int loc = UniformLocation(name);
    if (loc >= 0) glUniformMatrix3fv(loc, 1, GL_FALSE, glm::value_ptr(value));
}

void Shader::SetMat4(const char* name, const mat4& value) {
    const int loc = UniformLocation(name);
    if (loc >= 0) glUniformMatrix4fv(loc, 1, GL_FALSE, glm::value_ptr(value));
}

void Shader::SetFloatArray(const char* name, const float* values, int count) {
    const int loc = UniformLocation(name);
    if (loc >= 0) glUniform1fv(loc, count, values);
}

} // namespace derecho::render
