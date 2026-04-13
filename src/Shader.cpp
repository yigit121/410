#include "Shader.h"
#include <glad/gl.h>
#include <glm/gtc/type_ptr.hpp>
#include <fstream>
#include <sstream>
#include <iostream>
#include <stdexcept>

std::string Shader::readFile(const char* path) {
    std::ifstream f(path);
    if (!f) throw std::runtime_error(std::string("Shader file not found: ") + path);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

unsigned int Shader::compile(unsigned int type, const std::string& src) {
    unsigned int s = glCreateShader(type);
    const char* c = src.c_str();
    glShaderSource(s, 1, &c, nullptr);
    glCompileShader(s);

    int ok;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetShaderInfoLog(s, 1024, nullptr, log);
        glDeleteShader(s);
        throw std::runtime_error(std::string("Shader compile error:\n") + log);
    }
    return s;
}

Shader::Shader(const char* vertPath, const char* fragPath) {
    unsigned int vert = compile(GL_VERTEX_SHADER,   readFile(vertPath));
    unsigned int frag = compile(GL_FRAGMENT_SHADER, readFile(fragPath));

    id = glCreateProgram();
    glAttachShader(id, vert);
    glAttachShader(id, frag);
    glLinkProgram(id);
    glDeleteShader(vert);
    glDeleteShader(frag);

    int ok;
    glGetProgramiv(id, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetProgramInfoLog(id, 1024, nullptr, log);
        glDeleteProgram(id);
        id = 0;
        throw std::runtime_error(std::string("Shader link error:\n") + log);
    }
}

Shader::~Shader() {
    if (id) glDeleteProgram(id);
}

void Shader::use() const { glUseProgram(id); }

void Shader::setInt  (const char* n, int v)              const { glUniform1i (glGetUniformLocation(id, n), v); }
void Shader::setFloat(const char* n, float v)            const { glUniform1f (glGetUniformLocation(id, n), v); }
void Shader::setVec3 (const char* n, const glm::vec3& v) const { glUniform3fv(glGetUniformLocation(id, n), 1, glm::value_ptr(v)); }
void Shader::setMat4 (const char* n, const glm::mat4& m) const { glUniformMatrix4fv(glGetUniformLocation(id, n), 1, GL_FALSE, glm::value_ptr(m)); }
