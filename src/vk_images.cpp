#include <vk_images.h>
#include "vk_initializers.h"

void vkutil::generate_mipmaps(VkCommandBuffer cmd, VkImage image, VkExtent2D imageSize)
{
	int mipLevels = int(std::floor(std::log2(std::max(imageSize.width, imageSize.height)))) + 1;

	for (int mip = 0; mip < mipLevels; mip++)
	{
		VkExtent2D halfSize{ imageSize.width / 2, imageSize.height / 2 };

#pragma region transition current mip level to transfer dst
		VkImageMemoryBarrier2 imageBarrier{
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
			.pNext = nullptr,
			.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
			.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT,
			.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
			.dstAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_MEMORY_READ_BIT,
			.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
		};
		VkImageAspectFlags aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		imageBarrier.subresourceRange = vkinit::image_subresource_range(aspectMask);
		imageBarrier.subresourceRange.levelCount = 1;
		imageBarrier.subresourceRange.baseMipLevel = mip;
		imageBarrier.image = image;
		VkDependencyInfo depInfo{
			.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
			.pNext = nullptr,
			.imageMemoryBarrierCount = 1,
			.pImageMemoryBarriers = &imageBarrier,
		};
		vkCmdPipelineBarrier2(cmd, &depInfo);
#pragma endregion
		if (mip < mipLevels - 1)
		{
			VkImageBlit2 blitRegion{
				.sType = VK_STRUCTURE_TYPE_IMAGE_BLIT_2,
				.pNext = nullptr,
				.srcSubresource = {
					.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
					.mipLevel = static_cast<uint32_t>(mip),
					.baseArrayLayer = 0,
					.layerCount = 1,
				},
				.srcOffsets = {
					{0, 0, 0},
					{static_cast<int32_t>(imageSize.width), static_cast<int32_t>(imageSize.height), 1},
				},
				.dstSubresource = {
					.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
					.mipLevel = static_cast<uint32_t>(mip + 1),
					.baseArrayLayer = 0,
					.layerCount = 1,
				},
				.dstOffsets = {
					{0, 0, 0},
					{static_cast<int32_t>(halfSize.width), static_cast<int32_t>(halfSize.height), 1},
				},
			};
			VkBlitImageInfo2 blitInfo{
				.sType = VK_STRUCTURE_TYPE_BLIT_IMAGE_INFO_2,
				.srcImage = image,
				.srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				.dstImage = image,
				.dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				.regionCount = 1,
				.pRegions = &blitRegion,
				.filter = VK_FILTER_LINEAR,
			};
			vkCmdBlitImage2(cmd, &blitInfo);

			imageSize = halfSize;
		}
	}


	// transition all mip levels into the final read_only layout
	transition_image(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
}

void vkutil::transition_image(VkCommandBuffer cmd, VkImage image, VkImageLayout currentLayout, VkImageLayout newLayout)
{
	VkImageMemoryBarrier2 imageBarrier{
		.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
		.pNext = nullptr,
		.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
		.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT,
		.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
		.dstAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_MEMORY_READ_BIT,
		.oldLayout = currentLayout,
		.newLayout = newLayout,
	};

	VkImageAspectFlags aspectMask = (newLayout == VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL) ?
		VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
	imageBarrier.subresourceRange = vkinit::image_subresource_range(aspectMask);
	imageBarrier.image = image;

	VkDependencyInfo depInfo{
		.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
		.pNext = nullptr,
		.imageMemoryBarrierCount = 1,
		.pImageMemoryBarriers = &imageBarrier
	};

	vkCmdPipelineBarrier2(cmd, &depInfo);
}

void vkutil::copy_image_to_image(VkCommandBuffer cmd, VkImage srcImage, VkImage dstImage, VkExtent2D srcSize, VkExtent2D dstSize)
{
	VkImageBlit2  blitRegion{
		.sType = VK_STRUCTURE_TYPE_IMAGE_BLIT_2,
		.pNext = nullptr,
		.srcSubresource = {
			.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.mipLevel = 0,
			.baseArrayLayer = 0,
			.layerCount = 1
		},
		.srcOffsets = {
			{ 0, 0, 0 }, // src offset min
			{ static_cast<int32_t>(srcSize.width), static_cast<int32_t>(srcSize.height), 1 } // src offset max
		},
		.dstSubresource = {
			.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.mipLevel = 0,
			.baseArrayLayer = 0,
			.layerCount = 1
		},
		.dstOffsets = {
			{ 0, 0, 0 }, // dst offset min
			{ static_cast<int32_t>(dstSize.width), static_cast<int32_t>(dstSize.height), 1 } // dst offset max
		},
	};

	VkBlitImageInfo2 blitInfo{
		.sType = VK_STRUCTURE_TYPE_BLIT_IMAGE_INFO_2,
		.pNext = nullptr,
		.srcImage = srcImage,
		.srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
		.dstImage = dstImage,
		.dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		.regionCount = 1,
		.pRegions = &blitRegion
	};
	vkCmdBlitImage2(cmd, &blitInfo);
}

