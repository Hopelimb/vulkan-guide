// vulkan_guide.h : Include file for standard system include files,
// or project specific include files.

#pragma once

#include <vk_types.h>
#include <vk_descriptors.h>
#include <vk_loader.h>


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

	DeletionQueue _deletionQueue{};
	DescriptorAllocatorGrowable _frameDescriptors{};
};

struct GPUSceneData {
	glm::mat4 view;
	glm::mat4 proj;
	glm::mat4 viewporj;
	glm::vec4 ambientColor;
	glm::vec4 sunlightDirection;
	glm::vec4 sunlightColor;
};


struct ComputePushConstants {
	glm::vec4 data1;
	glm::vec4 data2;
	glm::vec4 data3;
	glm::vec4 data4;
};

struct ComputeEffect {
	const char* name;

	VkPipeline pipeline;
	VkPipelineLayout pipelineLayout;

	ComputePushConstants data;
};

constexpr unsigned int FRAME_OVERLAP = 2; // number of frames in flight


struct Vertex {
	glm::vec3 position;
	float uv_x;
	glm::vec3 normal;
	float uv_y;
	glm::vec4 color;
};

struct VertexBuffer {
	Vertex vertices[];
};

struct GLTFMetalic_Roughness {
	MaterialPipeline opaquePipeline;
	MaterialPipeline transparentPipeline;

	VkDescriptorSetLayout materialLayout;

	struct MaterialConstants {
		glm::vec4 colorFactors;
		glm::vec4 metal_rough_factors;
		glm::vec4 extra[14];
	};

	struct MaterialResources {
		AllocatedImage colorImage;
		VkSampler colorSampler;
		AllocatedImage metalRoughImage;
		VkSampler metalRoughSampler;
		VkBuffer dataBuffer;
		uint32_t dataBufferOffset;
	};

	DescriptorWriter writer;

	void build_pipelines(VulkanEngine* engine);
	void clear_resources(VkDevice device);

	MaterialInstance write_material(VkDevice device, MaterialPass pass, const MaterialResources& resources, DescriptorAllocator& descriptorAllocator);
};

struct MeshNode : public Node {

	std::shared_ptr<MeshAsset> mesh;

	virtual void Draw(const glm::mat4& topMatrix, DrawContext& ctx) override;
};

struct RenderObject {

	uint32_t indexCount{ 0 };
	uint32_t firstIndex{ 0 };
	VkBuffer indexBuffer{ VK_NULL_HANDLE };

	MaterialInstance* material{ nullptr };

	glm::mat4 transform;
	VkDeviceAddress vertexBufferAddress{ 0 };
};

struct DrawContext {

	std::vector<RenderObject> OpaqueSurfaces;
};


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
	AllocatedImage _depthImage{};
	VkExtent2D _drawExtent{};

	//VkPipeline _gradientPipeline{ VK_NULL_HANDLE };
	VkPipelineLayout _gradientPipelineLayout{ VK_NULL_HANDLE };
	DescriptorAllocator _globalDescriptorAllocator{};
	VkDescriptorSet _drawImageDescriptor{ VK_NULL_HANDLE };
	VkDescriptorSetLayout _drawImageDescriptorLayout{ VK_NULL_HANDLE };

	VkFence _immFence;
	VkCommandBuffer _immCommandBuffer{ VK_NULL_HANDLE };
	VkCommandPool _immCommandPool{ VK_NULL_HANDLE };

	//VkPipelineLayout _trianglePipelineLayout{ VK_NULL_HANDLE };
	//VkPipeline _trianglePipeline{ VK_NULL_HANDLE };

	//VkPipelineLayout _meshPipelineLayout{ VK_NULL_HANDLE };
	//VkPipeline _meshPipeline{ VK_NULL_HANDLE };

	//GPUMeshBuffers _rectangle;
	std::vector<std::shared_ptr<MeshAsset>> testMeshes;

	std::vector<ComputeEffect> backgroundEffects;
	int currentBachgroundEffect{ 0 };

	bool resize_requested{ false };
	float renderScale{ 1.0f }; // scale the rendering to fit the window

	GPUSceneData sceneData{};
	VkDescriptorSetLayout _gpuSceneDataDescriptorLayout{ VK_NULL_HANDLE };

	AllocatedImage _whiteImage{};
	AllocatedImage _blackImage{};
	AllocatedImage _greyImage{};
	AllocatedImage _errorCheckerboardImage{};

	VkSampler _defaultSamplerLinear{ VK_NULL_HANDLE };
	VkSampler _defaultSamplerNearest{ VK_NULL_HANDLE };

	VkDescriptorSetLayout _singleImageDescriptorlayout;

	MaterialInstance defaultMaterial;
	GLTFMetalic_Roughness metalRoughMaterial;

	DrawContext mainDrawContext;
	std::unordered_map<std::string, std::shared_ptr<Node>> loadedNodes;

	//initializes everything in the engine
	void init();

	//shuts down the engine
	void cleanup();

	//draw loop
	void draw();
	void draw_background(VkCommandBuffer cmd);
	void draw_geometry(VkCommandBuffer cmd);

	void update_scene();

	//run main loop
	void run();
	GPUMeshBuffers uploadMesh(std::span<uint32_t> indices, std::span<Vertex> vertices);


private:
	void init_vulkan();
	void init_swapchain();
	void init_commands();
	void init_sync_structures();
	void init_descriptors();

	void init_pipelines();
	void init_background_pipelines();
	//void init_triangle_pipeline();
	void init_default_data();


	void create_swapchain(uint32_t width, uint32_t height);
	void destroy_swapchain();

	void immediate_submit(std::function<void(VkCommandBuffer cmd)>&& function);
	void init_imgui();

	void draw_imgui(VkCommandBuffer cmd, VkImageView targetImageView);


	AllocatedBuffer create_buffer(size_t allocSize, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage);

	void destroy_buffer(const AllocatedBuffer& buffer);
	void resize_swapchain();

	AllocatedImage create_image(VkExtent3D size, VkFormat format, VkImageUsageFlags usage, bool mipmapped = false);
	AllocatedImage create_image(void* data, VkExtent3D size, VkFormat format, VkImageUsageFlags usage, bool mipmapped = false);
	void destroy_image(const AllocatedImage& image);
};
