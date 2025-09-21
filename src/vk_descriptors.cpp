#include <vk_descriptors.h>

void DescriptorLayoutBuilder::add_binding(uint32_t binding, VkDescriptorType type)
{
	VkDescriptorSetLayoutBinding newbind
	{
		.binding = binding,
		.descriptorType = type,
		.descriptorCount = 1,
	};
	bindings.push_back(newbind);
}

void DescriptorLayoutBuilder::clear()
{
	bindings.clear();
}

VkDescriptorSetLayout DescriptorLayoutBuilder::build(VkDevice device, VkShaderStageFlags shaderStages, void* pNext, VkDescriptorSetLayoutCreateFlags flags)
{
	for (auto& binding : bindings)
	{
		binding.stageFlags |= shaderStages;
	}
	VkDescriptorSetLayoutCreateInfo info 
	{
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.pNext = pNext,
		.flags = flags,
		.bindingCount = static_cast<uint32_t>(bindings.size()),
		.pBindings = bindings.data(),
	};

	VkDescriptorSetLayout setlayout{};

	VK_CHECK(vkCreateDescriptorSetLayout(device, &info, nullptr, &setlayout));

	return setlayout;
}

void DescriptorAllocator::init_pool(VkDevice device, uint32_t maxSets, std::span<PoolSizeRatio> poolRatios)
{
	std::vector<VkDescriptorPoolSize> poolSizes;

	for (const auto& ratio : poolRatios) 
	{
		poolSizes.push_back(
			VkDescriptorPoolSize{
			.type = ratio.type,
			.descriptorCount = static_cast<uint32_t>(maxSets * ratio.ratio),
		});
	}

	VkDescriptorPoolCreateInfo poolInfo
	{
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
		.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT,
		.maxSets = maxSets,	// maximum number of descriptor sets that can be allocated from this pool
		.poolSizeCount = static_cast<uint32_t>(poolSizes.size()), // pool size count means how many different types of descriptors we can allocate
		.pPoolSizes = poolSizes.data(),
	};

	vkCreateDescriptorPool(device, &poolInfo, nullptr, &pool);
}

void DescriptorAllocator::clear_descriptors(VkDevice device)
{
	vkResetDescriptorPool(device, pool, 0);
}

void DescriptorAllocator::destroy_pool(VkDevice device)
{
	vkDestroyDescriptorPool(device, pool, nullptr);
}

VkDescriptorSet DescriptorAllocator::allocate(VkDevice device, VkDescriptorSetLayout layout) const
{

	VkDescriptorSetAllocateInfo allocInfo
	{
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
		.pNext = nullptr,
		.descriptorPool = pool,
		.descriptorSetCount = 1,
		.pSetLayouts = &layout,
	};

	VkDescriptorSet ds;
	VK_CHECK(vkAllocateDescriptorSets(device, &allocInfo, &ds));
	return ds;
}

void DescriptorAllocatorGrowable::init(VkDevice device, uint32_t maxSets, std::span<PoolSizeRatio> poolRatios)
{
	ratios.clear();

	for (auto r : poolRatios) {
		ratios.push_back(r);
	}

	VkDescriptorPool newPool = create_pool(device, maxSets, poolRatios);

	setsPerPool = maxSets * 1.5;

	readyPools.push_back(newPool);
}

void DescriptorAllocatorGrowable::clear_pools(VkDevice device)
{

	for (auto p : readyPools) {

		vkResetDescriptorPool(device, p, 0);
	}

	for (auto p : fullPools) {
		vkResetDescriptorPool(device, p, 0);
		readyPools.push_back(p);
	}

	fullPools.clear();
}

void DescriptorAllocatorGrowable::destroy_pools(VkDevice device)
{
	for (auto p : readyPools) {
		vkDestroyDescriptorPool(device, p, nullptr);
	}
	for (auto p : fullPools) {
		vkDestroyDescriptorPool(device, p, nullptr);
	}
	readyPools.clear();
	fullPools.clear();
}

