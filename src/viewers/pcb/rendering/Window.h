#pragma once

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <string>

class Window {
public:
    Window(int width, int height, const std::string& title);
    ~Window();

    bool Initialize();
    void Cleanup();
    bool ShouldClose() const;
    void SwapBuffers();
    void PollEvents();
    GLFWwindow* GetHandle() const { return window; }

    // Callbacks
    static void FramebufferSizeCallback(GLFWwindow* window, int width, int height);
    static void KeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);
    static void MouseButtonCallback(GLFWwindow* window, int button, int action, int mods);
    static void ScrollCallback(GLFWwindow* window, double xoffset, double yoffset);
    static void CursorPosCallback(GLFWwindow* window, double xpos, double ypos);    // Getters
    int GetWidth() const { return width; }
    int GetHeight() const { return height; }
    
    // Update window size (for responsive handling)
    void UpdateSize() {
        if (window) {
            glfwGetFramebufferSize(window, &width, &height);
        }
    }

private:
    GLFWwindow* window = nullptr;
    int width;
    int height;
    std::string title;
    bool initialized = false;
};
