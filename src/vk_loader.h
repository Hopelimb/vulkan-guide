#pragma once
#include <vk_types.h>
#include <vk_descriptors.h>
#include <unordered_map>
#include <filesystem>

struct EngineMaterial {

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
	std::shared_ptr<EngineMaterial> material;
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
	VulkanEngine* creator;
	DescriptorAllocatorGrowable descriptorPool;
	std::vector<VkSampler> samplers;
	std::vector<AllocatedImage> images;
	std::vector<std::shared_ptr<MeshAsset>> meshes;
	std::vector<std::shared_ptr<Node>> nodes;
	std::vector<std::shared_ptr<Node>> topNodes;
	std::vector<std::shared_ptr<EngineMaterial>> materials;
	AllocatedBuffer materialDataBuffer;
	~RenderObjectData() { clearAll(); };

	virtual void Draw(const glm::mat4& topMatrix, DrawContext& ctx);

private:

	void clearAll();
};

std::optional<std::shared_ptr<RenderObjectData>> GetRenderObjectDataFromGltf(VulkanEngine* engine, std::string filePath);
