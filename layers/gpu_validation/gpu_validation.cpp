/* Copyright (c) 2018-2023 The Khronos Group Inc.
 * Copyright (c) 2018-2023 Valve Corporation
 * Copyright (c) 2018-2023 LunarG, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <cmath>
#include "utils/cast_utils.h"
#include "utils/shader_utils.h"
#include "gpu_validation/gpu_validation.h"
#include "spirv-tools/instrument.hpp"
#include "spirv-tools/linker.hpp"
#include "generated/layer_chassis_dispatch.h"
#include "gpu_vuids.h"
// Generated shaders
#include "gpu_shaders/gpu_shaders_constants.h"
#include "generated/gpu_pre_draw_vert.h"
#include "generated/gpu_pre_dispatch_comp.h"
#include "generated/gpu_as_inspection_comp.h"
#include "generated/inst_functions_comp.h"

// Keep in sync with the GLSL shader below.
struct GpuAccelerationStructureBuildValidationBuffer {
    uint32_t instances_to_validate;
    uint32_t replacement_handle_bits_0;
    uint32_t replacement_handle_bits_1;
    uint32_t invalid_handle_found;
    uint32_t invalid_handle_bits_0;
    uint32_t invalid_handle_bits_1;
    uint32_t valid_handles_count;
};

bool GpuAssisted::CheckForDescriptorIndexing(DeviceFeatures enabled_features) const {
    bool result =
        (IsExtEnabled(device_extensions.vk_ext_descriptor_indexing) &&
         (enabled_features.core12.descriptorIndexing || enabled_features.core12.shaderInputAttachmentArrayDynamicIndexing ||
          enabled_features.core12.shaderUniformTexelBufferArrayDynamicIndexing ||
          enabled_features.core12.shaderStorageTexelBufferArrayDynamicIndexing ||
          enabled_features.core12.shaderUniformBufferArrayNonUniformIndexing ||
          enabled_features.core12.shaderSampledImageArrayNonUniformIndexing ||
          enabled_features.core12.shaderStorageBufferArrayNonUniformIndexing ||
          enabled_features.core12.shaderStorageImageArrayNonUniformIndexing ||
          enabled_features.core12.shaderInputAttachmentArrayNonUniformIndexing ||
          enabled_features.core12.shaderUniformTexelBufferArrayNonUniformIndexing ||
          enabled_features.core12.shaderStorageTexelBufferArrayNonUniformIndexing ||
          enabled_features.core12.descriptorBindingUniformBufferUpdateAfterBind ||
          enabled_features.core12.descriptorBindingSampledImageUpdateAfterBind ||
          enabled_features.core12.descriptorBindingStorageImageUpdateAfterBind ||
          enabled_features.core12.descriptorBindingStorageBufferUpdateAfterBind ||
          enabled_features.core12.descriptorBindingUniformTexelBufferUpdateAfterBind ||
          enabled_features.core12.descriptorBindingStorageTexelBufferUpdateAfterBind ||
          enabled_features.core12.descriptorBindingUpdateUnusedWhilePending ||
          enabled_features.core12.descriptorBindingPartiallyBound ||
          enabled_features.core12.descriptorBindingVariableDescriptorCount || enabled_features.core12.runtimeDescriptorArray));
    return result;
}

void GpuAssisted::PreCallRecordCreateBuffer(VkDevice device, const VkBufferCreateInfo *pCreateInfo,
                                            const VkAllocationCallbacks *pAllocator, VkBuffer *pBuffer, void *cb_state_data) {
    create_buffer_api_state *cb_state = reinterpret_cast<create_buffer_api_state *>(cb_state_data);
    if (cb_state) {
        // Ray tracing acceleration structure instance buffers also need the storage buffer usage as
        // acceleration structure build validation will find and replace invalid acceleration structure
        // handles inside of a compute shader.
        if (cb_state->modified_create_info.usage & VK_BUFFER_USAGE_RAY_TRACING_BIT_NV) {
            cb_state->modified_create_info.usage |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        }

        // Indirect buffers will require validation shader to bind the indirect buffers as a storage buffer.
        if ((validate_draw_indirect || validate_dispatch_indirect) &&
            cb_state->modified_create_info.usage & VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT) {
            cb_state->modified_create_info.usage |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        }
    }

    ValidationStateTracker::PreCallRecordCreateBuffer(device, pCreateInfo, pAllocator, pBuffer, cb_state_data);
}
// Perform initializations that can be done at Create Device time.
void GpuAssisted::CreateDevice(const VkDeviceCreateInfo *pCreateInfo) {
    // GpuAssistedBase::CreateDevice will set up bindings
    VkDescriptorSetLayoutBinding binding = {0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1,
                                            VK_SHADER_STAGE_ALL_GRAPHICS | VK_SHADER_STAGE_COMPUTE_BIT |
                                                VK_SHADER_STAGE_MESH_BIT_EXT | VK_SHADER_STAGE_TASK_BIT_EXT |
                                                kShaderStageAllRayTracing,
                                            NULL};
    bindings_.push_back(binding);
    for (auto i = 1; i < 3; i++) {
        binding.binding = i;
        bindings_.push_back(binding);
    }
    GpuAssistedBase::CreateDevice(pCreateInfo);

    validate_descriptors = GpuGetOption("khronos_validation.gpuav_descriptor_checks", true);
    validate_draw_indirect = GpuGetOption("khronos_validation.validate_draw_indirect", true);
    validate_dispatch_indirect = GpuGetOption("khronos_validation.validate_dispatch_indirect", true);
    warn_on_robust_oob = GpuGetOption("khronos_validation.warn_on_robust_oob", true);
    validate_instrumented_shaders = (GetEnvironment("VK_LAYER_GPUAV_VALIDATE_INSTRUMENTED_SHADERS").size() > 0);

    if (api_version < VK_API_VERSION_1_1) {
        ReportSetupProblem(device, "GPU-Assisted validation requires Vulkan 1.1 or later.  GPU-Assisted Validation disabled.");
        aborted = true;
        return;
    }

    DispatchGetPhysicalDeviceFeatures(physical_device, &supported_features);
    if (!supported_features.fragmentStoresAndAtomics || !supported_features.vertexPipelineStoresAndAtomics) {
        ReportSetupProblem(device,
                           "GPU-Assisted validation requires fragmentStoresAndAtomics and vertexPipelineStoresAndAtomics.  "
                           "GPU-Assisted Validation disabled.");
        aborted = true;
        return;
    }

    shaderInt64 = supported_features.shaderInt64;
    if ((IsExtEnabled(device_extensions.vk_ext_buffer_device_address) ||
         IsExtEnabled(device_extensions.vk_khr_buffer_device_address)) &&
        !shaderInt64) {
        LogWarning(device, "UNASSIGNED-GPU-Assisted Validation Warning",
                   "shaderInt64 feature is not available.  No buffer device address checking will be attempted");
    }
    buffer_device_address = ((IsExtEnabled(device_extensions.vk_ext_buffer_device_address) ||
                              IsExtEnabled(device_extensions.vk_khr_buffer_device_address)) &&
                             shaderInt64 && enabled_features.core12.bufferDeviceAddress);

    if (buffer_device_address) {
        const char *size_string = getLayerOption("khronos_validation.gpuav_max_buffer_device_addresses");
        app_bda_max_addresses = *size_string ? atoi(size_string) : 10000;
        VkBufferCreateInfo buffer_info = vku::InitStructHelper();
        buffer_info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        VmaAllocationCreateInfo alloc_info = {};
        // We need 2 words per address (address and size), 1 word for the start of sizes index, 2 words for the address section
        // bounds, and 2 more words for the size section bounds
        app_bda_buffer_size = (1 + (app_bda_max_addresses + 2) + (app_bda_max_addresses + 2)) * 8;  // 64 bit words
        buffer_info.size = app_bda_buffer_size;
        // This buffer could be very large if an application uses many buffers. Allocating it as HOST_CACHED
        // and manually flushing it at the end of the state updates is faster than using HOST_COHERENT.
        alloc_info.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
        VkResult result = vmaCreateBuffer(vmaAllocator, &buffer_info, &alloc_info, &app_buffer_device_addresses.buffer,
                                          &app_buffer_device_addresses.allocation, nullptr);
        if (result != VK_SUCCESS) {
            ReportSetupProblem(
                device, "Unable to allocate device memory for buffer device address data.  Device could become unstable.", true);
            aborted = true;
            return;
        }
    }

    if (IsExtEnabled(device_extensions.vk_ext_descriptor_buffer)) {
        LogWarning(device, "UNASSIGNED-GPU-Assisted Validation Warning",
                   "VK_EXT_descriptor_buffer is enabled, but GPU-AV does not currently support validation of descriptor buffers. "
                   "No descriptor checking will be attempted");
        validate_descriptors = false;
    }

    output_buffer_size = sizeof(uint32_t) * (kInstMaxOutCnt + spvtools::kDebugOutputDataOffset);

    if (validate_descriptors && !force_buffer_device_address) {
        validate_descriptors = false;
        LogWarning(device, "UNASSIGNED-GPU-Assisted Validation Warning",
                   "Buffer Device Address + feature is not available.  No descriptor checking will be attempted");
    }

    const bool use_linear_output_pool = GpuGetOption("khronos_validation.vma_linear_output", true);
    if (use_linear_output_pool) {
        auto output_buffer_create_info = vku::InitStruct<VkBufferCreateInfo>();
        output_buffer_create_info.size = output_buffer_size;
        output_buffer_create_info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        VmaAllocationCreateInfo alloc_create_info = {};
        alloc_create_info.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        uint32_t mem_type_index;
        vmaFindMemoryTypeIndexForBufferInfo(vmaAllocator, &output_buffer_create_info, &alloc_create_info, &mem_type_index);
        VmaPoolCreateInfo pool_create_info = {};
        pool_create_info.memoryTypeIndex = mem_type_index;
        pool_create_info.blockSize = 0;
        pool_create_info.maxBlockCount = 0;
        pool_create_info.flags = VMA_POOL_CREATE_LINEAR_ALGORITHM_BIT;
        VkResult result = vmaCreatePool(vmaAllocator, &pool_create_info, &output_buffer_pool);
        if (result != VK_SUCCESS) {
            ReportSetupProblem(device, "Unable to create VMA memory pool");
        }
    }

    CreateAccelerationStructureBuildValidationState(pCreateInfo);
}

void GpuAssistedPreDrawValidationState::Destroy(VkDevice device) {
    if (shader_module != VK_NULL_HANDLE) {
        DispatchDestroyShaderModule(device, shader_module, nullptr);
        shader_module = VK_NULL_HANDLE;
    }
    if (ds_layout != VK_NULL_HANDLE) {
        DispatchDestroyDescriptorSetLayout(device, ds_layout, nullptr);
        ds_layout = VK_NULL_HANDLE;
    }
    if (pipeline_layout != VK_NULL_HANDLE) {
        DispatchDestroyPipelineLayout(device, pipeline_layout, nullptr);
        pipeline_layout = VK_NULL_HANDLE;
    }
    auto to_destroy = renderpass_to_pipeline.snapshot();
    for (auto &entry : to_destroy) {
        DispatchDestroyPipeline(device, entry.second, nullptr);
        renderpass_to_pipeline.erase(entry.first);
    }
    if (shader_object != VK_NULL_HANDLE) {
        DispatchDestroyShaderEXT(device, shader_object, nullptr);
        shader_object = VK_NULL_HANDLE;
    }
    initialized = false;
}

void GpuAssistedPreDispatchValidationState::Destroy(VkDevice device) {
    if (shader_module != VK_NULL_HANDLE) {
        DispatchDestroyShaderModule(device, shader_module, nullptr);
        shader_module = VK_NULL_HANDLE;
    }
    if (ds_layout != VK_NULL_HANDLE) {
        DispatchDestroyDescriptorSetLayout(device, ds_layout, nullptr);
        ds_layout = VK_NULL_HANDLE;
    }
    if (pipeline_layout != VK_NULL_HANDLE) {
        DispatchDestroyPipelineLayout(device, pipeline_layout, nullptr);
        pipeline_layout = VK_NULL_HANDLE;
    }
    if (pipeline != VK_NULL_HANDLE) {
        DispatchDestroyPipeline(device, pipeline, nullptr);
        pipeline = VK_NULL_HANDLE;
    }
    if (shader_object != VK_NULL_HANDLE) {
        DispatchDestroyShaderEXT(device, shader_object, nullptr);
        shader_object = VK_NULL_HANDLE;
    }
    initialized = false;
}

// Clean up device-related resources
void GpuAssisted::PreCallRecordDestroyDevice(VkDevice device, const VkAllocationCallbacks *pAllocator) {
    acceleration_structure_validation_state.Destroy(device, vmaAllocator);
    pre_draw_validation_state.Destroy(device);
    pre_dispatch_validation_state.Destroy(device);
    if (app_buffer_device_addresses.buffer) {
        vmaDestroyBuffer(vmaAllocator, app_buffer_device_addresses.buffer, app_buffer_device_addresses.allocation);
    }
    GpuAssistedBase::PreCallRecordDestroyDevice(device, pAllocator);
}

void GpuAssisted::CreateAccelerationStructureBuildValidationState(const VkDeviceCreateInfo *pCreateInfo) {
    if (aborted) {
        return;
    }

    auto &as_validation_state = acceleration_structure_validation_state;
    if (as_validation_state.initialized) {
        return;
    }

    if (!IsExtEnabled(device_extensions.vk_nv_ray_tracing)) {
        return;
    }

    // Cannot use this validation without a queue that supports graphics
    auto pd_state = Get<PHYSICAL_DEVICE_STATE>(physical_device);
    bool graphics_queue_exists = false;
    uint32_t graphics_queue_family = 0;
    for (uint32_t i = 0; i < pCreateInfo->queueCreateInfoCount; i++) {
        auto qfi = pCreateInfo->pQueueCreateInfos[i].queueFamilyIndex;
        if (pd_state->queue_family_properties[qfi].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            graphics_queue_family = qfi;
            graphics_queue_exists = true;
            break;
        }
    }
    if (!graphics_queue_exists) {
        LogWarning(device, "UNASSIGNED-GPU-Assisted Validation Warning", "No queue that supports graphics, GPU-AV aborted.");
        aborted = true;
        return;
    }

    // Outline:
    //   - Create valid bottom level acceleration structure which acts as replacement
    //      - Create and load vertex buffer
    //      - Create and load index buffer
    //      - Create, allocate memory for, and bind memory for acceleration structure
    //      - Query acceleration structure handle
    //      - Create command pool and command buffer
    //      - Record build acceleration structure command
    //      - Submit command buffer and wait for completion
    //      - Cleanup
    //  - Create compute pipeline for validating instance buffers
    //      - Create descriptor set layout
    //      - Create pipeline layout
    //      - Create pipeline
    //      - Cleanup

    VkResult result = VK_SUCCESS;

    VkBuffer vbo = VK_NULL_HANDLE;
    VmaAllocation vbo_allocation = VK_NULL_HANDLE;
    if (result == VK_SUCCESS) {
        auto vbo_ci = vku::InitStruct<VkBufferCreateInfo>();
        vbo_ci.size = sizeof(float) * 9;
        vbo_ci.usage = VK_BUFFER_USAGE_RAY_TRACING_BIT_NV;

        VmaAllocationCreateInfo vbo_ai = {};
        vbo_ai.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
        vbo_ai.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

        result = vmaCreateBuffer(vmaAllocator, &vbo_ci, &vbo_ai, &vbo, &vbo_allocation, nullptr);
        if (result != VK_SUCCESS) {
            ReportSetupProblem(device, "Failed to create vertex buffer for acceleration structure build validation.");
        }
    }

    if (result == VK_SUCCESS) {
        uint8_t *mapped_vbo_buffer = nullptr;
        result = vmaMapMemory(vmaAllocator, vbo_allocation, reinterpret_cast<void **>(&mapped_vbo_buffer));
        if (result != VK_SUCCESS) {
            ReportSetupProblem(device, "Failed to map vertex buffer for acceleration structure build validation.");
        } else {
            constexpr std::array vertices = {1.0f, 0.0f, 0.0f, 0.5f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f};
            std::memcpy(mapped_vbo_buffer, (uint8_t *)vertices.data(), sizeof(vertices[0]) * vertices.size());
            vmaUnmapMemory(vmaAllocator, vbo_allocation);
        }
    }

    VkBuffer ibo = VK_NULL_HANDLE;
    VmaAllocation ibo_allocation = VK_NULL_HANDLE;
    if (result == VK_SUCCESS) {
        auto ibo_ci = vku::InitStruct<VkBufferCreateInfo>();
        ibo_ci.size = sizeof(uint32_t) * 3;
        ibo_ci.usage = VK_BUFFER_USAGE_RAY_TRACING_BIT_NV;

        VmaAllocationCreateInfo ibo_ai = {};
        ibo_ai.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
        ibo_ai.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

        result = vmaCreateBuffer(vmaAllocator, &ibo_ci, &ibo_ai, &ibo, &ibo_allocation, nullptr);
        if (result != VK_SUCCESS) {
            ReportSetupProblem(device, "Failed to create index buffer for acceleration structure build validation.");
        }
    }

    if (result == VK_SUCCESS) {
        uint8_t *mapped_ibo_buffer = nullptr;
        result = vmaMapMemory(vmaAllocator, ibo_allocation, reinterpret_cast<void **>(&mapped_ibo_buffer));
        if (result != VK_SUCCESS) {
            ReportSetupProblem(device, "Failed to map index buffer for acceleration structure build validation.");
        } else {
            constexpr std::array<uint32_t, 3> indicies = {0, 1, 2};
            std::memcpy(mapped_ibo_buffer, (uint8_t *)indicies.data(), sizeof(indicies[0]) * indicies.size());
            vmaUnmapMemory(vmaAllocator, ibo_allocation);
        }
    }

    auto geometry = vku::InitStruct<VkGeometryNV>();
    geometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_NV;
    geometry.geometry.triangles = vku::InitStructHelper();
    geometry.geometry.triangles.vertexData = vbo;
    geometry.geometry.triangles.vertexOffset = 0;
    geometry.geometry.triangles.vertexCount = 3;
    geometry.geometry.triangles.vertexStride = 12;
    geometry.geometry.triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
    geometry.geometry.triangles.indexData = ibo;
    geometry.geometry.triangles.indexOffset = 0;
    geometry.geometry.triangles.indexCount = 3;
    geometry.geometry.triangles.indexType = VK_INDEX_TYPE_UINT32;
    geometry.geometry.triangles.transformData = VK_NULL_HANDLE;
    geometry.geometry.triangles.transformOffset = 0;
    geometry.geometry.aabbs = vku::InitStructHelper();

    auto as_ci = vku::InitStruct<VkAccelerationStructureCreateInfoNV>();
    as_ci.info = vku::InitStructHelper();
    as_ci.info.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_NV;
    as_ci.info.instanceCount = 0;
    as_ci.info.geometryCount = 1;
    as_ci.info.pGeometries = &geometry;
    if (result == VK_SUCCESS) {
        result = DispatchCreateAccelerationStructureNV(device, &as_ci, nullptr, &as_validation_state.replacement_as);
        if (result != VK_SUCCESS) {
            ReportSetupProblem(device, "Failed to create acceleration structure for acceleration structure build validation.");
        }
    }

    VkMemoryRequirements2 as_mem_requirements = {};
    if (result == VK_SUCCESS) {
        auto as_mem_requirements_info = vku::InitStruct<VkAccelerationStructureMemoryRequirementsInfoNV>();
        as_mem_requirements_info.type = VK_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_TYPE_OBJECT_NV;
        as_mem_requirements_info.accelerationStructure = as_validation_state.replacement_as;

        DispatchGetAccelerationStructureMemoryRequirementsNV(device, &as_mem_requirements_info, &as_mem_requirements);
    }

    VmaAllocationInfo as_memory_ai = {};
    if (result == VK_SUCCESS) {
        VmaAllocationCreateInfo as_memory_aci = {};
        as_memory_aci.usage = VMA_MEMORY_USAGE_GPU_ONLY;

        result = vmaAllocateMemory(vmaAllocator, &as_mem_requirements.memoryRequirements, &as_memory_aci,
                                   &as_validation_state.replacement_as_allocation, &as_memory_ai);
        if (result != VK_SUCCESS) {
            ReportSetupProblem(device,
                               "Failed to alloc acceleration structure memory for acceleration structure build validation.");
        }
    }

    if (result == VK_SUCCESS) {
        auto as_bind_info = vku::InitStruct<VkBindAccelerationStructureMemoryInfoNV>();
        as_bind_info.accelerationStructure = as_validation_state.replacement_as;
        as_bind_info.memory = as_memory_ai.deviceMemory;
        as_bind_info.memoryOffset = as_memory_ai.offset;

        result = DispatchBindAccelerationStructureMemoryNV(device, 1, &as_bind_info);
        if (result != VK_SUCCESS) {
            ReportSetupProblem(device, "Failed to bind acceleration structure memory for acceleration structure build validation.");
        }
    }

    if (result == VK_SUCCESS) {
        result = DispatchGetAccelerationStructureHandleNV(device, as_validation_state.replacement_as, sizeof(uint64_t),
                                                          &as_validation_state.replacement_as_handle);
        if (result != VK_SUCCESS) {
            ReportSetupProblem(device, "Failed to get acceleration structure handle for acceleration structure build validation.");
        }
    }

    VkMemoryRequirements2 scratch_mem_requirements = {};
    if (result == VK_SUCCESS) {
        auto scratch_mem_requirements_info = vku::InitStruct<VkAccelerationStructureMemoryRequirementsInfoNV>();
        scratch_mem_requirements_info.type = VK_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_TYPE_BUILD_SCRATCH_NV;
        scratch_mem_requirements_info.accelerationStructure = as_validation_state.replacement_as;

        DispatchGetAccelerationStructureMemoryRequirementsNV(device, &scratch_mem_requirements_info, &scratch_mem_requirements);
    }

    VkBuffer scratch = VK_NULL_HANDLE;
    VmaAllocation scratch_allocation = {};
    if (result == VK_SUCCESS) {
        auto scratch_ci = vku::InitStruct<VkBufferCreateInfo>();
        scratch_ci.size = scratch_mem_requirements.memoryRequirements.size;
        scratch_ci.usage = VK_BUFFER_USAGE_RAY_TRACING_BIT_NV;
        VmaAllocationCreateInfo scratch_aci = {};
        scratch_aci.usage = VMA_MEMORY_USAGE_GPU_ONLY;

        result = vmaCreateBuffer(vmaAllocator, &scratch_ci, &scratch_aci, &scratch, &scratch_allocation, nullptr);
        if (result != VK_SUCCESS) {
            ReportSetupProblem(device, "Failed to create scratch buffer for acceleration structure build validation.");
        }
    }

    VkCommandPool command_pool = VK_NULL_HANDLE;
    if (result == VK_SUCCESS) {
        auto command_pool_ci = vku::InitStruct<VkCommandPoolCreateInfo>();
        command_pool_ci.queueFamilyIndex = 0;

        result = DispatchCreateCommandPool(device, &command_pool_ci, nullptr, &command_pool);
        if (result != VK_SUCCESS) {
            ReportSetupProblem(device, "Failed to create command pool for acceleration structure build validation.");
        }
    }

    VkCommandBuffer command_buffer = VK_NULL_HANDLE;

    if (result == VK_SUCCESS) {
        auto command_buffer_ai = vku::InitStruct<VkCommandBufferAllocateInfo>();
        command_buffer_ai.commandPool = command_pool;
        command_buffer_ai.commandBufferCount = 1;
        command_buffer_ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;

        result = DispatchAllocateCommandBuffers(device, &command_buffer_ai, &command_buffer);
        if (result != VK_SUCCESS) {
            ReportSetupProblem(device, "Failed to create command buffer for acceleration structure build validation.");
        }

        // Hook up command buffer dispatch
        vkSetDeviceLoaderData(device, command_buffer);
    }

    if (result == VK_SUCCESS) {
        auto command_buffer_bi = vku::InitStruct<VkCommandBufferBeginInfo>();

        result = DispatchBeginCommandBuffer(command_buffer, &command_buffer_bi);
        if (result != VK_SUCCESS) {
            ReportSetupProblem(device, "Failed to begin command buffer for acceleration structure build validation.");
        }
    }

    if (result == VK_SUCCESS) {
        DispatchCmdBuildAccelerationStructureNV(command_buffer, &as_ci.info, VK_NULL_HANDLE, 0, VK_FALSE,
                                                as_validation_state.replacement_as, VK_NULL_HANDLE, scratch, 0);
        DispatchEndCommandBuffer(command_buffer);
    }

    VkQueue queue = VK_NULL_HANDLE;
    if (result == VK_SUCCESS) {
        DispatchGetDeviceQueue(device, graphics_queue_family, 0, &queue);

        // Hook up queue dispatch
        vkSetDeviceLoaderData(device, queue);

        auto submit_info = vku::InitStruct<VkSubmitInfo>();
        submit_info.commandBufferCount = 1;
        submit_info.pCommandBuffers = &command_buffer;
        result = DispatchQueueSubmit(queue, 1, &submit_info, VK_NULL_HANDLE);
        if (result != VK_SUCCESS) {
            ReportSetupProblem(device, "Failed to submit command buffer for acceleration structure build validation.");
        }
    }

    if (result == VK_SUCCESS) {
        result = DispatchQueueWaitIdle(queue);
        if (result != VK_SUCCESS) {
            ReportSetupProblem(device, "Failed to wait for queue idle for acceleration structure build validation.");
        }
    }

    if (vbo != VK_NULL_HANDLE) {
        vmaDestroyBuffer(vmaAllocator, vbo, vbo_allocation);
    }
    if (ibo != VK_NULL_HANDLE) {
        vmaDestroyBuffer(vmaAllocator, ibo, ibo_allocation);
    }
    if (scratch != VK_NULL_HANDLE) {
        vmaDestroyBuffer(vmaAllocator, scratch, scratch_allocation);
    }
    if (command_pool != VK_NULL_HANDLE) {
        DispatchDestroyCommandPool(device, command_pool, nullptr);
    }

    if (debug_desc_layout == VK_NULL_HANDLE) {
        ReportSetupProblem(device, "Failed to find descriptor set layout for acceleration structure build validation.");
        result = VK_INCOMPLETE;
    }

    if (result == VK_SUCCESS) {
        auto pipeline_layout_ci = vku::InitStruct<VkPipelineLayoutCreateInfo>();
        pipeline_layout_ci.setLayoutCount = 1;
        pipeline_layout_ci.pSetLayouts = &debug_desc_layout;
        result = DispatchCreatePipelineLayout(device, &pipeline_layout_ci, 0, &as_validation_state.pipeline_layout);
        if (result != VK_SUCCESS) {
            ReportSetupProblem(device, "Failed to create pipeline layout for acceleration structure build validation.");
        }
    }

    VkShaderModule shader_module = VK_NULL_HANDLE;
    if (result == VK_SUCCESS) {
        auto shader_module_ci = vku::InitStruct<VkShaderModuleCreateInfo>();
        shader_module_ci.codeSize = sizeof(gpu_as_inspection_comp);
        shader_module_ci.pCode = gpu_as_inspection_comp;

        result = DispatchCreateShaderModule(device, &shader_module_ci, nullptr, &shader_module);
        if (result != VK_SUCCESS) {
            ReportSetupProblem(device, "Failed to create compute shader module for acceleration structure build validation.");
        }
    }

    if (result == VK_SUCCESS) {
        auto pipeline_stage_ci = vku::InitStruct<VkPipelineShaderStageCreateInfo>();
        pipeline_stage_ci.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        pipeline_stage_ci.module = shader_module;
        pipeline_stage_ci.pName = "main";

        auto pipeline_ci = vku::InitStruct<VkComputePipelineCreateInfo>();
        pipeline_ci.stage = pipeline_stage_ci;
        pipeline_ci.layout = as_validation_state.pipeline_layout;

        result = DispatchCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipeline_ci, nullptr, &as_validation_state.pipeline);
        if (result != VK_SUCCESS) {
            ReportSetupProblem(device, "Failed to create compute pipeline for acceleration structure build validation.");
        }
    }

    if (shader_module != VK_NULL_HANDLE) {
        DispatchDestroyShaderModule(device, shader_module, nullptr);
    }

    if (result == VK_SUCCESS) {
        as_validation_state.initialized = true;
        LogInfo(device, "UNASSIGNED-GPU-Assisted Validation.", "Acceleration Structure Building GPU Validation Enabled.");
    } else {
        aborted = true;
    }
}

void GpuAssistedAccelerationStructureBuildValidationState::Destroy(VkDevice device, VmaAllocator &vmaAllocator) {
    if (pipeline != VK_NULL_HANDLE) {
        DispatchDestroyPipeline(device, pipeline, nullptr);
        pipeline = VK_NULL_HANDLE;
    }
    if (pipeline_layout != VK_NULL_HANDLE) {
        DispatchDestroyPipelineLayout(device, pipeline_layout, nullptr);
        pipeline_layout = VK_NULL_HANDLE;
    }
    if (replacement_as != VK_NULL_HANDLE) {
        DispatchDestroyAccelerationStructureNV(device, replacement_as, nullptr);
        replacement_as = VK_NULL_HANDLE;
    }
    if (replacement_as_allocation != VK_NULL_HANDLE) {
        vmaFreeMemory(vmaAllocator, replacement_as_allocation);
        replacement_as_allocation = VK_NULL_HANDLE;
    }
    initialized = false;
}

struct GPUAV_RESTORABLE_PIPELINE_STATE {
    VkPipelineBindPoint pipeline_bind_point = VK_PIPELINE_BIND_POINT_MAX_ENUM;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
    std::vector<std::pair<VkDescriptorSet, uint32_t>> descriptor_sets;
    std::vector<std::vector<uint32_t>> dynamic_offsets;
    uint32_t push_descriptor_set_index = 0;
    std::vector<safe_VkWriteDescriptorSet> push_descriptor_set_writes;
    std::vector<uint8_t> push_constants_data;
    PushConstantRangesId push_constants_ranges;

    void Create(CMD_BUFFER_STATE *cb_state, VkPipelineBindPoint bind_point) {
        pipeline_bind_point = bind_point;
        const auto lv_bind_point = ConvertToLvlBindPoint(bind_point);

        LAST_BOUND_STATE &last_bound = cb_state->lastBound[lv_bind_point];
        if (last_bound.pipeline_state) {
            pipeline = last_bound.pipeline_state->pipeline();
            pipeline_layout = last_bound.pipeline_layout;
            descriptor_sets.reserve(last_bound.per_set.size());
            for (std::size_t i = 0; i < last_bound.per_set.size(); i++) {
                const auto &bound_descriptor_set = last_bound.per_set[i].bound_descriptor_set;
                if (bound_descriptor_set) {
                    descriptor_sets.push_back(std::make_pair(bound_descriptor_set->GetSet(), static_cast<uint32_t>(i)));
                    if (bound_descriptor_set->IsPushDescriptor()) {
                        push_descriptor_set_index = static_cast<uint32_t>(i);
                    }
                    dynamic_offsets.push_back(last_bound.per_set[i].dynamicOffsets);
                }
            }

            if (last_bound.push_descriptor_set) {
                push_descriptor_set_writes = last_bound.push_descriptor_set->GetWrites();
            }
            const auto &pipeline_layout = last_bound.pipeline_state->PipelineLayoutState();
            if (pipeline_layout->push_constant_ranges == cb_state->push_constant_data_ranges) {
                push_constants_data = cb_state->push_constant_data;
                push_constants_ranges = pipeline_layout->push_constant_ranges;
            }
        }
    }

    void Restore(VkCommandBuffer command_buffer) const {
        if (pipeline != VK_NULL_HANDLE) {
            DispatchCmdBindPipeline(command_buffer, pipeline_bind_point, pipeline);
            if (!descriptor_sets.empty()) {
                for (std::size_t i = 0; i < descriptor_sets.size(); i++) {
                    VkDescriptorSet descriptor_set = descriptor_sets[i].first;
                    if (descriptor_set != VK_NULL_HANDLE) {
                        DispatchCmdBindDescriptorSets(command_buffer, pipeline_bind_point, pipeline_layout,
                                                      descriptor_sets[i].second, 1, &descriptor_set,
                                                      static_cast<uint32_t>(dynamic_offsets[i].size()), dynamic_offsets[i].data());
                    }
                }
            }
            if (!push_descriptor_set_writes.empty()) {
                DispatchCmdPushDescriptorSetKHR(command_buffer, pipeline_bind_point, pipeline_layout, push_descriptor_set_index,
                                                static_cast<uint32_t>(push_descriptor_set_writes.size()),
                                                reinterpret_cast<const VkWriteDescriptorSet *>(push_descriptor_set_writes.data()));
            }
            if (!push_constants_data.empty()) {
                for (const auto &push_constant_range : *push_constants_ranges) {
                    if (push_constant_range.size == 0) continue;
                    DispatchCmdPushConstants(command_buffer, pipeline_layout, push_constant_range.stageFlags,
                                             push_constant_range.offset, push_constant_range.size, push_constants_data.data());
                }
            }
        }
    }
};

void GpuAssisted::PreCallRecordCmdBuildAccelerationStructureNV(VkCommandBuffer commandBuffer,
                                                               const VkAccelerationStructureInfoNV *pInfo, VkBuffer instanceData,
                                                               VkDeviceSize instanceOffset, VkBool32 update,
                                                               VkAccelerationStructureNV dst, VkAccelerationStructureNV src,
                                                               VkBuffer scratch, VkDeviceSize scratchOffset) {
    ValidationStateTracker::PreCallRecordCmdBuildAccelerationStructureNV(commandBuffer, pInfo, instanceData, instanceOffset, update,
                                                                         dst, src, scratch, scratchOffset);
    if (pInfo == nullptr || pInfo->type != VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_NV) {
        return;
    }

    auto &as_validation_state = acceleration_structure_validation_state;
    if (!as_validation_state.initialized) {
        return;
    }

    // Empty acceleration structure is valid according to the spec.
    if (pInfo->instanceCount == 0 || instanceData == VK_NULL_HANDLE) {
        return;
    }

    auto cb_state = GetWrite<gpuav_state::CommandBuffer>(commandBuffer);
    assert(cb_state != nullptr);

    std::vector<uint64_t> current_valid_handles;
    ForEach<ACCELERATION_STRUCTURE_STATE_NV>([&current_valid_handles](const ACCELERATION_STRUCTURE_STATE_NV &as_state) {
        if (as_state.built && as_state.create_infoNV.info.type == VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_NV) {
            current_valid_handles.push_back(as_state.opaque_handle);
        }
    });

    GpuAssistedAccelerationStructureBuildValidationBufferInfo as_validation_buffer_info = {};
    as_validation_buffer_info.acceleration_structure = dst;

    const VkDeviceSize validation_buffer_size =
        // One uint for number of instances to validate
        4 +
        // Two uint for the replacement acceleration structure handle
        8 +
        // One uint for number of invalid handles found
        4 +
        // Two uint for the first invalid handle found
        8 +
        // One uint for the number of current valid handles
        4 +
        // Two uint for each current valid handle
        (8 * current_valid_handles.size());

    auto validation_buffer_create_info = vku::InitStruct<VkBufferCreateInfo>();
    validation_buffer_create_info.size = validation_buffer_size;
    validation_buffer_create_info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;

    VmaAllocationCreateInfo validation_buffer_alloc_info = {};
    validation_buffer_alloc_info.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;

    VkResult result = vmaCreateBuffer(vmaAllocator, &validation_buffer_create_info, &validation_buffer_alloc_info,
                                      &as_validation_buffer_info.buffer, &as_validation_buffer_info.buffer_allocation, nullptr);
    if (result != VK_SUCCESS) {
        ReportSetupProblem(device, "Unable to allocate device memory.  Device could become unstable.");
        aborted = true;
        return;
    }

    GpuAccelerationStructureBuildValidationBuffer *mapped_validation_buffer = nullptr;
    result = vmaMapMemory(vmaAllocator, as_validation_buffer_info.buffer_allocation,
                          reinterpret_cast<void **>(&mapped_validation_buffer));
    if (result != VK_SUCCESS) {
        ReportSetupProblem(device, "Unable to allocate device memory for acceleration structure build val buffer.");
        aborted = true;
        return;
    }

    mapped_validation_buffer->instances_to_validate = pInfo->instanceCount;
    {
        const auto replacement_as_handle = vvl_bit_cast<std::array<uint32_t, 2>>(as_validation_state.replacement_as_handle);
        mapped_validation_buffer->replacement_handle_bits_0 = replacement_as_handle[0];
        mapped_validation_buffer->replacement_handle_bits_1 = replacement_as_handle[1];
    }
    mapped_validation_buffer->invalid_handle_found = 0;
    mapped_validation_buffer->invalid_handle_bits_0 = 0;
    mapped_validation_buffer->invalid_handle_bits_1 = 0;
    mapped_validation_buffer->valid_handles_count = static_cast<uint32_t>(current_valid_handles.size());

    uint32_t *mapped_valid_handles = reinterpret_cast<uint32_t *>(&mapped_validation_buffer[1]);
    for (std::size_t i = 0; i < current_valid_handles.size(); i++) {
        const auto current_valid_handle = vvl_bit_cast<std::array<uint32_t, 2>>(current_valid_handles[i]);

        *mapped_valid_handles = current_valid_handle[0];
        ++mapped_valid_handles;
        *mapped_valid_handles = current_valid_handle[1];
        ++mapped_valid_handles;
    }

    vmaUnmapMemory(vmaAllocator, as_validation_buffer_info.buffer_allocation);

    static constexpr const VkDeviceSize k_instance_size = 64;
    const VkDeviceSize instance_buffer_size = k_instance_size * pInfo->instanceCount;

    result = desc_set_manager->GetDescriptorSet(&as_validation_buffer_info.descriptor_pool, debug_desc_layout,
                                                &as_validation_buffer_info.descriptor_set);
    if (result != VK_SUCCESS) {
        ReportSetupProblem(device, "Unable to get descriptor set for acceleration structure build.");
        aborted = true;
        return;
    }

    VkDescriptorBufferInfo descriptor_buffer_infos[2] = {};
    descriptor_buffer_infos[0].buffer = instanceData;
    descriptor_buffer_infos[0].offset = instanceOffset;
    descriptor_buffer_infos[0].range = instance_buffer_size;
    descriptor_buffer_infos[1].buffer = as_validation_buffer_info.buffer;
    descriptor_buffer_infos[1].offset = 0;
    descriptor_buffer_infos[1].range = validation_buffer_size;

    VkWriteDescriptorSet descriptor_set_writes[2] = {
        vku::InitStruct<VkWriteDescriptorSet>(),
        vku::InitStruct<VkWriteDescriptorSet>(),
    };
    descriptor_set_writes[0].dstSet = as_validation_buffer_info.descriptor_set;
    descriptor_set_writes[0].dstBinding = 0;
    descriptor_set_writes[0].descriptorCount = 1;
    descriptor_set_writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    descriptor_set_writes[0].pBufferInfo = &descriptor_buffer_infos[0];
    descriptor_set_writes[1].dstSet = as_validation_buffer_info.descriptor_set;
    descriptor_set_writes[1].dstBinding = 1;
    descriptor_set_writes[1].descriptorCount = 1;
    descriptor_set_writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    descriptor_set_writes[1].pBufferInfo = &descriptor_buffer_infos[1];

    DispatchUpdateDescriptorSets(device, 2, descriptor_set_writes, 0, nullptr);

    // Issue a memory barrier to make sure anything writing to the instance buffer has finished.
    auto memory_barrier = vku::InitStruct<VkMemoryBarrier>();
    memory_barrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
    memory_barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    DispatchCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1,
                               &memory_barrier, 0, nullptr, 0, nullptr);

    // Save a copy of the compute pipeline state that needs to be restored.
    GPUAV_RESTORABLE_PIPELINE_STATE restorable_state;
    restorable_state.Create(cb_state.get(), VK_PIPELINE_BIND_POINT_COMPUTE);

    // Switch to and launch the validation compute shader to find, replace, and report invalid acceleration structure handles.
    DispatchCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, as_validation_state.pipeline);
    DispatchCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, as_validation_state.pipeline_layout, 0, 1,
                                  &as_validation_buffer_info.descriptor_set, 0, nullptr);
    DispatchCmdDispatch(commandBuffer, 1, 1, 1);

    // Issue a buffer memory barrier to make sure that any invalid bottom level acceleration structure handles
    // have been replaced by the validation compute shader before any builds take place.
    auto instance_buffer_barrier = vku::InitStruct<VkBufferMemoryBarrier>();
    instance_buffer_barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    instance_buffer_barrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_NV;
    instance_buffer_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    instance_buffer_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    instance_buffer_barrier.buffer = instanceData;
    instance_buffer_barrier.offset = instanceOffset;
    instance_buffer_barrier.size = instance_buffer_size;
    DispatchCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                               VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_NV, 0, 0, nullptr, 1, &instance_buffer_barrier, 0,
                               nullptr);

    // Restore the previous compute pipeline state.
    restorable_state.Restore(commandBuffer);

    cb_state->as_validation_buffers.emplace_back(std::move(as_validation_buffer_info));
}

void gpuav_state::CommandBuffer::ProcessAccelerationStructure(VkQueue queue) {
    if (!has_build_as_cmd) {
        return;
    }
    auto *device_state = static_cast<GpuAssisted *>(dev_data);
    for (const auto &as_validation_buffer_info : as_validation_buffers) {
        GpuAccelerationStructureBuildValidationBuffer *mapped_validation_buffer = nullptr;

        VkResult result = vmaMapMemory(device_state->vmaAllocator, as_validation_buffer_info.buffer_allocation,
                                       reinterpret_cast<void **>(&mapped_validation_buffer));
        if (result == VK_SUCCESS) {
            if (mapped_validation_buffer->invalid_handle_found > 0) {
                const std::array<uint32_t, 2> invalid_handles = {mapped_validation_buffer->invalid_handle_bits_0,
                                                                 mapped_validation_buffer->invalid_handle_bits_1};
                const uint64_t invalid_handle = vvl_bit_cast<uint64_t>(invalid_handles);

                device_state->LogError(
                    as_validation_buffer_info.acceleration_structure, "UNASSIGNED-AccelerationStructure",
                    "Attempted to build top level acceleration structure using invalid bottom level acceleration structure "
                    "handle (%" PRIu64 ")",
                    invalid_handle);
            }
            vmaUnmapMemory(device_state->vmaAllocator, as_validation_buffer_info.buffer_allocation);
        }
    }
}

void GpuAssisted::PostCallRecordBindAccelerationStructureMemoryNV(VkDevice device, uint32_t bindInfoCount,
                                                                  const VkBindAccelerationStructureMemoryInfoNV *pBindInfos,
                                                                  const RecordObject &record_obj) {
    if (VK_SUCCESS != record_obj.result) return;
    ValidationStateTracker::PostCallRecordBindAccelerationStructureMemoryNV(device, bindInfoCount, pBindInfos, record_obj);
    for (uint32_t i = 0; i < bindInfoCount; i++) {
        const VkBindAccelerationStructureMemoryInfoNV &info = pBindInfos[i];
        auto as_state = Get<ACCELERATION_STRUCTURE_STATE_NV>(info.accelerationStructure);
        if (as_state) {
            DispatchGetAccelerationStructureHandleNV(device, info.accelerationStructure, 8, &as_state->opaque_handle);
        }
    }
}

// Free the device memory and descriptor set(s) associated with a command buffer.
void GpuAssisted::DestroyBuffer(GpuAssistedBufferInfo &buffer_info) {
    vmaDestroyBuffer(vmaAllocator, buffer_info.output_mem_block.buffer, buffer_info.output_mem_block.allocation);
    if (buffer_info.desc_set != VK_NULL_HANDLE) {
        desc_set_manager->PutBackDescriptorSet(buffer_info.desc_pool, buffer_info.desc_set);
    }
    if (buffer_info.pre_draw_resources.desc_set != VK_NULL_HANDLE) {
        desc_set_manager->PutBackDescriptorSet(buffer_info.pre_draw_resources.desc_pool, buffer_info.pre_draw_resources.desc_set);
    }
    if (buffer_info.pre_dispatch_resources.desc_set != VK_NULL_HANDLE) {
        desc_set_manager->PutBackDescriptorSet(buffer_info.pre_dispatch_resources.desc_pool,
                                               buffer_info.pre_dispatch_resources.desc_set);
    }
}

void GpuAssisted::DestroyBuffer(GpuAssistedAccelerationStructureBuildValidationBufferInfo &as_validation_buffer_info) {
    vmaDestroyBuffer(vmaAllocator, as_validation_buffer_info.buffer, as_validation_buffer_info.buffer_allocation);

    if (as_validation_buffer_info.descriptor_set != VK_NULL_HANDLE) {
        desc_set_manager->PutBackDescriptorSet(as_validation_buffer_info.descriptor_pool, as_validation_buffer_info.descriptor_set);
    }
}

void GpuAssisted::PostCallRecordGetPhysicalDeviceProperties(VkPhysicalDevice physicalDevice,
                                                            VkPhysicalDeviceProperties *pPhysicalDeviceProperties,
                                                            const RecordObject &record_obj) {
    // There is an implicit layer that can cause this call to return 0 for maxBoundDescriptorSets - Ignore such calls
    if (enabled[gpu_validation_reserve_binding_slot] && pPhysicalDeviceProperties->limits.maxBoundDescriptorSets > 0) {
        if (pPhysicalDeviceProperties->limits.maxBoundDescriptorSets > 1) {
            pPhysicalDeviceProperties->limits.maxBoundDescriptorSets -= 1;
        } else {
            LogWarning(physicalDevice, "UNASSIGNED-GPU-Assisted Validation Setup Error.",
                       "Unable to reserve descriptor binding slot on a device with only one slot.");
        }
    }
    ValidationStateTracker::PostCallRecordGetPhysicalDeviceProperties(physicalDevice, pPhysicalDeviceProperties, record_obj);
}

void GpuAssisted::PostCallRecordGetPhysicalDeviceProperties2(VkPhysicalDevice physicalDevice,
                                                             VkPhysicalDeviceProperties2 *pPhysicalDeviceProperties2,
                                                             const RecordObject &record_obj) {
    // There is an implicit layer that can cause this call to return 0 for maxBoundDescriptorSets - Ignore such calls
    if (enabled[gpu_validation_reserve_binding_slot] && pPhysicalDeviceProperties2->properties.limits.maxBoundDescriptorSets > 0) {
        if (pPhysicalDeviceProperties2->properties.limits.maxBoundDescriptorSets > 1) {
            pPhysicalDeviceProperties2->properties.limits.maxBoundDescriptorSets -= 1;
        } else {
            LogWarning(physicalDevice, "UNASSIGNED-GPU-Assisted Validation Setup Error.",
                       "Unable to reserve descriptor binding slot on a device with only one slot.");
        }
    }
    ValidationStateTracker::PostCallRecordGetPhysicalDeviceProperties2(physicalDevice, pPhysicalDeviceProperties2, record_obj);
}

void GpuAssisted::PreCallRecordDestroyRenderPass(VkDevice device, VkRenderPass renderPass,
                                                 const VkAllocationCallbacks *pAllocator) {
    auto pipeline = pre_draw_validation_state.renderpass_to_pipeline.pop(renderPass);
    if (pipeline != pre_draw_validation_state.renderpass_to_pipeline.end()) {
        DispatchDestroyPipeline(device, pipeline->second, nullptr);
    }
    ValidationStateTracker::PreCallRecordDestroyRenderPass(device, renderPass, pAllocator);
}

bool GpuValidateShader(const vvl::span<const uint32_t> &input, bool SetRelaxBlockLayout, bool SetScalerBlockLayout,
                       std::string &error) {
    // Use SPIRV-Tools validator to try and catch any issues with the module
    spv_target_env spirv_environment = SPV_ENV_VULKAN_1_1;
    spv_context ctx = spvContextCreate(spirv_environment);
    spv_const_binary_t binary{input.data(), input.size()};
    spv_diagnostic diag = nullptr;
    spv_validator_options options = spvValidatorOptionsCreate();
    spvValidatorOptionsSetRelaxBlockLayout(options, SetRelaxBlockLayout);
    spvValidatorOptionsSetScalarBlockLayout(options, SetScalerBlockLayout);
    spv_result_t result = spvValidateWithOptions(ctx, options, &binary, &diag);
    if (result != SPV_SUCCESS && diag) error = diag->error;
    return (result == SPV_SUCCESS);
}

// Call the SPIR-V Optimizer to run the instrumentation pass on the shader.
bool GpuAssisted::InstrumentShader(const vvl::span<const uint32_t> &input, std::vector<uint32_t> &new_pgm,
                                   uint32_t *unique_shader_id) {
    if (aborted) return false;
    if (input[0] != spv::MagicNumber) return false;

    const spvtools::MessageConsumer gpu_console_message_consumer =
        [this](spv_message_level_t level, const char *, const spv_position_t &position, const char *message) -> void {
        switch (level) {
            case SPV_MSG_FATAL:
            case SPV_MSG_INTERNAL_ERROR:
            case SPV_MSG_ERROR:
                this->LogError(this->device, "UNASSIGNED-GPU-Assisted", "Error during shader instrumentation: line %zu: %s",
                               position.index, message);
                break;
            default:
                break;
        }
    };
    std::vector<std::vector<uint32_t>> binaries(2);

    // Load original shader SPIR-V
    binaries[0].reserve(input.size());
    binaries[0].insert(binaries[0].end(), &input.front(), &input.back() + 1);

    // Call the optimizer to instrument the shader.
    // Use the unique_shader_module_id as a shader ID so we can look up its handle later in the shader_map.
    // If descriptor indexing is enabled, enable length checks and updated descriptor checks
    using namespace spvtools;
    spv_target_env target_env = PickSpirvEnv(api_version, IsExtEnabled(device_extensions.vk_khr_spirv_1_4));
    *unique_shader_id = unique_shader_module_id++;
    // Instrument the user's shader
    {
        ValidatorOptions val_options;
        AdjustValidatorOptions(device_extensions, enabled_features, val_options);
        OptimizerOptions opt_options;
        opt_options.set_run_validator(true);
        opt_options.set_validator_options(val_options);
        Optimizer inst_passes(target_env);
        inst_passes.SetMessageConsumer(gpu_console_message_consumer);
        if (validate_descriptors) {
            inst_passes.RegisterPass(CreateInstBindlessCheckPass(*unique_shader_id));
        }

        if ((IsExtEnabled(device_extensions.vk_ext_buffer_device_address) ||
             IsExtEnabled(device_extensions.vk_khr_buffer_device_address)) &&
            shaderInt64 && enabled_features.core12.bufferDeviceAddress) {
            inst_passes.RegisterPass(CreateInstBuffAddrCheckPass(*unique_shader_id));
        }
        if (!inst_passes.Run(binaries[0].data(), binaries[0].size(), &binaries[0], opt_options)) {
            ReportSetupProblem(device, "Failure to instrument shader.  Proceeding with non-instrumented shader.");
            assert(false);
            return false;
        }
    }
    {
        // The instrumentation code is not a complete SPIRV module so we cannot validate it separately
        OptimizerOptions options;
        options.set_run_validator(false);
        // Load instrumentation helper functions
        size_t inst_size = sizeof(inst_functions_comp) / sizeof(uint32_t);
        binaries[1].reserve(inst_size);  // the shader will be copied in by the optimizer

        // The compiled instrumentation functions use 7 for their data.
        // Switch that to the highest set number supported by the actual VkDevice.
        Optimizer switch_descriptorsets(target_env);
        switch_descriptorsets.SetMessageConsumer(gpu_console_message_consumer);
        switch_descriptorsets.RegisterPass(CreateSwitchDescriptorSetPass(7, desc_set_bind_index));

        if (!switch_descriptorsets.Run(inst_functions_comp, inst_size, &binaries[1], options)) {
            ReportSetupProblem(
                device, "Failure to switch descriptorsets in instrumentation code. Proceeding with non-instrumented shader.");
            assert(false);
            return false;
        }
    }
    // Link in the instrumentation helper functions
    {
        Context context(target_env);
        context.SetMessageConsumer(gpu_console_message_consumer);
        LinkerOptions link_options;
        link_options.SetUseHighestVersion(true);

        spv_result_t link_status = Link(context, binaries, &new_pgm, link_options);
        if (link_status != SPV_SUCCESS && link_status != SPV_WARNING) {
            std::ostringstream strm;
            strm << "Failed to link Instrumented shader, error = " << link_status << " Proceeding with non instrumented shader.";
            ReportSetupProblem(device, strm.str().c_str());
            assert(false);
            return false;
        }
    }
    // (Maybe) validate the instrumented and linked shader
    if (validate_instrumented_shaders) {
        std::string instrumented_error;
        if (!GpuValidateShader(new_pgm, device_extensions.vk_khr_relaxed_block_layout, device_extensions.vk_ext_scalar_block_layout,
                               instrumented_error)) {
            std::ostringstream strm;
            strm << "Instrumented shader is invalid, error = " << instrumented_error << " Proceeding with non instrumented shader.";
            ReportSetupProblem(device, strm.str().c_str());
            assert(false);
            return false;
        }
    }
    // Run Dead Code elimination
    {
        OptimizerOptions opt_options;
        opt_options.set_run_validator(false);
        Optimizer dce_pass(target_env);
        dce_pass.SetMessageConsumer(gpu_console_message_consumer);
        // Call CreateAggressiveDCEPass with preserve_interface == true
        dce_pass.RegisterPass(CreateAggressiveDCEPass(true));
        if (!dce_pass.Run(new_pgm.data(), new_pgm.size(), &new_pgm, opt_options)) {
            ReportSetupProblem(device, "Failure to run DCE on instrumented shader.  Proceeding with non-instrumented shader.");
            assert(false);
            return false;
        }
    }
    return true;
}

// Create the instrumented shader data to provide to the driver.
void GpuAssisted::PreCallRecordCreateShaderModule(VkDevice device, const VkShaderModuleCreateInfo *pCreateInfo,
                                                  const VkAllocationCallbacks *pAllocator, VkShaderModule *pShaderModule,
                                                  void *csm_state_data) {
    ValidationStateTracker::PreCallRecordCreateShaderModule(device, pCreateInfo, pAllocator, pShaderModule, csm_state_data);
    create_shader_module_api_state *csm_state = static_cast<create_shader_module_api_state *>(csm_state_data);
    const bool pass = InstrumentShader(vvl::make_span(pCreateInfo->pCode, pCreateInfo->codeSize / sizeof(uint32_t)),
                                       csm_state->instrumented_spirv, &csm_state->unique_shader_id);
    if (pass) {
        csm_state->instrumented_create_info.pCode = csm_state->instrumented_spirv.data();
        csm_state->instrumented_create_info.codeSize = csm_state->instrumented_spirv.size() * sizeof(uint32_t);
    }
}

void GpuAssisted::PreCallRecordCreateShadersEXT(VkDevice device, uint32_t createInfoCount,
                                                const VkShaderCreateInfoEXT *pCreateInfos, const VkAllocationCallbacks *pAllocator,
                                                VkShaderEXT *pShaders, void *csm_state_data) {
    ValidationStateTracker::PreCallRecordCreateShadersEXT(device, createInfoCount, pCreateInfos, pAllocator, pShaders,
                                                          csm_state_data);
    GpuAssistedBase::PreCallRecordCreateShadersEXT(device, createInfoCount, pCreateInfos, pAllocator, pShaders, csm_state_data);
    create_shader_object_api_state *csm_state = static_cast<create_shader_object_api_state *>(csm_state_data);
    for (uint32_t i = 0; i < createInfoCount; ++i) {
        const bool pass = InstrumentShader(
            vvl::make_span(static_cast<const uint32_t *>(pCreateInfos[i].pCode), pCreateInfos[i].codeSize / sizeof(uint32_t)),
            csm_state->instrumented_spirv[i], &csm_state->unique_shader_ids[i]);
        if (pass) {
            csm_state->instrumented_create_info[i].pCode = csm_state->instrumented_spirv[i].data();
            csm_state->instrumented_create_info[i].codeSize = csm_state->instrumented_spirv[i].size() * sizeof(uint32_t);
        }
    }
}

// Generate the part of the message describing the violation.
bool GenerateValidationMessage(const uint32_t *debug_record, std::string &msg, std::string &vuid_msg, bool &oob_access,
                               const GpuAssistedBufferInfo &buf_info, GpuAssisted *gpu_assisted,
                               const std::vector<GpuAssistedDescSetState> &descriptor_sets) {
    std::ostringstream strm;
    bool return_code = true;
    const GpuVuid vuid = GetGpuVuid(buf_info.command);
    oob_access = false;
    switch (debug_record[kInstValidationOutError]) {
        case kInstErrorBindlessBounds: {
            strm << "(set = " <<  debug_record[kInstBindlessBoundsOutDescSet] << ", binding = " << debug_record[kInstBindlessBoundsOutDescBinding] << ") Index of "
                 << debug_record[kInstBindlessBoundsOutDescIndex] << " used to index descriptor array of length " << debug_record[kInstBindlessBoundsOutDescBound] << ". ";
            vuid_msg = "UNASSIGNED-Descriptor index out of bounds";
        } break;
        case kInstErrorBindlessUninit: {
            strm << "(set = " << debug_record[kInstBindlessUninitOutDescSet] << ", binding = " << debug_record[kInstBindlessUninitOutBinding] << ") Descriptor index "
                 << debug_record[kInstBindlessUninitOutDescIndex] << " is uninitialized.";
            vuid_msg = "UNASSIGNED-Descriptor uninitialized";
        } break;
        case kInstErrorBuffAddrUnallocRef: {
            oob_access = true;
            uint64_t *ptr = (uint64_t *)&debug_record[kInstBuffAddrUnallocOutDescPtrLo];
            strm << "Device address 0x" << std::hex << *ptr << " access out of bounds. ";
            vuid_msg = "UNASSIGNED-Device address out of bounds";
        } break;
        case kInstErrorOOB: {
            const uint32_t set_num = debug_record[kInstBindlessBuffOOBOutDescSet];
            const uint32_t binding_num = debug_record[kInstBindlessBuffOOBOutDescBinding];
            const uint32_t desc_index = debug_record[kInstBindlessBuffOOBOutDescIndex];
            const uint32_t size = debug_record[kInstBindlessBuffOOBOutBuffSize];
            const uint32_t offset = debug_record[kInstBindlessBuffOOBOutBuffOff];
            const auto *binding_state = descriptor_sets[set_num].set_state->GetBinding(binding_num);
            assert(binding_state);
            if (size == 0) {
                strm << "(set = " << set_num << ", binding = " << binding_num << ") Descriptor index " << desc_index
                     << " is uninitialized.";
                vuid_msg = "UNASSIGNED-Descriptor uninitialized";
                break;
            }
            oob_access = true;
            auto desc_class = binding_state->descriptor_class;
            if (desc_class == cvdescriptorset::DescriptorClass::Mutable) {
                desc_class =
                    static_cast<const cvdescriptorset::MutableBinding *>(binding_state)->descriptors[desc_index].ActiveClass();
            }

            switch (desc_class) {
                case cvdescriptorset::DescriptorClass::GeneralBuffer:
                    strm << "(set = " << set_num << ", binding = " << binding_num << ") Descriptor index " << desc_index
                         << " access out of bounds. Descriptor size is " << size << " and highest byte accessed was " << offset;
                    if (binding_state->type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER ||
                        binding_state->type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC) {
                        vuid_msg = vuid.uniform_access_oob;
                    } else {
                        vuid_msg = vuid.storage_access_oob;
                    }
                    break;
                case cvdescriptorset::DescriptorClass::TexelBuffer:
                    strm << "(set = " << set_num << ", binding = " << binding_num << ") Descriptor index " << desc_index
                         << " access out of bounds. Descriptor size is " << size << " texels and highest texel accessed was "
                         << offset;
                    if (binding_state->type == VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER) {
                        vuid_msg = vuid.uniform_access_oob;
                    } else {
                        vuid_msg = vuid.storage_access_oob;
                    }
                    break;
                default:
                    // other OOB checks are not implemented yet
                    assert(false);
            }
        } break;
        case kInstErrorPreDrawValidate: {
            // Buffer size must be >= (stride * (drawCount - 1) + offset + sizeof(VkDrawIndexedIndirectCommand))
            if (debug_record[kPreValidateSubError] == pre_draw_count_exceeds_bufsize_error) {
                uint32_t count = debug_record[kPreValidateSubError + 1];
                uint32_t stride = buf_info.pre_draw_resources.stride;
                uint32_t offset = static_cast<uint32_t>(buf_info.pre_draw_resources.offset);
                uint32_t draw_size = (stride * (count - 1) + offset + sizeof(VkDrawIndexedIndirectCommand));
                strm << "Indirect draw count of " << count << " would exceed buffer size " << buf_info.pre_draw_resources.buf_size
                     << " of buffer " << buf_info.pre_draw_resources.buffer << " stride = " << stride << " offset = " << offset
                     << " (stride * (drawCount - 1) + offset + sizeof(VkDrawIndexedIndirectCommand)) = " << draw_size;
                if (count == 1) {
                    vuid_msg = vuid.count_exceeds_bufsize_1;
                } else {
                    vuid_msg = vuid.count_exceeds_bufsize;
                }
            } else if (debug_record[kPreValidateSubError] == pre_draw_count_exceeds_limit_error) {
                uint32_t count = debug_record[kPreValidateSubError + 1];
                strm << "Indirect draw count of " << count << " would exceed maxDrawIndirectCount limit of "
                     << gpu_assisted->phys_dev_props.limits.maxDrawIndirectCount;
                vuid_msg = vuid.count_exceeds_device_limit;
            } else if (debug_record[kPreValidateSubError] == pre_draw_first_instance_error) {
                uint32_t index = debug_record[kPreValidateSubError + 1];
                strm << "The drawIndirectFirstInstance feature is not enabled, but the firstInstance member of the "
                     << ((buf_info.command == Func::vkCmdDrawIndirect) ? "VkDrawIndirectCommand" : "VkDrawIndexedIndirectCommand")
                     << " structure at index " << index << " is not zero";
                vuid_msg = vuid.first_instance_not_zero;
            }
            return_code = false;
        } break;
        case kInstErrorPreDispatchValidate: {
            if (debug_record[kPreValidateSubError] == pre_dispatch_count_exceeds_limit_x_error) {
                uint32_t count = debug_record[kPreValidateSubError + 1];
                strm << "Indirect dispatch VkDispatchIndirectCommand::x of " << count
                     << " would exceed maxComputeWorkGroupCount[0] limit of "
                     << gpu_assisted->phys_dev_props.limits.maxComputeWorkGroupCount[0];
                vuid_msg = vuid.group_exceeds_device_limit_x;
            } else if (debug_record[kPreValidateSubError] == pre_dispatch_count_exceeds_limit_y_error) {
                uint32_t count = debug_record[kPreValidateSubError + 1];
                strm << "Indirect dispatch VkDispatchIndirectCommand:y of " << count
                     << " would exceed maxComputeWorkGroupCount[1] limit of "
                     << gpu_assisted->phys_dev_props.limits.maxComputeWorkGroupCount[1];
                vuid_msg = vuid.group_exceeds_device_limit_y;
            } else if (debug_record[kPreValidateSubError] == pre_dispatch_count_exceeds_limit_z_error) {
                uint32_t count = debug_record[kPreValidateSubError + 1];
                strm << "Indirect dispatch VkDispatchIndirectCommand::z of " << count
                     << " would exceed maxComputeWorkGroupCount[2] limit of "
                     << gpu_assisted->phys_dev_props.limits.maxComputeWorkGroupCount[2];
                vuid_msg = vuid.group_exceeds_device_limit_z;
            }
            return_code = false;
        } break;
        default: {
            strm << "Internal Error (unexpected error type = " << debug_record[kInstValidationOutError] << "). ";
            vuid_msg = "UNASSIGNED-Internal Error";
            assert(false);
        } break;
    }
    msg = strm.str();
    return return_code;
}

// Pull together all the information from the debug record to build the error message strings,
// and then assemble them into a single message string.
// Retrieve the shader program referenced by the unique shader ID provided in the debug record.
// We had to keep a copy of the shader program with the same lifecycle as the pipeline to make
// sure it is available when the pipeline is submitted.  (The ShaderModule tracking object also
// keeps a copy, but it can be destroyed after the pipeline is created and before it is submitted.)
//
void GpuAssisted::AnalyzeAndGenerateMessages(VkCommandBuffer command_buffer, VkQueue queue, GpuAssistedBufferInfo &buffer_info,
                                             uint32_t operation_index, uint32_t *const debug_output_buffer,
                                             const std::vector<GpuAssistedDescSetState> &descriptor_sets) {
    const uint32_t total_words = debug_output_buffer[spvtools::kDebugOutputSizeOffset];
    bool oob_access;
    // A zero here means that the shader instrumentation didn't write anything.
    // If you have nothing to say, don't say it here.
    if (0 == total_words) {
        return;
    }
    // The second word in the debug output buffer is the number of words that would have
    // been written by the shader instrumentation, if there was enough room in the buffer we provided.
    // The number of words actually written by the shaders is determined by the size of the buffer
    // we provide via the descriptor.  So, we process only the number of words that can fit in the
    // buffer.
    // Each "report" written by the shader instrumentation is considered a "record".  This function
    // is hard-coded to process only one record because it expects the buffer to be large enough to
    // hold only one record.  If there is a desire to process more than one record, this function needs
    // to be modified to loop over records and the buffer size increased.
    std::string validation_message;
    std::string stage_message;
    std::string common_message;
    std::string filename_message;
    std::string source_message;
    std::string vuid_msg;
    VkShaderModule shader_module_handle = VK_NULL_HANDLE;
    VkPipeline pipeline_handle = VK_NULL_HANDLE;
    VkShaderEXT shader_object_handle = VK_NULL_HANDLE;
    vvl::span<const uint32_t> pgm;
    // The first record starts at this offset after the total_words.
    const uint32_t *debug_record = &debug_output_buffer[spvtools::kDebugOutputDataOffset];
    // Lookup the VkShaderModule handle and SPIR-V code used to create the shader, using the unique shader ID value returned
    // by the instrumented shader.
    auto it = shader_map.find(debug_record[kInstCommonOutShaderId]);
    if (it != shader_map.end()) {
        shader_module_handle = it->second.shader_module;
        pipeline_handle = it->second.pipeline;
        shader_object_handle = it->second.shader_object;
        pgm = it->second.pgm;
    }
    const bool gen_full_message =
        GenerateValidationMessage(debug_record, validation_message, vuid_msg, oob_access, buffer_info, this, descriptor_sets);
    if (gen_full_message) {
        UtilGenerateStageMessage(debug_record, stage_message);
        UtilGenerateCommonMessage(report_data, command_buffer, debug_record, shader_module_handle, pipeline_handle,
                                  shader_object_handle, buffer_info.pipeline_bind_point, operation_index, common_message);
        UtilGenerateSourceMessages(pgm, debug_record, false, filename_message, source_message);
        if (buffer_info.uses_robustness && oob_access) {
            if (warn_on_robust_oob) {
                LogWarning(queue, vuid_msg.c_str(), "%s %s %s %s%s", validation_message.c_str(), common_message.c_str(),
                           stage_message.c_str(), filename_message.c_str(), source_message.c_str());
            }
        } else {
            LogError(queue, vuid_msg.c_str(), "%s %s %s %s%s", validation_message.c_str(), common_message.c_str(),
                     stage_message.c_str(), filename_message.c_str(), source_message.c_str());
        }
    } else {
        LogError(queue, vuid_msg.c_str(), "%s", validation_message.c_str());
    }

    // Clear the written size and any error messages. Note that this preserves the first word, which contains flags.
    const uint32_t words_to_clear = std::min(total_words, output_buffer_size - spvtools::kDebugOutputDataOffset);
    debug_output_buffer[spvtools::kDebugOutputSizeOffset] = 0;
    memset(&debug_output_buffer[spvtools::kDebugOutputDataOffset], 0, sizeof(uint32_t) * words_to_clear);
}

// For the given command buffer, map its debug data buffers and read their contents for analysis.
void gpuav_state::CommandBuffer::Process(VkQueue queue) {
    auto *device_state = static_cast<GpuAssisted *>(dev_data);
    if (has_draw_cmd || has_trace_rays_cmd || has_dispatch_cmd) {
        auto &gpu_buffer_list = per_draw_buffer_list;
        uint32_t draw_index = 0;
        uint32_t compute_index = 0;
        uint32_t ray_trace_index = 0;

        for (auto &buffer_info : gpu_buffer_list) {
            char *data;
            GpuAssistedInputBuffers *di_info = nullptr;
            if (buffer_info.desc_binding_index != vvl::kU32Max) {
                di_info = &di_input_buffer_list[buffer_info.desc_binding_index];
            }
            std::vector<GpuAssistedDescSetState> empty;

            uint32_t operation_index = 0;
            if (buffer_info.pipeline_bind_point == VK_PIPELINE_BIND_POINT_GRAPHICS) {
                operation_index = draw_index;
                draw_index++;
            } else if (buffer_info.pipeline_bind_point == VK_PIPELINE_BIND_POINT_COMPUTE) {
                operation_index = compute_index;
                compute_index++;
            } else if (buffer_info.pipeline_bind_point == VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR) {
                operation_index = ray_trace_index;
                ray_trace_index++;
            } else {
                assert(false);
            }

            VkResult result = vmaMapMemory(device_state->vmaAllocator, buffer_info.output_mem_block.allocation, (void **)&data);
            if (result == VK_SUCCESS) {
                device_state->AnalyzeAndGenerateMessages(commandBuffer(), queue, buffer_info, operation_index, (uint32_t *)data,
                                                         di_info ? di_info->descriptor_set_buffers : empty);
                vmaUnmapMemory(device_state->vmaAllocator, buffer_info.output_mem_block.allocation);
            }
        }
    }
    ProcessAccelerationStructure(queue);
}

// For the given command buffer, map its debug data buffers and update the status of any update after bind descriptors
void GpuAssisted::UpdateInstrumentationBuffer(gpuav_state::CommandBuffer *cb_node) {
    for (auto &buffer_info : cb_node->di_input_buffer_list) {
        VkDeviceAddress *address_data_ptr{nullptr};
        [[maybe_unused]] VkResult result;
        result = vmaMapMemory(vmaAllocator, buffer_info.address_buffer_allocation, reinterpret_cast<void **>(&address_data_ptr));
        assert(result == VK_SUCCESS);
        for (size_t i = 0; i < buffer_info.descriptor_set_buffers.size(); i++) {
            auto &set_buffer = buffer_info.descriptor_set_buffers[i];
            if (!set_buffer.gpu_state) {
                set_buffer.gpu_state = set_buffer.set_state->GetCurrentState();
                address_data_ptr[i] = set_buffer.gpu_state->device_addr;
            }
        }
        vmaUnmapMemory(vmaAllocator, buffer_info.address_buffer_allocation);
    }
}

void GpuAssisted::UpdateBDABuffer(GpuAssistedDeviceMemoryBlock device_address_buffer) {
    if (gpuav_bda_buffer_version == buffer_device_address_ranges_version) {
        return;
    }
    auto address_ranges = GetBufferAddressRanges();
    auto address_ranges_num_addresses = address_ranges.size();
    if (address_ranges_num_addresses == 0) return;

    // Example BDA input buffer assuming 2 buffers using BDA:
    // Word 0 | Index of start of buffer sizes (in this case 5)
    // Word 1 | 0x0000000000000000
    // Word 2 | Device Address of first buffer  (Addresses sorted in ascending order)
    // Word 3 | Device Address of second buffer
    // Word 4 | 0xffffffffffffffff
    // Word 5 | 0 (size of pretend buffer at word 1)
    // Word 6 | Size in bytes of first buffer
    // Word 7 | Size in bytes of second buffer
    // Word 8 | 0 (size of pretend buffer in word 4)

    uint64_t *bda_data;
    // Make sure to limit writes to size of the buffer
    [[maybe_unused]] VkResult result;
    result = vmaMapMemory(vmaAllocator, device_address_buffer.allocation, reinterpret_cast<void **>(&bda_data));
    assert(result == VK_SUCCESS);
    uint32_t address_index = 1;
    size_t size_index = 3 + address_ranges.size();
    memset(bda_data, 0, static_cast<size_t>(app_bda_buffer_size));
    bda_data[0] = size_index;       // Start of buffer sizes
    bda_data[address_index++] = 0;  // NULL address
    bda_data[size_index++] = 0;
    if (address_ranges_num_addresses > app_bda_max_addresses) {
        std::ostringstream problem_string;
        problem_string << "Number of buffer device addresses in use (" << address_ranges_num_addresses
                       << ") is greapter than khronos_validation.max_buffer_device_addresses (" << app_bda_max_addresses
                       << "). Truncating BDA table which could result in invalid validation";
        ReportSetupProblem(device, problem_string.str().c_str());
    }
    size_t num_addresses = std::min(address_ranges_num_addresses, app_bda_max_addresses);
    for (size_t i = 0; i < num_addresses; i++) {
        bda_data[address_index++] = address_ranges[i].begin;
        bda_data[size_index++] = address_ranges[i].end - address_ranges[i].begin;
    }
    bda_data[address_index] = std::numeric_limits<uintptr_t>::max();
    bda_data[size_index] = 0;
    // Flush the BDA buffer before unmapping so that the new state is visible to the GPU
    result = vmaFlushAllocation(vmaAllocator, device_address_buffer.allocation, 0, VK_WHOLE_SIZE);
    // No good way to handle this error, we should still try to unmap.
    assert(result == VK_SUCCESS);
    vmaUnmapMemory(vmaAllocator, device_address_buffer.allocation);
    gpuav_bda_buffer_version = buffer_device_address_ranges_version;
}

void GpuAssisted::UpdateBoundDescriptors(VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint) {
    if (aborted) return;
    auto cb_node = GetWrite<gpuav_state::CommandBuffer>(commandBuffer);
    if (!cb_node) {
        ReportSetupProblem(device, "Unrecognized command buffer");
        aborted = true;
        return;
    }
    const auto lv_bind_point = ConvertToLvlBindPoint(pipelineBindPoint);
    auto const &last_bound = cb_node->lastBound[lv_bind_point];

    uint32_t number_of_sets = static_cast<uint32_t>(last_bound.per_set.size());
    // Figure out how much memory we need for the input block based on how many sets and bindings there are
    // and how big each of the bindings is
    if (number_of_sets > 0 && validate_descriptors && force_buffer_device_address) {
        VkBufferCreateInfo buffer_info = vku::InitStructHelper();
        assert(number_of_sets <= kDebugInputBindlessMaxDescSets);
        buffer_info.size = kDebugInputBindlessMaxDescSets * 8;  // 64 bit addresses
        buffer_info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        VmaAllocationCreateInfo alloc_info = {};
        alloc_info.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        alloc_info.pool = VK_NULL_HANDLE;
        GpuAssistedInputBuffers di_buffers = {};
        VkResult result =
            vmaCreateBuffer(vmaAllocator, &buffer_info, &alloc_info, &di_buffers.address_buffer, &di_buffers.address_buffer_allocation, nullptr);
        if (result != VK_SUCCESS) {
            ReportSetupProblem(device, "Unable to allocate device memory.  Device could become unstable.", true);
            aborted = true;
            return;
        }
        // Allocate buffer for device addresses of the input buffer for each descriptor set.  This is the buffer written to each
        // draw's descriptor set.
        VkDeviceAddress *address_data_ptr{nullptr};
        result = vmaMapMemory(vmaAllocator, di_buffers.address_buffer_allocation, reinterpret_cast<void **>(&address_data_ptr));
        if (result != VK_SUCCESS) {
            ReportSetupProblem(device, "Unable to map device memory.  Device could become unstable.", true);
            aborted = true;
            return;
        }
        memset(address_data_ptr, 0, static_cast<size_t>(buffer_info.size));
        cb_node->current_input_buffer = di_buffers.address_buffer;
        buffer_info.usage |= VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT_KHR;
        for (const auto &s : last_bound.per_set) {
            auto set = s.bound_descriptor_set;
            if (!set) {
                continue;
            }
            if (validate_descriptors) {
                GpuAssistedDescSetState desc_set_state;
                desc_set_state.set_state = std::static_pointer_cast<gpuav_state::DescriptorSet>(set);
                if (!desc_set_state.set_state->IsUpdateAfterBind()) {
                    desc_set_state.gpu_state = desc_set_state.set_state->GetCurrentState();
                    *address_data_ptr = desc_set_state.gpu_state->device_addr;
                }

                di_buffers.descriptor_set_buffers.emplace_back(std::move(desc_set_state));
            }
            address_data_ptr++;
        }
        cb_node->di_input_buffer_list.emplace_back(di_buffers);
        vmaUnmapMemory(vmaAllocator, di_buffers.address_buffer_allocation);
    }
}

void GpuAssisted::PostCallRecordCmdBindDescriptorSets(VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint,
                                                      VkPipelineLayout layout, uint32_t firstSet, uint32_t descriptorSetCount,
                                                      const VkDescriptorSet *pDescriptorSets, uint32_t dynamicOffsetCount,
                                                      const uint32_t *pDynamicOffsets, const RecordObject &record_obj) {
    ValidationStateTracker::PostCallRecordCmdBindDescriptorSets(commandBuffer, pipelineBindPoint, layout, firstSet,
                                                                descriptorSetCount, pDescriptorSets, dynamicOffsetCount,
                                                                pDynamicOffsets, record_obj);
    UpdateBoundDescriptors(commandBuffer, pipelineBindPoint);
}

void GpuAssisted::PreCallRecordCmdPushDescriptorSetKHR(VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint,
                                              VkPipelineLayout layout, uint32_t set, uint32_t descriptorWriteCount,
                                              const VkWriteDescriptorSet* pDescriptorWrites) {
    ValidationStateTracker::PreCallRecordCmdPushDescriptorSetKHR(commandBuffer, pipelineBindPoint, layout, set,
                                                                 descriptorWriteCount, pDescriptorWrites);
    UpdateBoundDescriptors(commandBuffer, pipelineBindPoint);
}

void GpuAssisted::PreRecordCommandBuffer(VkCommandBuffer command_buffer) {
    auto cb_node = GetWrite<gpuav_state::CommandBuffer>(command_buffer);
    UpdateInstrumentationBuffer(cb_node.get());
    for (auto *secondary_cmd_buffer : cb_node->linkedCommandBuffers) {
        auto guard = secondary_cmd_buffer->WriteLock();
        UpdateInstrumentationBuffer(static_cast<gpuav_state::CommandBuffer *>(secondary_cmd_buffer));
    }
}

void GpuAssisted::PreCallRecordQueueSubmit(VkQueue queue, uint32_t submitCount, const VkSubmitInfo *pSubmits, VkFence fence) {
    ValidationStateTracker::PreCallRecordQueueSubmit(queue, submitCount, pSubmits, fence);
    for (uint32_t submit_idx = 0; submit_idx < submitCount; submit_idx++) {
        const VkSubmitInfo *submit = &pSubmits[submit_idx];
        for (uint32_t i = 0; i < submit->commandBufferCount; i++) {
            PreRecordCommandBuffer(submit->pCommandBuffers[i]);
        }
    }
    UpdateBDABuffer(app_buffer_device_addresses);
}

void GpuAssisted::PreCallRecordQueueSubmit2KHR(VkQueue queue, uint32_t submitCount, const VkSubmitInfo2KHR *pSubmits,
                                               VkFence fence) {
    ValidationStateTracker::PreCallRecordQueueSubmit2KHR(queue, submitCount, pSubmits, fence);
    for (uint32_t submit_idx = 0; submit_idx < submitCount; submit_idx++) {
        const VkSubmitInfo2KHR *submit = &pSubmits[submit_idx];
        for (uint32_t i = 0; i < submit->commandBufferInfoCount; i++) {
            PreRecordCommandBuffer(submit->pCommandBufferInfos[i].commandBuffer);
        }
    }
    UpdateBDABuffer(app_buffer_device_addresses);
}

void GpuAssisted::PreCallRecordQueueSubmit2(VkQueue queue, uint32_t submitCount, const VkSubmitInfo2 *pSubmits, VkFence fence) {
    ValidationStateTracker::PreCallRecordQueueSubmit2(queue, submitCount, pSubmits, fence);
    for (uint32_t submit_idx = 0; submit_idx < submitCount; submit_idx++) {
        const VkSubmitInfo2 *submit = &pSubmits[submit_idx];
        for (uint32_t i = 0; i < submit->commandBufferInfoCount; i++) {
            PreRecordCommandBuffer(submit->pCommandBufferInfos[i].commandBuffer);
        }
    }
    UpdateBDABuffer(app_buffer_device_addresses);
}

void GpuAssisted::PreCallRecordCmdDraw(VkCommandBuffer commandBuffer, uint32_t vertexCount, uint32_t instanceCount,
                                       uint32_t firstVertex, uint32_t firstInstance) {
    ValidationStateTracker::PreCallRecordCmdDraw(commandBuffer, vertexCount, instanceCount, firstVertex, firstInstance);
    AllocateValidationResources(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, Func::vkCmdDraw);
}

void GpuAssisted::PreCallRecordCmdDrawMultiEXT(VkCommandBuffer commandBuffer, uint32_t drawCount,
                                               const VkMultiDrawInfoEXT *pVertexInfo, uint32_t instanceCount,
                                               uint32_t firstInstance, uint32_t stride) {
    ValidationStateTracker::PreCallRecordCmdDrawMultiEXT(commandBuffer, drawCount, pVertexInfo, instanceCount, firstInstance,
                                                         stride);
    for (uint32_t i = 0; i < drawCount; i++) {
        AllocateValidationResources(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, Func::vkCmdDrawMultiEXT);
    }
}

void GpuAssisted::PreCallRecordCmdDrawIndexed(VkCommandBuffer commandBuffer, uint32_t indexCount, uint32_t instanceCount,
                                              uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance) {
    ValidationStateTracker::PreCallRecordCmdDrawIndexed(commandBuffer, indexCount, instanceCount, firstIndex, vertexOffset,
                                                        firstInstance);
    AllocateValidationResources(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, Func::vkCmdDrawIndexed);
}

void GpuAssisted::PreCallRecordCmdDrawMultiIndexedEXT(VkCommandBuffer commandBuffer, uint32_t drawCount,
                                                      const VkMultiDrawIndexedInfoEXT *pIndexInfo, uint32_t instanceCount,
                                                      uint32_t firstInstance, uint32_t stride, const int32_t *pVertexOffset) {
    ValidationStateTracker::PreCallRecordCmdDrawMultiIndexedEXT(commandBuffer, drawCount, pIndexInfo, instanceCount, firstInstance,
                                                                stride, pVertexOffset);
    for (uint32_t i = 0; i < drawCount; i++) {
        AllocateValidationResources(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, Func::vkCmdDrawMultiIndexedEXT);
    }
}

void GpuAssisted::PreCallRecordCmdDrawIndirect(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset, uint32_t count,
                                               uint32_t stride) {
    ValidationStateTracker::PreCallRecordCmdDrawIndirect(commandBuffer, buffer, offset, count, stride);
    GpuAssistedCmdIndirectState indirect_state = {buffer, offset, count, stride, VK_NULL_HANDLE, 0};
    AllocateValidationResources(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, Func::vkCmdDrawIndirect, &indirect_state);
}

void GpuAssisted::PreCallRecordCmdDrawIndexedIndirect(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset,
                                                      uint32_t count, uint32_t stride) {
    ValidationStateTracker::PreCallRecordCmdDrawIndexedIndirect(commandBuffer, buffer, offset, count, stride);
    GpuAssistedCmdIndirectState indirect_state = {buffer, offset, count, stride, VK_NULL_HANDLE, 0};
    AllocateValidationResources(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, Func::vkCmdDrawIndexedIndirect, &indirect_state);
}

void GpuAssisted::PreCallRecordCmdDrawIndirectCountKHR(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset,
                                                       VkBuffer countBuffer, VkDeviceSize countBufferOffset, uint32_t maxDrawCount,
                                                       uint32_t stride) {
    ValidationStateTracker::PreCallRecordCmdDrawIndirectCountKHR(commandBuffer, buffer, offset, countBuffer, countBufferOffset,
                                                                 maxDrawCount, stride);
    GpuAssistedCmdIndirectState indirect_state = {buffer, offset, 0, stride, countBuffer, countBufferOffset};
    AllocateValidationResources(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, Func::vkCmdDrawIndirectCountKHR, &indirect_state);
}

void GpuAssisted::PreCallRecordCmdDrawIndirectCount(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset,
                                                    VkBuffer countBuffer, VkDeviceSize countBufferOffset, uint32_t maxDrawCount,

                                                    uint32_t stride) {
    ValidationStateTracker::PreCallRecordCmdDrawIndirectCount(commandBuffer, buffer, offset, countBuffer, countBufferOffset,
                                                              maxDrawCount, stride);
    GpuAssistedCmdIndirectState indirect_state = {buffer, offset, 0, stride, countBuffer, countBufferOffset};
    AllocateValidationResources(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, Func::vkCmdDrawIndirectCount, &indirect_state);
}

void GpuAssisted::PreCallRecordCmdDrawIndirectByteCountEXT(VkCommandBuffer commandBuffer, uint32_t instanceCount,
                                                           uint32_t firstInstance, VkBuffer counterBuffer,
                                                           VkDeviceSize counterBufferOffset, uint32_t counterOffset,
                                                           uint32_t vertexStride) {
    ValidationStateTracker::PreCallRecordCmdDrawIndirectByteCountEXT(commandBuffer, instanceCount, firstInstance, counterBuffer,
                                                                     counterBufferOffset, counterOffset, vertexStride);
    AllocateValidationResources(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, Func::vkCmdDrawIndirectByteCountEXT);
}

void GpuAssisted::PreCallRecordCmdDrawIndexedIndirectCountKHR(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset,
                                                              VkBuffer countBuffer, VkDeviceSize countBufferOffset,
                                                              uint32_t maxDrawCount, uint32_t stride) {
    ValidationStateTracker::PreCallRecordCmdDrawIndexedIndirectCountKHR(commandBuffer, buffer, offset, countBuffer,
                                                                        countBufferOffset, maxDrawCount, stride);
    GpuAssistedCmdIndirectState indirect_state = {buffer, offset, 0, stride, countBuffer, countBufferOffset};
    AllocateValidationResources(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, Func::vkCmdDrawIndexedIndirectCountKHR,
                                &indirect_state);
}

void GpuAssisted::PreCallRecordCmdDrawIndexedIndirectCount(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset,
                                                           VkBuffer countBuffer, VkDeviceSize countBufferOffset,
                                                           uint32_t maxDrawCount, uint32_t stride) {
    ValidationStateTracker::PreCallRecordCmdDrawIndexedIndirectCount(commandBuffer, buffer, offset, countBuffer, countBufferOffset,
                                                                     maxDrawCount, stride);
    GpuAssistedCmdIndirectState indirect_state = {buffer, offset, 0, stride, countBuffer, countBufferOffset};
    AllocateValidationResources(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, Func::vkCmdDrawIndexedIndirectCount,
                                &indirect_state);
}

void GpuAssisted::PreCallRecordCmdDrawMeshTasksNV(VkCommandBuffer commandBuffer, uint32_t taskCount, uint32_t firstTask) {
    ValidationStateTracker::PreCallRecordCmdDrawMeshTasksNV(commandBuffer, taskCount, firstTask);
    AllocateValidationResources(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, Func::vkCmdDrawMeshTasksNV);
}

void GpuAssisted::PreCallRecordCmdDrawMeshTasksIndirectNV(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset,
                                                          uint32_t drawCount, uint32_t stride) {
    ValidationStateTracker::PreCallRecordCmdDrawMeshTasksIndirectNV(commandBuffer, buffer, offset, drawCount, stride);
    AllocateValidationResources(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, Func::vkCmdDrawMeshTasksIndirectNV);
}

void GpuAssisted::PreCallRecordCmdDrawMeshTasksIndirectCountNV(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset,
                                                               VkBuffer countBuffer, VkDeviceSize countBufferOffset,
                                                               uint32_t maxDrawCount, uint32_t stride) {
    ValidationStateTracker::PreCallRecordCmdDrawMeshTasksIndirectCountNV(commandBuffer, buffer, offset, countBuffer,
                                                                         countBufferOffset, maxDrawCount, stride);
    AllocateValidationResources(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, Func::vkCmdDrawMeshTasksIndirectCountNV);
}

void GpuAssisted::PreCallRecordCmdDrawMeshTasksEXT(VkCommandBuffer commandBuffer, uint32_t groupCountX, uint32_t groupCountY,
                                                   uint32_t groupCountZ) {
    ValidationStateTracker::PreCallRecordCmdDrawMeshTasksEXT(commandBuffer, groupCountX, groupCountY, groupCountZ);
    AllocateValidationResources(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, Func::vkCmdDrawMeshTasksEXT);
}

void GpuAssisted::PreCallRecordCmdDrawMeshTasksIndirectEXT(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset,
                                                           uint32_t drawCount, uint32_t stride) {
    ValidationStateTracker::PreCallRecordCmdDrawMeshTasksIndirectEXT(commandBuffer, buffer, offset, drawCount, stride);
    AllocateValidationResources(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, Func::vkCmdDrawMeshTasksIndirectEXT);
}

void GpuAssisted::PreCallRecordCmdDrawMeshTasksIndirectCountEXT(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset,
                                                                VkBuffer countBuffer, VkDeviceSize countBufferOffset,
                                                                uint32_t maxDrawCount, uint32_t stride) {
    ValidationStateTracker::PreCallRecordCmdDrawMeshTasksIndirectCountEXT(commandBuffer, buffer, offset, countBuffer,
                                                                          countBufferOffset, maxDrawCount, stride);
    AllocateValidationResources(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, Func::vkCmdDrawMeshTasksIndirectCountEXT);
}

void GpuAssisted::PreCallRecordCmdDispatch(VkCommandBuffer commandBuffer, uint32_t x, uint32_t y, uint32_t z) {
    ValidationStateTracker::PreCallRecordCmdDispatch(commandBuffer, x, y, z);
    AllocateValidationResources(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, Func::vkCmdDispatch);
}

void GpuAssisted::PreCallRecordCmdDispatchIndirect(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset) {
    ValidationStateTracker::PreCallRecordCmdDispatchIndirect(commandBuffer, buffer, offset);
    GpuAssistedCmdIndirectState indirect_state = {buffer, offset, 0, 0, VK_NULL_HANDLE, 0};
    AllocateValidationResources(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, Func::vkCmdDispatchIndirect, &indirect_state);
}

void GpuAssisted::PreCallRecordCmdDispatchBase(VkCommandBuffer commandBuffer, uint32_t baseGroupX, uint32_t baseGroupY,
                                               uint32_t baseGroupZ, uint32_t groupCountX, uint32_t groupCountY,
                                               uint32_t groupCountZ) {
    ValidationStateTracker::PreCallRecordCmdDispatchBase(commandBuffer, baseGroupX, baseGroupY, baseGroupZ, groupCountX,
                                                         groupCountY, groupCountZ);
    AllocateValidationResources(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, Func::vkCmdDispatchBaseKHR);
}

void GpuAssisted::PreCallRecordCmdDispatchBaseKHR(VkCommandBuffer commandBuffer, uint32_t baseGroupX, uint32_t baseGroupY,
                                                  uint32_t baseGroupZ, uint32_t groupCountX, uint32_t groupCountY,
                                                  uint32_t groupCountZ) {
    ValidationStateTracker::PreCallRecordCmdDispatchBaseKHR(commandBuffer, baseGroupX, baseGroupY, baseGroupZ, groupCountX,
                                                            groupCountY, groupCountZ);
    AllocateValidationResources(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, Func::vkCmdDispatchBase);
}

void GpuAssisted::PreCallRecordCmdTraceRaysNV(VkCommandBuffer commandBuffer, VkBuffer raygenShaderBindingTableBuffer,
                                              VkDeviceSize raygenShaderBindingOffset, VkBuffer missShaderBindingTableBuffer,
                                              VkDeviceSize missShaderBindingOffset, VkDeviceSize missShaderBindingStride,
                                              VkBuffer hitShaderBindingTableBuffer, VkDeviceSize hitShaderBindingOffset,
                                              VkDeviceSize hitShaderBindingStride, VkBuffer callableShaderBindingTableBuffer,
                                              VkDeviceSize callableShaderBindingOffset, VkDeviceSize callableShaderBindingStride,
                                              uint32_t width, uint32_t height, uint32_t depth) {
    ValidationStateTracker::PreCallRecordCmdTraceRaysNV(
        commandBuffer, raygenShaderBindingTableBuffer, raygenShaderBindingOffset, missShaderBindingTableBuffer,
        missShaderBindingOffset, missShaderBindingStride, hitShaderBindingTableBuffer, hitShaderBindingOffset,
        hitShaderBindingStride, callableShaderBindingTableBuffer, callableShaderBindingOffset, callableShaderBindingStride, width,
        height, depth);
    AllocateValidationResources(commandBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_NV, Func::vkCmdTraceRaysNV);
}

void GpuAssisted::PreCallRecordCmdTraceRaysKHR(VkCommandBuffer commandBuffer,
                                               const VkStridedDeviceAddressRegionKHR *pRaygenShaderBindingTable,
                                               const VkStridedDeviceAddressRegionKHR *pMissShaderBindingTable,
                                               const VkStridedDeviceAddressRegionKHR *pHitShaderBindingTable,
                                               const VkStridedDeviceAddressRegionKHR *pCallableShaderBindingTable, uint32_t width,
                                               uint32_t height, uint32_t depth) {
    ValidationStateTracker::PreCallRecordCmdTraceRaysKHR(commandBuffer, pRaygenShaderBindingTable, pMissShaderBindingTable,
                                                         pHitShaderBindingTable, pCallableShaderBindingTable, width, height, depth);
    AllocateValidationResources(commandBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, Func::vkCmdTraceRaysKHR);
}

void GpuAssisted::PreCallRecordCmdTraceRaysIndirectKHR(VkCommandBuffer commandBuffer,
                                                       const VkStridedDeviceAddressRegionKHR *pRaygenShaderBindingTable,
                                                       const VkStridedDeviceAddressRegionKHR *pMissShaderBindingTable,
                                                       const VkStridedDeviceAddressRegionKHR *pHitShaderBindingTable,
                                                       const VkStridedDeviceAddressRegionKHR *pCallableShaderBindingTable,
                                                       VkDeviceAddress indirectDeviceAddress) {
    ValidationStateTracker::PreCallRecordCmdTraceRaysIndirectKHR(commandBuffer, pRaygenShaderBindingTable, pMissShaderBindingTable,
                                                                 pHitShaderBindingTable, pCallableShaderBindingTable,
                                                                 indirectDeviceAddress);
    AllocateValidationResources(commandBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, Func::vkCmdTraceRaysIndirectKHR);
}

void GpuAssisted::PreCallRecordCmdTraceRaysIndirect2KHR(VkCommandBuffer commandBuffer, VkDeviceAddress indirectDeviceAddress) {
    ValidationStateTracker::PreCallRecordCmdTraceRaysIndirect2KHR(commandBuffer, indirectDeviceAddress);
    AllocateValidationResources(commandBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, Func::vkCmdTraceRaysIndirect2KHR);
}

// This function will add the returned VkPipeline handle to another object incharge of destroying it. Caller does NOT have to
// destroy it
VkPipeline GpuAssisted::GetValidationPipeline(VkRenderPass render_pass) {
    VkPipeline pipeline = VK_NULL_HANDLE;
    // NOTE: for dynamic rendering, render_pass will be VK_NULL_HANDLE but we'll use that as a map
    // key anyways;
    auto pipeentry = pre_draw_validation_state.renderpass_to_pipeline.find(render_pass);
    if (pipeentry != pre_draw_validation_state.renderpass_to_pipeline.end()) {
        pipeline = pipeentry->second;
    }
    if (pipeline != VK_NULL_HANDLE) {
        return pipeline;
    }
    auto pipeline_stage_ci = vku::InitStruct<VkPipelineShaderStageCreateInfo>();
    pipeline_stage_ci.stage = VK_SHADER_STAGE_VERTEX_BIT;
    pipeline_stage_ci.module = pre_draw_validation_state.shader_module;
    pipeline_stage_ci.pName = "main";

    auto pipeline_ci = vku::InitStruct<VkGraphicsPipelineCreateInfo>();
    auto vertex_input_state = vku::InitStruct<VkPipelineVertexInputStateCreateInfo>();
    auto input_assembly_state = vku::InitStruct<VkPipelineInputAssemblyStateCreateInfo>();
    input_assembly_state.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    auto rasterization_state = vku::InitStruct<VkPipelineRasterizationStateCreateInfo>();
    rasterization_state.rasterizerDiscardEnable = VK_TRUE;
    auto color_blend_state = vku::InitStruct<VkPipelineColorBlendStateCreateInfo>();

    pipeline_ci.pVertexInputState = &vertex_input_state;
    pipeline_ci.pInputAssemblyState = &input_assembly_state;
    pipeline_ci.pRasterizationState = &rasterization_state;
    pipeline_ci.pColorBlendState = &color_blend_state;
    pipeline_ci.renderPass = render_pass;
    pipeline_ci.layout = pre_draw_validation_state.pipeline_layout;
    pipeline_ci.stageCount = 1;
    pipeline_ci.pStages = &pipeline_stage_ci;

    VkResult result = DispatchCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipeline_ci, nullptr, &pipeline);
    if (result != VK_SUCCESS) {
        ReportSetupProblem(device, "Unable to create graphics pipeline.  Aborting GPU-AV");
        aborted = true;
        return VK_NULL_HANDLE;
    }

    pre_draw_validation_state.renderpass_to_pipeline.insert(render_pass, pipeline);
    return pipeline;
}

void GpuAssisted::AllocatePreDrawValidationResources(const GpuAssistedDeviceMemoryBlock &output_block,
                                                     GpuAssistedPreDrawResources &resources, const VkRenderPass render_pass,
                                                     const bool use_shader_objects, VkPipeline *pPipeline,
                                                     const GpuAssistedCmdIndirectState *indirect_state) {
    VkResult result;
    if (!pre_draw_validation_state.initialized) {
        std::vector<VkDescriptorSetLayoutBinding> bindings = {
            {0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr},  // output buffer
            {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr},  // count/draws buffer
        };

        VkDescriptorSetLayoutCreateInfo ds_layout_ci = vku::InitStructHelper();
        ds_layout_ci.bindingCount = static_cast<uint32_t>(bindings.size());
        ds_layout_ci.pBindings = bindings.data();
        result = DispatchCreateDescriptorSetLayout(device, &ds_layout_ci, nullptr, &pre_draw_validation_state.ds_layout);
        if (result != VK_SUCCESS) {
            ReportSetupProblem(device, "Unable to create descriptor set layout.  Aborting GPU-AV");
            aborted = true;
            return;
        }

        VkPushConstantRange push_constant_range = {};
        push_constant_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        push_constant_range.offset = 0;
        push_constant_range.size = resources.push_constant_words * sizeof(uint32_t);
        VkPipelineLayoutCreateInfo pipeline_layout_ci = vku::InitStructHelper();
        pipeline_layout_ci.pushConstantRangeCount = 1;
        pipeline_layout_ci.pPushConstantRanges = &push_constant_range;
        pipeline_layout_ci.setLayoutCount = 1;
        pipeline_layout_ci.pSetLayouts = &pre_draw_validation_state.ds_layout;
        result = DispatchCreatePipelineLayout(device, &pipeline_layout_ci, nullptr, &pre_draw_validation_state.pipeline_layout);
        if (result != VK_SUCCESS) {
            ReportSetupProblem(device, "Unable to create pipeline layout.  Aborting GPU-AV");
            aborted = true;
            return;
        }

        if (use_shader_objects) {
            auto shader_ci = vku::InitStruct<VkShaderCreateInfoEXT>();
            shader_ci.stage = VK_SHADER_STAGE_VERTEX_BIT;
            shader_ci.codeType = VK_SHADER_CODE_TYPE_SPIRV_EXT;
            shader_ci.codeSize = sizeof(gpu_pre_draw_vert);
            shader_ci.pCode = gpu_pre_draw_vert;
            shader_ci.pName = "main";
            shader_ci.setLayoutCount = 1u;
            shader_ci.pSetLayouts = &pre_draw_validation_state.ds_layout;
            shader_ci.pushConstantRangeCount = 1u;
            shader_ci.pPushConstantRanges = &push_constant_range;
            result = DispatchCreateShadersEXT(device, 1u, &shader_ci, nullptr, &pre_draw_validation_state.shader_object);
            if (result != VK_SUCCESS) {
                ReportSetupProblem(device, "Unable to create shader object.  Aborting GPU-AV");
                aborted = true;
                return;
            }
        } else {
            auto shader_module_ci = vku::InitStruct<VkShaderModuleCreateInfo>();
            shader_module_ci.codeSize = sizeof(gpu_pre_draw_vert);
            shader_module_ci.pCode = gpu_pre_draw_vert;
            result = DispatchCreateShaderModule(device, &shader_module_ci, nullptr, &pre_draw_validation_state.shader_module);
            if (result != VK_SUCCESS) {
                ReportSetupProblem(device, "Unable to create shader module.  Aborting GPU-AV");
                aborted = true;
                return;
            }
        }

        pre_draw_validation_state.initialized = true;
    }

    if (!use_shader_objects) {
        *pPipeline = GetValidationPipeline(render_pass);
        if (*pPipeline == VK_NULL_HANDLE) {
            ReportSetupProblem(device, "Could not find or create a pipeline.  Aborting GPU-AV");
            aborted = true;
            return;
        }
    }

    result = desc_set_manager->GetDescriptorSet(&resources.desc_pool, pre_draw_validation_state.ds_layout, &resources.desc_set);
    if (result != VK_SUCCESS) {
        ReportSetupProblem(device, "Unable to allocate descriptor set.  Aborting GPU-AV");
        aborted = true;
        return;
    }

    const uint32_t buffer_count = 2;
    VkDescriptorBufferInfo buffer_infos[buffer_count] = {};
    // Error output buffer
    buffer_infos[0].buffer = output_block.buffer;
    buffer_infos[0].offset = 0;
    buffer_infos[0].range = VK_WHOLE_SIZE;
    if (indirect_state->count_buffer) {
        // Count buffer
        buffer_infos[1].buffer = indirect_state->count_buffer;
    } else {
        // Draw Buffer
        buffer_infos[1].buffer = indirect_state->buffer;
    }
    buffer_infos[1].offset = 0;
    buffer_infos[1].range = VK_WHOLE_SIZE;

    VkWriteDescriptorSet desc_writes[buffer_count] = {};
    for (uint32_t i = 0; i < buffer_count; i++) {
        desc_writes[i] = vku::InitStructHelper();
        desc_writes[i].dstBinding = i;
        desc_writes[i].descriptorCount = 1;
        desc_writes[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        desc_writes[i].pBufferInfo = &buffer_infos[i];
        desc_writes[i].dstSet = resources.desc_set;
    }
    DispatchUpdateDescriptorSets(device, buffer_count, desc_writes, 0, NULL);
}

void GpuAssisted::AllocatePreDispatchValidationResources(const GpuAssistedDeviceMemoryBlock &output_block,
                                                         GpuAssistedPreDispatchResources &resources,
                                                         const GpuAssistedCmdIndirectState *indirect_state,
                                                         const bool use_shader_objects) {
    VkResult result;
    if (!pre_dispatch_validation_state.initialized) {
        std::vector<VkDescriptorSetLayoutBinding> bindings = {
            {0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},  // output buffer
            {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},  // indirect buffer
        };

        VkDescriptorSetLayoutCreateInfo ds_layout_ci = vku::InitStructHelper();
        ds_layout_ci.bindingCount = static_cast<uint32_t>(bindings.size());
        ds_layout_ci.pBindings = bindings.data();
        result = DispatchCreateDescriptorSetLayout(device, &ds_layout_ci, nullptr, &pre_dispatch_validation_state.ds_layout);
        if (result != VK_SUCCESS) {
            ReportSetupProblem(device, "Unable to create descriptor set layout.  Aborting GPU-AV");
            aborted = true;
            return;
        }

        VkPushConstantRange push_constant_range = {};
        push_constant_range.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        push_constant_range.offset = 0;
        push_constant_range.size = resources.push_constant_words * sizeof(uint32_t);
        VkPipelineLayoutCreateInfo pipeline_layout_ci = vku::InitStructHelper();
        pipeline_layout_ci.pushConstantRangeCount = 1;
        pipeline_layout_ci.pPushConstantRanges = &push_constant_range;
        pipeline_layout_ci.setLayoutCount = 1;
        pipeline_layout_ci.pSetLayouts = &pre_dispatch_validation_state.ds_layout;
        result = DispatchCreatePipelineLayout(device, &pipeline_layout_ci, nullptr, &pre_dispatch_validation_state.pipeline_layout);
        if (result != VK_SUCCESS) {
            ReportSetupProblem(device, "Unable to create pipeline layout.  Aborting GPU-AV");
            aborted = true;
            return;
        }

        if (use_shader_objects) {
            auto shader_ci = vku::InitStruct<VkShaderCreateInfoEXT>();
            shader_ci.stage = VK_SHADER_STAGE_COMPUTE_BIT;
            shader_ci.codeType = VK_SHADER_CODE_TYPE_SPIRV_EXT;
            shader_ci.codeSize = sizeof(gpu_pre_dispatch_comp);
            shader_ci.pCode = gpu_pre_dispatch_comp;
            shader_ci.pName = "main";
            shader_ci.setLayoutCount = 1u;
            shader_ci.pSetLayouts = &pre_dispatch_validation_state.ds_layout;
            shader_ci.pushConstantRangeCount = 1u;
            shader_ci.pPushConstantRanges = &push_constant_range;
            result = DispatchCreateShadersEXT(device, 1u, &shader_ci, nullptr, &pre_dispatch_validation_state.shader_object);
            if (result != VK_SUCCESS) {
                ReportSetupProblem(device, "Unable to create shader object.  Aborting GPU-AV");
                aborted = true;
                return;
            }
        } else {
            auto shader_module_ci = vku::InitStruct<VkShaderModuleCreateInfo>();
            shader_module_ci.codeSize = sizeof(gpu_pre_dispatch_comp);
            shader_module_ci.pCode = gpu_pre_dispatch_comp;
            result = DispatchCreateShaderModule(device, &shader_module_ci, nullptr, &pre_dispatch_validation_state.shader_module);
            if (result != VK_SUCCESS) {
                ReportSetupProblem(device, "Unable to create shader module.  Aborting GPU-AV");
                aborted = true;
                return;
            }

            // Create pipeline
            auto pipeline_stage_ci = vku::InitStruct<VkPipelineShaderStageCreateInfo>();
            pipeline_stage_ci.stage = VK_SHADER_STAGE_COMPUTE_BIT;
            pipeline_stage_ci.module = pre_dispatch_validation_state.shader_module;
            pipeline_stage_ci.pName = "main";

            auto pipeline_ci = vku::InitStruct<VkComputePipelineCreateInfo>();
            pipeline_ci.stage = pipeline_stage_ci;
            pipeline_ci.layout = pre_dispatch_validation_state.pipeline_layout;

            result = DispatchCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipeline_ci, nullptr,
                                                    &pre_dispatch_validation_state.pipeline);
            if (result != VK_SUCCESS) {
                ReportSetupProblem(device, "Failed to create compute pipeline for pre dispatch validation.");
            }
        }

        pre_dispatch_validation_state.initialized = true;
    }

    result = desc_set_manager->GetDescriptorSet(&resources.desc_pool, pre_dispatch_validation_state.ds_layout, &resources.desc_set);
    if (result != VK_SUCCESS) {
        ReportSetupProblem(device, "Unable to allocate descriptor set.  Aborting GPU-AV");
        aborted = true;
        return;
    }

    const uint32_t buffer_count = 2;
    VkDescriptorBufferInfo buffer_infos[buffer_count] = {};
    // Error output buffer
    buffer_infos[0].buffer = output_block.buffer;
    buffer_infos[0].offset = 0;
    buffer_infos[0].range = VK_WHOLE_SIZE;
    buffer_infos[1].buffer = indirect_state->buffer;
    buffer_infos[1].offset = 0;
    buffer_infos[1].range = VK_WHOLE_SIZE;

    VkWriteDescriptorSet desc_writes[buffer_count] = {};
    for (uint32_t i = 0; i < buffer_count; i++) {
        desc_writes[i] = vku::InitStructHelper();
        desc_writes[i].dstBinding = i;
        desc_writes[i].descriptorCount = 1;
        desc_writes[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        desc_writes[i].pBufferInfo = &buffer_infos[i];
        desc_writes[i].dstSet = resources.desc_set;
    }
    DispatchUpdateDescriptorSets(device, buffer_count, desc_writes, 0, nullptr);
}

void GpuAssisted::AllocateValidationResources(const VkCommandBuffer cmd_buffer, const VkPipelineBindPoint bind_point,
                                              vvl::Func command, const GpuAssistedCmdIndirectState *indirect_state) {
    if (bind_point != VK_PIPELINE_BIND_POINT_GRAPHICS && bind_point != VK_PIPELINE_BIND_POINT_COMPUTE &&
        bind_point != VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR) {
        return;
    }
    VkResult result;

    if (aborted) return;

    auto cb_node = GetWrite<gpuav_state::CommandBuffer>(cmd_buffer);
    if (!cb_node) {
        ReportSetupProblem(device, "Unrecognized command buffer");
        aborted = true;
        return;
    }
    const auto lv_bind_point = ConvertToLvlBindPoint(bind_point);
    auto const &last_bound = cb_node->lastBound[lv_bind_point];
    const auto *pipeline_state = last_bound.pipeline_state;
    bool uses_robustness = false;
    const bool use_shader_objects = pipeline_state == nullptr;

    if (!pipeline_state && !last_bound.HasShaderObjects()) {
        ReportSetupProblem(device, "Neither pipeline state nor shader object states were found, aborting GPU-AV");
        aborted = true;
        return;
    }

    std::vector<VkDescriptorSet> desc_sets;
    VkDescriptorPool desc_pool = VK_NULL_HANDLE;
    result = desc_set_manager->GetDescriptorSets(1, &desc_pool, debug_desc_layout, &desc_sets);
    assert(result == VK_SUCCESS);
    if (result != VK_SUCCESS) {
        ReportSetupProblem(device, "Unable to allocate descriptor sets.  Device could become unstable.");
        aborted = true;
        return;
    }

    VkDescriptorBufferInfo output_desc_buffer_info = {};
    output_desc_buffer_info.range = output_buffer_size;

    // Allocate memory for the output block that the gpu will use to return any error information
    GpuAssistedDeviceMemoryBlock output_block = {};
    VkBufferCreateInfo buffer_info = vku::InitStructHelper();
    buffer_info.size = output_buffer_size;
    buffer_info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    VmaAllocationCreateInfo alloc_info = {};
    alloc_info.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    alloc_info.pool = output_buffer_pool;
    result = vmaCreateBuffer(vmaAllocator, &buffer_info, &alloc_info, &output_block.buffer, &output_block.allocation, nullptr);
    if (result != VK_SUCCESS) {
        ReportSetupProblem(device, "Unable to allocate device memory.  Device could become unstable.", true);
        aborted = true;
        return;
    }

    uint32_t *data_ptr;
    result = vmaMapMemory(vmaAllocator, output_block.allocation, reinterpret_cast<void **>(&data_ptr));
    if (result == VK_SUCCESS) {
        memset(data_ptr, 0, output_buffer_size);
        if (validate_descriptors) {
            uses_robustness =
                (enabled_features.core.robustBufferAccess || enabled_features.robustness2_features.robustBufferAccess2 ||
                 (pipeline_state && pipeline_state->uses_pipeline_robustness));
            data_ptr[spvtools::kDebugOutputFlagsOffset] = spvtools::kInstBufferOOBEnable;
        }
        vmaUnmapMemory(vmaAllocator, output_block.allocation);
    }

    VkDescriptorBufferInfo di_input_desc_buffer_info = {};
    VkDescriptorBufferInfo bda_input_desc_buffer_info = {};
    VkWriteDescriptorSet desc_writes[3] = {};
    GpuAssistedPreDrawResources pre_draw_resources = {};
    GpuAssistedPreDispatchResources pre_dispatch_resources = {};
    uint32_t desc_count = 1;

    if (validate_draw_indirect &&
        ((command == Func::vkCmdDrawIndirectCount || command == Func::vkCmdDrawIndirectCountKHR ||
          command == Func::vkCmdDrawIndexedIndirectCount || command == Func::vkCmdDrawIndexedIndirectCountKHR) ||
         ((command == Func::vkCmdDrawIndirect || command == Func::vkCmdDrawIndexedIndirect) &&
          !(enabled_features.core.drawIndirectFirstInstance)))) {
        // Insert a draw that can examine some device memory right before the draw we're validating (Pre Draw Validation)
        //
        // NOTE that this validation does not attempt to abort invalid api calls as most other validation does.  A crash
        // or DEVICE_LOST resulting from the invalid call will prevent preceeding validation errors from being reported.

        assert(bind_point == VK_PIPELINE_BIND_POINT_GRAPHICS);
        assert(indirect_state != NULL);
        VkPipeline validation_pipeline = VK_NULL_HANDLE;
        AllocatePreDrawValidationResources(output_block, pre_draw_resources, cb_node->activeRenderPass.get()->renderPass(),
                                           use_shader_objects, &validation_pipeline, indirect_state);
        if (aborted) return;

        // Save current graphics pipeline state
        GPUAV_RESTORABLE_PIPELINE_STATE restorable_state;
        restorable_state.Create(cb_node.get(), VK_PIPELINE_BIND_POINT_GRAPHICS);

        // Save parameters for error message
        pre_draw_resources.buffer = indirect_state->buffer;
        pre_draw_resources.offset = indirect_state->offset;
        pre_draw_resources.stride = indirect_state->stride;

        uint32_t push_constants[pre_draw_resources.push_constant_words] = {};
        if (command == Func::vkCmdDrawIndirectCount || command == Func::vkCmdDrawIndirectCountKHR ||
            command == Func::vkCmdDrawIndexedIndirectCount || command == Func::vkCmdDrawIndexedIndirectCountKHR) {
            // Validate count buffer
            if (indirect_state->count_buffer_offset > std::numeric_limits<uint32_t>::max()) {
                ReportSetupProblem(device,
                                   "Count buffer offset is larger than can be contained in an unsigned int.  Aborting GPU-AV");
                aborted = true;
                return;
            }

            // Buffer size must be >= (stride * (drawCount - 1) + offset + sizeof(VkDrawIndirectCommand))
            uint32_t struct_size;
            if (command == Func::vkCmdDrawIndirectCount || command == Func::vkCmdDrawIndirectCountKHR) {
                struct_size = sizeof(VkDrawIndirectCommand);
            } else {
                assert(command == Func::vkCmdDrawIndexedIndirectCount || command == Func::vkCmdDrawIndexedIndirectCountKHR);
                struct_size = sizeof(VkDrawIndexedIndirectCommand);
            }
            auto buffer_state = Get<BUFFER_STATE>(indirect_state->buffer);
            uint32_t max_count;
            uint64_t bufsize = buffer_state->createInfo.size;
            uint64_t first_command_bytes = struct_size + indirect_state->offset;
            if (first_command_bytes > bufsize) {
                max_count = 0;
            } else {
                max_count = 1 + static_cast<uint32_t>(std::floor(((bufsize - first_command_bytes) / indirect_state->stride)));
            }
            pre_draw_resources.buf_size = buffer_state->createInfo.size;

            assert(phys_dev_props.limits.maxDrawIndirectCount > 0);
            push_constants[0] = phys_dev_props.limits.maxDrawIndirectCount;
            push_constants[1] = max_count;
            push_constants[2] = static_cast<uint32_t>((indirect_state->count_buffer_offset / sizeof(uint32_t)));
        } else {
            // Validate buffer for firstInstance check instead of count buffer check
            push_constants[0] = 0;
            push_constants[1] = indirect_state->draw_count;
            if (command == Func::vkCmdDrawIndirect) {
                push_constants[2] = static_cast<uint32_t>(
                    ((indirect_state->offset + offsetof(struct VkDrawIndirectCommand, firstInstance)) / sizeof(uint32_t)));
            } else {
                assert(command == Func::vkCmdDrawIndexedIndirect);
                push_constants[2] = static_cast<uint32_t>(
                    ((indirect_state->offset + offsetof(struct VkDrawIndexedIndirectCommand, firstInstance)) / sizeof(uint32_t)));
            }
            push_constants[3] = (indirect_state->stride / sizeof(uint32_t));
        }

        // Insert diagnostic draw
        if (use_shader_objects) {
            VkShaderStageFlagBits stage = VK_SHADER_STAGE_VERTEX_BIT;
            DispatchCmdBindShadersEXT(cmd_buffer, 1u, &stage, &pre_draw_validation_state.shader_object);
        } else {
            DispatchCmdBindPipeline(cmd_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, validation_pipeline);
        }
        DispatchCmdPushConstants(cmd_buffer, pre_draw_validation_state.pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0,
                                 sizeof(push_constants), push_constants);
        DispatchCmdBindDescriptorSets(cmd_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pre_draw_validation_state.pipeline_layout, 0, 1,
                                      &pre_draw_resources.desc_set, 0, nullptr);
        DispatchCmdDraw(cmd_buffer, 3, 1, 0, 0);

        // Restore the previous graphics pipeline state.
        restorable_state.Restore(cmd_buffer);
    } else if (validate_dispatch_indirect && command == Func::vkCmdDispatchIndirect) {
        // Insert a dispatch that can examine some device memory right before the dispatch we're validating
        //
        // NOTE that this validation does not attempt to abort invalid api calls as most other validation does.  A crash
        // or DEVICE_LOST resulting from the invalid call will prevent preceeding validation errors from being reported.

        AllocatePreDispatchValidationResources(output_block, pre_dispatch_resources, indirect_state, use_shader_objects);
        if (aborted) return;

        // Save current graphics pipeline state
        GPUAV_RESTORABLE_PIPELINE_STATE restorable_state;
        restorable_state.Create(cb_node.get(), VK_PIPELINE_BIND_POINT_COMPUTE);

        // Save parameters for error message
        pre_dispatch_resources.buffer = indirect_state->buffer;
        pre_dispatch_resources.offset = indirect_state->offset;

        uint32_t push_constants[pre_dispatch_resources.push_constant_words] = {};
        push_constants[0] = phys_dev_props.limits.maxComputeWorkGroupCount[0];
        push_constants[1] = phys_dev_props.limits.maxComputeWorkGroupCount[1];
        push_constants[2] = phys_dev_props.limits.maxComputeWorkGroupCount[2];
        push_constants[3] = static_cast<uint32_t>((indirect_state->offset / sizeof(uint32_t)));

        // Insert diagnostic dispatch
        if (use_shader_objects) {
            VkShaderStageFlagBits stage = VK_SHADER_STAGE_COMPUTE_BIT;
            DispatchCmdBindShadersEXT(cmd_buffer, 1u, &stage, &pre_dispatch_validation_state.shader_object);
        } else {
            DispatchCmdBindPipeline(cmd_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, pre_dispatch_validation_state.pipeline);
        }
        DispatchCmdPushConstants(cmd_buffer, pre_dispatch_validation_state.pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0,
                                 sizeof(push_constants), push_constants);
        DispatchCmdBindDescriptorSets(cmd_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, pre_dispatch_validation_state.pipeline_layout, 0,
                                      1, &pre_dispatch_resources.desc_set, 0, nullptr);
        DispatchCmdDispatch(cmd_buffer, 1, 1, 1);

        // Restore the previous compute pipeline state.
        restorable_state.Restore(cmd_buffer);
    }

    if (cb_node->current_input_buffer != VK_NULL_HANDLE) {
        di_input_desc_buffer_info.range = VK_WHOLE_SIZE;
        di_input_desc_buffer_info.buffer = cb_node->current_input_buffer;
        di_input_desc_buffer_info.offset = 0;

        desc_writes[desc_count] = vku::InitStructHelper();
        desc_writes[desc_count].dstBinding = 1;
        desc_writes[desc_count].descriptorCount = 1;
        desc_writes[desc_count].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        desc_writes[desc_count].pBufferInfo = &di_input_desc_buffer_info;
        desc_writes[desc_count].dstSet = desc_sets[0];
        desc_count++;
    }

    if (buffer_device_address) {
        bda_input_desc_buffer_info.range = app_bda_buffer_size;
        bda_input_desc_buffer_info.buffer = app_buffer_device_addresses.buffer;
        bda_input_desc_buffer_info.offset = 0;

        desc_writes[desc_count] = vku::InitStructHelper();
        desc_writes[desc_count].dstBinding = 2;
        desc_writes[desc_count].descriptorCount = 1;
        desc_writes[desc_count].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        desc_writes[desc_count].pBufferInfo = &bda_input_desc_buffer_info;
        desc_writes[desc_count].dstSet = desc_sets[0];
        desc_count++;
    }

    // Write the descriptor
    output_desc_buffer_info.buffer = output_block.buffer;
    output_desc_buffer_info.offset = 0;

    desc_writes[0] = vku::InitStructHelper();
    desc_writes[0].descriptorCount = 1;
    desc_writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    desc_writes[0].pBufferInfo = &output_desc_buffer_info;
    desc_writes[0].dstSet = desc_sets[0];
    DispatchUpdateDescriptorSets(device, desc_count, desc_writes, 0, NULL);

    const auto pipeline_layout =
        pipeline_state ? pipeline_state->PipelineLayoutState() : Get<PIPELINE_LAYOUT_STATE>(last_bound.pipeline_layout);
    // If GPL is used, it's possible the pipeline layout used at pipeline creation time is null. If CmdBindDescriptorSets has
    // not been called yet (i.e., state.pipeline_null), then fall back to the layout associated with pre-raster state.
    // PipelineLayoutState should be used for the purposes of determining the number of sets in the layout, but this layout
    // may be a "pseudo layout" used to represent the union of pre-raster and fragment shader layouts, and therefore have a
    // null handle.
    VkPipelineLayout pipeline_layout_handle = VK_NULL_HANDLE;
    if (last_bound.pipeline_layout) {
        pipeline_layout_handle = last_bound.pipeline_layout;
    } else if (pipeline_state && !pipeline_state->PreRasterPipelineLayoutState()->Destroyed()) {
        pipeline_layout_handle = pipeline_state->PreRasterPipelineLayoutState()->layout();
    }
    if ((pipeline_layout && pipeline_layout->set_layouts.size() <= desc_set_bind_index) &&
        pipeline_layout_handle != VK_NULL_HANDLE) {
        DispatchCmdBindDescriptorSets(cmd_buffer, bind_point, pipeline_layout_handle, desc_set_bind_index, 1, desc_sets.data(), 0,
                                      nullptr);
    } else {
        // If no pipeline layout was bound when using shader objects that don't use any descriptor set, bind the debug pipeline
        // layout
        DispatchCmdBindDescriptorSets(cmd_buffer, bind_point, debug_pipeline_layout, desc_set_bind_index, 1, desc_sets.data(), 0,
                                      nullptr);
    }

    if (pipeline_state && pipeline_layout_handle == VK_NULL_HANDLE) {
        ReportSetupProblem(device, "Unable to find pipeline layout to bind debug descriptor set. Aborting GPU-AV");
        aborted = true;
        vmaDestroyBuffer(vmaAllocator, output_block.buffer, output_block.allocation);
    } else {
        // It is possible to have no descriptor sets bound, for example if using push constants.
        uint32_t di_buf_index =
            cb_node->di_input_buffer_list.size() > 0 ? uint32_t(cb_node->di_input_buffer_list.size()) - 1 : vvl::kU32Max;
        // Record buffer and memory info in CB state tracking
        cb_node->per_draw_buffer_list.emplace_back(output_block, pre_draw_resources, pre_dispatch_resources, desc_sets[0],
                                                   desc_pool, bind_point, uses_robustness, command, di_buf_index);
    }
}

std::shared_ptr<cvdescriptorset::DescriptorSet> GpuAssisted::CreateDescriptorSet(
    VkDescriptorSet set, DESCRIPTOR_POOL_STATE *pool, const std::shared_ptr<cvdescriptorset::DescriptorSetLayout const> &layout,
    uint32_t variable_count) {
    return std::static_pointer_cast<cvdescriptorset::DescriptorSet>(
        std::make_shared<gpuav_state::DescriptorSet>(set, pool, layout, variable_count, this));
}

std::shared_ptr<CMD_BUFFER_STATE> GpuAssisted::CreateCmdBufferState(VkCommandBuffer cb,
                                                                    const VkCommandBufferAllocateInfo *pCreateInfo,
                                                                    const COMMAND_POOL_STATE *pool) {
    return std::static_pointer_cast<CMD_BUFFER_STATE>(std::make_shared<gpuav_state::CommandBuffer>(this, cb, pCreateInfo, pool));
}

gpuav_state::CommandBuffer::CommandBuffer(GpuAssisted *ga, VkCommandBuffer cb, const VkCommandBufferAllocateInfo *pCreateInfo,
                                          const COMMAND_POOL_STATE *pool)
    : gpu_utils_state::CommandBuffer(ga, cb, pCreateInfo, pool) {}

gpuav_state::CommandBuffer::~CommandBuffer() { Destroy(); }

void gpuav_state::CommandBuffer::Destroy() {
    ResetCBState();
    CMD_BUFFER_STATE::Destroy();
}

void gpuav_state::CommandBuffer::Reset() {
    CMD_BUFFER_STATE::Reset();
    ResetCBState();
}

void gpuav_state::CommandBuffer::ResetCBState() {
    auto gpuav = static_cast<GpuAssisted *>(dev_data);
    // Free the device memory and descriptor set(s) associated with a command buffer.
    for (auto &buffer_info : per_draw_buffer_list) {
        gpuav->DestroyBuffer(buffer_info);
    }
    per_draw_buffer_list.clear();

    for (auto &buffer_info : di_input_buffer_list) {
        vmaDestroyBuffer(gpuav->vmaAllocator, buffer_info.address_buffer, buffer_info.address_buffer_allocation);
    }
    di_input_buffer_list.clear();
    current_input_buffer = VK_NULL_HANDLE;

    for (auto &as_validation_buffer_info : as_validation_buffers) {
        gpuav->DestroyBuffer(as_validation_buffer_info);
    }
    as_validation_buffers.clear();
}
