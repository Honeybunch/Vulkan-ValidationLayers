// *** THIS FILE IS GENERATED - DO NOT EDIT ***
// See dynamic_state_generator.py for modifications


/***************************************************************************
 *
 * Copyright (c) 2023 Valve Corporation
 * Copyright (c) 2023 LunarG, Inc.
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
 ****************************************************************************/

// NOLINTBEGIN
#include "core_checks/core_validation.h"

static VkDynamicState ConvertToDynamicState(CBDynamicState dynamic_state) {
    switch (dynamic_state) {
        case CB_DYNAMIC_STATE_VIEWPORT:
            return VK_DYNAMIC_STATE_VIEWPORT;
        case CB_DYNAMIC_STATE_SCISSOR:
            return VK_DYNAMIC_STATE_SCISSOR;
        case CB_DYNAMIC_STATE_LINE_WIDTH:
            return VK_DYNAMIC_STATE_LINE_WIDTH;
        case CB_DYNAMIC_STATE_DEPTH_BIAS:
            return VK_DYNAMIC_STATE_DEPTH_BIAS;
        case CB_DYNAMIC_STATE_BLEND_CONSTANTS:
            return VK_DYNAMIC_STATE_BLEND_CONSTANTS;
        case CB_DYNAMIC_STATE_DEPTH_BOUNDS:
            return VK_DYNAMIC_STATE_DEPTH_BOUNDS;
        case CB_DYNAMIC_STATE_STENCIL_COMPARE_MASK:
            return VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK;
        case CB_DYNAMIC_STATE_STENCIL_WRITE_MASK:
            return VK_DYNAMIC_STATE_STENCIL_WRITE_MASK;
        case CB_DYNAMIC_STATE_STENCIL_REFERENCE:
            return VK_DYNAMIC_STATE_STENCIL_REFERENCE;
        case CB_DYNAMIC_STATE_CULL_MODE:
            return VK_DYNAMIC_STATE_CULL_MODE;
        case CB_DYNAMIC_STATE_FRONT_FACE:
            return VK_DYNAMIC_STATE_FRONT_FACE;
        case CB_DYNAMIC_STATE_PRIMITIVE_TOPOLOGY:
            return VK_DYNAMIC_STATE_PRIMITIVE_TOPOLOGY;
        case CB_DYNAMIC_STATE_VIEWPORT_WITH_COUNT:
            return VK_DYNAMIC_STATE_VIEWPORT_WITH_COUNT;
        case CB_DYNAMIC_STATE_SCISSOR_WITH_COUNT:
            return VK_DYNAMIC_STATE_SCISSOR_WITH_COUNT;
        case CB_DYNAMIC_STATE_VERTEX_INPUT_BINDING_STRIDE:
            return VK_DYNAMIC_STATE_VERTEX_INPUT_BINDING_STRIDE;
        case CB_DYNAMIC_STATE_DEPTH_TEST_ENABLE:
            return VK_DYNAMIC_STATE_DEPTH_TEST_ENABLE;
        case CB_DYNAMIC_STATE_DEPTH_WRITE_ENABLE:
            return VK_DYNAMIC_STATE_DEPTH_WRITE_ENABLE;
        case CB_DYNAMIC_STATE_DEPTH_COMPARE_OP:
            return VK_DYNAMIC_STATE_DEPTH_COMPARE_OP;
        case CB_DYNAMIC_STATE_DEPTH_BOUNDS_TEST_ENABLE:
            return VK_DYNAMIC_STATE_DEPTH_BOUNDS_TEST_ENABLE;
        case CB_DYNAMIC_STATE_STENCIL_TEST_ENABLE:
            return VK_DYNAMIC_STATE_STENCIL_TEST_ENABLE;
        case CB_DYNAMIC_STATE_STENCIL_OP:
            return VK_DYNAMIC_STATE_STENCIL_OP;
        case CB_DYNAMIC_STATE_RASTERIZER_DISCARD_ENABLE:
            return VK_DYNAMIC_STATE_RASTERIZER_DISCARD_ENABLE;
        case CB_DYNAMIC_STATE_DEPTH_BIAS_ENABLE:
            return VK_DYNAMIC_STATE_DEPTH_BIAS_ENABLE;
        case CB_DYNAMIC_STATE_PRIMITIVE_RESTART_ENABLE:
            return VK_DYNAMIC_STATE_PRIMITIVE_RESTART_ENABLE;
        case CB_DYNAMIC_STATE_VIEWPORT_W_SCALING_NV:
            return VK_DYNAMIC_STATE_VIEWPORT_W_SCALING_NV;
        case CB_DYNAMIC_STATE_DISCARD_RECTANGLE_EXT:
            return VK_DYNAMIC_STATE_DISCARD_RECTANGLE_EXT;
        case CB_DYNAMIC_STATE_DISCARD_RECTANGLE_ENABLE_EXT:
            return VK_DYNAMIC_STATE_DISCARD_RECTANGLE_ENABLE_EXT;
        case CB_DYNAMIC_STATE_DISCARD_RECTANGLE_MODE_EXT:
            return VK_DYNAMIC_STATE_DISCARD_RECTANGLE_MODE_EXT;
        case CB_DYNAMIC_STATE_SAMPLE_LOCATIONS_EXT:
            return VK_DYNAMIC_STATE_SAMPLE_LOCATIONS_EXT;
        case CB_DYNAMIC_STATE_RAY_TRACING_PIPELINE_STACK_SIZE_KHR:
            return VK_DYNAMIC_STATE_RAY_TRACING_PIPELINE_STACK_SIZE_KHR;
        case CB_DYNAMIC_STATE_VIEWPORT_SHADING_RATE_PALETTE_NV:
            return VK_DYNAMIC_STATE_VIEWPORT_SHADING_RATE_PALETTE_NV;
        case CB_DYNAMIC_STATE_VIEWPORT_COARSE_SAMPLE_ORDER_NV:
            return VK_DYNAMIC_STATE_VIEWPORT_COARSE_SAMPLE_ORDER_NV;
        case CB_DYNAMIC_STATE_EXCLUSIVE_SCISSOR_ENABLE_NV:
            return VK_DYNAMIC_STATE_EXCLUSIVE_SCISSOR_ENABLE_NV;
        case CB_DYNAMIC_STATE_EXCLUSIVE_SCISSOR_NV:
            return VK_DYNAMIC_STATE_EXCLUSIVE_SCISSOR_NV;
        case CB_DYNAMIC_STATE_FRAGMENT_SHADING_RATE_KHR:
            return VK_DYNAMIC_STATE_FRAGMENT_SHADING_RATE_KHR;
        case CB_DYNAMIC_STATE_LINE_STIPPLE_EXT:
            return VK_DYNAMIC_STATE_LINE_STIPPLE_EXT;
        case CB_DYNAMIC_STATE_VERTEX_INPUT_EXT:
            return VK_DYNAMIC_STATE_VERTEX_INPUT_EXT;
        case CB_DYNAMIC_STATE_PATCH_CONTROL_POINTS_EXT:
            return VK_DYNAMIC_STATE_PATCH_CONTROL_POINTS_EXT;
        case CB_DYNAMIC_STATE_LOGIC_OP_EXT:
            return VK_DYNAMIC_STATE_LOGIC_OP_EXT;
        case CB_DYNAMIC_STATE_COLOR_WRITE_ENABLE_EXT:
            return VK_DYNAMIC_STATE_COLOR_WRITE_ENABLE_EXT;
        case CB_DYNAMIC_STATE_TESSELLATION_DOMAIN_ORIGIN_EXT:
            return VK_DYNAMIC_STATE_TESSELLATION_DOMAIN_ORIGIN_EXT;
        case CB_DYNAMIC_STATE_DEPTH_CLAMP_ENABLE_EXT:
            return VK_DYNAMIC_STATE_DEPTH_CLAMP_ENABLE_EXT;
        case CB_DYNAMIC_STATE_POLYGON_MODE_EXT:
            return VK_DYNAMIC_STATE_POLYGON_MODE_EXT;
        case CB_DYNAMIC_STATE_RASTERIZATION_SAMPLES_EXT:
            return VK_DYNAMIC_STATE_RASTERIZATION_SAMPLES_EXT;
        case CB_DYNAMIC_STATE_SAMPLE_MASK_EXT:
            return VK_DYNAMIC_STATE_SAMPLE_MASK_EXT;
        case CB_DYNAMIC_STATE_ALPHA_TO_COVERAGE_ENABLE_EXT:
            return VK_DYNAMIC_STATE_ALPHA_TO_COVERAGE_ENABLE_EXT;
        case CB_DYNAMIC_STATE_ALPHA_TO_ONE_ENABLE_EXT:
            return VK_DYNAMIC_STATE_ALPHA_TO_ONE_ENABLE_EXT;
        case CB_DYNAMIC_STATE_LOGIC_OP_ENABLE_EXT:
            return VK_DYNAMIC_STATE_LOGIC_OP_ENABLE_EXT;
        case CB_DYNAMIC_STATE_COLOR_BLEND_ENABLE_EXT:
            return VK_DYNAMIC_STATE_COLOR_BLEND_ENABLE_EXT;
        case CB_DYNAMIC_STATE_COLOR_BLEND_EQUATION_EXT:
            return VK_DYNAMIC_STATE_COLOR_BLEND_EQUATION_EXT;
        case CB_DYNAMIC_STATE_COLOR_WRITE_MASK_EXT:
            return VK_DYNAMIC_STATE_COLOR_WRITE_MASK_EXT;
        case CB_DYNAMIC_STATE_RASTERIZATION_STREAM_EXT:
            return VK_DYNAMIC_STATE_RASTERIZATION_STREAM_EXT;
        case CB_DYNAMIC_STATE_CONSERVATIVE_RASTERIZATION_MODE_EXT:
            return VK_DYNAMIC_STATE_CONSERVATIVE_RASTERIZATION_MODE_EXT;
        case CB_DYNAMIC_STATE_EXTRA_PRIMITIVE_OVERESTIMATION_SIZE_EXT:
            return VK_DYNAMIC_STATE_EXTRA_PRIMITIVE_OVERESTIMATION_SIZE_EXT;
        case CB_DYNAMIC_STATE_DEPTH_CLIP_ENABLE_EXT:
            return VK_DYNAMIC_STATE_DEPTH_CLIP_ENABLE_EXT;
        case CB_DYNAMIC_STATE_SAMPLE_LOCATIONS_ENABLE_EXT:
            return VK_DYNAMIC_STATE_SAMPLE_LOCATIONS_ENABLE_EXT;
        case CB_DYNAMIC_STATE_COLOR_BLEND_ADVANCED_EXT:
            return VK_DYNAMIC_STATE_COLOR_BLEND_ADVANCED_EXT;
        case CB_DYNAMIC_STATE_PROVOKING_VERTEX_MODE_EXT:
            return VK_DYNAMIC_STATE_PROVOKING_VERTEX_MODE_EXT;
        case CB_DYNAMIC_STATE_LINE_RASTERIZATION_MODE_EXT:
            return VK_DYNAMIC_STATE_LINE_RASTERIZATION_MODE_EXT;
        case CB_DYNAMIC_STATE_LINE_STIPPLE_ENABLE_EXT:
            return VK_DYNAMIC_STATE_LINE_STIPPLE_ENABLE_EXT;
        case CB_DYNAMIC_STATE_DEPTH_CLIP_NEGATIVE_ONE_TO_ONE_EXT:
            return VK_DYNAMIC_STATE_DEPTH_CLIP_NEGATIVE_ONE_TO_ONE_EXT;
        case CB_DYNAMIC_STATE_VIEWPORT_W_SCALING_ENABLE_NV:
            return VK_DYNAMIC_STATE_VIEWPORT_W_SCALING_ENABLE_NV;
        case CB_DYNAMIC_STATE_VIEWPORT_SWIZZLE_NV:
            return VK_DYNAMIC_STATE_VIEWPORT_SWIZZLE_NV;
        case CB_DYNAMIC_STATE_COVERAGE_TO_COLOR_ENABLE_NV:
            return VK_DYNAMIC_STATE_COVERAGE_TO_COLOR_ENABLE_NV;
        case CB_DYNAMIC_STATE_COVERAGE_TO_COLOR_LOCATION_NV:
            return VK_DYNAMIC_STATE_COVERAGE_TO_COLOR_LOCATION_NV;
        case CB_DYNAMIC_STATE_COVERAGE_MODULATION_MODE_NV:
            return VK_DYNAMIC_STATE_COVERAGE_MODULATION_MODE_NV;
        case CB_DYNAMIC_STATE_COVERAGE_MODULATION_TABLE_ENABLE_NV:
            return VK_DYNAMIC_STATE_COVERAGE_MODULATION_TABLE_ENABLE_NV;
        case CB_DYNAMIC_STATE_COVERAGE_MODULATION_TABLE_NV:
            return VK_DYNAMIC_STATE_COVERAGE_MODULATION_TABLE_NV;
        case CB_DYNAMIC_STATE_SHADING_RATE_IMAGE_ENABLE_NV:
            return VK_DYNAMIC_STATE_SHADING_RATE_IMAGE_ENABLE_NV;
        case CB_DYNAMIC_STATE_REPRESENTATIVE_FRAGMENT_TEST_ENABLE_NV:
            return VK_DYNAMIC_STATE_REPRESENTATIVE_FRAGMENT_TEST_ENABLE_NV;
        case CB_DYNAMIC_STATE_COVERAGE_REDUCTION_MODE_NV:
            return VK_DYNAMIC_STATE_COVERAGE_REDUCTION_MODE_NV;
        case CB_DYNAMIC_STATE_ATTACHMENT_FEEDBACK_LOOP_ENABLE_EXT:
            return VK_DYNAMIC_STATE_ATTACHMENT_FEEDBACK_LOOP_ENABLE_EXT;
        default:
            return VK_DYNAMIC_STATE_MAX_ENUM;
    }
}

