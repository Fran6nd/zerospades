/*
 Copyright (c) 2013 yvt

 This file is part of OpenSpades.

 OpenSpades is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 OpenSpades is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with OpenSpades.  If not, see <http://www.gnu.org/licenses/>.

 */

#include "VulkanBuffer.h"
#include <Gui/SDLVulkanDevice.h>
#include <Core/Debug.h>
#include <Core/Exception.h>
#include <cstring>

namespace spades {
	namespace draw {

		VulkanBuffer::VulkanBuffer(Handle<gui::SDLVulkanDevice> dev, VkDeviceSize bufferSize,
		                           VkBufferUsageFlags bufferUsage, VkMemoryPropertyFlags memProperties)
		: device(std::move(dev)),
		  buffer(VK_NULL_HANDLE),
		  memory(VK_NULL_HANDLE),
		  size(bufferSize),
		  usage(bufferUsage),
		  properties(memProperties),
		  mappedData(nullptr) {

			SPADES_MARK_FUNCTION();

			VkDevice vkDevice = device->GetDevice();

			// Create buffer
			VkBufferCreateInfo bufferInfo{};
			bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
			bufferInfo.size = size;
			bufferInfo.usage = usage;
			bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

			VkResult result = vkCreateBuffer(vkDevice, &bufferInfo, nullptr, &buffer);
			if (result != VK_SUCCESS) {
				SPRaise("Failed to create Vulkan buffer (error code: %d)", result);
			}

			// Allocate memory
			VkMemoryRequirements memRequirements;
			vkGetBufferMemoryRequirements(vkDevice, buffer, &memRequirements);

			VkMemoryAllocateInfo allocInfo{};
			allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
			allocInfo.allocationSize = memRequirements.size;
			allocInfo.memoryTypeIndex = FindMemoryType(memRequirements.memoryTypeBits, properties);

			result = vkAllocateMemory(vkDevice, &allocInfo, nullptr, &memory);
			if (result != VK_SUCCESS) {
				vkDestroyBuffer(vkDevice, buffer, nullptr);
				SPRaise("Failed to allocate Vulkan buffer memory (error code: %d)", result);
			}

			// Bind buffer memory
			vkBindBufferMemory(vkDevice, buffer, memory, 0);
		}

		VulkanBuffer::~VulkanBuffer() {
			SPADES_MARK_FUNCTION();

			VkDevice vkDevice = device->GetDevice();

			if (mappedData) {
				Unmap();
			}

			if (buffer != VK_NULL_HANDLE) {
				vkDestroyBuffer(vkDevice, buffer, nullptr);
			}

			if (memory != VK_NULL_HANDLE) {
				vkFreeMemory(vkDevice, memory, nullptr);
			}
		}

		uint32_t VulkanBuffer::FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags props) {
			VkPhysicalDeviceMemoryProperties memProperties;
			vkGetPhysicalDeviceMemoryProperties(device->GetPhysicalDevice(), &memProperties);

			for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
				if ((typeFilter & (1 << i)) &&
				    (memProperties.memoryTypes[i].propertyFlags & props) == props) {
					return i;
				}
			}

			SPRaise("Failed to find suitable memory type");
		}

		void* VulkanBuffer::Map() {
			if (mappedData) {
				return mappedData;
			}

			VkResult result = vkMapMemory(device->GetDevice(), memory, 0, size, 0, &mappedData);
			if (result != VK_SUCCESS) {
				SPRaise("Failed to map buffer memory (error code: %d)", result);
			}

			return mappedData;
		}

		void VulkanBuffer::Unmap() {
			if (mappedData) {
				vkUnmapMemory(device->GetDevice(), memory);
				mappedData = nullptr;
			}
		}

		void VulkanBuffer::UpdateData(const void* data, VkDeviceSize dataSize, VkDeviceSize offset) {
			if (offset + dataSize > size) {
				SPRaise("Buffer update exceeds buffer size");
			}

			void* mapped = Map();
			std::memcpy(static_cast<char*>(mapped) + offset, data, dataSize);

			// Flush memory if not coherent to make writes visible to GPU
			if (!(properties & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
				VkMappedMemoryRange range{};
				range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
				range.memory = memory;
				range.offset = offset;
				range.size = dataSize;
				vkFlushMappedMemoryRanges(device->GetDevice(), 1, &range);
			}

			Unmap();
		}

		void VulkanBuffer::CopyFrom(VulkanBuffer& srcBuffer, VkCommandBuffer commandBuffer,
		                            VkDeviceSize srcOffset, VkDeviceSize dstOffset, VkDeviceSize copySize) {
			if (copySize == VK_WHOLE_SIZE) {
				copySize = srcBuffer.GetSize();
			}

			VkBufferCopy copyRegion{};
			copyRegion.srcOffset = srcOffset;
			copyRegion.dstOffset = dstOffset;
			copyRegion.size = copySize;

			vkCmdCopyBuffer(commandBuffer, srcBuffer.GetBuffer(), buffer, 1, &copyRegion);
		}

	} // namespace draw
} // namespace spades
