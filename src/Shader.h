#pragma once
#include <string>
#include <glm/glm.hpp>

class Shader {
public:
    unsigned int id = 0;

    Shader() = default;
    Shader(const char* vertPath, const char* fragPath);
    ~Shader();

    // Non-copyable, movable
    Shader(const Shader&) = delete;
    Shader& operator=(const Shader&) = delete;
    Shader(Shader&& o) noexcept : id(o.id) { o.id = 0; }
    Shader& operator=(Shader&& o) noexcept { id = o.id; o.id = 0; return *this; }

    void use() const;

    void setInt  (const char* name, int v)               const;
    void setFloat(const char* name, float v)             const;
    void setVec3 (const char* name, const glm::vec3& v)  const;
    void setMat4 (const char* name, const glm::mat4& m)  const;

private:
    static unsigned int compile(unsigned int type, const std::string& src);
    static std::string  readFile(const char* path);
};
