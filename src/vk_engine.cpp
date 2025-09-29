//> includes
#include <vk_engine.h>

#include <SDL.h>
#include <SDL_vulkan.h>

#include <vk_initializers.h>
#include <vk_types.h>

#include <VkBootstrap.h>

#include <chrono>
#include <thread>
#include <vk_images.h>
#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>
#include <vk_pipelines.h>

#include <imgui.h>
#include <imgui_impl_sdl2.h>
#include <imgui_impl_vulkan.h>
#include <glm/gtx/transform.hpp>
#include <chrono>

VulkanEngine* loadedEngine = nullptr;

const char* PATH_SHADER_COMP_GRADIENTCOLOR = "../../shaders/gradient_color.comp.spv";
const char* PATH_SHADER_COMP_SKY = "../../shaders/sky.comp.spv";
const char* PATH_SHADER_FRAG_MESH = "../../shaders/mesh.frag.spv";
const char* PATH_SHADER_VERT_MESH = "../../shaders/mesh.vert.spv";

const char* PATH_MESH_MONKEY = "../../assets/basicmesh.glb";
const char* PATH_MESH_STRUCTURE = "../../assets/structure.glb";

VulkanEngine& VulkanEngine::Get() { return *loadedEngine; }

bool is_visible(const RenderObject& obj, const glm::mat4& viewproj)
{
    std::array<glm::vec3, 8> corners{
        glm::vec3 {1, 1, 1},
        glm::vec3 {1, 1, -1},
        glm::vec3 {1, -1, 1},
        glm::vec3 {1, -1, -1},
        glm::vec3 {-1, 1, 1},
        glm::vec3 {-1, 1, -1},
        glm::vec3 {-1, -1, 1},
        glm::vec3 {-1, -1, -1},
    };

    glm::mat4 matrix = viewproj * obj.transform;

    glm::vec3 min = { 1.5, 1.5, 1.5 };
    glm::vec3 max = { -1.5, -1.5, -1.5 };

    for (int c = 0; c < 8; c++)
    {
        glm::vec4 v = matrix * glm::vec4(obj.bounds.origin + (corners[c] * obj.bounds.extents), 1.f);

        v.x = v.x / v.w;
        v.y = v.y / v.w;
        v.z = v.z / v.w;

        min = glm::min(min, glm::vec3{ v });
		max = glm::max(max, glm::vec3{ v });
    }

    if (min.z > 1.f ||
        max.z < 0.f ||
        min.x > 1.f ||
        max.x < -1.f ||
        min.y > 1.f ||
        max.y < -1.f)
    {
		return false;
    } 
    else {

        return true;
    }
}


void VulkanEngine::init()
{
    // only one engine initialization is allowed with the application.
    assert(loadedEngine == nullptr);
    loadedEngine = this;
    init_window();
    init_vulkan();
    init_swapchain();
    init_commands();
    init_sync_structures();
    init_descriptors();
    init_pipelines();
    init_imgui();
    init_create_resources();
    // everything went fine
	auto structureFile = loadGltf(this, PATH_MESH_STRUCTURE);
    assert(structureFile.has_value());
    loadedScenes["structure"] = structureFile.value();

    _isInitialized = true;
}

void VulkanEngine::init_window()
{
    // We initialize SDL and create a window with it.
    SDL_Init(SDL_INIT_VIDEO);

    SDL_WindowFlags window_flags = (SDL_WindowFlags)(SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);

    _window = SDL_CreateWindow(
        "Vulkan Engine",
        SDL_WINDOWPOS_UNDEFINED,
        SDL_WINDOWPOS_UNDEFINED,
        _windowExtent.width,
        _windowExtent.height,
        window_flags);
}

void VulkanEngine::cleanup()
{
    if (_isInitialized) {

        // wait for the device to finish all operations
        vkDeviceWaitIdle(_device);

        loadedScenes.clear();

        for (auto& mesh : testMeshes) {
            destroy_buffer(mesh->meshBuffers.indexBuffer);
            destroy_buffer(mesh->meshBuffers.vertexBuffer);
        }


        for (int i = 0; i < FRAME_OVERLAP; i++) {
            vkDestroyCommandPool(_device, _frames[i]._commandPool, nullptr);

            vkDestroyFence(_device, _frames[i]._renderFence, nullptr);
            vkDestroySemaphore(_device, _frames[i]._renderSemaphore, nullptr);
            vkDestroySemaphore(_device, _frames[i]._swapchainSemaphore, nullptr);
            _frames[i]._deletionQueue.flush();
        }

        _mainDeletionQueue.flush();

        destroy_swapchain();

        vkDestroySurfaceKHR(_instance, _surface, nullptr);
        vkDestroyDevice(_device, nullptr);

        vkb::destroy_debug_utils_messenger(_instance, _debug_messenger);
        vkDestroyInstance(_instance, nullptr);

        SDL_DestroyWindow(_window);
    }

    // clear engine pointer
    loadedEngine = nullptr;
}
void VulkanEngine::draw()
{
    update_scene();
    update_imgui();

#pragma region synchronization_pre
    // nothing yet
    VK_CHECK(vkWaitForFences(_device, 1, &get_current_frame()._renderFence, true, TIMEOUT));
    // reset the deletion queue for the current frame
    get_current_frame()._deletionQueue.flush();
    get_current_frame()._frameDescriptors.clear_pools(_device);
    VK_CHECK(vkResetFences(_device, 1, &get_current_frame()._renderFence));
    // acquire next image from the swapchain
    uint32_t swapchainImageIndex;
    {
        auto result = vkAcquireNextImageKHR(_device, _swapchain, TIMEOUT, get_current_frame()._swapchainSemaphore, VK_NULL_HANDLE, &swapchainImageIndex);
        if (result == VK_ERROR_OUT_OF_DATE_KHR) {
			resize_requested = true;
            return;
        }
    }
#pragma endregion

#pragma region create command buffer
    VkCommandBuffer cmdBuffer = get_current_frame()._mainCommandBuffer;
    VK_CHECK(vkResetCommandBuffer(cmdBuffer, 0));
    // this is a one-time submit command buffer, so we use the VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT flag to get better performance
    VkCommandBufferBeginInfo cmdBeginInfo = vkinit::command_buffer_begin_info(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
    VK_CHECK(vkBeginCommandBuffer(cmdBuffer, &cmdBeginInfo));
#pragma endregion

    draw_recordRenderCmds(cmdBuffer, swapchainImageIndex);

#pragma region submit process
    VkCommandBufferSubmitInfo cmdinfo = vkinit::command_buffer_submit_info(cmdBuffer);
    VkSemaphoreSubmitInfo waitInfo = vkinit::semaphore_submit_info(VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT_KHR,
        get_current_frame()._swapchainSemaphore);
    VkSemaphoreSubmitInfo signalInfo = vkinit::semaphore_submit_info(VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT,
        get_current_frame()._renderSemaphore);
    VkSubmitInfo2 submit = vkinit::submit_info(&cmdinfo, &signalInfo, &waitInfo);
    VK_CHECK(vkQueueSubmit2(_graphicsQueue, 1, &submit, get_current_frame()._renderFence));
#pragma endregion

#pragma region presentation
    VkPresentInfoKHR presentinfo{
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .pNext = nullptr,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &get_current_frame()._renderSemaphore,
        .swapchainCount = 1,
        .pSwapchains = &_swapchain,
        .pImageIndices = &swapchainImageIndex,
    };
	auto result = vkQueuePresentKHR(_graphicsQueue, &presentinfo);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
        resize_requested = true;
    }
    else {
        VK_CHECK(result);
	}
#pragma endregion

    _frameNumber++;
}