VkDescriptorSet DescriptorAllocatorGrowable::allocate(VkDevice device, VkDescriptorSetLayout layout, void* pNext)
{

	VkDescriptorPool poolToUse = get_pool(device);

	VkDescriptorSetAllocateInfo allocInfo{
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
		.pNext = pNext,
		.descriptorPool = poolToUse,
		.descriptorSetCount = 1,
		.pSetLayouts = &layout,
	};

	VkDescriptorSet ds;
	VkResult result = vkAllocateDescriptorSets(device, &allocInfo, &ds);

	if (result == VK_ERROR_OUT_OF_POOL_MEMORY || result == VK_ERROR_FRAGMENTED_POOL) {
		fullPools.push_back(poolToUse);
		poolToUse = get_pool(device);
		allocInfo.descriptorPool = poolToUse;

		// Retry allocation with the new pool
		// If the second try fails, we will throw an error
		VK_CHECK(vkAllocateDescriptorSets(device, &allocInfo, &ds));
	}

	readyPools.push_back(poolToUse);
	return ds;
}

VkDescriptorPool DescriptorAllocatorGrowable::get_pool(VkDevice device)
{
	VkDescriptorPool newPool;
	if (readyPools.size() != 0) {
		newPool = readyPools.back();
		readyPools.pop_back();
	}
	else {
		//need to create a new pool
		newPool = create_pool(device, setsPerPool, ratios);

		setsPerPool = setsPerPool * 1.5;
		if (setsPerPool > 4092) {
			setsPerPool = 4092;
		}
	}

	return newPool;
}

VkDescriptorPool DescriptorAllocatorGrowable::create_pool(VkDevice device, uint32_t setCount, std::span<PoolSizeRatio> poolRatios)
{
	std::vector<VkDescriptorPoolSize> poolSizes;
	// Create the pool sizes based on the ratios provided
	// the ratio is the number of descriptors of that type per set
	for (PoolSizeRatio ratio : poolRatios) {
		poolSizes.push_back(
			VkDescriptorPoolSize{
				.type = ratio.type,
				// the number of descriptors of this type in the pool
				.descriptorCount = static_cast<uint32_t>(setCount * ratio.ratio),
			}
		);
	}
	VkDescriptorPoolCreateInfo poolInfo{
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
		.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT,
		.maxSets = setCount,
		.poolSizeCount = static_cast<uint32_t>(poolSizes.size()),
		.pPoolSizes = poolSizes.data(),
	};

	VkDescriptorPool newPool{};
	vkCreateDescriptorPool(device, &poolInfo, nullptr, &newPool);
	return newPool;
}

void DescriptorWriter::write_Image(int binding, VkImageView image, VkSampler sampler, VkImageLayout layout, VkDescriptorType type)
{
	VkDescriptorImageInfo& info = imageInfos.emplace_back(VkDescriptorImageInfo{
		.sampler = sampler,
		.imageView = image,
		.imageLayout = layout,
		});

	VkWriteDescriptorSet write{
		.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
		.dstSet = VK_NULL_HANDLE, // This will be set later
		.dstBinding = static_cast<uint32_t>(binding),
		.dstArrayElement = 0,		.descriptorCount = 1,
		.descriptorType = type,
		.pImageInfo = &info,
	};

	writes.push_back(write);
}

void DescriptorWriter::write_buffer(int binding, VkBuffer buffer, size_t size, size_t offset, VkDescriptorType type)
{
	VkDescriptorBufferInfo& info = bufferInfos.emplace_back(VkDescriptorBufferInfo{
		.buffer= buffer,
		.offset = offset,
		.range = size,
		});

	VkWriteDescriptorSet write{
		.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
		.dstSet = VK_NULL_HANDLE, // This will be set later
		.dstBinding = static_cast<uint32_t>(binding),
		.dstArrayElement = 0,
		.descriptorCount = 1,
		.descriptorType = type,
		.pBufferInfo = &info,
	};

	writes.push_back(write);
}

void DescriptorWriter::clear() 
{
	imageInfos.clear();
	writes.clear();
	bufferInfos.clear();
}
void DescriptorWriter::update_set(VkDevice device, VkDescriptorSet set)
{
	for (VkWriteDescriptorSet& write : writes) {
		write.dstSet = set;
	}

	vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
}