CBDynamicState ConvertToCBDynamicState(VkDynamicState dynamic_state) {
    switch (dynamic_state) {
        case VK_DYNAMIC_STATE_VIEWPORT:
            return CB_DYNAMIC_STATE_VIEWPORT;
        case VK_DYNAMIC_STATE_SCISSOR:
            return CB_DYNAMIC_STATE_SCISSOR;
        case VK_DYNAMIC_STATE_LINE_WIDTH:
            return CB_DYNAMIC_STATE_LINE_WIDTH;
        case VK_DYNAMIC_STATE_DEPTH_BIAS:
            return CB_DYNAMIC_STATE_DEPTH_BIAS;
        case VK_DYNAMIC_STATE_BLEND_CONSTANTS:
            return CB_DYNAMIC_STATE_BLEND_CONSTANTS;
        case VK_DYNAMIC_STATE_DEPTH_BOUNDS:
            return CB_DYNAMIC_STATE_DEPTH_BOUNDS;
        case VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK:
            return CB_DYNAMIC_STATE_STENCIL_COMPARE_MASK;
        case VK_DYNAMIC_STATE_STENCIL_WRITE_MASK:
            return CB_DYNAMIC_STATE_STENCIL_WRITE_MASK;
        case VK_DYNAMIC_STATE_STENCIL_REFERENCE:
            return CB_DYNAMIC_STATE_STENCIL_REFERENCE;
        case VK_DYNAMIC_STATE_CULL_MODE:
            return CB_DYNAMIC_STATE_CULL_MODE;
        case VK_DYNAMIC_STATE_FRONT_FACE:
            return CB_DYNAMIC_STATE_FRONT_FACE;
        case VK_DYNAMIC_STATE_PRIMITIVE_TOPOLOGY:
            return CB_DYNAMIC_STATE_PRIMITIVE_TOPOLOGY;
        case VK_DYNAMIC_STATE_VIEWPORT_WITH_COUNT:
            return CB_DYNAMIC_STATE_VIEWPORT_WITH_COUNT;
        case VK_DYNAMIC_STATE_SCISSOR_WITH_COUNT:
            return CB_DYNAMIC_STATE_SCISSOR_WITH_COUNT;
        case VK_DYNAMIC_STATE_VERTEX_INPUT_BINDING_STRIDE:
            return CB_DYNAMIC_STATE_VERTEX_INPUT_BINDING_STRIDE;
        case VK_DYNAMIC_STATE_DEPTH_TEST_ENABLE:
            return CB_DYNAMIC_STATE_DEPTH_TEST_ENABLE;
        case VK_DYNAMIC_STATE_DEPTH_WRITE_ENABLE:
            return CB_DYNAMIC_STATE_DEPTH_WRITE_ENABLE;
        case VK_DYNAMIC_STATE_DEPTH_COMPARE_OP:
            return CB_DYNAMIC_STATE_DEPTH_COMPARE_OP;
        case VK_DYNAMIC_STATE_DEPTH_BOUNDS_TEST_ENABLE:
            return CB_DYNAMIC_STATE_DEPTH_BOUNDS_TEST_ENABLE;
        case VK_DYNAMIC_STATE_STENCIL_TEST_ENABLE:
            return CB_DYNAMIC_STATE_STENCIL_TEST_ENABLE;
        case VK_DYNAMIC_STATE_STENCIL_OP:
            return CB_DYNAMIC_STATE_STENCIL_OP;
        case VK_DYNAMIC_STATE_RASTERIZER_DISCARD_ENABLE:
            return CB_DYNAMIC_STATE_RASTERIZER_DISCARD_ENABLE;
        case VK_DYNAMIC_STATE_DEPTH_BIAS_ENABLE:
            return CB_DYNAMIC_STATE_DEPTH_BIAS_ENABLE;
        case VK_DYNAMIC_STATE_PRIMITIVE_RESTART_ENABLE:
            return CB_DYNAMIC_STATE_PRIMITIVE_RESTART_ENABLE;
        case VK_DYNAMIC_STATE_VIEWPORT_W_SCALING_NV:
            return CB_DYNAMIC_STATE_VIEWPORT_W_SCALING_NV;
        case VK_DYNAMIC_STATE_DISCARD_RECTANGLE_EXT:
            return CB_DYNAMIC_STATE_DISCARD_RECTANGLE_EXT;
        case VK_DYNAMIC_STATE_DISCARD_RECTANGLE_ENABLE_EXT:
            return CB_DYNAMIC_STATE_DISCARD_RECTANGLE_ENABLE_EXT;
        case VK_DYNAMIC_STATE_DISCARD_RECTANGLE_MODE_EXT:
            return CB_DYNAMIC_STATE_DISCARD_RECTANGLE_MODE_EXT;
        case VK_DYNAMIC_STATE_SAMPLE_LOCATIONS_EXT:
            return CB_DYNAMIC_STATE_SAMPLE_LOCATIONS_EXT;
        case VK_DYNAMIC_STATE_RAY_TRACING_PIPELINE_STACK_SIZE_KHR:
            return CB_DYNAMIC_STATE_RAY_TRACING_PIPELINE_STACK_SIZE_KHR;
        case VK_DYNAMIC_STATE_VIEWPORT_SHADING_RATE_PALETTE_NV:
            return CB_DYNAMIC_STATE_VIEWPORT_SHADING_RATE_PALETTE_NV;
        case VK_DYNAMIC_STATE_VIEWPORT_COARSE_SAMPLE_ORDER_NV:
            return CB_DYNAMIC_STATE_VIEWPORT_COARSE_SAMPLE_ORDER_NV;
        case VK_DYNAMIC_STATE_EXCLUSIVE_SCISSOR_ENABLE_NV:
            return CB_DYNAMIC_STATE_EXCLUSIVE_SCISSOR_ENABLE_NV;
        case VK_DYNAMIC_STATE_EXCLUSIVE_SCISSOR_NV:
            return CB_DYNAMIC_STATE_EXCLUSIVE_SCISSOR_NV;
        case VK_DYNAMIC_STATE_FRAGMENT_SHADING_RATE_KHR:
            return CB_DYNAMIC_STATE_FRAGMENT_SHADING_RATE_KHR;
        case VK_DYNAMIC_STATE_LINE_STIPPLE_EXT:
            return CB_DYNAMIC_STATE_LINE_STIPPLE_EXT;
        case VK_DYNAMIC_STATE_VERTEX_INPUT_EXT:
            return CB_DYNAMIC_STATE_VERTEX_INPUT_EXT;
        case VK_DYNAMIC_STATE_PATCH_CONTROL_POINTS_EXT:
            return CB_DYNAMIC_STATE_PATCH_CONTROL_POINTS_EXT;
        case VK_DYNAMIC_STATE_LOGIC_OP_EXT:
            return CB_DYNAMIC_STATE_LOGIC_OP_EXT;
        case VK_DYNAMIC_STATE_COLOR_WRITE_ENABLE_EXT:
            return CB_DYNAMIC_STATE_COLOR_WRITE_ENABLE_EXT;
        case VK_DYNAMIC_STATE_TESSELLATION_DOMAIN_ORIGIN_EXT:
            return CB_DYNAMIC_STATE_TESSELLATION_DOMAIN_ORIGIN_EXT;
        case VK_DYNAMIC_STATE_DEPTH_CLAMP_ENABLE_EXT:
            return CB_DYNAMIC_STATE_DEPTH_CLAMP_ENABLE_EXT;
        case VK_DYNAMIC_STATE_POLYGON_MODE_EXT:
            return CB_DYNAMIC_STATE_POLYGON_MODE_EXT;
        case VK_DYNAMIC_STATE_RASTERIZATION_SAMPLES_EXT:
            return CB_DYNAMIC_STATE_RASTERIZATION_SAMPLES_EXT;
        case VK_DYNAMIC_STATE_SAMPLE_MASK_EXT:
            return CB_DYNAMIC_STATE_SAMPLE_MASK_EXT;
        case VK_DYNAMIC_STATE_ALPHA_TO_COVERAGE_ENABLE_EXT:
            return CB_DYNAMIC_STATE_ALPHA_TO_COVERAGE_ENABLE_EXT;
        case VK_DYNAMIC_STATE_ALPHA_TO_ONE_ENABLE_EXT:
            return CB_DYNAMIC_STATE_ALPHA_TO_ONE_ENABLE_EXT;
        case VK_DYNAMIC_STATE_LOGIC_OP_ENABLE_EXT:
            return CB_DYNAMIC_STATE_LOGIC_OP_ENABLE_EXT;
        case VK_DYNAMIC_STATE_COLOR_BLEND_ENABLE_EXT:
            return CB_DYNAMIC_STATE_COLOR_BLEND_ENABLE_EXT;
        case VK_DYNAMIC_STATE_COLOR_BLEND_EQUATION_EXT:
            return CB_DYNAMIC_STATE_COLOR_BLEND_EQUATION_EXT;
        case VK_DYNAMIC_STATE_COLOR_WRITE_MASK_EXT:
            return CB_DYNAMIC_STATE_COLOR_WRITE_MASK_EXT;
        case VK_DYNAMIC_STATE_RASTERIZATION_STREAM_EXT:
            return CB_DYNAMIC_STATE_RASTERIZATION_STREAM_EXT;
        case VK_DYNAMIC_STATE_CONSERVATIVE_RASTERIZATION_MODE_EXT:
            return CB_DYNAMIC_STATE_CONSERVATIVE_RASTERIZATION_MODE_EXT;
        case VK_DYNAMIC_STATE_EXTRA_PRIMITIVE_OVERESTIMATION_SIZE_EXT:
            return CB_DYNAMIC_STATE_EXTRA_PRIMITIVE_OVERESTIMATION_SIZE_EXT;
        case VK_DYNAMIC_STATE_DEPTH_CLIP_ENABLE_EXT:
            return CB_DYNAMIC_STATE_DEPTH_CLIP_ENABLE_EXT;
        case VK_DYNAMIC_STATE_SAMPLE_LOCATIONS_ENABLE_EXT:
            return CB_DYNAMIC_STATE_SAMPLE_LOCATIONS_ENABLE_EXT;
        case VK_DYNAMIC_STATE_COLOR_BLEND_ADVANCED_EXT:
            return CB_DYNAMIC_STATE_COLOR_BLEND_ADVANCED_EXT;
        case VK_DYNAMIC_STATE_PROVOKING_VERTEX_MODE_EXT:
            return CB_DYNAMIC_STATE_PROVOKING_VERTEX_MODE_EXT;
        case VK_DYNAMIC_STATE_LINE_RASTERIZATION_MODE_EXT:
            return CB_DYNAMIC_STATE_LINE_RASTERIZATION_MODE_EXT;
        case VK_DYNAMIC_STATE_LINE_STIPPLE_ENABLE_EXT:
            return CB_DYNAMIC_STATE_LINE_STIPPLE_ENABLE_EXT;
        case VK_DYNAMIC_STATE_DEPTH_CLIP_NEGATIVE_ONE_TO_ONE_EXT:
            return CB_DYNAMIC_STATE_DEPTH_CLIP_NEGATIVE_ONE_TO_ONE_EXT;
        case VK_DYNAMIC_STATE_VIEWPORT_W_SCALING_ENABLE_NV:
            return CB_DYNAMIC_STATE_VIEWPORT_W_SCALING_ENABLE_NV;
        case VK_DYNAMIC_STATE_VIEWPORT_SWIZZLE_NV:
            return CB_DYNAMIC_STATE_VIEWPORT_SWIZZLE_NV;
        case VK_DYNAMIC_STATE_COVERAGE_TO_COLOR_ENABLE_NV:
            return CB_DYNAMIC_STATE_COVERAGE_TO_COLOR_ENABLE_NV;
        case VK_DYNAMIC_STATE_COVERAGE_TO_COLOR_LOCATION_NV:
            return CB_DYNAMIC_STATE_COVERAGE_TO_COLOR_LOCATION_NV;
        case VK_DYNAMIC_STATE_COVERAGE_MODULATION_MODE_NV:
            return CB_DYNAMIC_STATE_COVERAGE_MODULATION_MODE_NV;
        case VK_DYNAMIC_STATE_COVERAGE_MODULATION_TABLE_ENABLE_NV:
            return CB_DYNAMIC_STATE_COVERAGE_MODULATION_TABLE_ENABLE_NV;
        case VK_DYNAMIC_STATE_COVERAGE_MODULATION_TABLE_NV:
            return CB_DYNAMIC_STATE_COVERAGE_MODULATION_TABLE_NV;
        case VK_DYNAMIC_STATE_SHADING_RATE_IMAGE_ENABLE_NV:
            return CB_DYNAMIC_STATE_SHADING_RATE_IMAGE_ENABLE_NV;
        case VK_DYNAMIC_STATE_REPRESENTATIVE_FRAGMENT_TEST_ENABLE_NV:
            return CB_DYNAMIC_STATE_REPRESENTATIVE_FRAGMENT_TEST_ENABLE_NV;
        case VK_DYNAMIC_STATE_COVERAGE_REDUCTION_MODE_NV:
            return CB_DYNAMIC_STATE_COVERAGE_REDUCTION_MODE_NV;
        case VK_DYNAMIC_STATE_ATTACHMENT_FEEDBACK_LOOP_ENABLE_EXT:
            return CB_DYNAMIC_STATE_ATTACHMENT_FEEDBACK_LOOP_ENABLE_EXT;
        default:
            return CB_DYNAMIC_STATE_STATUS_NUM;
    }
}

const char* DynamicStateToString(CBDynamicState dynamic_state) {
    return string_VkDynamicState(ConvertToDynamicState(dynamic_state));
}

std::string DynamicStatesToString(CBDynamicFlags const &dynamic_states) {
    std::string ret;
    // enum is not zero based
    for (int index = 1; index < CB_DYNAMIC_STATE_STATUS_NUM; ++index) {
        CBDynamicState status = static_cast<CBDynamicState>(index);
        if (dynamic_states[status]) {
            if (!ret.empty()) ret.append("|");
            ret.append(string_VkDynamicState(ConvertToDynamicState(status)));
        }
    }
    if (ret.empty()) ret.append(string_VkDynamicState(ConvertToDynamicState(CB_DYNAMIC_STATE_STATUS_NUM)));
    return ret;
}

// NOLINTEND
