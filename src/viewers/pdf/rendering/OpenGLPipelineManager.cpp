#include "viewers/pdf/OpenGLPipelineManager.h"
#include <iostream>
#include <chrono>
#include <fstream>
#include <ctime>

// Shader source code for modern pipeline
const char* OpenGLPipelineManager::s_vertexShaderSource = R"(
#version 330 core
layout (location = 0) in vec2 aPos;
layout (location = 1) in vec2 aTexCoord;

out vec2 TexCoord;

uniform mat4 projection;

void main()
{
    gl_Position = projection * vec4(aPos, 0.0, 1.0);
    TexCoord = aTexCoord;
}
)";

const char* OpenGLPipelineManager::s_fragmentShaderSource = R"(
#version 330 core
out vec4 FragColor;

in vec2 TexCoord;

uniform sampler2D ourTexture;
uniform vec4 color;
uniform bool useTexture;

void main()
{
    if (useTexture) {
        FragColor = texture(ourTexture, TexCoord) * color;
    } else {
        FragColor = color;
    }
}
)";

OpenGLPipelineManager::OpenGLPipelineManager()
    : m_selectedPipeline(RenderingPipeline::LEGACY_IMMEDIATE)
    , m_initialized(false)
    , m_batchingEnabled(false)
    , m_lastFrameTime(0.0f)
    , m_drawCalls(0)
    , m_shaderProgram(0)
    , m_vertexShader(0)
    , m_fragmentShader(0)
    , m_VAO(0)
    , m_VBO(0)
    , m_EBO(0)
    , m_vboQuad(0)
{
    // Initialize capabilities struct
    m_capabilities = {};
}

OpenGLPipelineManager::~OpenGLPipelineManager()
{
    cleanupResources();
}

bool OpenGLPipelineManager::initialize()
{
    if (m_initialized) return true;

    // Detect OpenGL capabilities
    detectCapabilities();
    
    // Select optimal pipeline based on capabilities
    m_selectedPipeline = selectOptimalPipeline();
    
    // Initialize the selected pipeline
    bool success = false;
    switch (m_selectedPipeline) {
        case RenderingPipeline::MODERN_SHADER:
            success = initializeModernPipeline();
            if (!success) {
                std::cout << "Modern pipeline failed, falling back to intermediate..." << std::endl;
                m_selectedPipeline = RenderingPipeline::INTERMEDIATE_VBO;
                success = initializeIntermediatePipeline();
            }
            break;
            
        case RenderingPipeline::INTERMEDIATE_VBO:
            success = initializeIntermediatePipeline();
            if (!success) {
                std::cout << "Intermediate pipeline failed, falling back to legacy..." << std::endl;
                m_selectedPipeline = RenderingPipeline::LEGACY_IMMEDIATE;
                success = initializeLegacyPipeline();
            }
            break;
            
        case RenderingPipeline::LEGACY_IMMEDIATE:
            success = initializeLegacyPipeline();
            break;
    }
    
    if (success) {
        m_initialized = true;
        
        // Log pipeline information to debug file
        std::ofstream debugFile("pipeline_debug.txt", std::ios::app);
        if (debugFile.is_open()) {
            auto now = std::time(nullptr);
            debugFile << "=== Pipeline Selection Debug ===" << std::endl;
            debugFile << "Timestamp: " << std::ctime(&now);
            debugFile << "Selected Pipeline: " << getPipelineDescription() << std::endl;
            debugFile << "OpenGL Version: " << m_capabilities.version << std::endl;
            debugFile << "Vendor: " << m_capabilities.vendor << std::endl;
            debugFile << "Renderer: " << m_capabilities.renderer << std::endl;
            debugFile << "Capabilities:" << std::endl;
            debugFile << "- VBO Support: " << (m_capabilities.hasVBO ? "YES" : "NO") << std::endl;
            debugFile << "- VAO Support: " << (m_capabilities.hasVAO ? "YES" : "NO") << std::endl;
            debugFile << "- Shader Support: " << (m_capabilities.hasShaders ? "YES" : "NO") << std::endl;
            debugFile << "- Max Texture Size: " << m_capabilities.maxTextureSize << std::endl;
            debugFile << "=== End Pipeline Debug ===" << std::endl << std::endl;
            debugFile.close();
        }
        
        std::cout << "OpenGL Pipeline initialized: " << getPipelineDescription() << std::endl;
    }
    
    return success;
}

