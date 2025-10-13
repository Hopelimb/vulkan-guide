#pragma once
#include <vk_types.h>
#include <vk_descriptors.h>
#include <unordered_map>
#include <filesystem>

struct GLTFMaterial {

	MaterialInstance data;
};

struct Vertex {
	glm::vec3 position;
	float uv_x;
	glm::vec3 normal;
	float uv_y;
	glm::vec4 joints;
	glm::vec4 weights;
};

struct Bounds {
	glm::vec3 origin;
	float sphereRadius;
	glm::vec3 extents;
};

struct GeoSurface {
	uint32_t startIndex;
	uint32_t count;
	Bounds bounds;
	std::shared_ptr<GLTFMaterial> material;
};

struct MeshAsset {
	std::string name;
	std::vector<uint32_t> indices;
	std::vector<Vertex> vertices;
	std::vector<glm::mat4> jointMatrices;
	std::vector<GeoSurface> surfaces;
	GPUMeshBuffers meshBuffers;
};

class VulkanEngine;


struct RenderObjectData : public IRenderable{
	std::unordered_map<std::string, std::shared_ptr<MeshAsset>> meshes;
	std::unordered_map<std::string, std::shared_ptr<Node>> nodes;
	std::unordered_map<std::string, AllocatedImage> images;
	std::unordered_map<std::string, std::shared_ptr<GLTFMaterial>> materials;

	std::vector<std::shared_ptr<Node>> topNodes;
	std::vector<VkSampler> samplers;
	DescriptorAllocatorGrowable descriptorPool;
	AllocatedBuffer materialDataBuffer;

	VulkanEngine* creator;

	~RenderObjectData() { clearAll(); };

	virtual void Draw(const glm::mat4& topMatrix, DrawContext& ctx);

private:

	void clearAll();
};

std::optional<std::shared_ptr<RenderObjectData>> GetRenderObjectDataFromGltf(VulkanEngine* engine, std::string filePath);

//std::optional<std::vector<std::shared_ptr<MeshAsset>>> loadGltfMeshes(VulkanEngine* engine, std::filesystem::path filePath);
