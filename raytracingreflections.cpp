#include "VulkanRaytracingSample.h"
#include "VulkanglTFModel.h"

#include <vector>
using std::vector;

#include <set>
using std::set;

#include <string>
using std::string;

#include <fstream>
using std::ofstream;

#include <ios>
using std::ios;

#include <mutex>
using std::mutex;

#include <sstream>
using std::ostringstream;


#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>


#include <OpenImageDenoise/oidn.hpp>
#pragma comment(lib, "OpenImageDenoise")


// https://manual.notch.one/0.9.23/en/docs/faq/extending-gpu-timeout-detection/




size_t tri_count = 0;
size_t light_tri_count = 0;
mutex m;

tinygltf::Model model;
std::vector<uint32_t> indexBuffer;
std::vector<vkglTF::Vertex> vertexBuffer;
std::vector<tinygltf::Image> gltfimages;


bool taking_screenshot = false;


class VulkanExample : public VulkanRaytracingSample
{
public:
	AccelerationStructure bottomLevelAS;
	AccelerationStructure topLevelAS;

	const float fovy = 45.0f;
	const float near_plane = 0.01f;
	const float far_plane = 10000.0f;
	const float pi = 4.0f * atanf(1.0f);
	const float deg_to_rad = (1.0f / 360.0f) * 2.0f * pi;

	virtual void keyPressed(uint32_t keyCode)
	{
		switch (keyCode)
		{
			case KEY_SPACE:
			{
				paused = true;
				taking_screenshot = true;
				screenshot(4, "v_rt_reflect.png");
				taking_screenshot = false;
				paused = false;

				break;
			}
		}
	}


	VkDescriptorSet materialSet;



	struct ray_test
	{
		glm::vec4 direction;
		glm::vec4 origin;

		bool in_use;

		glm::vec3 normal;
		float depth;

		int child_refract_id;
		int child_reflect_id;
		int child_subsurface_id;
		int parent_id;

		float base_color;
		float accumulated_color;

		float sss_base_opacity;
		float sss_base_colour;

		float reflection_constant;
		float refraction_constant;
		float sss_constant;
		float sss_density;

		float tint_constant;
		glm::vec3 tint_colour;

		bool external_reflection_ray;
		bool external_refraction_ray;
		bool internal_subsurface_ray;

		int level;

	};


	// Change this in raygen.rgen as well
	const size_t ray_buffer_size = 8;

	ray_test r[8];


	/*
		Create the descriptor sets used for the ray tracing dispatch
	*/

