#include "Window.h"
#include "Utility\Logger.h"
#include "Input\Input.h"
#include "Raster Engine\RasterBackend.h"

Window::Window( uint x, uint y, uint width, uint height, GLFWmonitor* monitorIn)
{

	WindowType  win = BORDERLESS;

	glfwWindowHint(GLFW_SAMPLES, 0);
	glfwWindowHint(GLFW_VISIBLE, true);
	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

	const GLFWvidmode* mode = glfwGetVideoMode(monitorIn);

	if (win == FULLSCREEN) {

		glfwWindowHint(GLFW_RESIZABLE, false);
		glfwWindowHint(GLFW_DECORATED, false);

		glfwWindowHint(GLFW_RED_BITS, mode->redBits);
		glfwWindowHint(GLFW_GREEN_BITS, mode->greenBits);
		glfwWindowHint(GLFW_BLUE_BITS, mode->blueBits);
		glfwWindowHint(GLFW_REFRESH_RATE, mode->refreshRate);
		windowHandle = glfwCreateWindow(mode->width, mode->height, "Soul Engine", monitorIn, NULL);

	}
	else if (win == WINDOWED) {

		glfwWindowHint(GLFW_RESIZABLE, true);
		windowHandle = glfwCreateWindow(width, height, "Soul Engine", NULL, NULL);

	}

	else if (win == BORDERLESS) {

		glfwWindowHint(GLFW_RESIZABLE, false);
		glfwWindowHint(GLFW_DECORATED, false);

		glfwWindowHint(GLFW_RED_BITS, mode->redBits);
		glfwWindowHint(GLFW_GREEN_BITS, mode->greenBits);
		glfwWindowHint(GLFW_BLUE_BITS, mode->blueBits);
		glfwWindowHint(GLFW_REFRESH_RATE, mode->refreshRate);
		windowHandle = glfwCreateWindow(width, height, "Soul Engine", NULL, NULL);

	}
	else {
		LOG(ERROR,"No Window setting found");
	}

	if (!windowHandle) {
		LOG(ERROR, "Window creation failed");
	}

	glfwSetWindowPos(windowHandle,x,y);

	glfwSetWindowUserPointer(windowHandle, this);

	glfwSetKeyCallback(windowHandle, Input::KeyCallback);
	glfwSetScrollCallback(windowHandle, Input::ScrollCallback);
	glfwSetCursorPosCallback(windowHandle, Input::MouseCallback);


	RasterBackend::CreateWindow(*this);
	//glfwSetWindowUserPointer(windowOut, &VulkanBackend::GetInstance());
	//glfwSetWindowSizeCallback(windowOut, VulkanBackend::OnWindowResized);

	//////////////////////////

	/*Soul::renderer rend = {
	new Renderer(glm::uvec2(int(xSize*mode->width), int(ySize*mode->height))),
	SPECTRAL,
	1.0f
	};

	Soul::renderObjects.push_back(rend);*/

	//////////////////////////

	//VulkanBackend::GetInstance().AddWindow(windowOut, int(xSize*mode->width), int(ySize*mode->height));

}


Window::~Window()
{
}

void Window::Draw() {
	//int width, height;
	//glfwGetWindowSize(masterWindow, &width, &height);
	//glfwSetCursorPos(masterWindow, width / 2.0f, height / 2.0f);
//	VulkanBackend::GetInstance().DrawFrame(masterWindow, width, height);
}