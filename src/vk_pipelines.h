#pragma once 
#include <vk_types.h>

namespace vkutil {
	bool load_shader_module(const char* filePath, VkDevice device, VkShaderModule* outShaderModule);

	class PipelineBuilder {
	public:
		std::vector<VkPipelineShaderStageCreateInfo> _shaderStages{ };
		VkPipelineInputAssemblyStateCreateInfo _inputAssembly{ };
		VkPipelineRasterizationStateCreateInfo _rasterization{ };
		VkPipelineColorBlendAttachmentState _colorBlendAttachment{ };
		VkPipelineMultisampleStateCreateInfo _multisampleState{ };
		VkPipelineLayout _pipelineLayout{ VK_NULL_HANDLE };	
		VkPipelineDepthStencilStateCreateInfo _depthStencilState{ };
		VkPipelineRenderingCreateInfo _renderInfo{ };
		VkFormat _colorAttachmentFormat{ VK_FORMAT_UNDEFINED };

		PipelineBuilder() {
			Clear();
		}
		

		void Clear();

		VkPipeline build_pipeline(VkDevice device);

		void set_shaders(VkShaderModule vertexShader, VkShaderModule fragmentShader);

		void set_input_topology(VkPrimitiveTopology topology);

		void set_polygon_mode(VkPolygonMode polygonMode);

		void set_cull_mode(VkCullModeFlags cullMode, VkFrontFace frontFace);

		void set_multisampling_none();

		void disable_blending();

		void set_color_attachment_format(VkFormat format);

		void set_depth_format(VkFormat format);

		void disable_depthtest();

		void enable_depthtest(bool depthWriteEnable, VkCompareOp op);

		void enable_blending_additive();

		void enable_blending_alpha();
	};
};