	void createDescriptorSets()
	{
		std::vector<VkDescriptorPoolSize> poolSizes = {
			{ VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1 },
			{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1 },
			{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1 },
			{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2 }, // 3
			{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2 },
		};
		VkDescriptorPoolCreateInfo descriptorPoolCreateInfo = vks::initializers::descriptorPoolCreateInfo(poolSizes, 2);
		VK_CHECK_RESULT(vkCreateDescriptorPool(device, &descriptorPoolCreateInfo, nullptr, &descriptorPool));

		VkDescriptorSetAllocateInfo descriptorSetAllocateInfo = vks::initializers::descriptorSetAllocateInfo(descriptorPool, &descriptorSetLayout, 1);
		VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &descriptorSetAllocateInfo, &descriptorSet));

		VkDescriptorSetAllocateInfo materialSetAllocateInfo = vks::initializers::descriptorSetAllocateInfo(descriptorPool, &materialSetLayout, 1);
		VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &materialSetAllocateInfo, &materialSet));

		VkWriteDescriptorSetAccelerationStructureKHR descriptorAccelerationStructureInfo = vks::initializers::writeDescriptorSetAccelerationStructureKHR();
		descriptorAccelerationStructureInfo.accelerationStructureCount = 1;
		descriptorAccelerationStructureInfo.pAccelerationStructures = &topLevelAS.handle;

		VkWriteDescriptorSet accelerationStructureWrite{};
		accelerationStructureWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		// The specialized acceleration structure descriptor has to be chained
		accelerationStructureWrite.pNext = &descriptorAccelerationStructureInfo;
		accelerationStructureWrite.dstSet = descriptorSet;
		accelerationStructureWrite.dstBinding = 0;
		accelerationStructureWrite.descriptorCount = 1;
		accelerationStructureWrite.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;

		VkDescriptorImageInfo storageImageDescriptor{ VK_NULL_HANDLE, storageImage.view, VK_IMAGE_LAYOUT_GENERAL };
		VkDescriptorBufferInfo vertexBufferDescriptor{ scene.vertices.buffer, 0, VK_WHOLE_SIZE };
		VkDescriptorBufferInfo indexBufferDescriptor{ scene.indices.buffer, 0, VK_WHOLE_SIZE };

		//size_t ray_testBufferSize = ray_buffer_size * sizeof(ray_test);

		//struct StagingBuffer {
		//	VkBuffer buffer;
		//	VkDeviceMemory memory;
		//} ray_test_staging;



		//VK_CHECK_RESULT(vulkanDevice->createBuffer(
		//	VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		//	VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
		//	ray_testBufferSize,
		//	&ray_test_staging.buffer,
		//	&ray_test_staging.memory,
		//	&r[0]));

		//VkDescriptorBufferInfo ray_testBufferDescriptor{ ray_test_staging.buffer, 0, VK_WHOLE_SIZE };


		std::vector<VkWriteDescriptorSet> writeDescriptorSets = {
			// Binding 0: Top level acceleration structure
			accelerationStructureWrite,
			// Binding 1: Ray tracing result image
			vks::initializers::writeDescriptorSet(descriptorSet, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, &storageImageDescriptor),
			// Binding 2: Uniform data
			vks::initializers::writeDescriptorSet(descriptorSet, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 2, &ubo.descriptor),
			// Binding 3: Scene vertex buffer
			vks::initializers::writeDescriptorSet(descriptorSet, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 3, &vertexBufferDescriptor),
			// Binding 4: Scene index buffer
			vks::initializers::writeDescriptorSet(descriptorSet, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 4, &indexBufferDescriptor),
			// Binding 5: Scene ray buffer
			//vks::initializers::writeDescriptorSet(descriptorSet, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 5, &ray_testBufferDescriptor),

			// Binding 0: Base color texture
			vks::initializers::writeDescriptorSet(materialSet, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 0, &scene.materials[0].baseColorTexture->descriptor),
			// Binding 1: Normal texture
			vks::initializers::writeDescriptorSet(materialSet, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, &scene.materials[0].normalTexture->descriptor),
		};
		vkUpdateDescriptorSets(device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, VK_NULL_HANDLE);
	}

	/*
		Create our ray tracing pipeline
	*/
	void createRayTracingPipeline()
	{
		std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings = {
			// Binding 0: Acceleration structure
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, 0),
			// Binding 1: Storage image
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_RAYGEN_BIT_KHR, 1),
			// Binding 2: Uniform buffer
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_MISS_BIT_KHR, 2),
			// Binding 3: Vertex buffer 
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, 3),
			// Binding 4: Index buffer
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, 4),
			// Binding 5: raytest buffer
			//vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, 5),

		};

		VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCI = vks::initializers::descriptorSetLayoutCreateInfo(setLayoutBindings);
		VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorSetLayoutCI, nullptr, &descriptorSetLayout));

		std::vector<VkDescriptorSetLayoutBinding> materialSetLayoutBindings = {
			// Binding 0: Base color texture
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, 0),
			// Binding 1: Normal texture
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, 1),
		};

		VkDescriptorSetLayoutCreateInfo materialSetLayoutCI = vks::initializers::descriptorSetLayoutCreateInfo(materialSetLayoutBindings);
		VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &materialSetLayoutCI, nullptr, &materialSetLayout));

		VkDescriptorSetLayout layouts[] = { descriptorSetLayout, materialSetLayout };

		VkPipelineLayoutCreateInfo pPipelineLayoutCI = vks::initializers::pipelineLayoutCreateInfo(layouts, 2);
		VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pPipelineLayoutCI, nullptr, &pipelineLayout));

		/*
			Setup ray tracing shader groups
		*/
		std::vector<VkPipelineShaderStageCreateInfo> shaderStages;

		VkSpecializationMapEntry specializationMapEntry = vks::initializers::specializationMapEntry(0, 0, sizeof(uint32_t));
		uint32_t maxRecursion = 8;
		VkSpecializationInfo specializationInfo = vks::initializers::specializationInfo(1, &specializationMapEntry, sizeof(maxRecursion), &maxRecursion);

		// Ray generation group
		{
			shaderStages.push_back(loadShader(getShadersPath() + "raytracingreflections/raygen.rgen.spv", VK_SHADER_STAGE_RAYGEN_BIT_KHR));
			// Pass recursion depth for reflections to ray generation shader via specialization constant
			shaderStages.back().pSpecializationInfo = &specializationInfo;
			VkRayTracingShaderGroupCreateInfoKHR shaderGroup{};
			shaderGroup.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
			shaderGroup.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
			shaderGroup.generalShader = static_cast<uint32_t>(shaderStages.size()) - 1;
			shaderGroup.closestHitShader = VK_SHADER_UNUSED_KHR;
			shaderGroup.anyHitShader = VK_SHADER_UNUSED_KHR;
			shaderGroup.intersectionShader = VK_SHADER_UNUSED_KHR;
			shaderGroups.push_back(shaderGroup);
		}

		// Miss group
		{
			shaderStages.push_back(loadShader(getShadersPath() + "raytracingreflections/miss.rmiss.spv", VK_SHADER_STAGE_MISS_BIT_KHR));
			VkRayTracingShaderGroupCreateInfoKHR shaderGroup{};
			shaderGroup.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
			shaderGroup.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
			shaderGroup.generalShader = static_cast<uint32_t>(shaderStages.size()) - 1;
			shaderGroup.closestHitShader = VK_SHADER_UNUSED_KHR;
			shaderGroup.anyHitShader = VK_SHADER_UNUSED_KHR;
			shaderGroup.intersectionShader = VK_SHADER_UNUSED_KHR;
			shaderGroups.push_back(shaderGroup);
		}

		// Shadow miss group
		{
			shaderStages.push_back(loadShader(getShadersPath() + "raytracingreflections/shadow.rmiss.spv", VK_SHADER_STAGE_MISS_BIT_KHR));
			VkRayTracingShaderGroupCreateInfoKHR shaderGroup{};
			shaderGroup.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
			shaderGroup.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
			shaderGroup.generalShader = static_cast<uint32_t>(shaderStages.size()) - 1;
			shaderGroup.closestHitShader = VK_SHADER_UNUSED_KHR;
			shaderGroup.anyHitShader = VK_SHADER_UNUSED_KHR;
			shaderGroup.intersectionShader = VK_SHADER_UNUSED_KHR;
			shaderGroups.push_back(shaderGroup);
		}

		// Closest hit group
		{
			shaderStages.push_back(loadShader(getShadersPath() + "raytracingreflections/closesthit.rchit.spv", VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR));
			VkRayTracingShaderGroupCreateInfoKHR shaderGroup{};
			shaderGroup.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
			shaderGroup.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR;
			shaderGroup.generalShader = VK_SHADER_UNUSED_KHR;
			shaderGroup.closestHitShader = static_cast<uint32_t>(shaderStages.size()) - 1;
			shaderGroup.anyHitShader = VK_SHADER_UNUSED_KHR;
			shaderGroup.intersectionShader = VK_SHADER_UNUSED_KHR;
			shaderGroups.push_back(shaderGroup);
		}

		VkRayTracingPipelineCreateInfoKHR rayTracingPipelineCI = vks::initializers::rayTracingPipelineCreateInfoKHR();
		rayTracingPipelineCI.stageCount = static_cast<uint32_t>(shaderStages.size());
		rayTracingPipelineCI.pStages = shaderStages.data();
		rayTracingPipelineCI.groupCount = static_cast<uint32_t>(shaderGroups.size());
		rayTracingPipelineCI.pGroups = shaderGroups.data();
		rayTracingPipelineCI.maxPipelineRayRecursionDepth = maxRecursion + 1;
		rayTracingPipelineCI.layout = pipelineLayout;
		VK_CHECK_RESULT(vkCreateRayTracingPipelinesKHR(device, VK_NULL_HANDLE, VK_NULL_HANDLE, 1, &rayTracingPipelineCI, nullptr, &pipeline));
	}


	VkDescriptorSet screenshotDescriptorSet;
	VkDescriptorPool screenshotDescriptorPool;

	void createScreenshotDescriptorSet()
	{
		std::vector<VkDescriptorPoolSize> poolSizes = {
			{ VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1 },
			{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1 },
			{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1 },
			{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2 } // 3
		};
		VkDescriptorPoolCreateInfo descriptorPoolCreateInfo = vks::initializers::descriptorPoolCreateInfo(poolSizes, 1);
		VK_CHECK_RESULT(vkCreateDescriptorPool(device, &descriptorPoolCreateInfo, nullptr, &screenshotDescriptorPool));

		VkDescriptorSetAllocateInfo descriptorSetAllocateInfo = vks::initializers::descriptorSetAllocateInfo(screenshotDescriptorPool, &descriptorSetLayout, 1);
		VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &descriptorSetAllocateInfo, &screenshotDescriptorSet));

		VkWriteDescriptorSetAccelerationStructureKHR descriptorAccelerationStructureInfo = vks::initializers::writeDescriptorSetAccelerationStructureKHR();
		descriptorAccelerationStructureInfo.accelerationStructureCount = 1;
		descriptorAccelerationStructureInfo.pAccelerationStructures = &topLevelAS.handle;

		VkWriteDescriptorSet accelerationStructureWrite{};
		accelerationStructureWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		// The specialized acceleration structure descriptor has to be chained
		accelerationStructureWrite.pNext = &descriptorAccelerationStructureInfo;
		accelerationStructureWrite.dstSet = screenshotDescriptorSet;
		accelerationStructureWrite.dstBinding = 0;
		accelerationStructureWrite.descriptorCount = 1;
		accelerationStructureWrite.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;

		VkDescriptorImageInfo storageImageDescriptor{ VK_NULL_HANDLE, screenshotStorageImage.view, VK_IMAGE_LAYOUT_GENERAL };
		VkDescriptorBufferInfo vertexBufferDescriptor{ scene.vertices.buffer, 0, VK_WHOLE_SIZE };
		VkDescriptorBufferInfo indexBufferDescriptor{ scene.indices.buffer, 0, VK_WHOLE_SIZE };
	


		//size_t ray_testBufferSize = ray_buffer_size * sizeof(ray_test);

		//struct StagingBuffer {
		//	VkBuffer buffer;
		//	VkDeviceMemory memory;
		//} ray_test_staging;


		//VK_CHECK_RESULT(vulkanDevice->createBuffer(
		//	VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		//	VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
		//	ray_testBufferSize,
		//	&ray_test_staging.buffer,
		//	&ray_test_staging.memory,
		//	&r[0]));

		//VkDescriptorBufferInfo ray_testBufferDescriptor{ ray_test_staging.buffer, 0, VK_WHOLE_SIZE };


		std::vector<VkWriteDescriptorSet> writeDescriptorSets = {
			// Binding 0: Top level acceleration structure
			accelerationStructureWrite,
			// Binding 1: Ray tracing result image
			vks::initializers::writeDescriptorSet(screenshotDescriptorSet, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, &storageImageDescriptor),
			// Binding 2: Uniform data
			vks::initializers::writeDescriptorSet(screenshotDescriptorSet, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 2, &ubo.descriptor),
			// Binding 3: Scene vertex buffer
			vks::initializers::writeDescriptorSet(screenshotDescriptorSet, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 3, &vertexBufferDescriptor),
			// Binding 4: Scene index buffer
			vks::initializers::writeDescriptorSet(screenshotDescriptorSet, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 4, &indexBufferDescriptor),
			// Binding 5: Scene raytest
			//vks::initializers::writeDescriptorSet(screenshotDescriptorSet, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 5, &ray_testBufferDescriptor),

		};
		vkUpdateDescriptorSets(device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, VK_NULL_HANDLE);
	}











	void screenshot(size_t num_cams_wide, const char* filename)
	{
		bool do_denoising = true;

		if (num_cams_wide == 0)
			return;

		static size_t last_num_cams_wide = 0;
		static size_t last_width = 0;
		static size_t last_height = 0;

		bool resized_something = false;

		if (last_width != width)
			resized_something = true;
		else if (last_height != height)
			resized_something = true;
		else if (last_num_cams_wide != num_cams_wide)
			resized_something = true;

		const unsigned long int size_x = width;
		const unsigned long int size_y = height;

		const glm::mat4x4 cam_mat = camera.matrices.perspective;

		VkDeviceSize size = size_x * size_y * 4 * sizeof(unsigned char); // 4 bytes per pixel

		// Create screenshot image

		if (resized_something)
		{
			deleteScreenshotStorageImage();

			if (num_cams_wide == 1) // Strange
				createScreenshotStorageImage(VK_FORMAT_B8G8R8A8_UNORM, { size_x, size_y, 1 });
			else
				createScreenshotStorageImage(VK_FORMAT_R8G8B8A8_UNORM, { size_x, size_y, 1 });
		}

		// Create screenshot descriptor set
		createScreenshotDescriptorSet();

		vks::Buffer screenshotStagingBuffer;


		if (resized_something)
		{
			//// Delete staging buffer
			screenshotStagingBuffer.destroy();
			// Create staging buffer
			VK_CHECK_RESULT(vulkanDevice->createBuffer(
				VK_BUFFER_USAGE_TRANSFER_DST_BIT,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
				&screenshotStagingBuffer,
				size
			));

			VK_CHECK_RESULT(screenshotStagingBuffer.map());
		}

		unsigned short int px = size_x * static_cast<unsigned short>(num_cams_wide);
		unsigned short int py = size_y * static_cast<unsigned short>(num_cams_wide);

		vector<unsigned char> pixel_data(4 * px * py);

		vector<unsigned char> fbpixels(4 * size_x * size_y);

		// Loop through subcameras
		// We break the screenshot up into pieces like this, so that we don't
		// hang the GPU for too long at one time
		for (size_t cam_num_x = 0; cam_num_x < num_cams_wide; cam_num_x++)
		{
			for (size_t cam_num_y = 0; cam_num_y < num_cams_wide; cam_num_y++)
			{
				const float aspect = static_cast<float>(size_x) / static_cast<float>(size_y);
				const float tangent = tan((fovy / 2.0f) * deg_to_rad);
				const float h = near_plane * tangent; // Half height of near_plane plane.
				const float w = h * aspect; // Half width of near_plane plane.

				const float cam_width = 2.0 * w / num_cams_wide;
				const float cam_height = 2.0 * h / num_cams_wide;

				const float left = -w + cam_num_x * cam_width;
				const float right = -w + (cam_num_x + 1) * cam_width;
				const float bottom = -h + cam_num_y * cam_height;
				const float top = -h + (cam_num_y + 1) * cam_height;

				camera.matrices.perspective = glm::frustum(left, right, bottom, top, near_plane, far_plane);
				updateUniformBuffers();

				// Prepare & flush command buffer
				{
					VkCommandBuffer screenshotCmdBuffer = vulkanDevice->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);

					VkImageSubresourceRange subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

					VkDescriptorSet sets[] = { screenshotDescriptorSet, materialSet };

					vkCmdBindPipeline(screenshotCmdBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, pipeline);
					vkCmdBindDescriptorSets(screenshotCmdBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, pipelineLayout, 0, 2, sets, 0, 0);

					VkStridedDeviceAddressRegionKHR emptySbtEntry = {};
					vkCmdTraceRaysKHR(
						screenshotCmdBuffer,
						&shaderBindingTables.raygen.stridedDeviceAddressRegion,
						&shaderBindingTables.miss.stridedDeviceAddressRegion,
						&shaderBindingTables.hit.stridedDeviceAddressRegion,
						&emptySbtEntry,
						size_x,
						size_y,
						1);

					vks::tools::setImageLayout(
						screenshotCmdBuffer,
						screenshotStorageImage.image,
						VK_IMAGE_LAYOUT_GENERAL,
						VK_IMAGE_LAYOUT_GENERAL,
						subresourceRange);

					VkBufferImageCopy copyRegion{};
					copyRegion.bufferOffset = 0;
					copyRegion.bufferRowLength = 0;
					copyRegion.bufferImageHeight = 0;
					copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
					copyRegion.imageSubresource.mipLevel = 0;
					copyRegion.imageSubresource.baseArrayLayer = 0;
					copyRegion.imageSubresource.layerCount = 1;
					copyRegion.imageOffset = { 0, 0, 0 };
					copyRegion.imageExtent.width = size_x;
					copyRegion.imageExtent.height = size_y;
					copyRegion.imageExtent.depth = 1;

					vkCmdCopyImageToBuffer(screenshotCmdBuffer, screenshotStorageImage.image, VK_IMAGE_LAYOUT_GENERAL, screenshotStagingBuffer.buffer, 1, &copyRegion);

					VkBufferMemoryBarrier barrier = {};
					barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
					barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
					barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
					barrier.buffer = screenshotStagingBuffer.buffer;
					barrier.size = screenshotStagingBuffer.size;

					vkCmdPipelineBarrier(
						screenshotCmdBuffer,
						VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
						0,
						0, nullptr,
						1, &barrier,
						0, nullptr
					);

					vulkanDevice->flushCommandBuffer(screenshotCmdBuffer, queue);

					memcpy(&fbpixels[0], screenshotStagingBuffer.mapped, size);

					for (size_t i = 0; i < size_x; i++)
					{
						for (size_t j = 0; j < size_y; j++)
						{
							size_t fb_index = 4 * (j * size_x + i);

							size_t screenshot_x = cam_num_x * size_x + i;
							size_t screenshot_y = cam_num_y * size_y + j;
							size_t screenshot_index = 4 * (screenshot_y * (size_x * num_cams_wide) + screenshot_x);

							pixel_data[screenshot_index + 0] = fbpixels[fb_index + 0];
							pixel_data[screenshot_index + 1] = fbpixels[fb_index + 1];
							pixel_data[screenshot_index + 2] = fbpixels[fb_index + 2];
							pixel_data[screenshot_index + 3] = fbpixels[fb_index + 3];
						}
					}
				}
			}
		}

		vector <unsigned char> uc_output_data(4 * px * py, 0);

		if (false == do_denoising)
		{
			uc_output_data = pixel_data; // could use a .swap() here

			for (size_t i = 0; i < px; i++)
			{
				for (size_t j = 0; j < py; j++)
				{
					size_t uc_index = 4 * (j * px + i);

					uc_output_data[uc_index + 3] = 255;
				}
			}
		}
		else
		{
			vector<float> float_data(3 * px * py);

			for (size_t i = 0; i < px; i++)
			{
				for (size_t j = 0; j < py; j++)
				{
					size_t float_index = 3 * (j * px + i);
					size_t index = 4 * (j * px + i);

					float_data[float_index + 0] = static_cast<float>(pixel_data[index + 0]) / 255.0f;
					float_data[float_index + 1] = static_cast<float>(pixel_data[index + 1]) / 255.0f;
					float_data[float_index + 2] = static_cast<float>(pixel_data[index + 2]) / 255.0f;
				}
			}

			//ofstream out("test.pfm", std::ios_base::binary);
			//ostringstream oss;
			//oss << "PF" << '\n' << px << ' ' << py << '\n' << "-1.0" << '\n';
			//out.write(reinterpret_cast<const char*>(oss.str().c_str()), oss.str().size());
			//out.write(reinterpret_cast<const char*>(&float_data[0]), float_data.size()*sizeof(float));
			//out.close();

			oidn::DeviceRef dev = oidn::newDevice();
			dev.commit();

			oidn::BufferRef colorBuf = dev.newBuffer(px * py * 3 * sizeof(float));
			colorBuf.write(0, px * py * 3 * sizeof(float), &float_data[0]);

			oidn::FilterRef filter = dev.newFilter("RT");

			filter.setImage("color", colorBuf, oidn::Format::Float3, px, py);
			filter.setImage("output", colorBuf, oidn::Format::Float3, px, py);
			filter.set("hdr", false); // Do not enable this, or the lights will look strange
			filter.commit();
			filter.execute();

			// Check for errors
			const char* errorMessage;
			if (dev.getError(errorMessage) != oidn::Error::None)
				MessageBox(NULL, errorMessage, "Error", MB_OK);

			colorBuf.read(0, px * py * 3 * sizeof(float), &float_data[0]);

			for (size_t i = 0; i < px; i++)
			{
				for (size_t j = 0; j < py; j++)
				{
					size_t uc_index = 4 * (j * px + i);
					size_t data_index = 3 * (j * px + i);

					uc_output_data[uc_index + 0] = static_cast<unsigned char>(fabsf(float_data[data_index + 0]) * 255.0f);
					uc_output_data[uc_index + 1] = static_cast<unsigned char>(fabsf(float_data[data_index + 1]) * 255.0f);
					uc_output_data[uc_index + 2] = static_cast<unsigned char>(fabsf(float_data[data_index + 2]) * 255.0f);
					uc_output_data[uc_index + 3] = 255;// pixel_data[uc_index];
				}
			}
		}


		if (filename != NULL)
			int result = stbi_write_png(filename, px, py, 4, &uc_output_data[0], 0);


		if (num_cams_wide == 1)
		{
			VkCommandBuffer screenshotCmdBuffer = vulkanDevice->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);

			memcpy(screenshotStagingBuffer.mapped, &uc_output_data[0], size);

			VkImageSubresourceRange subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

			vks::tools::setImageLayout(
				screenshotCmdBuffer,
				screenshotStorageImage.image,
				VK_IMAGE_LAYOUT_UNDEFINED,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				subresourceRange);

			VkBufferImageCopy copyRegion{};
			copyRegion.bufferOffset = 0;
			copyRegion.bufferRowLength = 0;
			copyRegion.bufferImageHeight = 0;
			copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			copyRegion.imageSubresource.mipLevel = 0;
			copyRegion.imageSubresource.baseArrayLayer = 0;
			copyRegion.imageSubresource.layerCount = 1;
			copyRegion.imageOffset = { 0, 0, 0 };
			copyRegion.imageExtent.width = size_x;
			copyRegion.imageExtent.height = size_y;
			copyRegion.imageExtent.depth = 1;

			VkImageMemoryBarrier imageMemoryBarrier;
			imageMemoryBarrier.image = screenshotStorageImage.image;
			imageMemoryBarrier.subresourceRange = subresourceRange;
			imageMemoryBarrier.srcAccessMask = VK_ACCESS_HOST_READ_BIT;
			imageMemoryBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			imageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			imageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			imageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;

			vkCmdCopyBufferToImage(
				screenshotCmdBuffer,
				screenshotStagingBuffer.buffer,
				screenshotStorageImage.image,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				1,
				&copyRegion);

			vkCmdPipelineBarrier(
				screenshotCmdBuffer,
				VK_PIPELINE_STAGE_HOST_BIT,
				VK_PIPELINE_STAGE_TRANSFER_BIT,
				0,
				0, nullptr,
				0, nullptr,
				1, &imageMemoryBarrier);

			vulkanDevice->flushCommandBuffer(screenshotCmdBuffer, queue);
		}




		// do depth of field here







		camera.matrices.perspective = cam_mat;
		updateUniformBuffers();


		// Delete screenshot descriptor pool
		vkDestroyDescriptorPool(device, screenshotDescriptorPool, nullptr);
	}





	void createScreenshotStorageImage(VkFormat format, VkExtent3D extent)
	{
		// Release ressources if image is to be recreated
		if (screenshotStorageImage.image != VK_NULL_HANDLE)
		{
			vkDestroyImageView(device, screenshotStorageImage.view, nullptr);
			vkDestroyImage(device, screenshotStorageImage.image, nullptr);
			vkFreeMemory(device, screenshotStorageImage.memory, nullptr);
			screenshotStorageImage = {};
		}

		VkImageCreateInfo image = vks::initializers::imageCreateInfo();
		image.imageType = VK_IMAGE_TYPE_2D;
		image.format = format;
		image.extent = extent;
		image.mipLevels = 1;
		image.arrayLayers = 1;
		image.samples = VK_SAMPLE_COUNT_1_BIT;
		image.tiling = VK_IMAGE_TILING_OPTIMAL;
		image.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
		image.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		VK_CHECK_RESULT(vkCreateImage(vulkanDevice->logicalDevice, &image, nullptr, &screenshotStorageImage.image));

		VkMemoryRequirements memReqs;
		vkGetImageMemoryRequirements(vulkanDevice->logicalDevice, screenshotStorageImage.image, &memReqs);
		VkMemoryAllocateInfo memoryAllocateInfo = vks::initializers::memoryAllocateInfo();
		memoryAllocateInfo.allocationSize = memReqs.size;
		memoryAllocateInfo.memoryTypeIndex = vulkanDevice->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		VK_CHECK_RESULT(vkAllocateMemory(vulkanDevice->logicalDevice, &memoryAllocateInfo, nullptr, &screenshotStorageImage.memory));
		VK_CHECK_RESULT(vkBindImageMemory(vulkanDevice->logicalDevice, screenshotStorageImage.image, screenshotStorageImage.memory, 0));

		VkImageViewCreateInfo colorImageView = vks::initializers::imageViewCreateInfo();
		colorImageView.viewType = VK_IMAGE_VIEW_TYPE_2D;
		colorImageView.format = format;
		colorImageView.subresourceRange = {};
		colorImageView.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		colorImageView.subresourceRange.baseMipLevel = 0;
		colorImageView.subresourceRange.levelCount = 1;
		colorImageView.subresourceRange.baseArrayLayer = 0;
		colorImageView.subresourceRange.layerCount = 1;
		colorImageView.image = screenshotStorageImage.image;
		VK_CHECK_RESULT(vkCreateImageView(vulkanDevice->logicalDevice, &colorImageView, nullptr, &screenshotStorageImage.view));

		VkCommandBuffer cmdBuffer = vulkanDevice->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);
		vks::tools::setImageLayout(cmdBuffer, screenshotStorageImage.image,
			VK_IMAGE_LAYOUT_UNDEFINED,
			VK_IMAGE_LAYOUT_GENERAL,
			{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 });





		vulkanDevice->flushCommandBuffer(cmdBuffer, queue);
	}

	void deleteScreenshotStorageImage()
	{
		vkDestroyImageView(vulkanDevice->logicalDevice, screenshotStorageImage.view, nullptr);
		vkDestroyImage(vulkanDevice->logicalDevice, screenshotStorageImage.image, nullptr);
		vkFreeMemory(vulkanDevice->logicalDevice, screenshotStorageImage.memory, nullptr);

		screenshotStorageImage = {};
	}






	std::vector<VkRayTracingShaderGroupCreateInfoKHR> shaderGroups{};
	struct ShaderBindingTables {
		ShaderBindingTable raygen;
		ShaderBindingTable miss;
		ShaderBindingTable hit;
	} shaderBindingTables;


	VkTransformMatrixKHR transformMatrix = {
				1.0f, 0.0f, 0.0f, 0.0f,
				0.0f, 1.0f, 0.0f, 0.0f,
				0.0f, 0.0f, 1.0f, 0.0f };

	clock_t start = std::clock();

	struct UniformData {

		glm::mat4 viewInverse;
		glm::mat4 projInverse;
		glm::mat4 transformation_matrix;

		glm::vec3 camera_pos;

		int32_t vertexSize;

		bool screenshot_mode;

		uint32_t tri_count;
		uint32_t light_tri_count;

	} uniformData;
	vks::Buffer ubo;

	VkPipeline pipeline;
	VkPipelineLayout pipelineLayout;
	VkDescriptorSet descriptorSet;
	VkDescriptorSetLayout descriptorSetLayout;
	VkDescriptorSetLayout materialSetLayout;

	vkglTF::Model scene;

	VulkanRaytracingSample::StorageImage screenshotStorageImage;
	

	// This sample is derived from an extended base class that saves most of the ray tracing setup boiler plate
	VulkanExample() : VulkanRaytracingSample()
	{
		title = "Ray tracing shadows & reflections";
		//timerSpeed *= 0.5f;
		//camera.rotationSpeed *= 0.25f;
		camera.type = Camera::CameraType::lookat;
		camera.setPerspective(fovy, (float)width / (float)height, near_plane, far_plane);
		camera.setRotation(glm::vec3(0.0f, 0.0f, 0.0f));
		camera.setTranslation(glm::vec3(0.0f, 0.0f, -6.0));
		enableExtensions();
	}

	~VulkanExample()
	{
		vkDestroyPipeline(device, pipeline, nullptr);
		vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
		vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);
		vkDestroyDescriptorSetLayout(device, materialSetLayout, nullptr);
		deleteStorageImage();
		deleteAccelerationStructure(bottomLevelAS);
		deleteAccelerationStructure(topLevelAS);
		shaderBindingTables.raygen.destroy();
		shaderBindingTables.miss.destroy();
		shaderBindingTables.hit.destroy();
		ubo.destroy();
	}

	/*
		Create the bottom level acceleration structure contains the scene's actual geometry (vertices, triangles)
	*/
	void createBottomLevelAccelerationStructure(bool do_init)
	{
		// Instead of a simple triangle, we'll be loading a more complex scene for this example
		// The shaders are accessing the vertex and index buffers of the scene, so the proper usage flag has to be set on the vertex and index buffers for the scene
		vkglTF::memoryPropertyFlags = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
		vkglTF::descriptorBindingFlags = vkglTF::DescriptorBindingFlags::ImageBaseColor | vkglTF::DescriptorBindingFlags::ImageNormalMap;
		const uint32_t glTFLoadingFlags = vkglTF::FileLoadingFlags::PreTransformVertices | vkglTF::FileLoadingFlags::PreMultiplyVertexColors | vkglTF::FileLoadingFlags::FlipY;





		// This fractal_500.gltf file can be downloaded from:
		// https://drive.google.com/file/d/1BJJSC_K8NwaH8kP4tQpxlAmc6h6N3Ii1/view
		if (true)
		{
			scene.loadFromFile(
				model,
				start,
				do_init,
				indexBuffer,
				vertexBuffer,
				gltfimages,
				"C:/temp/rob_rau_cornell/gltf/cornell.gltf",
				//"C:/temp/rob_rau_cornell/circle_light/circle_light.gltf",
				//"C:/temp/rob_rau_cornell/sphere_light/sphere_light.gltf",
				///"C:/temp/rob_rau_cornell/simple_building/simple_building.gltf",
				//"C:/temp/rob_rau_cornell/bunny2/bunny2_nobox.gltf",
				//"C:/temp/rob_rau_cornell/bunny2/bunny2.gltf",
				//"C:/temp/rob_rau_cornell/bunny2/bunny_array.gltf",
				//"C:/temp/rob_rau_cornell/prism3/cornell_prism3.gltf",
				//"C:/temp/rob_rau_cornell/barrel/barrel.gltf",
				tri_count,
				light_tri_count,
				vulkanDevice,
				queue,
				glTFLoadingFlags,
				1.0f);

			//ostringstream oss;
			//oss << tri_count << " " << light_tri_count << " " << indexBuffer.size() << " " << vertexBuffer.size();
		
			//MessageBox(NULL, oss.str().c_str(), "test", MB_OK);
		}





		//scene.loadFromFile("C:/temp/rob_rau_cornell/prism3/cornell_prism3.gltf", vulkanDevice, queue, glTFLoadingFlags);
		//scene.loadFromFile("C:/temp/rob_rau_cornell/bunny2/bunny2.gltf", vulkanDevice, queue, glTFLoadingFlags);
		//scene.loadFromFile("C:/temp/rob_rau_cornell/cylinder/cylinder.gltf", vulkanDevice, queue, glTFLoadingFlags);
		//scene.loadFromFile("C:/temp/rob_rau_cornell/small_light/small_light.gltf", vulkanDevice, queue, glTFLoadingFlags);
		//scene.loadFromFile("C:/temp/rob_rau_cornell/barrel/barrel.gltf", vulkanDevice, queue, glTFLoadingFlags);



		//scene.loadFromFile("C:/temp/rob_rau_cornell/prism3/cornell_prism3_merged.gltf", vulkanDevice, queue, glTFLoadingFlags);			
		//scene.loadFromFile("C:/temp/rob_rau_cornell/prism_rounded/prism_rounded.gltf", vulkanDevice, queue, glTFLoadingFlags);
		//scene.loadFromFile("C:/temp/rob_rau_cornell/prism/cornell_prism.gltf", vulkanDevice, queue, glTFLoadingFlags);
		//scene.loadFromFile("C:/temp/rob_rau_cornell/prism2/prism2.gltf", vulkanDevice, queue, glTFLoadingFlags);
		//scene.loadFromFile("C:/temp/rob_rau_cornell/prism4/cornell_prism4.gltf", vulkanDevice, queue, glTFLoadingFlags);

		//scene.loadFromFile("C:/temp/rob_rau_cornell/bunny/bunny.gltf", vulkanDevice, queue, glTFLoadingFlags);		
		//scene.loadFromFile("C:/temp/rob_rau_cornell/bunny3/bunny3.gltf", vulkanDevice, queue, glTFLoadingFlags);

		//scene.loadFromFile("C:/temp/rob_rau_cornell/ring/ring.gltf", vulkanDevice, queue, glTFLoadingFlags);
		//scene.loadFromFile("C:/temp/rob_rau_cornell/ring2/ring2.gltf", vulkanDevice, queue, glTFLoadingFlags);
		//scene.loadFromFile("C:/temp/rob_rau_cornell/ring3/ring3.gltf", vulkanDevice, queue, glTFLoadingFlags);
		//scene.loadFromFile("C:/temp/rob_rau_cornell/sphere2/sphere2.gltf", vulkanDevice, queue, glTFLoadingFlags);

//scene.loadFromFile("C:/temp/cyl_tex/cylinder.gltf", vulkanDevice, queue, glTFLoadingFlags);


//scene.loadFromFile(getAssetPath() + "models/FlightHelmet/glTF/FlightHelmet.gltf", vulkanDevice, queue, glTFLoadingFlags);












		VkDeviceOrHostAddressConstKHR vertexBufferDeviceAddress{};
		VkDeviceOrHostAddressConstKHR indexBufferDeviceAddress{};

		vertexBufferDeviceAddress.deviceAddress = getBufferDeviceAddress(scene.vertices.buffer);
		indexBufferDeviceAddress.deviceAddress = getBufferDeviceAddress(scene.indices.buffer);

		uint32_t numTriangles = static_cast<uint32_t>(scene.indices.count) / 3;
		uint32_t maxVertex = scene.vertices.count;

		// Build
		VkAccelerationStructureGeometryKHR accelerationStructureGeometry = vks::initializers::accelerationStructureGeometryKHR();
		accelerationStructureGeometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
		accelerationStructureGeometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
		accelerationStructureGeometry.geometry.triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
		accelerationStructureGeometry.geometry.triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
		accelerationStructureGeometry.geometry.triangles.vertexData = vertexBufferDeviceAddress;
		accelerationStructureGeometry.geometry.triangles.maxVertex = maxVertex;
		accelerationStructureGeometry.geometry.triangles.vertexStride = sizeof(vkglTF::Vertex);
		accelerationStructureGeometry.geometry.triangles.indexType = VK_INDEX_TYPE_UINT32;
		accelerationStructureGeometry.geometry.triangles.indexData = indexBufferDeviceAddress;
		accelerationStructureGeometry.geometry.triangles.transformData.deviceAddress = 0;
		accelerationStructureGeometry.geometry.triangles.transformData.hostAddress = nullptr;

		// Get size info
		VkAccelerationStructureBuildGeometryInfoKHR accelerationStructureBuildGeometryInfo = vks::initializers::accelerationStructureBuildGeometryInfoKHR();
		accelerationStructureBuildGeometryInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
		accelerationStructureBuildGeometryInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR | VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR;//VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
		accelerationStructureBuildGeometryInfo.geometryCount = 1;
		accelerationStructureBuildGeometryInfo.pGeometries = &accelerationStructureGeometry;

		VkAccelerationStructureBuildSizesInfoKHR accelerationStructureBuildSizesInfo = vks::initializers::accelerationStructureBuildSizesInfoKHR();
		vkGetAccelerationStructureBuildSizesKHR(
			device,
			VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
			&accelerationStructureBuildGeometryInfo,
			&numTriangles,
			&accelerationStructureBuildSizesInfo);

		//if (false == do_init)

		//deleteAccelerationStructure(bottomLevelAS);

		if (do_init)
			createAccelerationStructure(bottomLevelAS, VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR, accelerationStructureBuildSizesInfo);
		else
		{
			//			deleteAccelerationStructure(bottomLevelAS);
			//			createAccelerationStructure(bottomLevelAS, VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR, accelerationStructureBuildSizesInfo);
		}


		// Create a small scratch buffer used during build of the bottom level acceleration structure
		ScratchBuffer scratchBuffer = createScratchBuffer(accelerationStructureBuildSizesInfo.buildScratchSize);

		VkAccelerationStructureBuildGeometryInfoKHR accelerationBuildGeometryInfo = vks::initializers::accelerationStructureBuildGeometryInfoKHR();
		accelerationBuildGeometryInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
		accelerationBuildGeometryInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR | VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR; //VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
		accelerationBuildGeometryInfo.mode = do_init ? VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR : VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR; //VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
		accelerationBuildGeometryInfo.dstAccelerationStructure = bottomLevelAS.handle;
		accelerationBuildGeometryInfo.geometryCount = 1;
		accelerationBuildGeometryInfo.pGeometries = &accelerationStructureGeometry;
		accelerationBuildGeometryInfo.scratchData.deviceAddress = scratchBuffer.deviceAddress;

		VkAccelerationStructureBuildRangeInfoKHR accelerationStructureBuildRangeInfo{};
		accelerationStructureBuildRangeInfo.primitiveCount = numTriangles;
		accelerationStructureBuildRangeInfo.primitiveOffset = 0;
		accelerationStructureBuildRangeInfo.firstVertex = 0;
		accelerationStructureBuildRangeInfo.transformOffset = 0;
		std::vector<VkAccelerationStructureBuildRangeInfoKHR*> accelerationBuildStructureRangeInfos = { &accelerationStructureBuildRangeInfo };

		// Build the acceleration structure on the device via a one-time command buffer submission
		// Some implementations may support acceleration structure building on the host (VkPhysicalDeviceAccelerationStructureFeaturesKHR->accelerationStructureHostCommands), but we prefer device builds
		VkCommandBuffer commandBuffer = vulkanDevice->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);
		vkCmdBuildAccelerationStructuresKHR(
			commandBuffer,
			1,
			&accelerationBuildGeometryInfo,
			accelerationBuildStructureRangeInfos.data());



		vulkanDevice->flushCommandBuffer(commandBuffer, queue);

		deleteScratchBuffer(scratchBuffer);
	}

	/*
		The top level acceleration structure contains the scene's object instances
	*/
	void createTopLevelAccelerationStructure(bool do_init)
	{


		static const float pi = 4.0f * atanf(1.0f);
		float duration = (std::clock() - start) / (float)CLOCKS_PER_SEC;
		float radians = duration * 2.0f * pi * 0.05f;

		//// Rotate on y axis
		//transformMatrix = {
		//	cos(radians), 0.0f, -sin(radians), 0.0f,
		//	0.0f,         1.0f,  0.0f,         0.0f,
		//	sin(radians), 0.0f,  cos(radians), 0.0f };

		VkAccelerationStructureInstanceKHR instance{};
		instance.transform = transformMatrix;
		instance.instanceCustomIndex = 0;
		instance.mask = 0xFF;
		instance.instanceShaderBindingTableRecordOffset = 0;
		instance.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
		instance.accelerationStructureReference = bottomLevelAS.deviceAddress;

		// Buffer for instance data
		vks::Buffer instancesBuffer;
		VK_CHECK_RESULT(vulkanDevice->createBuffer(
			VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			&instancesBuffer,
			sizeof(VkAccelerationStructureInstanceKHR),
			&instance));

		VkDeviceOrHostAddressConstKHR instanceDataDeviceAddress{};
		instanceDataDeviceAddress.deviceAddress = getBufferDeviceAddress(instancesBuffer.buffer);

		VkAccelerationStructureGeometryKHR accelerationStructureGeometry = vks::initializers::accelerationStructureGeometryKHR();
		accelerationStructureGeometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
		accelerationStructureGeometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
		accelerationStructureGeometry.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
		accelerationStructureGeometry.geometry.instances.arrayOfPointers = VK_FALSE;
		accelerationStructureGeometry.geometry.instances.data = instanceDataDeviceAddress;

		// Get size info
		VkAccelerationStructureBuildGeometryInfoKHR accelerationStructureBuildGeometryInfo = vks::initializers::accelerationStructureBuildGeometryInfoKHR();
		accelerationStructureBuildGeometryInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
		accelerationStructureBuildGeometryInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR | VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR; //VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
		accelerationStructureBuildGeometryInfo.geometryCount = 1;
		accelerationStructureBuildGeometryInfo.pGeometries = &accelerationStructureGeometry;

		uint32_t primitive_count = 1;

		VkAccelerationStructureBuildSizesInfoKHR accelerationStructureBuildSizesInfo = vks::initializers::accelerationStructureBuildSizesInfoKHR();
		vkGetAccelerationStructureBuildSizesKHR(
			device,
			VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
			&accelerationStructureBuildGeometryInfo,
			&primitive_count,
			&accelerationStructureBuildSizesInfo);


		//ostringstream oss;

		//oss << primitive_count;

		//MessageBox(NULL, oss.str().c_str(), "", MB_OK);





		//if (false == do_init)
		//deleteAccelerationStructure(topLevelAS);

		if (do_init)
			createAccelerationStructure(topLevelAS, VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR, accelerationStructureBuildSizesInfo);
		else
		{
			//			deleteAccelerationStructure(topLevelAS);
			//			createAccelerationStructure(topLevelAS, VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR, accelerationStructureBuildSizesInfo);
		}
		// Create a small scratch buffer used during build of the top level acceleration structure
		ScratchBuffer scratchBuffer = createScratchBuffer(accelerationStructureBuildSizesInfo.buildScratchSize);

		VkAccelerationStructureBuildGeometryInfoKHR accelerationBuildGeometryInfo = vks::initializers::accelerationStructureBuildGeometryInfoKHR();
		accelerationBuildGeometryInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
		accelerationBuildGeometryInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR | VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR; // VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
		accelerationBuildGeometryInfo.mode = do_init ? VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR : VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR;
		accelerationBuildGeometryInfo.dstAccelerationStructure = topLevelAS.handle;
		accelerationBuildGeometryInfo.geometryCount = 1;
		accelerationBuildGeometryInfo.pGeometries = &accelerationStructureGeometry;
		accelerationBuildGeometryInfo.scratchData.deviceAddress = scratchBuffer.deviceAddress;

		VkAccelerationStructureBuildRangeInfoKHR accelerationStructureBuildRangeInfo{};
		accelerationStructureBuildRangeInfo.primitiveCount = 1;
		accelerationStructureBuildRangeInfo.primitiveOffset = 0;
		accelerationStructureBuildRangeInfo.firstVertex = 0;
		accelerationStructureBuildRangeInfo.transformOffset = 0;
		std::vector<VkAccelerationStructureBuildRangeInfoKHR*> accelerationBuildStructureRangeInfos = { &accelerationStructureBuildRangeInfo };

		// Build the acceleration structure on the device via a one-time command buffer submission
		// Some implementations may support acceleration structure building on the host (VkPhysicalDeviceAccelerationStructureFeaturesKHR->accelerationStructureHostCommands), but we prefer device builds
		VkCommandBuffer commandBuffer = vulkanDevice->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);
		vkCmdBuildAccelerationStructuresKHR(
			commandBuffer,
			1,
			&accelerationBuildGeometryInfo,
			accelerationBuildStructureRangeInfos.data());
		vulkanDevice->flushCommandBuffer(commandBuffer, queue);

		deleteScratchBuffer(scratchBuffer);
		instancesBuffer.destroy();
	}

	/*
		Create the Shader Binding Tables that binds the programs and top-level acceleration structure

		SBT Layout used in this sample:

			/-----------\
			| raygen    |
			|-----------|
			| miss      |
			|-----------|
			| hit       |
			\-----------/

	*/
	void createShaderBindingTables() {
		const uint32_t handleSize = rayTracingPipelineProperties.shaderGroupHandleSize;
		const uint32_t handleSizeAligned = vks::tools::alignedSize(rayTracingPipelineProperties.shaderGroupHandleSize, rayTracingPipelineProperties.shaderGroupHandleAlignment);
		const uint32_t groupCount = static_cast<uint32_t>(shaderGroups.size());
		const uint32_t sbtSize = groupCount * handleSizeAligned;

		std::vector<uint8_t> shaderHandleStorage(sbtSize);
		VK_CHECK_RESULT(vkGetRayTracingShaderGroupHandlesKHR(device, pipeline, 0, groupCount, sbtSize, shaderHandleStorage.data()));

		createShaderBindingTable(shaderBindingTables.raygen, 1);
		createShaderBindingTable(shaderBindingTables.miss, 2);
		createShaderBindingTable(shaderBindingTables.hit, 1);

		// Copy handles
		memcpy(shaderBindingTables.raygen.mapped, shaderHandleStorage.data(), handleSize);
		memcpy(shaderBindingTables.miss.mapped, shaderHandleStorage.data() + handleSizeAligned, handleSize * 2);
		memcpy(shaderBindingTables.hit.mapped, shaderHandleStorage.data() + handleSizeAligned * 3, handleSize);
	}


	/*
		Create the uniform buffer used to pass matrices to the ray tracing ray generation shader
	*/
	void createUniformBuffer()
	{
		VK_CHECK_RESULT(vulkanDevice->createBuffer(
			VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			&ubo,
			sizeof(uniformData),
			&uniformData));
		VK_CHECK_RESULT(ubo.map());

		updateUniformBuffers();
	}

	/*
		If the window has been resized, we need to recreate the storage image and it's descriptor
	*/
	void handleResize()
	{
		m.lock();
		
		deleteStorageImage();
		
		// Recreate image
		createStorageImage(swapChain.colorFormat, { width, height, 1 });
		// Update descriptor
		VkDescriptorImageInfo storageImageDescriptor{ VK_NULL_HANDLE, storageImage.view, VK_IMAGE_LAYOUT_GENERAL };
		VkWriteDescriptorSet resultImageWrite = vks::initializers::writeDescriptorSet(descriptorSet, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, &storageImageDescriptor);
		vkUpdateDescriptorSets(device, 1, &resultImageWrite, 0, VK_NULL_HANDLE);
		resized = false;
		

		m.unlock();
	}

	/*
		Command buffer generation
	*/
	void buildCommandBuffers()
	{
		if (resized)
		{
			handleResize();
		}

		VkCommandBufferBeginInfo cmdBufInfo = vks::initializers::commandBufferBeginInfo();

		VkImageSubresourceRange subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

		VkDescriptorSet sets[] = { descriptorSet, scene.materials[0].descriptorSet };

		for (int32_t i = 0; i < drawCmdBuffers.size(); ++i)
		{
			VK_CHECK_RESULT(vkBeginCommandBuffer(drawCmdBuffers[i], &cmdBufInfo));

			vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, pipeline);
			vkCmdBindDescriptorSets(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, pipelineLayout, 0, 2, sets, 0, 0);

			m.lock();

			paused = true;
			screenshot(1, NULL);
			paused = false;

			m.unlock();

			/*
				Copy ray tracing output to swap chain image
			*/

			// Prepare current swap chain image as transfer destination
			vks::tools::setImageLayout(
				drawCmdBuffers[i],
				swapChain.images[i],
				VK_IMAGE_LAYOUT_UNDEFINED,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				subresourceRange);

			// Prepare ray tracing output image as transfer source
			vks::tools::setImageLayout(
				drawCmdBuffers[i],
				screenshotStorageImage.image,
				VK_IMAGE_LAYOUT_GENERAL,
				VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				subresourceRange);

			VkImageCopy copyRegion{};
			copyRegion.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
			copyRegion.srcOffset = { 0, 0, 0 };
			copyRegion.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
			copyRegion.dstOffset = { 0, 0, 0 };
			copyRegion.extent = { width, height, 1 };

			vkCmdCopyImage(drawCmdBuffers[i], screenshotStorageImage.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, swapChain.images[i], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);

			// Transition swap chain image back for presentation
			vks::tools::setImageLayout(
				drawCmdBuffers[i],
				swapChain.images[i],
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
				subresourceRange);

			// Transition ray tracing output image back to general layout
			vks::tools::setImageLayout(
				drawCmdBuffers[i],
				screenshotStorageImage.image,
				VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				VK_IMAGE_LAYOUT_GENERAL,
				subresourceRange);


			drawUI(drawCmdBuffers[i], frameBuffers[i]);

			VK_CHECK_RESULT(vkEndCommandBuffer(drawCmdBuffers[i]));
		}
	}

	void updateUniformBuffers()
	{
		uniformData.projInverse = glm::inverse(camera.matrices.perspective);
		uniformData.viewInverse = glm::inverse(camera.matrices.view);

		// For rendering the normals correctly in the closest hit shader
		// This is only needed when doing moving objects where you have to 
		// recreate / reuse the top-level acceleration structure
		for (size_t i = 0; i < 3; i++)
			for (size_t j = 0; j < 4; j++)
				uniformData.transformation_matrix[j][i] = transformMatrix.matrix[i][j];

		uniformData.camera_pos = camera.position;

		// Pass the vertex size to the shader for unpacking vertices
		uniformData.vertexSize = sizeof(vkglTF::Vertex);

		uniformData.screenshot_mode = taking_screenshot;

		uniformData.tri_count = tri_count;
		uniformData.light_tri_count = light_tri_count;

		memcpy(ubo.mapped, &uniformData, sizeof(uniformData));
	}

	void getEnabledFeatures()
	{
		// Enable features required for ray tracing using feature chaining via pNext		
		enabledBufferDeviceAddresFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES;
		enabledBufferDeviceAddresFeatures.bufferDeviceAddress = VK_TRUE;

		enabledRayTracingPipelineFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR;
		enabledRayTracingPipelineFeatures.rayTracingPipeline = VK_TRUE;
		enabledRayTracingPipelineFeatures.pNext = &enabledBufferDeviceAddresFeatures;

		enabledAccelerationStructureFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
		enabledAccelerationStructureFeatures.accelerationStructure = VK_TRUE;
		enabledAccelerationStructureFeatures.pNext = &enabledRayTracingPipelineFeatures;

		deviceCreatepNextChain = &enabledAccelerationStructureFeatures;
	}

	void prepare()
	{
		VulkanRaytracingSample::prepare();

		// Create the acceleration structures used to render the ray traced scene
		createBottomLevelAccelerationStructure(true);
		createTopLevelAccelerationStructure(true);

		createStorageImage(swapChain.colorFormat, { width, height, 1 });
		createUniformBuffer();
		createRayTracingPipeline();
		createShaderBindingTables();
		createDescriptorSets();
		buildCommandBuffers();

		prepared = true;
	}

	void draw()
	{
		VulkanExampleBase::prepareFrame();
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &drawCmdBuffers[currentBuffer];

		VK_CHECK_RESULT(vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE));
		VulkanExampleBase::submitFrame();
	}



	virtual void render()
	{
		if (!prepared)
			return;

		m.lock();

		createBottomLevelAccelerationStructure(false);
		createTopLevelAccelerationStructure(false);
		
		paused = true;
		screenshot(1, NULL);
		paused = false;

		draw();

		if (!paused || camera.updated)
			updateUniformBuffers();

		m.unlock();
	}
};

VULKAN_EXAMPLE_MAIN()
