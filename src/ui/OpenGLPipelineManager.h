#pragma once

#include <GL/glew.h>
#include <string>
#include <memory>

/**
 * OpenGL Pipeline Manager - Automatically detects capabilities and chooses optimal rendering pipeline
 * 
 * Supports multiple rendering paths:
 * - Modern Pipeline: OpenGL 3.0+ with VBO/VAO/Shaders (fastest)
 * - Intermediate Pipeline: OpenGL 2.0+ with VBO but no shaders (good performance)
 * - Legacy Pipeline: OpenGL 1.1+ with immediate mode (maximum compatibility)
 */

enum class RenderingPipeline {
    LEGACY_IMMEDIATE,    // OpenGL 1.1+ - glBegin/glEnd (maximum compatibility)
    INTERMEDIATE_VBO,    // OpenGL 2.0+ - VBO without shaders (good performance)
    MODERN_SHADER        // OpenGL 3.0+ - VBO/VAO/Shaders (best performance)
};

struct OpenGLCapabilities {
    int majorVersion;
    int minorVersion;
    bool hasVBO;
    bool hasVAO;
    bool hasShaders;
    bool hasFramebuffers;
    int maxTextureSize;
    std::string vendor;
    std::string renderer;
    std::string version;
};

class OpenGLPipelineManager {
public:
    OpenGLPipelineManager();
    ~OpenGLPipelineManager();

    /**
     * Initialize and detect optimal pipeline
     * Call this after OpenGL context is created
     */
    bool initialize();

    /**
     * Get detected capabilities
     */
    const OpenGLCapabilities& getCapabilities() const { return m_capabilities; }

    /**
     * Get selected rendering pipeline
     */
    RenderingPipeline getSelectedPipeline() const { return m_selectedPipeline; }

    /**
     * Get pipeline description for logging/debugging
     */
    std::string getPipelineDescription() const;

    /**
     * Rendering interface - automatically uses best available method
     */
    void beginFrame();
    void endFrame();
    
    // Texture rendering (adapts to pipeline)
    void renderTexture(unsigned int textureId, float x, float y, float width, float height);
    
    // Rectangle rendering (adapts to pipeline)
    void renderRectangle(float x, float y, float width, float height, 
                        float r = 1.0f, float g = 1.0f, float b = 1.0f, float a = 1.0f);
    
    // Line rendering (adapts to pipeline)
    void renderLine(float x1, float y1, float x2, float y2, 
                   float r = 1.0f, float g = 1.0f, float b = 1.0f, float a = 1.0f);

    /**
     * Pipeline-specific optimizations
     */
    void enableBatching(bool enable) { m_batchingEnabled = enable; }
    void setVSync(bool enable);
    
    /**
     * Performance monitoring
     */
    float getLastFrameTime() const { return m_lastFrameTime; }
    int getDrawCalls() const { return m_drawCalls; }

private:
    // Capability detection
    void detectCapabilities();
    RenderingPipeline selectOptimalPipeline();
    
    // Pipeline initialization
    bool initializeLegacyPipeline();
    bool initializeIntermediatePipeline();
    bool initializeModernPipeline();
    
    // Rendering implementations
    void renderTextureLegacy(unsigned int textureId, float x, float y, float width, float height);
    void renderTextureVBO(unsigned int textureId, float x, float y, float width, float height);
    void renderTextureModern(unsigned int textureId, float x, float y, float width, float height);
    
    void renderRectangleLegacy(float x, float y, float width, float height, float r, float g, float b, float a);
    void renderRectangleVBO(float x, float y, float width, float height, float r, float g, float b, float a);
    void renderRectangleModern(float x, float y, float width, float height, float r, float g, float b, float a);

    // Modern pipeline resources
    void createShaders();
    void createBuffers();
    void cleanupResources();
    
    // Member variables
    OpenGLCapabilities m_capabilities;
    RenderingPipeline m_selectedPipeline;
    bool m_initialized;
    bool m_batchingEnabled;
    
    // Performance tracking
    float m_lastFrameTime;
    int m_drawCalls;
    
    // Modern pipeline resources
    unsigned int m_shaderProgram;
    unsigned int m_vertexShader;
    unsigned int m_fragmentShader;
    unsigned int m_VAO;
    unsigned int m_VBO;
    unsigned int m_EBO;
    
    // Intermediate pipeline resources
    unsigned int m_vboQuad;
    
    // Shader source code
    static const char* s_vertexShaderSource;
    static const char* s_fragmentShaderSource;
};
