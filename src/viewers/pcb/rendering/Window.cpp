#include "Window.h"
#include "Utils.h"
#include <iostream>

Window::Window(int width, int height, const std::string& title)
    : width(width), height(height), title(title) {
}

Window::~Window() {
    Cleanup();
}

bool Window::Initialize() {
    // Initialize GLFW
    if (!glfwInit()) {
        LOG_ERROR("Failed to initialize GLFW");
        return false;
    }

    // Set GLFW window hints
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

    // Create window
    window = glfwCreateWindow(width, height, title.c_str(), nullptr, nullptr);
    if (!window) {
        LOG_ERROR("Failed to create GLFW window");
        glfwTerminate();
        return false;
    }    // Make the window's context current
    glfwMakeContextCurrent(window);
    
    // Enable V-sync for better performance and responsiveness
    glfwSwapInterval(1);

    // Set callbacks
    glfwSetFramebufferSizeCallback(window, FramebufferSizeCallback);
    glfwSetKeyCallback(window, KeyCallback);
    glfwSetMouseButtonCallback(window, MouseButtonCallback);
    glfwSetScrollCallback(window, ScrollCallback);
    glfwSetCursorPosCallback(window, CursorPosCallback);

    // Initialize GLEW
    if (glewInit() != GLEW_OK) {
        LOG_ERROR("Failed to initialize GLEW");
        return false;
    }

    // Set viewport
    glViewport(0, 0, width, height);

    // Enable depth testing
    glEnable(GL_DEPTH_TEST);

    // Enable blending for transparency
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    initialized = true;
    LOG_INFO("Window initialized successfully");
    return true;
}

void Window::Cleanup() {
    if (window) {
        glfwDestroyWindow(window);
        window = nullptr;
    }
    if (initialized) {
        glfwTerminate();
        initialized = false;
    }
}

bool Window::ShouldClose() const {
    return window && glfwWindowShouldClose(window);
}

void Window::SwapBuffers() {
    if (window) {
        glfwSwapBuffers(window);
    }
}

void Window::PollEvents() {
    glfwPollEvents();
}

// Callback implementations
void Window::FramebufferSizeCallback(GLFWwindow* window, int width, int height) {
    glViewport(0, 0, width, height);
    
    // Update the Window instance size if we have a way to access it
    // This ensures the window size is always current
}

void Window::KeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
        glfwSetWindowShouldClose(window, true);
    }
}

void Window::MouseButtonCallback(GLFWwindow* window, int button, int action, int mods) {
    // Handle mouse button events
    if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS) {
        double xpos, ypos;
        glfwGetCursorPos(window, &xpos, &ypos);
        // Handle left mouse button click
    }
}

void Window::ScrollCallback(GLFWwindow* window, double xoffset, double yoffset) {
    // Handle scroll events for zooming
    // Handle mouse scroll for zoom
}

void Window::CursorPosCallback(GLFWwindow* window, double xpos, double ypos) {
    // Handle mouse movement
    // LOG_DEBUG("Mouse position: " + std::to_string(xpos) + ", " + std::to_string(ypos));
}
