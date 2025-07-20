// vulkan_guide.h : Include file for standard system include files,
// or project specific include files.

#pragma once

#include <vk_types.h>
#include <vk_descriptors.h>

struct DeletionQueue
{
	std::deque<std::function<void()>> _deletors;

	void push_function(std::function<void()> && function) {
		_deletors.push_back(std::move(function));
	}

	void flush()
	{
		for (auto it = _deletors.rbegin(); it != _deletors.rend(); it++)
		{
			(*it)();
		}

		_deletors.clear();
	}
};

struct FrameData {
	VkCommandPool _commandPool{ VK_NULL_HANDLE };
	VkCommandBuffer _mainCommandBuffer{ VK_NULL_HANDLE };
	VkSemaphore _swapchainSemaphore{ VK_NULL_HANDLE }, _renderSemaphore{ VK_NULL_HANDLE };
	VkFence _renderFence{ VK_NULL_HANDLE };

	DeletionQueue _deletionQueue;
};

constexpr unsigned int FRAME_OVERLAP = 2; // number of frames in flight

class VulkanEngine {
public:
	const uint64_t TIMEOUT = 1000000000; // 1 second in nanoseconds

	bool bUseValidationLayers{ true };
	bool _isInitialized{ false };
	int _frameNumber {0};
	bool stop_rendering{ false };
	VkExtent2D _windowExtent{ 1280 , 720 };
	VkInstance _instance{ VK_NULL_HANDLE };
	VkDebugUtilsMessengerEXT _debug_messenger{ VK_NULL_HANDLE };
	VkPhysicalDevice _chosenGPU{ VK_NULL_HANDLE };
	VkDevice _device{ VK_NULL_HANDLE };
	VkSurfaceKHR _surface{ VK_NULL_HANDLE };

	struct SDL_Window* _window{ nullptr };

	static VulkanEngine& Get();

	VkSwapchainKHR _swapchain{ VK_NULL_HANDLE };
	VkFormat _swapchainImageFormat{ VK_FORMAT_UNDEFINED };

	std::vector<VkImage> _swapchainImages{};
	std::vector<VkImageView> _swapchainImageViews{};
	VkExtent2D _swapchainExtent{ 0, 0 };

	FrameData _frames[FRAME_OVERLAP];
	FrameData& get_current_frame() { return _frames[_frameNumber % FRAME_OVERLAP]; };


	VkQueue _graphicsQueue{ VK_NULL_HANDLE };
	uint32_t _graphicsQueueFamily{ 0 };

	DeletionQueue _mainDeletionQueue{};
	VmaAllocator _allocator{ VK_NULL_HANDLE };
	AllocatedImage _drawImage{};
	VkExtent2D _drawExtent{};

	VkPipeline _gradientPipeline{ VK_NULL_HANDLE };
	VkPipelineLayout _gradientPipelineLayout{ VK_NULL_HANDLE };
	DescriptorAllocator _globalDescriptorAllocator{};
	VkDescriptorSet _drawImageDescriptor{ VK_NULL_HANDLE };
	VkDescriptorSetLayout _drawImageDescriptorLayout{ VK_NULL_HANDLE };

	VkFence _immFence;
	VkCommandBuffer _immCommandBuffer{ VK_NULL_HANDLE };
	VkCommandPool _immCommandPool{ VK_NULL_HANDLE };


	//initializes everything in the engine
	void init();

	//shuts down the engine
	void cleanup();

	//draw loop
	void draw();
	void draw_background(VkCommandBuffer cmd);

	//run main loop
	void run();


private:
	void init_vulkan();
	void init_swapchain();
	void init_commands();
	void init_sync_structures();
	void init_descriptors();

	void init_pipelines();
	void init_background_pipelines();

	void create_swapchain(uint32_t width, uint32_t height);
	void destroy_swapchain();

	void immediate_submit(std::function<void(VkCommandBuffer cmd)>&& function);
	void init_imgui();

	void draw_imgui(VkCommandBuffer cmd, VkImageView targetImageView);
};