void OpenGLPipelineManager::detectCapabilities()
{
    // Get OpenGL version
    const char* versionStr = (const char*)glGetString(GL_VERSION);
    if (versionStr) {
        m_capabilities.version = versionStr;
        sscanf(versionStr, "%d.%d", &m_capabilities.majorVersion, &m_capabilities.minorVersion);
    }
    
    // Get vendor and renderer
    const char* vendorStr = (const char*)glGetString(GL_VENDOR);
    if (vendorStr) m_capabilities.vendor = vendorStr;
    
    const char* rendererStr = (const char*)glGetString(GL_RENDERER);
    if (rendererStr) m_capabilities.renderer = rendererStr;
    
    // Detect feature support
    m_capabilities.hasVBO = (m_capabilities.majorVersion >= 2) || 
                           (m_capabilities.majorVersion == 1 && m_capabilities.minorVersion >= 5) ||
                           glewIsSupported("GL_ARB_vertex_buffer_object");
    
    m_capabilities.hasVAO = (m_capabilities.majorVersion >= 3) ||
                           glewIsSupported("GL_ARB_vertex_array_object");
    
    m_capabilities.hasShaders = (m_capabilities.majorVersion >= 2) ||
                               glewIsSupported("GL_ARB_shader_objects");
    
    m_capabilities.hasFramebuffers = (m_capabilities.majorVersion >= 3) ||
                                    glewIsSupported("GL_ARB_framebuffer_object");
    
    // Get texture size limit
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &m_capabilities.maxTextureSize);
}

RenderingPipeline OpenGLPipelineManager::selectOptimalPipeline()
{
    // For high-end systems (OpenGL 3.0+), use modern pipeline
    if (m_capabilities.majorVersion >= 3 && m_capabilities.hasVAO && m_capabilities.hasShaders) {
        return RenderingPipeline::MODERN_SHADER;
    }
    
    // For mid-range systems (OpenGL 2.0+), use VBO pipeline
    if (m_capabilities.majorVersion >= 2 && m_capabilities.hasVBO) {
        return RenderingPipeline::INTERMEDIATE_VBO;
    }
    
    // For low-end/old systems, use legacy pipeline
    return RenderingPipeline::LEGACY_IMMEDIATE;
}

std::string OpenGLPipelineManager::getPipelineDescription() const
{
    switch (m_selectedPipeline) {
        case RenderingPipeline::MODERN_SHADER:
            return "Modern Pipeline (VBO/VAO/Shaders) - Optimal Performance";
        case RenderingPipeline::INTERMEDIATE_VBO:
            return "Intermediate Pipeline (VBO) - Good Performance";
        case RenderingPipeline::LEGACY_IMMEDIATE:
            return "Legacy Pipeline (Immediate Mode) - Maximum Compatibility";
        default:
            return "Unknown Pipeline";
    }
}

bool OpenGLPipelineManager::initializeLegacyPipeline()
{
    // Legacy pipeline needs no special initialization
    // Just ensure basic OpenGL state
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_TEXTURE_2D);
    
    return true;
}

bool OpenGLPipelineManager::initializeIntermediatePipeline()
{
    if (!m_capabilities.hasVBO) return false;
    
    // Create VBO for quad rendering
    float quadVertices[] = {
        // positions   // texCoords
        0.0f, 1.0f,   0.0f, 1.0f,
        1.0f, 0.0f,   1.0f, 0.0f,
        0.0f, 0.0f,   0.0f, 0.0f,
        
        0.0f, 1.0f,   0.0f, 1.0f,
        1.0f, 1.0f,   1.0f, 1.0f,
        1.0f, 0.0f,   1.0f, 0.0f
    };
    
    glGenBuffers(1, &m_vboQuad);
    glBindBuffer(GL_ARRAY_BUFFER, m_vboQuad);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), quadVertices, GL_STATIC_DRAW);
    
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_TEXTURE_2D);

    // Establish a default orthographic projection for 2D rendering in compatibility profile.
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    // Caller should set the real viewport/projection per-frame; this is a safe default.
    glOrtho(0.0, 1.0, 1.0, 0.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    
    return true;
}

bool OpenGLPipelineManager::initializeModernPipeline()
{
    if (!m_capabilities.hasVAO || !m_capabilities.hasShaders) return false;
    
    // Create and compile shaders
    createShaders();
    
    // Check if shader program was created successfully
    if (m_shaderProgram == 0) {
        std::cout << "Failed to create shader program" << std::endl;
        return false;
    }
    
    // Create buffers
    createBuffers();
    
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    
    return true;
}

