// vulkan_guide.h : Include file for standard system include files,
// or project specific include files.

#pragma once

#include <vk_types.h>

class VulkanEngine {
public:

	bool bUseValidationLayers{ true };
	bool _isInitialized{ false };
	int _frameNumber {0};
	bool stop_rendering{ false };
	VkExtent2D _windowExtent{ 1700 , 900 };
	VkInstance _instance{ VK_NULL_HANDLE };
	VkDebugUtilsMessengerEXT _debug_messenger{ VK_NULL_HANDLE };
	VkPhysicalDevice _chosenGPU{ VK_NULL_HANDLE };
	VkDevice _device{ VK_NULL_HANDLE };
	VkSurfaceKHR _surface{ VK_NULL_HANDLE };

	struct SDL_Window* _window{ nullptr };

	static VulkanEngine& Get();

	VkSwapchainKHR _swapchain{ VK_NULL_HANDLE };
	VkFormat _swapchainImageFormat{ VK_FORMAT_UNDEFINED };

	std::vector<VkImage> _swapchainImages;
	std::vector<VkImageView> _swapchainImageViews;
	VkExtent2D _swapchainExtent{ 0, 0 };

	//initializes everything in the engine
	void init();

	//shuts down the engine
	void cleanup();

	//draw loop
	void draw();

	//run main loop
	void run();


private:
	void init_vulkan();
	void init_swapchain();
	void init_commands();
	void init_sync_structures();

	void create_swapchain(uint32_t width, uint32_t height);
	void destroy_swapchain();
};