void VulkanEngine::update_imgui()
{
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();

    if (ImGui::Begin("background")) {
        ComputePipelineObject& currentEffect = backgroundPipelines[currentBachgroundEffect];

        ImGui::Text("Current Effect: ", currentEffect.name);
        ImGui::SliderInt("Effect Index", &currentBachgroundEffect, 0, backgroundPipelines.size() - 1);
        ImGui::InputFloat4("data1", (float*)&currentEffect.data.data1);
        ImGui::InputFloat4("data2", (float*)&currentEffect.data.data2);
        ImGui::InputFloat4("data3", (float*)&currentEffect.data.data3);
        ImGui::InputFloat4("data4", (float*)&currentEffect.data.data4);
        ImGui::SliderFloat("Render Scale", &renderScale, 0.3f, 1.f);
        ImGui::End();
    }


    // calculate average stats from stats_queue
    if (ImGui::Begin("Latest Stats")) {
        ImGui::Text("Frame: %d", _frameNumber);
        ImGui::Text("Drawcalls: %d", stats.drawcall_count);
        ImGui::Text("Triangles: %d", stats.triangle_count);
        ImGui::Text("frame time: %.2f ms", stats.GetCurrentFrameTime());
        // print the sceme update time
        ImGui::Text("scene update time: %.2f ms", stats.GetCurrentSceneUpdateTime());
        ImGui::Text("FPS: %.2f", 1.0f / ImGui::GetIO().DeltaTime);
        ImGui::Text("mesh draw time: %.2f ms", stats.GetCurrentMeshDrawTime());
        ImGui::End();
    }

    if (ImGui::Begin("Average Stats")) {
        ImGui::Text("frame time: %.2f ms", stats.GetAverageFrameTime());
        // print the sceme update time
        ImGui::Text("scene update time: %.2f ms", stats.GetAverageSceneUpdateTime());
        ImGui::Text("FPS: %.2f", 1.0f / ImGui::GetIO().DeltaTime);
        ImGui::Text("mesh draw time: %.2f ms", stats.GetAverageMeshDrawTime());
        ImGui::End();
    }

    ImGui::ShowDemoWindow();
    ImGui::Render();
}