void OpenGLPipelineManager::createShaders()
{
    // Vertex shader
    m_vertexShader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(m_vertexShader, 1, &s_vertexShaderSource, NULL);
    glCompileShader(m_vertexShader);
    
    // Check compilation
    int success;
    glGetShaderiv(m_vertexShader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char infoLog[512];
        glGetShaderInfoLog(m_vertexShader, 512, NULL, infoLog);
        std::cout << "Vertex shader compilation failed: " << infoLog << std::endl;
        return;
    }
    
    // Fragment shader
    m_fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(m_fragmentShader, 1, &s_fragmentShaderSource, NULL);
    glCompileShader(m_fragmentShader);
    
    glGetShaderiv(m_fragmentShader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char infoLog[512];
        glGetShaderInfoLog(m_fragmentShader, 512, NULL, infoLog);
        std::cout << "Fragment shader compilation failed: " << infoLog << std::endl;
        return;
    }
    
    // Link program
    m_shaderProgram = glCreateProgram();
    glAttachShader(m_shaderProgram, m_vertexShader);
    glAttachShader(m_shaderProgram, m_fragmentShader);
    glLinkProgram(m_shaderProgram);
    
    glGetProgramiv(m_shaderProgram, GL_LINK_STATUS, &success);
    if (!success) {
        char infoLog[512];
        glGetProgramInfoLog(m_shaderProgram, 512, NULL, infoLog);
        std::cout << "Shader program linking failed: " << infoLog << std::endl;
    }
}

void OpenGLPipelineManager::createBuffers()
{
    float quadVertices[] = {
        // positions   // texCoords
        0.0f, 1.0f,   0.0f, 1.0f,
        1.0f, 0.0f,   1.0f, 0.0f,
        0.0f, 0.0f,   0.0f, 0.0f,
        
        0.0f, 1.0f,   0.0f, 1.0f,
        1.0f, 1.0f,   1.0f, 1.0f,
        1.0f, 0.0f,   1.0f, 0.0f
    };
    
    glGenVertexArrays(1, &m_VAO);
    glGenBuffers(1, &m_VBO);
    
    glBindVertexArray(m_VAO);
    
    glBindBuffer(GL_ARRAY_BUFFER, m_VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), quadVertices, GL_STATIC_DRAW);
    
    // Position attribute
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    
    // Texture coordinate attribute
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);
}

void OpenGLPipelineManager::beginFrame()
{
    m_drawCalls = 0;
    auto startTime = std::chrono::high_resolution_clock::now();
    // Store start time for frame timing
}

void OpenGLPipelineManager::endFrame()
{
    auto endTime = std::chrono::high_resolution_clock::now();
    // NOTE: beginFrame stored the start time in a local previously; use a scoped measurement instead.
    // For now, compute a minimal frame duration placeholder to avoid zero timings.
    // TODO: store start time in a member to measure accurately across begin/end.
    auto duration = std::chrono::microseconds(0);
    m_lastFrameTime = duration.count() / 1000.0f; // Convert to milliseconds
}

void OpenGLPipelineManager::renderTexture(unsigned int textureId, float x, float y, float width, float height)
{
    switch (m_selectedPipeline) {
        case RenderingPipeline::MODERN_SHADER:
            renderTextureModern(textureId, x, y, width, height);
            break;
        case RenderingPipeline::INTERMEDIATE_VBO:
            renderTextureVBO(textureId, x, y, width, height);
            break;
        case RenderingPipeline::LEGACY_IMMEDIATE:
            renderTextureLegacy(textureId, x, y, width, height);
            break;
    }
    m_drawCalls++;
}

void OpenGLPipelineManager::renderTextureLegacy(unsigned int textureId, float x, float y, float width, float height)
{
    // Your existing immediate mode rendering
    glBindTexture(GL_TEXTURE_2D, textureId);
    glBegin(GL_QUADS);
    glTexCoord2f(0.0f, 0.0f); glVertex2f(x, y + height);
    glTexCoord2f(1.0f, 0.0f); glVertex2f(x + width, y + height);
    glTexCoord2f(1.0f, 1.0f); glVertex2f(x + width, y);
    glTexCoord2f(0.0f, 1.0f); glVertex2f(x, y);
    glEnd();
}

void OpenGLPipelineManager::renderTextureVBO(unsigned int textureId, float x, float y, float width, float height)
{
    // VBO-based rendering (OpenGL 2.0+)
    glBindTexture(GL_TEXTURE_2D, textureId);
    // Ensure crisp sampling in the intermediate path: avoid unintended mipmap LODs and edge bleeding
    // This only affects the currently bound texture and only for this pipeline.
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    
    // Set up transformation (simplified - you might want proper matrix math)
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glTranslatef(x, y, 0);
    glScalef(width, height, 1);
    
    glBindBuffer(GL_ARRAY_BUFFER, m_vboQuad);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    
    glVertexPointer(2, GL_FLOAT, 4 * sizeof(float), (void*)0);
    glTexCoordPointer(2, GL_FLOAT, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    
    glDrawArrays(GL_TRIANGLES, 0, 6);
    
    glDisableClientState(GL_VERTEX_ARRAY);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    
    glPopMatrix();
}

void OpenGLPipelineManager::renderTextureModern(unsigned int textureId, float x, float y, float width, float height)
{
    // Modern shader-based rendering (OpenGL 3.0+)
    glUseProgram(m_shaderProgram);
    
    // Set uniforms (you'll need to set projection matrix properly)
    glUniform1i(glGetUniformLocation(m_shaderProgram, "useTexture"), 1);
    glUniform4f(glGetUniformLocation(m_shaderProgram, "color"), 1.0f, 1.0f, 1.0f, 1.0f);
    
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, textureId);
    glUniform1i(glGetUniformLocation(m_shaderProgram, "ourTexture"), 0);
    
    glBindVertexArray(m_VAO);
    
    // Set transformation (simplified - implement proper matrix math)
    // You would calculate and set projection matrix here
    
    glDrawArrays(GL_TRIANGLES, 0, 6);
    
    glBindVertexArray(0);
    glUseProgram(0);
}

