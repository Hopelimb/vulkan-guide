// vulkan_guide.h : Include file for standard system include files,
// or project specific include files.
#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <span>
#include <array>
#include <functional>
#include <deque>

#include <vulkan/vulkan.h>
#include <vulkan/vulkan_core.h>
#include <vulkan/vk_enum_string_helper.h>
#include <vk_mem_alloc.h>

#include <fmt/core.h>

#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>


#define VK_CHECK(x)                                                     \
    do {                                                                \
        VkResult err = x;                                               \
        if (err) {                                                      \
            fmt::println("Detected Vulkan error: {}", string_VkResult(err)); \
            abort();                                                    \
        }                                                               \
    } while (0)

struct AllocatedImage {
    VkImage image;
    VkImageView imageView;
	VmaAllocation allocation;
    VkExtent3D imageExtent;
    VkFormat imageFormat;
};

struct AllocatedBuffer {
    VkBuffer buffer; 					// Vulkan buffer object	
    VmaAllocation allocation;			// memory allocation for the buffer
    VmaAllocationInfo allocationInfo;	// metadata about the allocation and the buffer, such as size, memory type, etc.
};


struct GPUMeshBuffers {
    AllocatedBuffer indexBuffer;
    AllocatedBuffer vertexBuffer;
    VkDeviceAddress vertexBufferAddress;
    //AllocatedBuffer jointMatrixBuffer;
    //VkDeviceAddress jointMatrixBufferAddress;
};

struct GPUSkinBuffers {
    AllocatedBuffer ibmBuffer;
    AllocatedBuffer stagingBuffer;
    VkDeviceAddress ibmBufferAddress;
};

struct GPUDrawPushConstants {
    glm::mat4 worldMatrix;
    VkDeviceAddress vertexBuffer;
    VkDeviceAddress jointMatrixBuffer;
};

enum class MaterialPass :uint8_t {
    MainColor, 
    Transparent,
    Other
};

struct MaterialPipeline {
    VkPipeline pipeline;
    VkPipelineLayout layout;
};

struct MaterialInstance {
    MaterialPipeline* materialPipeline;
    VkDescriptorSet materialSet;
    MaterialPass passType;
};

struct DrawContext;

// base class for a renderable dynamic object
class IRenderable {
    virtual void Draw(const glm::mat4& topMatrix, DrawContext& ctx) = 0;
};

// implementation of a drawable scene node.
// the scene node can hold children and will also keep a transform to propagate to them
struct Node : public IRenderable {

    std::weak_ptr<Node> parent; // parent pointer must be a week pointer to avoid circular dependencies
    std::vector<std::shared_ptr<Node>> children;
    std::size_t meshIndex = -1;
    std::size_t skinIndex = -1;
    glm::mat4 localTransform;
    glm::mat4 worldTransform;

    void refreshTransform(const glm::mat4& parentMatrix)
    {
        worldTransform = parentMatrix * localTransform;
        for (auto c : children) {
            c->refreshTransform(worldTransform);
        }
    }

    virtual void Draw(const glm::mat4& topMatrix, DrawContext& ctx) {
        for (auto& c : children) {
            c->Draw(topMatrix, ctx);
        }
    }
};