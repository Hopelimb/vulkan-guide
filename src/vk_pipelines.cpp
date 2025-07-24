#include <vk_pipelines.h>
#include <fstream>
#include <vk_initializers.h>

//> load_shader
bool vkutil::load_shader_module(const char* filePath,
    VkDevice device,
    VkShaderModule* outShaderModule)
{
    // open the file. With cursor at the end
    std::ifstream file(filePath, std::ios::ate | std::ios::binary);

    if (!file.is_open()) {
        return false;
    }

    // find what the size of the file is by looking up the location of the cursor
    // because the cursor is at the end, it gives the size directly in bytes
    size_t fileSize = (size_t)file.tellg();

    // spirv expects the buffer to be on uint32, so make sure to reserve a int
    // vector big enough for the entire file
    std::vector<uint32_t> buffer(fileSize / sizeof(uint32_t));

    // put file cursor at beginning
    file.seekg(0);

    // load the entire file into the buffer
    file.read((char*)buffer.data(), fileSize);

    // now that the file is loaded into the buffer, we can close it
    file.close();

    // create a new shader module, using the buffer we loaded
    VkShaderModuleCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.pNext = nullptr;

    // codeSize has to be in bytes, so multply the ints in the buffer by size of
    // int to know the real size of the buffer
    createInfo.codeSize = buffer.size() * sizeof(uint32_t);
    createInfo.pCode = buffer.data();

    // check that the creation goes well.
    VkShaderModule shaderModule;
    if (vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
        return false;
    }
    *outShaderModule = shaderModule;
    return true;
}
//< load_shader

VkPipeline vkutil::PipelineBuilder::builder_Pipeline(VkDevice device)
{
    VkPipelineViewportStateCreateInfo viewportState{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
		.pNext = nullptr,
		.viewportCount = 1,
		.scissorCount = 1,
    };


    VkPipelineColorBlendStateCreateInfo colorBlending{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
		.pNext = nullptr,
		.logicOpEnable = VK_FALSE,
		.logicOp = VK_LOGIC_OP_COPY,
		.attachmentCount = 1,
		.pAttachments = &_colorBlendAttachment,
    };

    VkPipelineVertexInputStateCreateInfo _vertexInputInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
    };

    VkDynamicState dynamicStates[] = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR,
	};

    VkPipelineDynamicStateCreateInfo dynamicInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .pNext = nullptr,
        .dynamicStateCount = static_cast<uint32_t>(std::size(dynamicStates)),
        .pDynamicStates = dynamicStates,
    };


    VkGraphicsPipelineCreateInfo pipelineInfo{
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pNext = &_renderInfo,
        .stageCount = static_cast<uint32_t>(_shaderStages.size()),
        .pStages = _shaderStages.data(),
        .pVertexInputState = &_vertexInputInfo,
        .pInputAssemblyState = &_inputAssembly,
        .pViewportState = &viewportState,
        .pRasterizationState = &_rasterization,
        .pMultisampleState = &_multisampleState,
        .pDepthStencilState = &_depthStencilState,
        .pColorBlendState = &colorBlending,
	    .pDynamicState = &dynamicInfo,
        .layout = _pipelineLayout,
    };

    VkPipeline newPipeline{};

	VK_CHECK(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &newPipeline));

    return newPipeline;
}

void vkutil::PipelineBuilder::set_shaders(VkShaderModule vertexShader, VkShaderModule fragmentShader)
{
	_shaderStages.clear();
    _shaderStages.push_back(
        vkinit::pipeline_shader_stage_create_info(
            VK_SHADER_STAGE_VERTEX_BIT, vertexShader
        )
    );
    _shaderStages.push_back(
        vkinit::pipeline_shader_stage_create_info(
            VK_SHADER_STAGE_FRAGMENT_BIT, fragmentShader
        )
    );
}

void vkutil::PipelineBuilder::set_input_typology(VkPrimitiveTopology topology)
{
	_inputAssembly.topology = topology;

	// primitive restart is used to allow a single draw call to render multiple disconnected primitives
	_inputAssembly.primitiveRestartEnable = VK_FALSE; // no primitive restart
}

void vkutil::PipelineBuilder::set_polygon_mode(VkPolygonMode polygonMode)
{
    _rasterization.polygonMode = polygonMode;
	_rasterization.lineWidth = 1.0f;
}

void vkutil::PipelineBuilder::set_cull_mode(VkCullModeFlags cullMode, VkFrontFace frontFace)
{
	_rasterization.cullMode = cullMode;
	_rasterization.frontFace = frontFace;
}

void vkutil::PipelineBuilder::set_multisampling_none()
{

    _multisampleState.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT; // no multisampling
    _multisampleState.sampleShadingEnable = VK_FALSE; // no sample shading
    _multisampleState.alphaToCoverageEnable = VK_FALSE; // no alpha to coverage
    _multisampleState.alphaToOneEnable = VK_FALSE; // no alpha to one
	_multisampleState.pSampleMask = nullptr; // no sample mask
	_multisampleState.minSampleShading = 1.0f; // no sample shading
}

void vkutil::PipelineBuilder::disable_blending()
{
    _colorBlendAttachment.blendEnable = VK_FALSE; // disable blending
    _colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
		VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT; // write all color components
}

void vkutil::PipelineBuilder::set_color_attachment_format(VkFormat format)
{
	_colorAttachmentFormat = format;
	_renderInfo.colorAttachmentCount = 1;
    _renderInfo.pColorAttachmentFormats = &_colorAttachmentFormat;
}

void vkutil::PipelineBuilder::set_depth_format(VkFormat format)
{
    _renderInfo.depthAttachmentFormat = format;
}

void vkutil::PipelineBuilder::disable_depthtest()
{
    _depthStencilState.depthTestEnable = VK_FALSE; // disable depth testing
    _depthStencilState.depthWriteEnable = VK_FALSE; // disable depth writing
	_depthStencilState.depthCompareOp = VK_COMPARE_OP_NEVER; // never pass depth comparison
    _depthStencilState.depthBoundsTestEnable = VK_FALSE; // disable depth bounds test
	_depthStencilState.stencilTestEnable = VK_FALSE; // disable stencil testing
	_depthStencilState.front = {}; // no front stencil state
	_depthStencilState.back = {}; // no back stencil state
    _depthStencilState.minDepthBounds = 0.0f; // no depth bounds
	_depthStencilState.maxDepthBounds = 1.0f; // no depth bounds
}





void vkutil::PipelineBuilder::Clear( )
{
    // clear all of the structs we need back to 0 with their correct stype

    _inputAssembly = { .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };

    _rasterization = { .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };

    _colorBlendAttachment = {};

    _multisampleState = { .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };

    _pipelineLayout = {};

    _depthStencilState = { .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };

    _renderInfo = { .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };

    _shaderStages.clear();
}