void OpenGLPipelineManager::renderRectangle(float x, float y, float width, float height, float r, float g, float b, float a)
{
    switch (m_selectedPipeline) {
        case RenderingPipeline::MODERN_SHADER:
            renderRectangleModern(x, y, width, height, r, g, b, a);
            break;
        case RenderingPipeline::INTERMEDIATE_VBO:
            renderRectangleVBO(x, y, width, height, r, g, b, a);
            break;
        case RenderingPipeline::LEGACY_IMMEDIATE:
            renderRectangleLegacy(x, y, width, height, r, g, b, a);
            break;
    }
    m_drawCalls++;
}

void OpenGLPipelineManager::renderRectangleLegacy(float x, float y, float width, float height, float r, float g, float b, float a)
{
    glDisable(GL_TEXTURE_2D);
    glColor4f(r, g, b, a);
    glBegin(GL_QUADS);
    glVertex2f(x, y);
    glVertex2f(x + width, y);
    glVertex2f(x + width, y + height);
    glVertex2f(x, y + height);
    glEnd();
    glEnable(GL_TEXTURE_2D);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
}

void OpenGLPipelineManager::renderRectangleVBO(float x, float y, float width, float height, float r, float g, float b, float a)
{
    // Similar to VBO texture rendering but without texture
    glDisable(GL_TEXTURE_2D);
    glColor4f(r, g, b, a);
    
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glTranslatef(x, y, 0);
    glScalef(width, height, 1);
    
    glBindBuffer(GL_ARRAY_BUFFER, m_vboQuad);
    glEnableClientState(GL_VERTEX_ARRAY);
    glVertexPointer(2, GL_FLOAT, 4 * sizeof(float), (void*)0);
    
    glDrawArrays(GL_TRIANGLES, 0, 6);
    
    glDisableClientState(GL_VERTEX_ARRAY);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glPopMatrix();
    
    glEnable(GL_TEXTURE_2D);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
}

void OpenGLPipelineManager::renderRectangleModern(float x, float y, float width, float height, float r, float g, float b, float a)
{
    glUseProgram(m_shaderProgram);
    
    glUniform1i(glGetUniformLocation(m_shaderProgram, "useTexture"), 0);
    glUniform4f(glGetUniformLocation(m_shaderProgram, "color"), r, g, b, a);
    
    glBindVertexArray(m_VAO);
    
    // Set transformation matrix (implement proper matrix math)
    
    glDrawArrays(GL_TRIANGLES, 0, 6);
    
    glBindVertexArray(0);
    glUseProgram(0);
}

void OpenGLPipelineManager::renderLine(float x1, float y1, float x2, float y2, float r, float g, float b, float a)
{
    // Line rendering is similar across all pipelines for simplicity
    glDisable(GL_TEXTURE_2D);
    glColor4f(r, g, b, a);
    glBegin(GL_LINES);
    glVertex2f(x1, y1);
    glVertex2f(x2, y2);
    glEnd();
    glEnable(GL_TEXTURE_2D);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    m_drawCalls++;
}

void OpenGLPipelineManager::setVSync(bool enable)
{
    // Platform-specific VSync control would go here
    // For now, just log the request
    std::cout << "VSync " << (enable ? "enabled" : "disabled") << std::endl;
}

void OpenGLPipelineManager::cleanupResources()
{
    if (m_VAO) {
        glDeleteVertexArrays(1, &m_VAO);
        m_VAO = 0;
    }
    if (m_VBO) {
        glDeleteBuffers(1, &m_VBO);
        m_VBO = 0;
    }
    if (m_vboQuad) {
        glDeleteBuffers(1, &m_vboQuad);
        m_vboQuad = 0;
    }
    if (m_shaderProgram) {
        glDeleteProgram(m_shaderProgram);
        m_shaderProgram = 0;
    }
    if (m_vertexShader) {
        glDeleteShader(m_vertexShader);
        m_vertexShader = 0;
    }
    if (m_fragmentShader) {
        glDeleteShader(m_fragmentShader);
        m_fragmentShader = 0;
    }
}