void VulkanEngine::draw_recordRenderCmds(VkCommandBuffer cmdBuffer, uint32_t swapchainImageIndex)
{
    _drawExtent.height = std::min(_swapchainExtent.height, _drawImage.imageExtent.height) * renderScale;
    _drawExtent.width = std::min(_swapchainExtent.width, _drawImage.imageExtent.width) * renderScale;

    vkutil::transition_image(cmdBuffer, _drawImage.image,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
    compute_background(cmdBuffer);

    vkutil::transition_image(cmdBuffer, _drawImage.image,
        VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    vkutil::transition_image(cmdBuffer, _depthImage.image,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);
    draw_geometry(cmdBuffer);

    // copy the draw image to the swapchain image
    vkutil::transition_image(cmdBuffer, _drawImage.image,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    vkutil::transition_image(cmdBuffer, _swapchainImages[swapchainImageIndex],
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    vkutil::copy_image_to_image(cmdBuffer, _drawImage.image, _swapchainImages[swapchainImageIndex],
        _drawExtent, _swapchainExtent);

    // set swapchain image layout to Attachment Optimal so we can draw it
    vkutil::transition_image(cmdBuffer, _swapchainImages[swapchainImageIndex],
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

    draw_imgui(cmdBuffer, _swapchainImageViews[swapchainImageIndex]);

    // set swapchain image layout to Present so we can draw it
    vkutil::transition_image(cmdBuffer, _swapchainImages[swapchainImageIndex],
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

    VK_CHECK(vkEndCommandBuffer(cmdBuffer));
}

void VulkanEngine::compute_background(VkCommandBuffer cmdBuffer)
{
    ComputePipelineObject& currentEffect = backgroundPipelines[currentBachgroundEffect];

    vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, currentEffect.pipeline);
    vkCmdBindDescriptorSets(cmdBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, currentEffect.pipelineLayout, 0, 1,
        &currentEffect.descriptorSet, 0, nullptr
    );
    vkCmdPushConstants(cmdBuffer, currentEffect.pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(ComputePushConstants), &currentEffect.data);
    vkCmdDispatch(cmdBuffer, std::ceil(_drawExtent.width / 16.0), std::ceil(_drawExtent.width / 16.0), 1);
}

void VulkanEngine::draw_geometry(VkCommandBuffer cmdBuffer)
{
    std::vector<uint32_t> opaque_draw_indices;
    opaque_draw_indices.reserve(mainDrawContext.OpaqueSurfaces.size());

    for (uint32_t i = 0; i < mainDrawContext.OpaqueSurfaces.size(); i++) {
        if (is_visible(mainDrawContext.OpaqueSurfaces[i], sceneData.viewproj))
        {
            opaque_draw_indices.push_back(i);
        }
    }

    // sort the opaque surfacrs by material and mesh
    std::sort(opaque_draw_indices.begin(), opaque_draw_indices.end(), [&](const auto& iA, const auto& iB){
        const RenderObject& A = mainDrawContext.OpaqueSurfaces[iA];
        const RenderObject& B = mainDrawContext.OpaqueSurfaces[iB];

        if (A.material == B.material) {
            return A.indexBuffer < B.indexBuffer;
        }
        else {
            return A.material < B.material;
        }
    });

    stats.drawcall_count = 0;
    stats.triangle_count = 0;
	auto start = std::chrono::high_resolution_clock::now();

    VkRenderingAttachmentInfo colorAttachment = vkinit::attachment_info(
        _drawImage.imageView,
        nullptr, // no clear value, we already cleared it in the background
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    VkRenderingAttachmentInfo depthAttachment = vkinit::depth_attachment_info(_depthImage.imageView, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);

    VkRenderingInfo renderInfo = vkinit::rendering_info(_drawExtent, &colorAttachment, &depthAttachment);
    vkCmdBeginRendering(cmdBuffer, &renderInfo);

#pragma region Scene Data Upload
    //allocate a new uniform buffer for the scene data
    AllocatedBuffer gpuSceneDataBuffer = create_buffer(sizeof(GPUSceneData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);

    //add it to the deletion queue of this frame so it gets deleted once its been used
    get_current_frame()._deletionQueue.push_function([=, this]() {
        destroy_buffer(gpuSceneDataBuffer);
        });

    //write the buffer
    GPUSceneData* sceneUniformData = (GPUSceneData*)gpuSceneDataBuffer.allocation->GetMappedData();
    *sceneUniformData = sceneData;

    //create a descriptor set that binds that buffer and update it
    VkDescriptorSet globalDescriptor = get_current_frame()._frameDescriptors.allocate(_device, _gpuSceneDataDescriptorLayout);
    {
        DescriptorWriter writer;
        writer.write_buffer(0, gpuSceneDataBuffer.buffer, sizeof(GPUSceneData), 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
        writer.update_set(_device, globalDescriptor);
    }
#pragma endregion


    MaterialPipeline* lastPipeline = nullptr;
    MaterialInstance* lastMaterial = nullptr;
    VkBuffer lastIndexBuffer = VK_NULL_HANDLE;

    auto drawLamda = [&](const RenderObject& renderObject) {

        if (renderObject.material != lastMaterial) 
        {
            lastMaterial = renderObject.material;
            // rebind pipeline and descriptor sets only if the material changed
            if (renderObject.material->pipeline != lastPipeline) 
            {
                lastPipeline = renderObject.material->pipeline;
                vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, renderObject.material->pipeline->pipeline);
                vkCmdBindDescriptorSets(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, renderObject.material->pipeline->layout, 0, 1, &globalDescriptor, 0, nullptr);
#pragma region setup viewport and scissor
                VkViewport viewport{
                    .x = 0.0f,
                    .y = 0.0f,
                    .width = (float)_drawExtent.width,
                    .height = (float)_drawExtent.height,
                    .minDepth = 0.0f,
                    .maxDepth = 1.0f
                };

                vkCmdSetViewport(cmdBuffer, 0, 1, &viewport);

                VkRect2D scissor{
                    .offset = { 0, 0 },
                    .extent = _drawExtent
                };

                vkCmdSetScissor(cmdBuffer, 0, 1, &scissor);
#pragma endregion
            }
            vkCmdBindDescriptorSets(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, renderObject.material->pipeline->layout, 1, 1, &renderObject.material->materialSet, 0, nullptr);
        }
        if (renderObject.indexBuffer != lastIndexBuffer) {
            lastIndexBuffer = renderObject.indexBuffer;
            vkCmdBindIndexBuffer(cmdBuffer, renderObject.indexBuffer, 0, VK_INDEX_TYPE_UINT32);
        }

        GPUDrawPushConstants push_constants{};
        push_constants.worldMatrix = renderObject.transform;
        push_constants.vertexBuffer = renderObject.vertexBufferAddress;
        vkCmdPushConstants(cmdBuffer, renderObject.material->pipeline->layout,
            VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(GPUDrawPushConstants), &push_constants);

        vkCmdDrawIndexed(cmdBuffer, renderObject.indexCount, 1, renderObject.firstIndex, 0, 0);

        stats.drawcall_count++;
        stats.triangle_count += renderObject.indexCount / 3;
        };

    for (const uint32_t& index : opaque_draw_indices) {
        drawLamda(mainDrawContext.OpaqueSurfaces[index]);
    }

    for (const RenderObject& r : mainDrawContext.TransparentSurfaces) {
        drawLamda(r);
    }
    vkCmdEndRendering(cmdBuffer);

	auto end = std::chrono::high_resolution_clock::now();
	auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    stats.add_mesh_draw_time(elapsed.count() / 1000.0f);
}

void VulkanEngine::update_scene()
{
    auto start = std::chrono::high_resolution_clock::now();

    mainDrawContext.OpaqueSurfaces.clear();
    mainDrawContext.TransparentSurfaces.clear();
    loadedScenes["structure"]->Draw(glm::mat4{ 1.f }, mainDrawContext);
    loadedNodes["Suzanne"]->Draw(glm::mat4{1.f}, mainDrawContext);
    for (int x = -3; x < 3; x++) {

        glm::mat4 scale = glm::scale(glm::vec3{ 0.2 });
        glm::mat4 translation = glm::translate(glm::vec3{ x, 1, 0 });

        loadedNodes["Cube"]->Draw(translation * scale, mainDrawContext);
    }

	// update camera matrices
    mainCamera.update();
    glm::mat4 viewMatrix = mainCamera.getViewMatrix();
    constexpr float fov = glm::radians(70.f);
    float aspect = (float)_windowExtent.width / _windowExtent.height;
    float farPlane = 0.1f;
    float nearPlane = 10000.f;
    glm::mat4 projection = glm::perspective(fov, aspect, nearPlane, farPlane);
    projection[1][1] *= -1; // vulkan inverts the Y axis so we need to invert our projection matrix

    sceneData.view = viewMatrix;
    sceneData.proj = projection;
    sceneData.viewproj = sceneData.proj * sceneData.view;

    sceneData.ambientColor = glm::vec4(0.1f);
    sceneData.sunlightColor = glm::vec4(0.5f);
    sceneData.sunlightDirection = glm::vec4(0, 1, 0.5f, 1);

	auto end = std::chrono::high_resolution_clock::now();
	auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
	stats.add_scene_update_time(elapsed.count() / 1000.0f);
}

void VulkanEngine::run()
{
    SDL_Event e;
    bool bQuit = false;

    // main loop
    while (!bQuit) {
        auto startTime = std::chrono::system_clock::now();

        // Handle events on queue
        while (SDL_PollEvent(&e) != 0) {
            // close the window when user alt-f4s or clicks the X button
            if (e.type == SDL_QUIT)
                bQuit = true;

            if (e.type == SDL_WINDOWEVENT) {
                if (e.window.event == SDL_WINDOWEVENT_MINIMIZED) {
                    stop_rendering = true;
                }
                if (e.window.event == SDL_WINDOWEVENT_RESTORED) {
                    stop_rendering = false;
                }
            }

            mainCamera.processSDLEvent(e);
			ImGui_ImplSDL2_ProcessEvent(&e);
        }

        if (resize_requested) {
            resize_swapchain();
		}

        // do not draw if we are minimized
        if (stop_rendering) {
            // throttle the speed to avoid the endless spinning
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        draw();

		auto endTime = std::chrono::system_clock::now();
		auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime);

        stats.add_frame_time(elapsed.count() / 1000.0f);
        stats.update_current_frame();
    }
}

void VulkanEngine::init_vulkan()
{

    vkb::InstanceBuilder builder;
    auto inst_ret = builder.set_app_name("Example Vulkan Application")
        .request_validation_layers(bUseValidationLayers)
        .use_default_debug_messenger()
        .require_api_version(1, 3, 0)
        .build();

    vkb::Instance vkb_inst = inst_ret.value();
    _instance = vkb_inst.instance;
    _debug_messenger = vkb_inst.debug_messenger;


    SDL_Vulkan_CreateSurface(_window, _instance, &_surface);

    // vulkan 1.3 features
    VkPhysicalDeviceVulkan13Features features{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
        .synchronization2 = true,
        .dynamicRendering = true,
    };
    // vulkan 1.2 features
    VkPhysicalDeviceVulkan12Features features12{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
        .descriptorIndexing = true,
        .bufferDeviceAddress = true,
    };

    // Select a physical device that supports the required features
    vkb::PhysicalDeviceSelector selector{ vkb_inst };
    vkb::PhysicalDevice physicalDevice = selector
        .set_minimum_version(1, 3)
        .set_required_features_13(features)
        .set_required_features_12(features12)
        .set_surface(_surface)
        .select()
        .value();

    // create the final vulkan device
    vkb::DeviceBuilder deviceBuilder{ physicalDevice };
    vkb::Device vkb_device = deviceBuilder.build().value();

    // assign the VKDevice handle used in the rest of a vulkan application
    _device = vkb_device.device;
    _chosenGPU = physicalDevice.physical_device;

    _graphicsQueue = vkb_device.get_queue(vkb::QueueType::graphics).value();
    _graphicsQueueFamily = vkb_device.get_queue_index(vkb::QueueType::graphics).value();

    VmaAllocatorCreateInfo allocatorInfo{
        .flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT,
        .physicalDevice = _chosenGPU,
        .device = _device,
        .instance = _instance,
    };
    vmaCreateAllocator(&allocatorInfo, &_allocator);

    _mainDeletionQueue.push_function([&]() {
        vmaDestroyAllocator(_allocator);
        });
}

void VulkanEngine::init_swapchain()
{
    VkExtent3D drawImageExtent{
        _windowExtent.width,
        _windowExtent.height,
        1,
    };

    _drawImage.imageFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
    _drawImage.imageExtent = drawImageExtent;

    VkImageUsageFlags drawImageUsages{};
    drawImageUsages |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    drawImageUsages |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    drawImageUsages |= VK_IMAGE_USAGE_STORAGE_BIT;
    drawImageUsages |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    VkImageCreateInfo rimg_info = vkinit::image_create_info(
        _drawImage.imageFormat, drawImageUsages, drawImageExtent
    );

    // for the draw image, we want to allocate it from gpu local memory, so we use VMA_MEMORY_USAGE_GPU_ONLY
    VmaAllocationCreateInfo rimg_allocinfo{
        .usage = VMA_MEMORY_USAGE_GPU_ONLY,
        .requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT),
    };

    VK_CHECK(vmaCreateImage(_allocator, &rimg_info, &rimg_allocinfo, &_drawImage.image, &_drawImage.allocation, nullptr));

    // build a image-view for the draw image to use for rendering
    VkImageViewCreateInfo rview_info = vkinit::imageview_create_info(
        _drawImage.imageFormat, _drawImage.image, VK_IMAGE_ASPECT_COLOR_BIT
    );
    VK_CHECK(vkCreateImageView(_device, &rview_info, nullptr, &_drawImage.imageView));



    create_swapchain(_windowExtent.width, _windowExtent.height);

    _depthImage.imageFormat = VK_FORMAT_D32_SFLOAT;
    _depthImage.imageExtent = drawImageExtent;
    VkImageUsageFlags depthImageUsages{};

    depthImageUsages |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;

    VkImageCreateInfo dimg_info = vkinit::image_create_info(
        _depthImage.imageFormat, depthImageUsages, _depthImage.imageExtent
	);

	VK_CHECK(vmaCreateImage(_allocator, &dimg_info, &rimg_allocinfo, &_depthImage.image, &_depthImage.allocation, nullptr));

    VkImageViewCreateInfo dview_info = vkinit::imageview_create_info(
        _depthImage.imageFormat, _depthImage.image, VK_IMAGE_ASPECT_DEPTH_BIT
	);

	VK_CHECK(vkCreateImageView(_device, &dview_info, nullptr, &_depthImage.imageView));
    
    _mainDeletionQueue.push_function([&]() {
        vmaDestroyImage(_allocator, _drawImage.image, _drawImage.allocation);
        vkDestroyImageView(_device, _drawImage.imageView, nullptr);
        vmaDestroyImage(_allocator, _depthImage.image, _depthImage.allocation);
        vkDestroyImageView(_device, _depthImage.imageView, nullptr);
        });

}

void VulkanEngine::init_commands()
{
    VkCommandPoolCreateInfo commandPoolInfo = vkinit::command_pool_create_info(_graphicsQueueFamily, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);

    for (int i = 0; i < FRAME_OVERLAP; i++) {
        VK_CHECK(vkCreateCommandPool(_device, &commandPoolInfo, nullptr, &_frames[i]._commandPool));
        VkCommandBufferAllocateInfo cmdAllocInfo = vkinit::command_buffer_allocate_info(_frames[i]._commandPool, 1);
        VK_CHECK(vkAllocateCommandBuffers(_device, &cmdAllocInfo, &_frames[i]._mainCommandBuffer));
    }

    VK_CHECK(vkCreateCommandPool(_device, &commandPoolInfo, nullptr, &_immCommandPool));

    VkCommandBufferAllocateInfo cmdAllocInfo = vkinit::command_buffer_allocate_info(_immCommandPool, 1);
    VK_CHECK(vkAllocateCommandBuffers(_device, &cmdAllocInfo, &_immCommandBuffer));

    _mainDeletionQueue.push_function([&]() {
        vkDestroyCommandPool(_device, _immCommandPool, nullptr);
        });
}

void VulkanEngine::init_sync_structures()
{
    VkFenceCreateInfo fenceCreateInfo = vkinit::fence_create_info(VK_FENCE_CREATE_SIGNALED_BIT);
    VkSemaphoreCreateInfo semaphoreCreateInfo = vkinit::semaphore_create_info();

    for (int i = 0; i < FRAME_OVERLAP; i++) {
        VK_CHECK(vkCreateFence(_device, &fenceCreateInfo, nullptr, &_frames[i]._renderFence));
        VK_CHECK(vkCreateSemaphore(_device, &semaphoreCreateInfo, nullptr, &_frames[i]._swapchainSemaphore));
        VK_CHECK(vkCreateSemaphore(_device, &semaphoreCreateInfo, nullptr, &_frames[i]._renderSemaphore));
    }

    VK_CHECK(vkCreateFence(_device, &fenceCreateInfo, nullptr, &_immFence));
    _mainDeletionQueue.push_function([&]() {
        vkDestroyFence(_device, _immFence, nullptr);
        });
}

void VulkanEngine::init_descriptors()
{

    std::vector<DescriptorAllocatorGrowable::PoolSizeRatio> sizes
    {
        {.type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, .ratio = 0.4f },
        {.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .ratio = 0.3f },
        {.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .ratio = 0.3f },
    };

    _globalDescriptorAllocator.init(_device, 10, sizes);

    {
        DescriptorLayoutBuilder builder{};
        builder.add_binding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
        _gpuSceneDataDescriptorLayout = builder.build(_device, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);
    }
    {
        DescriptorLayoutBuilder builder;
        builder.add_binding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
        _singleImageDescriptorlayout = builder.build(_device, VK_SHADER_STAGE_FRAGMENT_BIT);
    }
    _mainDeletionQueue.push_function([&]() {
        _globalDescriptorAllocator.destroy_pools(_device);
        //vkDestroyDescriptorSetLayout(_device, _drawImageDescriptorLayout, nullptr);
        vkDestroyDescriptorSetLayout(_device, _gpuSceneDataDescriptorLayout, nullptr);
        vkDestroyDescriptorSetLayout(_device, _singleImageDescriptorlayout, nullptr);
        });


    for (int i= 0; i < FRAME_OVERLAP; i++) {
        std::vector<DescriptorAllocatorGrowable::PoolSizeRatio> frame_sizes{
            {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 3},
			{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 3},
			{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 3},
			{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4},
        };
        _frames[i]._frameDescriptors = DescriptorAllocatorGrowable{};
		_frames[i]._frameDescriptors.init(_device, 1000, frame_sizes);

        _mainDeletionQueue.push_function([&, i]() {
            _frames[i]._frameDescriptors.destroy_pools(_device);
			});
	}
}

void VulkanEngine::init_pipelines()
{
    init_background_pipelines();

	metalRoughMaterial.build_pipelines(this);
    _mainDeletionQueue.push_function([&]() {
        metalRoughMaterial.clear_resources(_device);
		});
}

void VulkanEngine::init_create_resources() {

    testMeshes = loadGltfMeshes(this, PATH_MESH_MONKEY).value();

    VkExtent3D size = {
        1,1,1
	};
	VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;
    VkImageUsageFlagBits usage = VK_IMAGE_USAGE_SAMPLED_BIT;

	uint32_t white = glm::packUnorm4x8(glm::vec4(1.0f, 1.0f, 1.0f, 1.0f));
	uint32_t black = glm::packUnorm4x8(glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
	uint32_t gray = glm::packUnorm4x8(glm::vec4(0.66f, 0.66f, 0.66f, 1.0f));
	uint32_t magenta = glm::packUnorm4x8(glm::vec4(1.0f, 0.0f, 1.0f, 1.0f));
    _whiteImage = create_image((void*)&white, size, format,usage);
    _blackImage = create_image((void*)&black, size, format,usage);
    _greyImage = create_image((void*)&gray, size, format,usage);

    std::array<uint32_t, 16 * 16 > pixels; //for 16x16 checkerboard texture
    for (int x = 0; x < 16; x++) {
        for (int y = 0; y < 16; y++) {
            pixels[y * 16 + x] = ((x % 2) ^ (y % 2)) ? magenta : black;
        }
    }	
    _errorCheckerboardImage = create_image(pixels.data(), VkExtent3D{ 16, 16, 1 }, VK_FORMAT_R8G8B8A8_UNORM,
        VK_IMAGE_USAGE_SAMPLED_BIT);

    VkSamplerCreateInfo sampl = { .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
    sampl.magFilter = VK_FILTER_NEAREST;
    sampl.minFilter = VK_FILTER_NEAREST;
    vkCreateSampler(_device, &sampl, nullptr, &_defaultSamplerNearest);
    sampl.magFilter = VK_FILTER_LINEAR;
    sampl.minFilter = VK_FILTER_LINEAR;
    vkCreateSampler(_device, &sampl, nullptr, &_defaultSamplerLinear);

    GLTFMetallic_Roughness::MaterialResources materialResources{
        .colorImage = _whiteImage,
        .colorSampler = _defaultSamplerLinear,
        .metalRoughImage = _whiteImage,
        .metalRoughSampler = _defaultSamplerLinear,
    };

    AllocatedBuffer materialDataBuffer = create_buffer(
        sizeof(GLTFMetallic_Roughness::MaterialConstants),
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VMA_MEMORY_USAGE_CPU_TO_GPU);

    GLTFMetallic_Roughness::MaterialConstants* materialData =
        static_cast<GLTFMetallic_Roughness::MaterialConstants*> (materialDataBuffer.allocation->GetMappedData());
    materialData->colorFactors = glm::vec4(1, 1, 1, 1);
    materialData->metal_rough_factors = glm::vec4(1, 0.5, 0, 0);

    _mainDeletionQueue.push_function(
        [=, this]() {
			destroy_buffer(materialDataBuffer);
        }
    );

    materialResources.dataBuffer = materialDataBuffer.buffer;
    materialResources.dataBufferOffset = 0;

    defaultMaterial = metalRoughMaterial.write_material(
        _device, MaterialPass::MainColor, materialResources,
        _globalDescriptorAllocator
    );

    for (auto& m : testMeshes) {
        std::shared_ptr<MeshNode> newNode = std::make_shared<MeshNode>();
        newNode->mesh = m;

        newNode->localTransform = glm::mat4(1.f);
        newNode->worldTransform = glm::mat4(1.f);

        for (auto& s : newNode->mesh->surfaces) {
            s.material = std::make_shared<GLTFMaterial>();
            s.material->data = defaultMaterial;
        }

        loadedNodes[m->name] = std::move(newNode);
    }

	mainCamera.velocity = glm::vec3(0, 0, 0);
	mainCamera.position = glm::vec3(0, 0, 5);
	mainCamera.pitch = 0;
	mainCamera.yaw = 0;

    _mainDeletionQueue.push_function([&]() {
        vkDestroySampler(_device, _defaultSamplerNearest, nullptr);
        vkDestroySampler(_device, _defaultSamplerLinear, nullptr);
        destroy_image(_whiteImage);
        destroy_image(_greyImage);
        destroy_image(_blackImage);
        destroy_image(_errorCheckerboardImage);
        });
}

void VulkanEngine::init_background_pipelines()
{
    // create gradient compute pipeline
    VkShaderModule gradientShader{};
    if (!vkutil::load_shader_module(PATH_SHADER_COMP_GRADIENTCOLOR, _device, &gradientShader)) {
        return;
    }
    ComputePushConstants gradientData{
        .data1 = glm::vec4(0.0f, 0.0f, 1.0f, 1.0f),
        .data2 = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f),
    };
	ComputePipelineObject gradientEffect = create_compute_pipeline("gradient", gradientShader, gradientData);

    // create sky compute pipeline
    VkShaderModule skyShader{};
    if (!vkutil::load_shader_module(PATH_SHADER_COMP_SKY, _device, &skyShader)) {
        return;
    }
    ComputePushConstants skyData{
        .data1 = glm::vec4(0.1f, 0.2f, 0.4f, 0.97f),
    };
	ComputePipelineObject skyEffect = create_compute_pipeline("sky", skyShader, skyData);

	// store the effects
    backgroundPipelines.push_back(gradientEffect);
    backgroundPipelines.push_back(skyEffect);

    // add deletion of pipelines and pipeline layouts
    vkDestroyShaderModule(_device, gradientShader, nullptr);
    vkDestroyShaderModule(_device, skyShader, nullptr);
    _mainDeletionQueue.push_function([=]() {
        vkDestroyDescriptorSetLayout(_device, gradientEffect.descriptorSetLayout, nullptr);
        vkDestroyDescriptorSetLayout(_device, skyEffect.descriptorSetLayout, nullptr);
        vkDestroyPipelineLayout(_device, gradientEffect.pipelineLayout, nullptr);
        vkDestroyPipelineLayout(_device, skyEffect.pipelineLayout, nullptr);
        vkDestroyPipeline(_device, gradientEffect.pipeline, nullptr);
        vkDestroyPipeline(_device, skyEffect.pipeline, nullptr);
        });
}

void VulkanEngine::create_swapchain(uint32_t width, uint32_t height)
{
    vkb::SwapchainBuilder swapchainBuilder{ _chosenGPU, _device, _surface };
    _swapchainImageFormat = VK_FORMAT_B8G8R8A8_UNORM;

    vkb::Swapchain vkbSwapchain = swapchainBuilder
        .set_desired_format(VkSurfaceFormatKHR{
            .format = _swapchainImageFormat,
            .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR })
            .set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR)
        .set_desired_extent(width, height)
        .set_desired_min_image_count(FRAME_OVERLAP)
        .add_image_usage_flags(VK_IMAGE_USAGE_TRANSFER_DST_BIT)
        .build()
        .value();

    _swapchainExtent = vkbSwapchain.extent;
    // store swapchain and its related images
    _swapchain = vkbSwapchain.swapchain;
    _swapchainImages = vkbSwapchain.get_images().value();
    _swapchainImageViews = vkbSwapchain.get_image_views().value();
}

void VulkanEngine::destroy_swapchain()
{
    vkDestroySwapchainKHR(_device, _swapchain, nullptr);

    // destroy swapchain resources
    for (auto& imageView : _swapchainImageViews) {
        vkDestroyImageView(_device, imageView, nullptr);
    }
}

void VulkanEngine::immediate_submit(std::function<void(VkCommandBuffer cmd)>&& function)
{
	VK_CHECK(vkResetFences(_device, 1, &_immFence));
	VK_CHECK(vkResetCommandBuffer(_immCommandBuffer, 0));

	VkCommandBuffer cmdBuffer= _immCommandBuffer;
	VkCommandBufferBeginInfo cmdBeginInfo = vkinit::command_buffer_begin_info(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

	VK_CHECK(vkBeginCommandBuffer(cmdBuffer, &cmdBeginInfo));

	function(cmdBuffer);

	VK_CHECK(vkEndCommandBuffer(cmdBuffer));

	VkCommandBufferSubmitInfo commandbufferSubmitInfo = vkinit::command_buffer_submit_info(cmdBuffer);
	VkSubmitInfo2 submitInfo = vkinit::submit_info(&commandbufferSubmitInfo, nullptr, nullptr);

	VK_CHECK(vkQueueSubmit2(_graphicsQueue, 1, &submitInfo, _immFence));
	VK_CHECK(vkWaitForFences(_device, 1, &_immFence, true, TIMEOUT));
}

void VulkanEngine::init_imgui()
{
    VkDescriptorPoolSize poolSizes[] = {
        { VK_DESCRIPTOR_TYPE_SAMPLER, 1000 },
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
        { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
        { VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000 }
	};

    VkDescriptorPoolCreateInfo poo_info
    {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
		.pNext = nullptr,
		.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
		.maxSets = 1000,
		.poolSizeCount = static_cast<uint32_t>(IM_ARRAYSIZE(poolSizes)),
		.pPoolSizes = poolSizes
    };

    VkDescriptorPool imguiPool;;

	VK_CHECK(vkCreateDescriptorPool(_device, &poo_info, nullptr, &imguiPool));

	ImGui::CreateContext();

	ImGui_ImplSDL2_InitForVulkan(_window);

	ImGui_ImplVulkan_InitInfo init_info{
		.Instance = _instance,
        .PhysicalDevice = _chosenGPU, 
		.Device = _device,
		.Queue = _graphicsQueue,
		.DescriptorPool = imguiPool,
		.MinImageCount = FRAME_OVERLAP,
		.ImageCount = FRAME_OVERLAP,
        .MSAASamples = VK_SAMPLE_COUNT_1_BIT,
        .UseDynamicRendering = true,
    
        .PipelineRenderingCreateInfo = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
            .colorAttachmentCount = 1,
            .pColorAttachmentFormats = &_swapchainImageFormat,
		},

    };
	ImGui_ImplVulkan_Init(&init_info);
	ImGui_ImplVulkan_CreateFontsTexture();

    _mainDeletionQueue.push_function([=]() {
        ImGui_ImplVulkan_Shutdown();
        vkDestroyDescriptorPool(_device, imguiPool, nullptr);
		});
}

void VulkanEngine::draw_imgui(VkCommandBuffer cmd, VkImageView targetImageView)
{    
    VkRenderingAttachmentInfo colorAttachment = vkinit::attachment_info(targetImageView, nullptr, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    VkRenderingInfo renderInfo = vkinit::rendering_info(_swapchainExtent, &colorAttachment, nullptr);

    vkCmdBeginRendering(cmd, &renderInfo);

    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);

    vkCmdEndRendering(cmd);
}

AllocatedBuffer VulkanEngine::create_buffer(size_t allocSize, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage)
{
    // allocate buffer
    VkBufferCreateInfo bufferinfo{
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		.pNext = nullptr,
		.size = allocSize,
		.usage = usage,
    };

    VmaAllocationCreateInfo vmaallocInfo{
        .flags = VMA_ALLOCATION_CREATE_MAPPED_BIT,
        .usage = memoryUsage,
    };

	AllocatedBuffer buffer;
    VK_CHECK(vmaCreateBuffer(_allocator, &bufferinfo, &vmaallocInfo, &buffer.buffer, &buffer.allocation, &buffer.allocationInfo));
	return buffer;
}

void VulkanEngine::destroy_buffer(const AllocatedBuffer& buffer)
{
	vmaDestroyBuffer(_allocator, buffer.buffer, buffer.allocation);
}

void VulkanEngine::resize_swapchain()
{

	vkDeviceWaitIdle(_device);

    destroy_swapchain();

    int w, h;
    SDL_GetWindowSize(_window, &w, &h);
	_windowExtent.width = w;
    _windowExtent.height = h;
    // recreate the swapchain
    create_swapchain(_windowExtent.width, _windowExtent.height);
	resize_requested = false;
}

ComputePipelineObject VulkanEngine::create_compute_pipeline(const char* name, VkShaderModule shaderModule, ComputePushConstants data)
{
	// create local references to global objects to make the code cleaner
	const auto& device = _device;
    const auto& drawImage = _drawImage;
    auto& allocator = _globalDescriptorAllocator;

    // create descriptor set layout
    DescriptorLayoutBuilder builder{};
    builder.add_binding(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
    VkDescriptorSetLayout setlayout = builder.build(_device, VK_SHADER_STAGE_COMPUTE_BIT);

    // create descriptor set
    VkDescriptorSet descriptor = allocator.allocate(device, setlayout);
    DescriptorWriter writer{};
    writer.write_Image(0, drawImage.imageView, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
    writer.update_set(device, descriptor);

    // create pipeline layout
    VkPipelineLayout computePipelineLayout;
    VkPushConstantRange pushConstant{
        .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
        .offset = 0,
        .size = sizeof(ComputePushConstants),
    };
    VkPipelineLayoutCreateInfo computeLayoutCreateInfo
    {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .pNext = nullptr,
        .setLayoutCount = 1,
        .pSetLayouts = &setlayout,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &pushConstant,
    };
    VK_CHECK(vkCreatePipelineLayout(device, &computeLayoutCreateInfo, nullptr, &computePipelineLayout));

    // create pipeline
    VkPipelineShaderStageCreateInfo stageInfo
    {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .pNext = nullptr,
        .stage = VK_SHADER_STAGE_COMPUTE_BIT,
        .module = shaderModule,
        .pName = "main",
    };
    VkComputePipelineCreateInfo cpCreateInfo{
        .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .pNext = nullptr,
        .stage = stageInfo,
        .layout = computePipelineLayout,
    };
    VkPipeline pipeline{};
    VK_CHECK(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &cpCreateInfo, nullptr, &pipeline));

    // return the result
    return ComputePipelineObject{
        .name = name,
		.pipeline = pipeline,
        .pipelineLayout = cpCreateInfo.layout,
        .descriptorSet = descriptor,
        .descriptorSetLayout = setlayout,
        .data = data,
    };
}

AllocatedImage VulkanEngine::create_image(VkExtent3D size, VkFormat format, VkImageUsageFlags usage, bool mipmapped)
{
    AllocatedImage newImage{
        .imageExtent = size,
        .imageFormat = format,
	};

    VkImageCreateInfo img_info = vkinit::image_create_info(
        format,
        usage,
        size
	);

    if (mipmapped) {
        img_info.mipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(size.width, size.height)))) + 1;
	}

    VmaAllocationCreateInfo allocinfo{
		.usage = VMA_MEMORY_USAGE_GPU_ONLY,
		// use Vk_MEMORY_PROPERTY_DEVICE_LOCAL_BIT flag to ensure the image is in GPU local memory
		.requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT),
	};

	VK_CHECK(vmaCreateImage(_allocator, &img_info, &allocinfo, &newImage.image, &newImage.allocation, nullptr));


	VkImageAspectFlags aspectFlag = VK_IMAGE_ASPECT_COLOR_BIT;

    if (format == VK_FORMAT_D32_SFLOAT) {
		aspectFlag = VK_IMAGE_ASPECT_DEPTH_BIT;
	}
    
    VkImageViewCreateInfo view_info = vkinit::imageview_create_info(
        format,
        newImage.image,
        aspectFlag);
    view_info.subresourceRange.levelCount = img_info.mipLevels;

    VK_CHECK(vkCreateImageView(_device, &view_info, nullptr, &newImage.imageView));

    return newImage;
}

AllocatedImage VulkanEngine::create_image(void* data, VkExtent3D size, VkFormat format, VkImageUsageFlags usage, bool mipmapped)
{

    size_t data_size = size.depth * size.width * size.height * 4;
	AllocatedBuffer uploadbuffer =  create_buffer(data_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY);
    
    memcpy(uploadbuffer.allocationInfo.pMappedData, data, data_size);
	AllocatedImage newImage = create_image(size, format, 
        usage | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        mipmapped);

    immediate_submit([&](VkCommandBuffer cmd) {
        vkutil::transition_image(cmd, newImage.image,
            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

        VkBufferImageCopy copyRegion{
		    .bufferOffset = 0,
		    .bufferRowLength = 0,
		    .bufferImageHeight = 0,
        };
        copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copyRegion.imageSubresource.mipLevel = 0;
        copyRegion.imageSubresource.baseArrayLayer = 0;
		copyRegion.imageSubresource.layerCount = 1;
        copyRegion.imageExtent = size;

        vkCmdCopyBufferToImage(cmd, uploadbuffer.buffer, newImage.image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1, &copyRegion);

        if (mipmapped) {
            vkutil::generate_mipmaps(cmd, newImage.image, VkExtent2D(newImage.imageExtent.width, newImage.imageExtent.height));
        }
        else {
            vkutil::transition_image(cmd, newImage.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        }
    });
	destroy_buffer(uploadbuffer);
    return newImage;
}

void VulkanEngine::destroy_image(const AllocatedImage& image)
{
    vkDestroyImageView(_device, image.imageView, nullptr);
    vmaDestroyImage(_allocator, image.image, image.allocation);
}

GPUMeshBuffers VulkanEngine::uploadMesh(std::span<uint32_t> indices, std::span<Vertex> vertices)
{
	const size_t vertexBufferSize = vertices.size() * sizeof(Vertex);
	const size_t indexBufferSize = indices.size() * sizeof(uint32_t);

    GPUMeshBuffers newSurface{};

	newSurface.vertexBuffer = create_buffer(
        vertexBufferSize,
		VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        VMA_MEMORY_USAGE_GPU_ONLY);


    VkBufferDeviceAddressInfo deviceAddressInfo{
        .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
		.buffer = newSurface.vertexBuffer.buffer,
    };
	newSurface.vertexBufferAddress = vkGetBufferDeviceAddress(_device, &deviceAddressInfo);


    newSurface.indexBuffer = create_buffer(
        indexBufferSize,
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VMA_MEMORY_USAGE_GPU_ONLY
    );

    AllocatedBuffer staging = create_buffer(
        vertexBufferSize + indexBufferSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VMA_MEMORY_USAGE_CPU_ONLY
    );

    void* data = staging.allocation->GetMappedData();

	memcpy((uint8_t*)data, vertices.data(), vertexBufferSize);
	memcpy((uint8_t*)data + vertexBufferSize, indices.data(), indexBufferSize);

    immediate_submit([&](VkCommandBuffer cmd) {
        VkBufferCopy vertexCopy{
            .srcOffset = 0,
            .dstOffset = 0,
            .size = vertexBufferSize,
        };
        vkCmdCopyBuffer(cmd, staging.buffer, newSurface.vertexBuffer.buffer, 1, &vertexCopy);
        VkBufferCopy indexCopy{
            .srcOffset = vertexBufferSize,
            .dstOffset = 0,
            .size = indexBufferSize,
        };
        vkCmdCopyBuffer(cmd, staging.buffer, newSurface.indexBuffer.buffer, 1, &indexCopy);
    });

    destroy_buffer(staging);

	return newSurface;
}

void GLTFMetallic_Roughness::build_pipelines(VulkanEngine* engine)
{
    // Load Shader Modules
    VkShaderModule meshFragShader;
    vkutil::load_shader_module(PATH_SHADER_FRAG_MESH, engine->_device, &meshFragShader);
    VkShaderModule meshVertexShader;
    vkutil::load_shader_module(PATH_SHADER_VERT_MESH, engine->_device, &meshVertexShader);

    // Create DescriptorLayout
    VkPushConstantRange matrixRange[] = {
        {
            .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
            .offset = 0,
            .size = sizeof(GPUDrawPushConstants),
        }
    };

    DescriptorLayoutBuilder layoutbuilder;
    layoutbuilder.add_binding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
    layoutbuilder.add_binding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    layoutbuilder.add_binding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);

    materialLayout = layoutbuilder.build(engine->_device, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);

    VkDescriptorSetLayout layouts[] = {
        engine->_gpuSceneDataDescriptorLayout,
        materialLayout,
        //engine->_singleImageDescriptorlayout,
    };

    VkPipelineLayoutCreateInfo mesh_layout_info = vkinit::pipeline_layout_create_info();
    mesh_layout_info.setLayoutCount = 2;
    mesh_layout_info.pSetLayouts = layouts;
    mesh_layout_info.pPushConstantRanges = matrixRange;
    mesh_layout_info.pushConstantRangeCount = 1;

    VkPipelineLayout newlayout;
    VK_CHECK(vkCreatePipelineLayout(engine->_device, &mesh_layout_info, nullptr, &newlayout));

    opaquePipeline.layout = newlayout;
    transparentPipeline.layout = newlayout;

    vkutil::PipelineBuilder pipelineBuilder;
    pipelineBuilder.set_shaders(meshVertexShader, meshFragShader);
    pipelineBuilder.set_input_topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    pipelineBuilder.set_polygon_mode(VK_POLYGON_MODE_FILL);
    pipelineBuilder.set_cull_mode(VK_CULL_MODE_NONE, VK_FRONT_FACE_CLOCKWISE);
    pipelineBuilder.set_multisampling_none();
    pipelineBuilder.disable_blending();
    pipelineBuilder.enable_depthtest(true, VK_COMPARE_OP_GREATER_OR_EQUAL);

    pipelineBuilder.set_color_attachment_format(engine->_drawImage.imageFormat);
    pipelineBuilder.set_depth_format(engine->_depthImage.imageFormat);

    pipelineBuilder._pipelineLayout = newlayout;
    opaquePipeline.pipeline = pipelineBuilder.build_pipeline(engine->_device);


    pipelineBuilder.enable_blending_additive();
    pipelineBuilder.enable_depthtest(false, VK_COMPARE_OP_GREATER_OR_EQUAL);
    transparentPipeline.pipeline = pipelineBuilder.build_pipeline(engine->_device);


    vkDestroyShaderModule(engine->_device, meshFragShader, nullptr);
    vkDestroyShaderModule(engine->_device, meshVertexShader, nullptr);

}

void GLTFMetallic_Roughness::clear_resources(VkDevice device)
{
    vkDestroyPipelineLayout(device, opaquePipeline.layout, nullptr);
    vkDestroyPipeline(device, opaquePipeline.pipeline, nullptr);
    vkDestroyPipeline(device, transparentPipeline.pipeline, nullptr);
	vkDestroyDescriptorSetLayout(device, materialLayout, nullptr);
}

MaterialInstance GLTFMetallic_Roughness::write_material(VkDevice device, MaterialPass pass, const MaterialResources& resources, DescriptorAllocatorGrowable& descriptorAllocator)
{

    MaterialInstance matData;
    matData.passType = pass;
    if (pass == MaterialPass::Transparent)
    {
        matData.pipeline = &transparentPipeline;
    }
    else {
        matData.pipeline = &opaquePipeline;
    }

    matData.materialSet = descriptorAllocator.allocate(device, materialLayout);

    writer.clear();
    writer.write_buffer(
        0, resources.dataBuffer, 
        sizeof(MaterialConstants), resources.dataBufferOffset,
        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER
    );
    writer.write_Image(
        1, resources.colorImage.imageView, 
        resources.colorSampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER
    );

    writer.write_Image(
        2, resources.metalRoughImage.imageView,
        resources.metalRoughSampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER
    );
	writer.update_set(device, matData.materialSet);
    return matData;
}

void MeshNode::Draw(const glm::mat4& topMatrix, DrawContext& ctx)
{
    glm::mat4 nodeMatrix = topMatrix * worldTransform;

    for (auto& s : mesh->surfaces) {
        RenderObject  def;
        def.indexCount = s.count;
		def.firstIndex = s.startIndex;
        def.indexBuffer = mesh->meshBuffers.indexBuffer.buffer;
        def.material = &s.material->data;
        def.bounds = s.bounds;
        def.transform = nodeMatrix;
        def.vertexBufferAddress = mesh->meshBuffers.vertexBufferAddress;

        if (s.material->data.passType == MaterialPass::Transparent) {
            ctx.TransparentSurfaces.push_back(def);
        }
        else {
            ctx.OpaqueSurfaces.push_back(def);
        }
    }

    Node::Draw(topMatrix, ctx);
}
