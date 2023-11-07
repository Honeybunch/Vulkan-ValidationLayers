/*
 * Copyright (c) 2019-2023 Valve Corporation
 * Copyright (c) 2019-2023 LunarG, Inc.
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

#pragma once

#include <limits>
#include <memory>
#include <set>
#include <vulkan/vulkan.h>

#include "state_tracker/state_tracker.h"
#include "state_tracker/cmd_buffer_state.h"
#include "state_tracker/render_pass_state.h"

#include "sync/sync_model.h"

class AccessContext;
class CommandBufferAccessContext;
class CommandExecutionContext;
struct PresentedImage;
class QueueBatchContext;
struct QueueSubmitCmdState;
class RenderPassAccessContext;
class ReplayState;
class ResourceAccessState;
class ResourceAccessWriteState;
struct ResourceFirstAccess;
class SyncEventsContext;
struct SyncEventState;
class SyncValidator;
struct ClearAttachmentInfo;

using ImageRangeGen = subresource_adapter::ImageRangeGenerator;

namespace syncval_state {
class CommandBuffer;
class Swapchain;

class ImageState : public IMAGE_STATE {
  public:
    ImageState(const ValidationStateTracker *dev_data, VkImage img, const VkImageCreateInfo *pCreateInfo,
               VkFormatFeatureFlags2KHR features)
        : IMAGE_STATE(dev_data, img, pCreateInfo, features), opaque_base_address_(0U) {}

    ImageState(const ValidationStateTracker *dev_data, VkImage img, const VkImageCreateInfo *pCreateInfo, VkSwapchainKHR swapchain,
               uint32_t swapchain_index, VkFormatFeatureFlags2KHR features)
        : IMAGE_STATE(dev_data, img, pCreateInfo, swapchain, swapchain_index, features), opaque_base_address_(0U) {}
    bool IsLinear() const { return fragment_encoder->IsLinearImage(); }
    bool IsTiled() const { return !IsLinear(); }
    bool IsSimplyBound() const;

    void SetOpaqueBaseAddress(ValidationStateTracker &dev_data);

    VkDeviceSize GetOpaqueBaseAddress() const { return opaque_base_address_; }
    bool HasOpaqueMapping() const { return 0U != opaque_base_address_; }
    VkDeviceSize GetResourceBaseAddress() const;
    ImageRangeGen MakeImageRangeGen(const VkImageSubresourceRange &subresource_range, bool is_depth_sliced) const;
    ImageRangeGen MakeImageRangeGen(const VkImageSubresourceRange &subresource_range, const VkOffset3D &offset,
                                    const VkExtent3D &extent, bool is_depth_sliced) const;

  protected:
    VkDeviceSize opaque_base_address_ = 0U;
};
class ImageViewState : public IMAGE_VIEW_STATE {
  public:
    ImageViewState(const std::shared_ptr<IMAGE_STATE> &image_state, VkImageView iv, const VkImageViewCreateInfo *ci,
                   VkFormatFeatureFlags2KHR ff, const VkFilterCubicImageViewImageFormatPropertiesEXT &cubic_props);
    const ImageState *GetImageState() const { return static_cast<const syncval_state::ImageState *>(image_state.get()); }
    ImageRangeGen MakeImageRangeGen(const VkOffset3D &offset, const VkExtent3D &extent, VkImageAspectFlags aspect_mask = 0) const;
    const ImageRangeGen &GetFullViewImageRangeGen() const { return view_range_gen; }

  protected:
    ImageRangeGen MakeImageRangeGen() const;
    // All data members needs for MakeImageRangeGen() must be set before initializing view_range_gen... i.e. above this line.
    const ImageRangeGen view_range_gen;
};

enum class AttachmentType { kColor, kDepth, kStencil };

struct DynamicRenderingInfo {
    struct Attachment {
        const safe_VkRenderingAttachmentInfo &info;
        std::shared_ptr<const ImageViewState> view;
        std::shared_ptr<const ImageViewState> resolve_view;
        ImageRangeGen view_gen;
        std::optional<ImageRangeGen> resolve_gen;
        AttachmentType type;

        Attachment(const SyncValidator &state, const safe_VkRenderingAttachmentInfo &info, const AttachmentType type_,
                   const VkOffset3D &offset, const VkExtent3D &extent);

        SyncStageAccessIndex GetLoadUsage() const;
        SyncStageAccessIndex GetStoreUsage() const;
        SyncOrdering GetOrdering() const;
        Location GetLocation(const Location &loc, uint32_t index = 0) const;
        bool IsWriteable(const LAST_BOUND_STATE &last_bound_state) const;
        bool IsValid() const { return view.get(); }
    };

    // attachments store references to this info, so make sure this doesn't get moved around.
    DynamicRenderingInfo(const DynamicRenderingInfo &) = delete;
    DynamicRenderingInfo(DynamicRenderingInfo &&) = delete;
    DynamicRenderingInfo &operator=(const DynamicRenderingInfo &) = delete;
    DynamicRenderingInfo &operator=(DynamicRenderingInfo &&) = delete;

    DynamicRenderingInfo(const SyncValidator &state, const VkRenderingInfo &rendering_info);
    ClearAttachmentInfo GetClearAttachmentInfo(const VkClearAttachment &clear_attachment, const VkClearRect &rect) const;
    safe_VkRenderingInfo info;
    std::vector<Attachment> attachments;  // All attachments (with internal typing)
};

struct BeginRenderingCmdState {
    BeginRenderingCmdState(std::shared_ptr<const syncval_state::CommandBuffer> &&cb_state_) : cb_state(std::move(cb_state_)) {}
    void AddRenderingInfo(const SyncValidator &state, const VkRenderingInfo &rendering_info);
    const DynamicRenderingInfo &GetRenderingInfo() const;
    std::shared_ptr<const CommandBuffer> cb_state;
    std::unique_ptr<DynamicRenderingInfo> info;
};

}  // namespace syncval_state
VALSTATETRACK_DERIVED_STATE_OBJECT(VkImage, syncval_state::ImageState, IMAGE_STATE)
VALSTATETRACK_DERIVED_STATE_OBJECT(VkImageView, syncval_state::ImageViewState, IMAGE_VIEW_STATE)

struct ClearAttachmentInfo {
    using ImageViewState = syncval_state::ImageViewState;

    const ImageViewState *view = nullptr;
    VkImageAspectFlags aspects_to_clear = {};
    VkImageSubresourceRange subresource_range{};
    VkOffset3D offset = {};
    VkExtent3D extent = {};
    uint32_t attachment_index = VK_ATTACHMENT_UNUSED;
    uint32_t subpass = 0;

    static VkImageSubresourceRange RestrictSubresourceRange(const VkClearRect &clear_rect, const ImageViewState &view);
    static VkImageAspectFlags GetAspectsToClear(VkImageAspectFlags clear_aspect_mask, const ImageViewState &view);
    ClearAttachmentInfo() = default;
    ClearAttachmentInfo(const VkClearAttachment &clear_attachment, const VkClearRect &rect, const ImageViewState &view_,
                        uint32_t attachment_index_ = VK_ATTACHMENT_UNUSED /* renderpass instance only */,
                        uint32_t subpass_ = 0 /* renderpass instance only */)
        : view(&view_),
          aspects_to_clear(GetAspectsToClear(clear_attachment.aspectMask, view_)),
          subresource_range(RestrictSubresourceRange(rect, view_)),
          offset(CastTo3D(rect.rect.offset)),
          extent(CastTo3D(rect.rect.extent)),
          attachment_index(attachment_index_),
          subpass(subpass_) {}

    // ClearAttachmentInfo can be invalid for several reasons based on the VkClearAttachment and the rendering
    // attachment state, including some caught by the constructor.  Consumers *must* check validity before use
    bool IsValid() const { return view && (aspects_to_clear != 0U) && (subresource_range.layerCount != 0U); }
    std::string GetSubpassAttachmentText() const;
};

class AlternateResourceUsage {
  public:
    struct RecordBase;
    struct RecordBase {
        using Record = std::unique_ptr<RecordBase>;
        virtual Record MakeRecord() const = 0;
        virtual std::ostream &Format(std::ostream &out, const SyncValidator &sync_state) const = 0;
        virtual ~RecordBase() {}
    };

    struct FormatterState {
        FormatterState(const SyncValidator &sync_state_, const AlternateResourceUsage &usage_)
            : sync_state(sync_state_), usage(usage_) {}
        const SyncValidator &sync_state;
        const AlternateResourceUsage &usage;
    };

    FormatterState Formatter(const SyncValidator &sync_state) const { return FormatterState(sync_state, *this); };

    std::ostream &Format(std::ostream &out, const SyncValidator &sync_state) const { return record_->Format(out, sync_state); };
    AlternateResourceUsage() = default;
    AlternateResourceUsage(const RecordBase &record) : record_(record.MakeRecord()) {}
    AlternateResourceUsage(const AlternateResourceUsage &other) : record_() {
        if (bool(other.record_)) {
            record_ = other.record_->MakeRecord();
        }
    }
    AlternateResourceUsage &operator=(const AlternateResourceUsage &other) {
        if (bool(other.record_)) {
            record_ = other.record_->MakeRecord();
        } else {
            record_.reset();
        }
        return *this;
    }

    operator bool() const { return bool(record_); }

  private:
    RecordBase::Record record_;
};

inline std::ostream &operator<<(std::ostream &out, const AlternateResourceUsage::FormatterState &formatter) {
    formatter.usage.Format(out, formatter.sync_state);
    return out;
}

template <typename State, typename T>
struct FormatterImpl {
    using That = T;
    friend T;
    const State &state;
    const That &that;

  private:
    // Only intended to be invoke with from That method
    FormatterImpl(const State &state_, const That &that_) : state(state_), that(that_) {}
};

struct NamedHandle {
    const static size_t kInvalidIndex = std::numeric_limits<size_t>::max();
    std::string name;
    VulkanTypedHandle handle;
    size_t index = kInvalidIndex;

    using FormatterState = FormatterImpl<SyncValidator, NamedHandle>;
    // NOTE: CRTP could DRY this
    FormatterState Formatter(const SyncValidator &sync_state) const { return FormatterState(sync_state, *this); }

    NamedHandle() = default;
    NamedHandle(const NamedHandle &other) = default;
    NamedHandle(NamedHandle &&other) = default;
    NamedHandle(const std::string &name_, const VulkanTypedHandle &handle_, size_t index_ = kInvalidIndex)
        : name(name_), handle(handle_), index(index_) {}
    NamedHandle(const char *name_, const VulkanTypedHandle &handle_, size_t index_ = kInvalidIndex)
        : name(name_), handle(handle_), index(index_) {}
    NamedHandle(const VulkanTypedHandle &handle_) : name(), handle(handle_) {}
    NamedHandle &operator=(const NamedHandle &other) = default;
    NamedHandle &operator=(NamedHandle &&other) = default;

    operator bool() const { return (handle.handle != 0U) && (handle.type != VulkanObjectType::kVulkanObjectTypeUnknown); }
    bool IsIndexed() const { return index != kInvalidIndex; }
};

struct ResourceCmdUsageRecord {
    using TagIndex = ResourceUsageTag;
    using Count = uint32_t;
    constexpr static TagIndex kMaxIndex = std::numeric_limits<TagIndex>::max();

    enum class SubcommandType { kNone, kSubpassTransition, kLoadOp, kStoreOp, kResolveOp, kIndex };

    ResourceCmdUsageRecord() = default;
    ResourceCmdUsageRecord(vvl::Func command_, Count seq_num_, SubcommandType sub_type_, Count sub_command_,
                           const CMD_BUFFER_STATE *cb_state_, Count reset_count_)
        : command(command_),
          seq_num(seq_num_),
          sub_command_type(sub_type_),
          sub_command(sub_command_),
          cb_state(cb_state_),
          reset_count(reset_count_) {}

    // NamedHandle must be constructable from args
    template <class... Args>
    void AddHandle(Args &&...args) {
        handles.emplace_back(std::forward<Args>(args)...);
    }

    vvl::Func command = vvl::Func::Empty;
    Count seq_num = 0U;
    SubcommandType sub_command_type = SubcommandType::kNone;
    Count sub_command = 0U;

    // This is somewhat repetitive, but it prevents the need for Exec/Submit time touchup, after which usage records can be
    // from different command buffers and resets.
    // plain pointer as a shared pointer is held by the context storing this record
    const CMD_BUFFER_STATE *cb_state = nullptr;
    Count reset_count;
    small_vector<NamedHandle, 1> handles;
};

struct ResourceUsageRecord : public ResourceCmdUsageRecord {
    struct FormatterState {
        FormatterState(const SyncValidator &sync_state_, const ResourceUsageRecord &record_, const CMD_BUFFER_STATE *cb_state_)
            : sync_state(sync_state_), record(record_), ex_cb_state(cb_state_) {}
        const SyncValidator &sync_state;
        const ResourceUsageRecord &record;
        const CMD_BUFFER_STATE *ex_cb_state;
    };
    FormatterState Formatter(const SyncValidator &sync_state, const CMD_BUFFER_STATE *ex_cb_state) const {
        return FormatterState(sync_state, *this, ex_cb_state);
    }

    AlternateResourceUsage alt_usage;

    ResourceUsageRecord() = default;
    ResourceUsageRecord(vvl::Func command_, Count seq_num_, SubcommandType sub_type_, Count sub_command_,
                        const CMD_BUFFER_STATE *cb_state_, Count reset_count_)
        : ResourceCmdUsageRecord(command_, seq_num_, sub_type_, sub_command_, cb_state_, reset_count_) {}

    ResourceUsageRecord(const AlternateResourceUsage &other) : ResourceCmdUsageRecord(), alt_usage(other) {}
    ResourceUsageRecord(const ResourceUsageRecord &other) : ResourceCmdUsageRecord(other), alt_usage(other.alt_usage) {}
    ResourceUsageRecord &operator=(const ResourceUsageRecord &other) = default;
};

struct AcquiredImage {
    std::shared_ptr<const syncval_state::ImageState> image;
    subresource_adapter::ImageRangeGenerator generator;
    ResourceUsageTag present_tag;
    ResourceUsageTag acquire_tag;
    bool Invalid() const { return BASE_NODE::Invalid(image); }

    AcquiredImage() = default;
    AcquiredImage(const PresentedImage &presented, ResourceUsageTag acq_tag);
};

class SignaledSemaphores {
  public:
    // Is the record of a signaled semaphore, deleted when unsignaled
    struct Signal {
        Signal() = delete;
        Signal(const Signal &other) = default;
        Signal(Signal &&other) = default;
        Signal &operator=(const Signal &other) = default;
        Signal &operator=(Signal &&other) = default;
        Signal(const std::shared_ptr<const vvl::Semaphore> &sem_state_, const std::shared_ptr<QueueBatchContext> &batch_,
               const SyncExecScope &exec_scope_);
        Signal(const std::shared_ptr<const vvl::Semaphore> &sem_state_, const PresentedImage &presented, ResourceUsageTag acq_tag);

        std::shared_ptr<const vvl::Semaphore> sem_state;
        std::shared_ptr<QueueBatchContext> batch;
        // Use the SyncExecScope::valid_accesses for first access scope
        SemaphoreScope first_scope;

        // Swapchain specific support signal info
        // IFF swapchain_image is non-null
        //     batch is the batch of the last present for the acquired image
        //     The address_type, range_generator pair further limit the scope of the resolve operation, and the "barrier" will
        //     also be special case (updating "PRESENTED" write with "ACQUIRE" read, as well as setting the barrier)
        AcquiredImage acquired;

        // TODO add timeline semaphore support.
    };

    using SignalMap = vvl::unordered_map<VkSemaphore, std::shared_ptr<Signal>>;
    using iterator = SignalMap::iterator;
    using const_iterator = SignalMap::const_iterator;
    using mapped_type = SignalMap::mapped_type;
    iterator begin() { return signaled_.begin(); }
    const_iterator begin() const { return signaled_.begin(); }
    iterator end() { return signaled_.end(); }
    const_iterator end() const { return signaled_.end(); }

    bool SignalSemaphore(const std::shared_ptr<const vvl::Semaphore> &sem_state, const std::shared_ptr<QueueBatchContext> &batch,
                         const VkSemaphoreSubmitInfo &signal_info);
    bool Insert(const std::shared_ptr<const vvl::Semaphore> &sem_state, std::shared_ptr<Signal> &&signal);
    bool SignalSemaphore(const std::shared_ptr<const vvl::Semaphore> &sem_state, const PresentedImage &presented,
                         ResourceUsageTag acq_tag);
    std::shared_ptr<const Signal> Unsignal(VkSemaphore);
    void Resolve(SignaledSemaphores &parent, std::shared_ptr<QueueBatchContext> &last_batch);
    SignaledSemaphores() : prev_(nullptr) {}
    SignaledSemaphores(const SignaledSemaphores &prev) : prev_(&prev) {}

  private:
    void Import(VkSemaphore sem, std::shared_ptr<Signal> &&move_from);
    void Reset();
    std::shared_ptr<const Signal> GetPrev(VkSemaphore sem) const;
    vvl::unordered_map<VkSemaphore, std::shared_ptr<Signal>> signaled_;
    const SignaledSemaphores *prev_;  // Allowing this type to act as a writable overlay
};

struct FenceSyncState {
    std::shared_ptr<const vvl::Fence> fence;
    ResourceUsageTag tag;
    QueueId queue_id;
    AcquiredImage acquired;  // Iff queue == invalid and acquired.image valid.
    FenceSyncState();
    FenceSyncState(const FenceSyncState &other) = default;
    FenceSyncState(FenceSyncState &&other) = default;
    FenceSyncState &operator=(const FenceSyncState &other) = default;
    FenceSyncState &operator=(FenceSyncState &&other) = default;

    FenceSyncState(const std::shared_ptr<const vvl::Fence> &fence_, QueueId queue_id_, ResourceUsageTag tag_);
    FenceSyncState(const std::shared_ptr<const vvl::Fence> &fence_, const PresentedImage &image, ResourceUsageTag tag_);
};

class AttachmentViewGen {
  public:
    enum Gen { kViewSubresource = 0, kRenderArea = 1, kDepthOnlyRenderArea = 2, kStencilOnlyRenderArea = 3, kGenSize = 4 };
    AttachmentViewGen(const syncval_state::ImageViewState *image_view, const VkOffset3D &offset, const VkExtent3D &extent);
    AttachmentViewGen(const AttachmentViewGen &other) = default;
    AttachmentViewGen(AttachmentViewGen &&other) = default;
    const syncval_state::ImageViewState *GetViewState() const { return view_; }
    const std::optional<ImageRangeGen> &GetRangeGen(Gen type) const;
    bool IsValid() const { return gen_store_[Gen::kViewSubresource].has_value(); }
    Gen GetDepthStencilRenderAreaGenType(bool depth_op, bool stencil_op) const;

  private:
    const syncval_state::ImageViewState *view_ = nullptr;
    VkImageAspectFlags view_mask_ = 0U;
    std::array<std::optional<ImageRangeGen>, Gen::kGenSize> gen_store_;
};

using AttachmentViewGenVector = std::vector<AttachmentViewGen>;

using SyncMemoryBarrier = SyncBarrier;
struct SyncBufferMemoryBarrier {
    using Buffer = std::shared_ptr<const BUFFER_STATE>;
    Buffer buffer;
    SyncBarrier barrier;
    ResourceAccessRange range;
    bool IsLayoutTransition() const { return false; }
    const ResourceAccessRange &Range() const { return range; };
    const BUFFER_STATE *GetState() const { return buffer.get(); }
    SyncBufferMemoryBarrier(const Buffer &buffer_, const SyncBarrier &barrier_, const ResourceAccessRange &range_)
        : buffer(buffer_), barrier(barrier_), range(range_) {}
    SyncBufferMemoryBarrier() = default;
};

struct SyncImageMemoryBarrier {
    using ImageState = syncval_state::ImageState;
    using Image = std::shared_ptr<const ImageState>;

    Image image;
    uint32_t index;
    SyncBarrier barrier;
    VkImageLayout old_layout;
    VkImageLayout new_layout;
    VkImageSubresourceRange range;

    bool IsLayoutTransition() const { return old_layout != new_layout; }
    const VkImageSubresourceRange &Range() const { return range; };
    const ImageState *GetState() const { return image.get(); }
    SyncImageMemoryBarrier(const Image &image_, uint32_t index_, const SyncBarrier &barrier_, VkImageLayout old_layout_,
                           VkImageLayout new_layout_, const VkImageSubresourceRange &subresource_range_)
        : image(image_),
          index(index_),
          barrier(barrier_),
          old_layout(old_layout_),
          new_layout(new_layout_),
          range(subresource_range_) {}
    SyncImageMemoryBarrier() = default;
};

template <typename SubpassNode>
struct SubpassBarrierTrackback {
    std::vector<SyncBarrier> barriers;
    const SubpassNode *source_subpass = nullptr;
    SubpassBarrierTrackback() = default;
    SubpassBarrierTrackback(const SubpassBarrierTrackback &) = default;
    SubpassBarrierTrackback(const SubpassNode *source_subpass_, VkQueueFlags queue_flags_,
                            const std::vector<const VkSubpassDependency2 *> &subpass_dependencies_)
        : barriers(), source_subpass(source_subpass_) {
        barriers.reserve(subpass_dependencies_.size());
        for (const VkSubpassDependency2 *dependency : subpass_dependencies_) {
            assert(dependency);
            barriers.emplace_back(queue_flags_, *dependency);
        }
    }
    SubpassBarrierTrackback(const SubpassNode *source_subpass_, const SyncBarrier &barrier_)
        : barriers(1, barrier_), source_subpass(source_subpass_) {}
    SubpassBarrierTrackback &operator=(const SubpassBarrierTrackback &) = default;
};

class SyncOpBase {
  public:
    SyncOpBase() : command_(vvl::Func::Empty) {}
    SyncOpBase(vvl::Func command) : command_(command) {}
    virtual ~SyncOpBase() = default;

    const char *CmdName() const { return vvl::String(command_); }

    virtual bool Validate(const CommandBufferAccessContext &cb_context) const = 0;
    virtual ResourceUsageTag Record(CommandBufferAccessContext *cb_context) = 0;
    virtual bool ReplayValidate(ReplayState &replay, ResourceUsageTag recorded_tag) const = 0;
    virtual void ReplayRecord(CommandExecutionContext &exec_context, ResourceUsageTag exec_tag) const = 0;

  protected:
    // Only non-null and valid for SyncOps within a render pass instance  WIP -- think about how to manage for non RPI calls within
    // RPI and 2ndarys...
    uint32_t subpass_ = VK_SUBPASS_EXTERNAL;
    vvl::Func command_;
};

class SyncOpBarriers : public SyncOpBase {
  protected:
    template <typename Barriers, typename FunctorFactory>
    static void ApplyBarriers(const Barriers &barriers, const FunctorFactory &factory, QueueId queue_id, ResourceUsageTag tag,
                              AccessContext *context);
    template <typename Barriers, typename FunctorFactory>
    static void ApplyGlobalBarriers(const Barriers &barriers, const FunctorFactory &factory, QueueId queue_id, ResourceUsageTag tag,
                                    AccessContext *access_context);

    SyncOpBarriers(vvl::Func command, const SyncValidator &sync_state, VkQueueFlags queue_flags, VkPipelineStageFlags srcStageMask,
                   VkPipelineStageFlags dstStageMask, VkDependencyFlags dependencyFlags, uint32_t memoryBarrierCount,
                   const VkMemoryBarrier *pMemoryBarriers, uint32_t bufferMemoryBarrierCount,
                   const VkBufferMemoryBarrier *pBufferMemoryBarriers, uint32_t imageMemoryBarrierCount,
                   const VkImageMemoryBarrier *pImageMemoryBarriers);
    SyncOpBarriers(vvl::Func command, const SyncValidator &sync_state, VkQueueFlags queue_flags, uint32_t event_count,
                   const VkDependencyInfoKHR *pDependencyInfo);

    ~SyncOpBarriers() override = default;

  protected:
    struct BarrierSet {
        using ImageState = syncval_state::ImageState;
        VkDependencyFlags dependency_flags;
        SyncExecScope src_exec_scope;
        SyncExecScope dst_exec_scope;
        std::vector<SyncMemoryBarrier> memory_barriers;
        std::vector<SyncBufferMemoryBarrier> buffer_memory_barriers;
        std::vector<SyncImageMemoryBarrier> image_memory_barriers;
        bool single_exec_scope;
        void MakeMemoryBarriers(const SyncExecScope &src, const SyncExecScope &dst, VkDependencyFlags dependencyFlags,
                                uint32_t memoryBarrierCount, const VkMemoryBarrier *pMemoryBarriers);
        void MakeBufferMemoryBarriers(const SyncValidator &sync_state, const SyncExecScope &src, const SyncExecScope &dst,
                                      VkDependencyFlags dependencyFlags, uint32_t bufferMemoryBarrierCount,
                                      const VkBufferMemoryBarrier *pBufferMemoryBarriers);
        void MakeImageMemoryBarriers(const SyncValidator &sync_state, const SyncExecScope &src, const SyncExecScope &dst,
                                     VkDependencyFlags dependencyFlags, uint32_t imageMemoryBarrierCount,
                                     const VkImageMemoryBarrier *pImageMemoryBarriers);
        void MakeMemoryBarriers(VkQueueFlags queue_flags, VkDependencyFlags dependency_flags, uint32_t barrier_count,
                                const VkMemoryBarrier2 *barriers);
        void MakeBufferMemoryBarriers(const SyncValidator &sync_state, VkQueueFlags queue_flags, VkDependencyFlags dependency_flags,
                                      uint32_t barrier_count, const VkBufferMemoryBarrier2 *barriers);
        void MakeImageMemoryBarriers(const SyncValidator &sync_state, VkQueueFlags queue_flags, VkDependencyFlags dependency_flags,
                                     uint32_t barrier_count, const VkImageMemoryBarrier2 *barriers);
    };
    std::vector<BarrierSet> barriers_;
};

class SyncOpPipelineBarrier : public SyncOpBarriers {
  public:
    SyncOpPipelineBarrier(vvl::Func command, const SyncValidator &sync_state, VkQueueFlags queue_flags,
                          VkPipelineStageFlags srcStageMask, VkPipelineStageFlags dstStageMask, VkDependencyFlags dependencyFlags,
                          uint32_t memoryBarrierCount, const VkMemoryBarrier *pMemoryBarriers, uint32_t bufferMemoryBarrierCount,
                          const VkBufferMemoryBarrier *pBufferMemoryBarriers, uint32_t imageMemoryBarrierCount,
                          const VkImageMemoryBarrier *pImageMemoryBarriers);
    SyncOpPipelineBarrier(vvl::Func command, const SyncValidator &sync_state, VkQueueFlags queue_flags,
                          const VkDependencyInfoKHR &pDependencyInfo);
    ~SyncOpPipelineBarrier() override = default;

    bool Validate(const CommandBufferAccessContext &cb_context) const override;
    ResourceUsageTag Record(CommandBufferAccessContext *cb_context) override;
    bool ReplayValidate(ReplayState &replay, ResourceUsageTag recorded_tag) const override;
    void ReplayRecord(CommandExecutionContext &exec_context, ResourceUsageTag exec_tag) const override;
};

class SyncOpWaitEvents : public SyncOpBarriers {
  public:
    SyncOpWaitEvents(vvl::Func command, const SyncValidator &sync_state, VkQueueFlags queue_flags, uint32_t eventCount,
                     const VkEvent *pEvents, VkPipelineStageFlags srcStageMask, VkPipelineStageFlags dstStageMask,
                     uint32_t memoryBarrierCount, const VkMemoryBarrier *pMemoryBarriers, uint32_t bufferMemoryBarrierCount,
                     const VkBufferMemoryBarrier *pBufferMemoryBarriers, uint32_t imageMemoryBarrierCount,
                     const VkImageMemoryBarrier *pImageMemoryBarriers);

    SyncOpWaitEvents(vvl::Func command, const SyncValidator &sync_state, VkQueueFlags queue_flags, uint32_t eventCount,
                     const VkEvent *pEvents, const VkDependencyInfoKHR *pDependencyInfo);
    ~SyncOpWaitEvents() override = default;

    bool Validate(const CommandBufferAccessContext &cb_context) const override;
    ResourceUsageTag Record(CommandBufferAccessContext *cb_context) override;
    bool ReplayValidate(ReplayState &replay, ResourceUsageTag recorded_tag) const override;
    void ReplayRecord(CommandExecutionContext &exec_context, ResourceUsageTag exec_tag) const override;

  protected:
    static const char *const kIgnored;
    bool DoValidate(const CommandExecutionContext &ex_context, const ResourceUsageTag base_tag) const;
    void DoRecord(CommandExecutionContext &ex_context, const ResourceUsageTag base_tag) const;
    // TODO PHASE2 This is the wrong thing to use for "replay".. as the event state will have moved on since the record
    // TODO PHASE2 May need to capture by value w.r.t. "first use" or build up in calling/enqueue context through replay.
    std::vector<std::shared_ptr<const EVENT_STATE>> events_;
    void MakeEventsList(const SyncValidator &sync_state, uint32_t event_count, const VkEvent *events);
};

class SyncOpResetEvent : public SyncOpBase {
  public:
    SyncOpResetEvent(vvl::Func command, const SyncValidator &sync_state, VkQueueFlags queue_flags, VkEvent event,
                     VkPipelineStageFlags2KHR stageMask);
    ~SyncOpResetEvent() override = default;

    bool Validate(const CommandBufferAccessContext &cb_context) const override;
    ResourceUsageTag Record(CommandBufferAccessContext *cb_context) override;
    bool ReplayValidate(ReplayState &replay, ResourceUsageTag recorded_tag) const override;
    void ReplayRecord(CommandExecutionContext &exec_context, ResourceUsageTag exec_tag) const override;

  private:
    bool DoValidate(const CommandExecutionContext &ex_context, const ResourceUsageTag base_tag) const;
    std::shared_ptr<const EVENT_STATE> event_;
    SyncExecScope exec_scope_;
};

class SyncOpSetEvent : public SyncOpBase {
  public:
    SyncOpSetEvent(vvl::Func command, const SyncValidator &sync_state, VkQueueFlags queue_flags, VkEvent event,
                   VkPipelineStageFlags2KHR stageMask, const AccessContext *access_context);
    SyncOpSetEvent(vvl::Func command, const SyncValidator &sync_state, VkQueueFlags queue_flags, VkEvent event,
                   const VkDependencyInfoKHR &dep_info, const AccessContext *access_context);
    ~SyncOpSetEvent() override = default;

    bool Validate(const CommandBufferAccessContext &cb_context) const override;
    ResourceUsageTag Record(CommandBufferAccessContext *cb_context) override;
    bool ReplayValidate(ReplayState &replay, ResourceUsageTag recorded_tag) const override;
    void ReplayRecord(CommandExecutionContext &exec_context, ResourceUsageTag exec_tag) const override;

  private:
    bool DoValidate(const CommandExecutionContext &ex_context, const ResourceUsageTag base_tag) const;
    void DoRecord(QueueId queue_id, ResourceUsageTag recorded_tag, const std::shared_ptr<const AccessContext> &access_context,
                  SyncEventsContext *events_context) const;
    std::shared_ptr<const EVENT_STATE> event_;
    // The Access context of the command buffer at record set event time.
    std::shared_ptr<const AccessContext> recorded_context_;
    SyncExecScope src_exec_scope_;
    // Note that the dep info is *not* dehandled, but retained for comparison with a future WaitEvents2
    std::shared_ptr<safe_VkDependencyInfo> dep_info_;
};

class SyncOpBeginRenderPass : public SyncOpBase {
  public:
    SyncOpBeginRenderPass(vvl::Func command, const SyncValidator &sync_state, const VkRenderPassBeginInfo *pRenderPassBegin,
                          const VkSubpassBeginInfo *pSubpassBeginInfo);
    ~SyncOpBeginRenderPass() override = default;

    bool Validate(const CommandBufferAccessContext &cb_context) const override;
    ResourceUsageTag Record(CommandBufferAccessContext *cb_context) override;
    bool ReplayValidate(ReplayState &replay, ResourceUsageTag recorded_tag) const override;
    void ReplayRecord(CommandExecutionContext &exec_context, ResourceUsageTag exec_tag) const override;
    const RenderPassAccessContext *GetRenderPassAccessContext() const { return rp_context_; }

  protected:
    safe_VkRenderPassBeginInfo renderpass_begin_info_;
    safe_VkSubpassBeginInfo subpass_begin_info_;
    std::vector<std::shared_ptr<const IMAGE_VIEW_STATE>> shared_attachments_;
    std::vector<const syncval_state::ImageViewState *> attachments_;
    std::shared_ptr<const RENDER_PASS_STATE> rp_state_;
    const RenderPassAccessContext *rp_context_;
};

class SyncOpNextSubpass : public SyncOpBase {
  public:
    SyncOpNextSubpass(vvl::Func command, const SyncValidator &sync_state, const VkSubpassBeginInfo *pSubpassBeginInfo,
                      const VkSubpassEndInfo *pSubpassEndInfo);
    ~SyncOpNextSubpass() override = default;

    bool Validate(const CommandBufferAccessContext &cb_context) const override;
    ResourceUsageTag Record(CommandBufferAccessContext *cb_context) override;
    bool ReplayValidate(ReplayState &replay, ResourceUsageTag recorded_tag) const override;
    void ReplayRecord(CommandExecutionContext &exec_context, ResourceUsageTag exec_tag) const override;

  protected:
    safe_VkSubpassBeginInfo subpass_begin_info_;
    safe_VkSubpassEndInfo subpass_end_info_;
};

class SyncOpEndRenderPass : public SyncOpBase {
  public:
    SyncOpEndRenderPass(vvl::Func command, const SyncValidator &sync_state, const VkSubpassEndInfo *pSubpassEndInfo);
    ~SyncOpEndRenderPass() override = default;

    bool Validate(const CommandBufferAccessContext &cb_context) const override;
    ResourceUsageTag Record(CommandBufferAccessContext *cb_context) override;
    bool ReplayValidate(ReplayState &replay, ResourceUsageTag recorded_tag) const override;
    void ReplayRecord(CommandExecutionContext &exec_context, ResourceUsageTag exec_tag) const override;

  protected:
    safe_VkSubpassEndInfo subpass_end_info_;
};

class AccessContext {
  public:
    using ImageState = syncval_state::ImageState;
    using ImageViewState = syncval_state::ImageViewState;
    using ScopeMap = ResourceAccessRangeMap;
    enum DetectOptions : uint32_t {
        kDetectPrevious = 1U << 0,
        kDetectAsync = 1U << 1,
        kDetectAll = (kDetectPrevious | kDetectAsync)
    };

    using TrackBack = SubpassBarrierTrackback<AccessContext>;


    HazardResult DetectHazard(const BUFFER_STATE &buffer, SyncStageAccessIndex usage_index, const ResourceAccessRange &range) const;
    HazardResult DetectHazard(const ImageState &image, SyncStageAccessIndex current_usage,
                              const VkImageSubresourceRange &subresource_range, bool is_depth_sliced) const;
    HazardResult DetectHazard(const ImageViewState &image_view, SyncStageAccessIndex current_usage) const;
    HazardResult DetectHazard(const ImageRangeGen &ref_range_gen, SyncStageAccessIndex current_usage,
                              SyncOrdering ordering_rule = SyncOrdering::kOrderingNone) const;
    HazardResult DetectHazard(const ImageViewState &image_view, const VkOffset3D &offset, const VkExtent3D &extent,
                              SyncStageAccessIndex current_usage, SyncOrdering ordering_rule) const;
    HazardResult DetectHazard(const AttachmentViewGen &view_gen, AttachmentViewGen::Gen gen_type,
                              SyncStageAccessIndex current_usage, SyncOrdering ordering_rule) const;
    HazardResult DetectHazard(const ImageState &image, const VkImageSubresourceRange &subresource_range, const VkOffset3D &offset,
                              const VkExtent3D &extent, bool is_depth_sliced, SyncStageAccessIndex current_usage,
                              SyncOrdering ordering_rule = SyncOrdering::kOrderingNone) const;

    HazardResult DetectImageBarrierHazard(const ImageState &image, const VkImageSubresourceRange &subresource_range,
                                          VkPipelineStageFlags2KHR src_exec_scope, const SyncStageAccessFlags &src_access_scope,
                                          QueueId queue_id, const ScopeMap &scope_map, ResourceUsageTag scope_tag,
                                          DetectOptions options) const;
    HazardResult DetectImageBarrierHazard(const AttachmentViewGen &attachment_view, const SyncBarrier &barrier,
                                          DetectOptions options) const;
    HazardResult DetectImageBarrierHazard(const ImageState &image, VkPipelineStageFlags2KHR src_exec_scope,
                                          const SyncStageAccessFlags &src_access_scope,
                                          const VkImageSubresourceRange &subresource_range, DetectOptions options) const;
    HazardResult DetectImageBarrierHazard(const SyncImageMemoryBarrier &image_barrier) const;
    HazardResult DetectSubpassTransitionHazard(const TrackBack &track_back, const AttachmentViewGen &attach_view) const;

    void RecordLayoutTransitions(const RENDER_PASS_STATE &rp_state, uint32_t subpass,
                                 const AttachmentViewGenVector &attachment_views, ResourceUsageTag tag);

    HazardResult DetectFirstUseHazard(QueueId queue_id, const ResourceUsageRange &tag_range,
                                      const AccessContext &access_context) const;

    const TrackBack &GetDstExternalTrackBack() const { return dst_external_; }
    void Reset() {
        prev_.clear();
        prev_by_subpass_.clear();
        async_.clear();
        src_external_ = nullptr;
        dst_external_ = TrackBack();
        start_tag_ = ResourceUsageTag();
        access_state_map_.clear();
    }

    void ResolvePreviousAccesses();
    void ResolveFromContext(const AccessContext &from);

    template <typename ResolveOp>
    void ResolveFromContext(ResolveOp &&resolve_op, const AccessContext &from_context,
                            const ResourceAccessState *infill_state = nullptr, bool recur_to_infill = false);
    template <typename ResolveOp, typename RangeGenerator>
    void ResolveFromContext(ResolveOp &&resolve_op, const AccessContext &from_context, RangeGenerator range_gen,
                            const ResourceAccessState *infill_state = nullptr, bool recur_to_infill = false);

    void UpdateAccessState(const BUFFER_STATE &buffer, SyncStageAccessIndex current_usage, SyncOrdering ordering_rule,
                           const ResourceAccessRange &range, ResourceUsageTag tag);
    void UpdateAccessState(const ImageState &image, SyncStageAccessIndex current_usage, SyncOrdering ordering_rule,
                           const VkImageSubresourceRange &subresource_range, const ResourceUsageTag &tag);
    void UpdateAccessState(const ImageState &image, SyncStageAccessIndex current_usage, SyncOrdering ordering_rule,
                           const VkImageSubresourceRange &subresource_range, const VkOffset3D &offset, const VkExtent3D &extent,
                           ResourceUsageTag tag);
    void UpdateAccessState(const AttachmentViewGen &view_gen, AttachmentViewGen::Gen gen_type, SyncStageAccessIndex current_usage,
                           SyncOrdering ordering_rule, ResourceUsageTag tag);
    void UpdateAccessState(const ImageViewState &image_view, SyncStageAccessIndex current_usage, SyncOrdering ordering_rule,
                           const VkOffset3D &offset, const VkExtent3D &extent, ResourceUsageTag tag);
    void UpdateAccessState(const ImageViewState &image_view, SyncStageAccessIndex current_usage, SyncOrdering ordering_rule,
                           ResourceUsageTag tag);
    void UpdateAccessState(const ImageRangeGen &range_gen, SyncStageAccessIndex current_usage, SyncOrdering ordering_rule,
                           ResourceUsageTag tag);
    void UpdateAccessState(ImageRangeGen &range_gen, SyncStageAccessIndex current_usage, SyncOrdering ordering_rule,
                           ResourceUsageTag tag);
    void UpdateAttachmentResolveAccess(const RENDER_PASS_STATE &rp_state, const AttachmentViewGenVector &attachment_views,
                                       uint32_t subpass, ResourceUsageTag tag);
    void UpdateAttachmentStoreAccess(const RENDER_PASS_STATE &rp_state, const AttachmentViewGenVector &attachment_views,
                                     uint32_t subpass, ResourceUsageTag tag);

    void ResolveChildContexts(const std::vector<AccessContext> &contexts);

    void ImportAsyncContexts(const AccessContext &from);
    void ClearAsyncContexts() { async_.clear(); }
    template <typename Action>
    void ApplyUpdateAction(const AttachmentViewGen &view_gen, AttachmentViewGen::Gen gen_type, const Action &action);
    template <typename Action>
    void ApplyToContext(const Action &barrier_action);

    AccessContext(uint32_t subpass, VkQueueFlags queue_flags, const std::vector<SubpassDependencyGraphNode> &dependencies,
                  const std::vector<AccessContext> &contexts, const AccessContext *external_context);

    AccessContext() { Reset(); }
    AccessContext(const AccessContext &copy_from) = default;
    void Trim();
    void TrimAndClearFirstAccess();
    void AddReferencedTags(ResourceUsageTagSet &referenced) const;

    ResourceAccessRangeMap &GetAccessStateMap() { return access_state_map_; }
    const ResourceAccessRangeMap &GetAccessStateMap() const { return access_state_map_; }
    const TrackBack *GetTrackBackFromSubpass(uint32_t subpass) const {
        if (subpass == VK_SUBPASS_EXTERNAL) {
            return src_external_;
        } else {
            assert(subpass < prev_by_subpass_.size());
            return prev_by_subpass_[subpass];
        }
    }

    bool ValidateLayoutTransitions(const SyncValidationInfo &val_info, const RENDER_PASS_STATE &rp_state,
                                   const VkRect2D &render_area, uint32_t subpass, const AttachmentViewGenVector &attachment_views,
                                   vvl::Func command) const;
    bool ValidateLoadOperation(const SyncValidationInfo &val_info, const RENDER_PASS_STATE &rp_state, const VkRect2D &render_area,
                               uint32_t subpass, const AttachmentViewGenVector &attachment_views, vvl::Func command) const;
    bool ValidateStoreOperation(const SyncValidationInfo &val_info, const RENDER_PASS_STATE &rp_state, const VkRect2D &render_area,
                                uint32_t subpass, const AttachmentViewGenVector &attachment_views, vvl::Func command) const;
    bool ValidateResolveOperations(const SyncValidationInfo &val_info, const RENDER_PASS_STATE &rp_state,
                                   const VkRect2D &render_area, const AttachmentViewGenVector &attachment_views, vvl::Func command,
                                   uint32_t subpass) const;

    void SetStartTag(ResourceUsageTag tag) { start_tag_ = tag; }
    ResourceUsageTag StartTag() const { return start_tag_; }

    template <typename Action>
    void ForAll(Action &&action);
    template <typename Action>
    void ConstForAll(Action &&action) const;
    template <typename Predicate>
    void EraseIf(Predicate &&pred);

    // For use during queue submit building up the QueueBatchContext AccessContext for validation, otherwise clear.
    void AddAsyncContext(const AccessContext *context, ResourceUsageTag tag);

    class AsyncReference {
      public:
        AsyncReference(const AccessContext &async_context, ResourceUsageTag async_tag)
            : context_(&async_context), tag_(async_tag) {}
        const AccessContext &Context() const { return *context_; }
        // For RenderPass time validation this is "start tag", for QueueSubmit, this is the earliest
        // unsynchronized tag for the Queue being tested against (max synchrononous + 1, perhaps)
        ResourceUsageTag StartTag() const;

      protected:
        const AccessContext *context_;
        ResourceUsageTag tag_;  // Start of open ended asynchronous range
    };

  private:
    struct UpdateMemoryAccessStateFunctor {
        using Iterator = ResourceAccessRangeMap::iterator;
        Iterator Infill(ResourceAccessRangeMap *accesses, const Iterator &pos, const ResourceAccessRange &range) const;
        void operator()(const Iterator &pos) const;
        UpdateMemoryAccessStateFunctor(const AccessContext &context_, SyncStageAccessIndex usage_, SyncOrdering ordering_rule_,
                                       ResourceUsageTag tag_)
            : context(context_), usage_info(SyncStageAccess::UsageInfo(usage_)), ordering_rule(ordering_rule_), tag(tag_) {}
        const AccessContext &context;
        const SyncStageAccessInfoType &usage_info;
        const SyncOrdering ordering_rule;
        const ResourceUsageTag tag;
    };

    // Follow the context previous to access the access state, supporting "lazy" import into the context. Not intended for
    // subpass layout transition, as the pending state handling is more complex
    // TODO: See if returning the lower_bound would be useful from a performance POV -- look at the lower_bound overhead
    // Would need to add a "hint" overload to parallel_iterator::invalidate_[AB] call, if so.
    void ResolvePreviousAccess(const ResourceAccessRange &range, ResourceAccessRangeMap *descent_map,
                               const ResourceAccessState *infill_state,
                               const ResourceAccessStateFunction *previous_barrier = nullptr) const;
    template <typename BarrierAction>
    void ResolvePreviousAccessStack(const ResourceAccessRange &range, ResourceAccessRangeMap *descent_map,
                                    const ResourceAccessState *infill_state, const BarrierAction &previous_barrie) const;
    template <typename BarrierAction>
    void ResolveAccessRange(const AttachmentViewGen &view_gen, AttachmentViewGen::Gen gen_type, BarrierAction &barrier_action,
                            ResourceAccessRangeMap *descent_map, const ResourceAccessState *infill_state) const;
    template <typename BarrierAction>
    void ResolveAccessRange(const ResourceAccessRange &range, BarrierAction &barrier_action, ResourceAccessRangeMap *resolve_map,
                            const ResourceAccessState *infill_state, bool recur_to_infill = true) const;

    template <typename Detector>
    HazardResult DetectHazardRange(Detector &detector, const ResourceAccessRange &range, DetectOptions options) const;
    template <typename Detector, typename RangeGen>
    HazardResult DetectHazardGeneratedRanges(Detector &detector, RangeGen &range_gen, DetectOptions options) const;
    template <typename Detector, typename RangeGen>
    HazardResult DetectHazardGeneratedRanges(Detector &detector, const RangeGen &range_gen, DetectOptions options) const {
        RangeGen mutable_gen(range_gen);
        return DetectHazardGeneratedRanges<Detector, RangeGen>(detector, mutable_gen, options);
    }
    template <typename Detector>
    HazardResult DetectHazard(Detector &detector, const AttachmentViewGen &view_gen, AttachmentViewGen::Gen gen_type,
                              DetectOptions options) const;
    template <typename Detector>
    HazardResult DetectHazard(Detector &detector, const ImageState &image, const VkImageSubresourceRange &subresource_range,
                              const VkOffset3D &offset, const VkExtent3D &extent, bool is_depth_sliced,
                              DetectOptions options) const;
    template <typename Detector>
    HazardResult DetectHazard(Detector &detector, const ImageState &image, const VkImageSubresourceRange &subresource_range,
                              bool is_depth_sliced, DetectOptions options) const;

    template <typename Detector>
    HazardResult DetectHazardOneRange(Detector &detector, bool detect_prev, ResourceAccessRangeMap::const_iterator &pos,
                                      const ResourceAccessRangeMap::const_iterator &the_end,
                                      const ResourceAccessRange &range) const;

    template <typename Detector, typename RangeGen>
    HazardResult DetectAsyncHazard(const Detector &detector, const RangeGen &const_range_gen, ResourceUsageTag async_tag) const;

    template <typename NormalizeOp>
    void Trim(NormalizeOp &&normalize);

    template <typename Detector>
    HazardResult DetectPreviousHazard(Detector &detector, const ResourceAccessRange &range) const;

    ResourceAccessRangeMap access_state_map_;
    std::vector<TrackBack> prev_;
    std::vector<TrackBack *> prev_by_subpass_;
    // These contexts *must* have the same lifespan as this context, or be cleared, before the referenced contexts can expire
    std::vector<AsyncReference> async_;
    TrackBack *src_external_;
    TrackBack dst_external_;
    ResourceUsageTag start_tag_;
};

struct SyncEventState {
    enum IgnoreReason { NotIgnored = 0, ResetWaitRace, Reset2WaitRace, SetRace, MissingStageBits, SetVsWait2, MissingSetEvent };
    using EventPointer = std::shared_ptr<const EVENT_STATE>;
    EventPointer event;
    vvl::Func last_command;             // Only Event commands are valid here.
    ResourceUsageTag last_command_tag;  // Needed to filter replay validation
    vvl::Func unsynchronized_set;
    VkPipelineStageFlags2KHR barriers;
    SyncExecScope scope;
    ResourceUsageTag first_scope_tag;
    bool destroyed;
    std::shared_ptr<const AccessContext> first_scope;

    SyncEventState()
        : event(),
          last_command(vvl::Func::Empty),
          last_command_tag(0),
          unsynchronized_set(vvl::Func::Empty),
          barriers(0U),
          scope(),
          first_scope_tag(),
          destroyed(true) {}

    SyncEventState(const SyncEventState &) = default;
    SyncEventState(SyncEventState &&) = default;

    SyncEventState(const SyncEventState::EventPointer &event_state) : SyncEventState() {
        event = event_state;
        destroyed = (event.get() == nullptr) || event_state->Destroyed();
    }

    void ResetFirstScope();
    const AccessContext::ScopeMap &FirstScope() const { return first_scope->GetAccessStateMap(); }
    IgnoreReason IsIgnoredByWait(vvl::Func command, VkPipelineStageFlags2KHR srcStageMask) const;
    bool HasBarrier(VkPipelineStageFlags2KHR stageMask, VkPipelineStageFlags2KHR exec_scope) const;
    void AddReferencedTags(ResourceUsageTagSet &referenced) const;
};

class SyncEventsContext {
  public:
    using Map = vvl::unordered_map<const EVENT_STATE *, std::shared_ptr<SyncEventState>>;
    using iterator = Map::iterator;
    using const_iterator = Map::const_iterator;

    SyncEventState *GetFromShared(const SyncEventState::EventPointer &event_state) {
        const auto find_it = map_.find(event_state.get());
        if (find_it == map_.end()) {
            if (!event_state.get()) return nullptr;

            const auto *event_plain_ptr = event_state.get();
            auto sync_state = std::make_shared<SyncEventState>(event_state);
            auto insert_pair = map_.emplace(event_plain_ptr, sync_state);
            return insert_pair.first->second.get();
        }
        return find_it->second.get();
    }

    const SyncEventState *Get(const EVENT_STATE *event_state) const {
        const auto find_it = map_.find(event_state);
        if (find_it == map_.end()) {
            return nullptr;
        }
        return find_it->second.get();
    }
    const SyncEventState *Get(const SyncEventState::EventPointer &event_state) const { return Get(event_state.get()); }

    void ApplyBarrier(const SyncExecScope &src, const SyncExecScope &dst, ResourceUsageTag tag);
    void ApplyTaggedWait(VkQueueFlags queue_flags, ResourceUsageTag tag);

    void Destroy(const EVENT_STATE *event_state) {
        auto sync_it = map_.find(event_state);
        if (sync_it != map_.end()) {
            sync_it->second->destroyed = true;
            map_.erase(sync_it);
        }
    }
    void Clear() { map_.clear(); }

    SyncEventsContext &DeepCopy(const SyncEventsContext &from);
    void AddReferencedTags(ResourceUsageTagSet &referenced) const;

  private:
    Map map_;
};

class RenderPassAccessContext {
  public:
    static AttachmentViewGenVector CreateAttachmentViewGen(
        const VkRect2D &render_area, const std::vector<const syncval_state::ImageViewState *> &attachment_views);
    RenderPassAccessContext() : rp_state_(nullptr), render_area_(VkRect2D()), current_subpass_(0) {}
    RenderPassAccessContext(const RENDER_PASS_STATE &rp_state, const VkRect2D &render_area, VkQueueFlags queue_flags,
                            const std::vector<const syncval_state::ImageViewState *> &attachment_views,
                            const AccessContext *external_context);

    bool ValidateDrawSubpassAttachment(const CommandExecutionContext &ex_context, const CMD_BUFFER_STATE &cmd_buffer,
                                       vvl::Func command) const;
    void RecordDrawSubpassAttachment(const CMD_BUFFER_STATE &cmd_buffer, ResourceUsageTag tag);

    uint32_t GetAttachmentIndex(const VkClearAttachment &clear_attachment) const;
    ClearAttachmentInfo GetClearAttachmentInfo(const VkClearAttachment &clear_attachment, const VkClearRect &rect) const;

    bool ValidateNextSubpass(const CommandExecutionContext &ex_context, vvl::Func command) const;
    bool ValidateEndRenderPass(const CommandExecutionContext &ex_context, vvl::Func command) const;
    bool ValidateFinalSubpassLayoutTransitions(const CommandExecutionContext &ex_context, vvl::Func command) const;

    void RecordLayoutTransitions(ResourceUsageTag tag);
    void RecordLoadOperations(ResourceUsageTag tag);
    void RecordBeginRenderPass(ResourceUsageTag tag, ResourceUsageTag load_tag);
    void RecordNextSubpass(ResourceUsageTag store_tag, ResourceUsageTag barrier_tag, ResourceUsageTag load_tag);
    void RecordEndRenderPass(AccessContext *external_context, ResourceUsageTag store_tag, ResourceUsageTag barrier_tag);

    AccessContext &CurrentContext() { return subpass_contexts_[current_subpass_]; }
    const AccessContext &CurrentContext() const { return subpass_contexts_[current_subpass_]; }
    const std::vector<AccessContext> &GetContexts() const { return subpass_contexts_; }
    uint32_t GetCurrentSubpass() const { return current_subpass_; }
    const RENDER_PASS_STATE *GetRenderPassState() const { return rp_state_; }
    AccessContext *CreateStoreResolveProxy() const;

  private:
    const RENDER_PASS_STATE *rp_state_;
    const VkRect2D render_area_;
    uint32_t current_subpass_;
    std::vector<AccessContext> subpass_contexts_;
    AttachmentViewGenVector attachment_views_;
};

// Command execution context is the base class for command buffer and queue contexts
// Preventing unintented leakage of subclass specific state, storing enough information
// for message logging.
// TODO: determine where to draw the design split for tag tracking (is there anything command to Queues and CB's)

class CommandExecutionContext : public SyncValidationInfo {
  public:
    using AccessLog = std::vector<ResourceUsageRecord>;
    using CommandBufferSet = vvl::unordered_set<std::shared_ptr<const CMD_BUFFER_STATE>>;
    CommandExecutionContext() : SyncValidationInfo(nullptr) {}
    CommandExecutionContext(const SyncValidator *sync_validator) : SyncValidationInfo(sync_validator) {}
    virtual ~CommandExecutionContext() = default;
    virtual AccessContext *GetCurrentAccessContext() = 0;
    virtual SyncEventsContext *GetCurrentEventsContext() = 0;
    virtual const AccessContext *GetCurrentAccessContext() const = 0;
    virtual const SyncEventsContext *GetCurrentEventsContext() const = 0;
    virtual QueueId GetQueueId() const = 0;


    ResourceUsageRange ImportRecordedAccessLog(const CommandBufferAccessContext &recorded_context);

    virtual ResourceUsageTag GetTagLimit() const = 0;
    virtual VulkanTypedHandle Handle() const = 0;
    virtual void InsertRecordedAccessLogEntries(const CommandBufferAccessContext &cb_context) = 0;

    virtual void BeginRenderPassReplaySetup(ReplayState &replay, const SyncOpBeginRenderPass &begin_op) {
        // Must override if use by derived type is valid
        assert(false);
    }

    virtual void NextSubpassReplaySetup(ReplayState &replay) {
        // Must override if use by derived type is valid
        assert(false);
    }

    virtual void EndRenderPassReplayCleanup(ReplayState &replay) {
        // Must override if use by derived type is valid
        assert(false);
    }

    bool ValidForSyncOps() const;
};

class CommandBufferAccessContext : public CommandExecutionContext {
  public:
    using SyncOpPointer = std::shared_ptr<SyncOpBase>;
    constexpr static SyncStageAccessIndex kResolveRead = SYNC_COLOR_ATTACHMENT_OUTPUT_COLOR_ATTACHMENT_READ;
    constexpr static SyncStageAccessIndex kResolveWrite = SYNC_COLOR_ATTACHMENT_OUTPUT_COLOR_ATTACHMENT_WRITE;
    constexpr static SyncOrdering kResolveOrder = SyncOrdering::kColorAttachment;
    constexpr static SyncOrdering kStoreOrder = SyncOrdering::kRaster;

    struct SyncOpEntry {
        ResourceUsageTag tag;
        SyncOpPointer sync_op;
        SyncOpEntry(ResourceUsageTag tag_, SyncOpPointer &&sync_op_) : tag(tag_), sync_op(std::move(sync_op_)) {}
        SyncOpEntry() = default;
        SyncOpEntry(const SyncOpEntry &other) = default;
    };

    CommandBufferAccessContext(const SyncValidator *sync_validator = nullptr)
        : CommandExecutionContext(sync_validator),
          cb_state_(),
          access_log_(std::make_shared<AccessLog>()),
          cbs_referenced_(std::make_shared<CommandBufferSet>()),
          command_number_(0),
          subcommand_number_(0),
          reset_count_(0),
          cb_access_context_(),
          current_context_(&cb_access_context_),
          events_context_(),
          render_pass_contexts_(),
          current_renderpass_context_(),
          sync_ops_() {}
    CommandBufferAccessContext(SyncValidator &sync_validator, CMD_BUFFER_STATE *cb_state)
        : CommandBufferAccessContext(&sync_validator) {
        cb_state_ = cb_state;
    }

    struct AsProxyContext {};
    CommandBufferAccessContext(const CommandBufferAccessContext &real_context, AsProxyContext dummy);

    // NOTE: because this class is encapsulated in syncval_state::CommandBuffer, it isn't safe
    // to use shared_from_this from the constructor.
    void SetSelfReference() { cbs_referenced_->insert(cb_state_->shared_from_this()); }

    ~CommandBufferAccessContext() override = default;
    const CommandExecutionContext &GetExecutionContext() const { return *this; }

    void Destroy() {
        // the cb self reference must be cleared or the command buffer reference count will never go to 0
        cbs_referenced_.reset();
        cb_state_ = nullptr;
    }

    void Reset() {
        access_log_ = std::make_shared<AccessLog>();
        cbs_referenced_ = std::make_shared<CommandBufferSet>();
        if (cb_state_) {
            cbs_referenced_->insert(cb_state_->shared_from_this());
        }
        sync_ops_.clear();
        command_number_ = 0;
        subcommand_number_ = 0;
        reset_count_++;
        command_handles_.clear();
        cb_access_context_.Reset();
        render_pass_contexts_.clear();
        current_context_ = &cb_access_context_;
        current_renderpass_context_ = nullptr;
        events_context_.Clear();
        dynamic_rendering_info_.reset();
    }

    std::string FormatUsage(ResourceUsageTag tag) const override;
    std::string FormatUsage(const ResourceFirstAccess &access) const;  //  Only command buffers have "first usage"
    AccessContext *GetCurrentAccessContext() override { return current_context_; }
    SyncEventsContext *GetCurrentEventsContext() override { return &events_context_; }
    const AccessContext *GetCurrentAccessContext() const override { return current_context_; }
    const SyncEventsContext *GetCurrentEventsContext() const override { return &events_context_; }
    QueueId GetQueueId() const override;

    RenderPassAccessContext *GetCurrentRenderPassContext() { return current_renderpass_context_; }
    const RenderPassAccessContext *GetCurrentRenderPassContext() const { return current_renderpass_context_; }
    ResourceUsageTag RecordBeginRenderPass(vvl::Func command, const RENDER_PASS_STATE &rp_state, const VkRect2D &render_area,
                                           const std::vector<const syncval_state::ImageViewState *> &attachment_views);

    bool ValidateBeginRendering(const ErrorObject &error_obj, syncval_state::BeginRenderingCmdState &cmd_state) const;
    void RecordBeginRendering(syncval_state::BeginRenderingCmdState &cmd_state, const RecordObject &record_obj);
    bool ValidateEndRendering(const ErrorObject &error_obj) const;
    void RecordEndRendering(const RecordObject &record_obj);
    bool ValidateDispatchDrawDescriptorSet(VkPipelineBindPoint pipelineBindPoint, const Location &loc) const;
    void RecordDispatchDrawDescriptorSet(VkPipelineBindPoint pipelineBindPoint, ResourceUsageTag tag);
    bool ValidateDrawVertex(const std::optional<uint32_t> &vertexCount, uint32_t firstVertex, const Location &loc) const;
    void RecordDrawVertex(const std::optional<uint32_t> &vertexCount, uint32_t firstVertex, ResourceUsageTag tag);
    bool ValidateDrawVertexIndex(const std::optional<uint32_t> &indexCount, uint32_t firstIndex, const Location &loc) const;
    void RecordDrawVertexIndex(const std::optional<uint32_t> &indexCount, uint32_t firstIndex, ResourceUsageTag tag);
    bool ValidateDrawAttachment(const Location &loc) const;
    bool ValidateDrawDynamicRenderingAttachment(const Location &loc) const;
    void RecordDrawAttachment(ResourceUsageTag tag);
    void RecordDrawDynamicRenderingAttachment(ResourceUsageTag tag);
    ClearAttachmentInfo GetClearAttachmentInfo(const VkClearAttachment &clear_attachment, const VkClearRect &rect) const;
    bool ValidateClearAttachment(const Location &loc, const VkClearAttachment &clear_attachment, const VkClearRect &rect) const;
    void RecordClearAttachment(ResourceUsageTag tag, const VkClearAttachment &clear_attachment, const VkClearRect &rect);

    ResourceUsageTag RecordNextSubpass(vvl::Func command);
    ResourceUsageTag RecordEndRenderPass(vvl::Func command);
    void RecordDestroyEvent(EVENT_STATE *event_state);

    void RecordExecutedCommandBuffer(const CommandBufferAccessContext &recorded_context);
    void ResolveExecutedCommandBuffer(const AccessContext &recorded_context, ResourceUsageTag offset);

    VkQueueFlags GetQueueFlags() const { return cb_state_ ? cb_state_->GetQueueFlags() : 0; }

    ResourceUsageTag NextSubcommandTag(vvl::Func command, ResourceUsageRecord::SubcommandType subcommand);
    ResourceUsageTag NextSubcommandTag(vvl::Func command, NamedHandle &&handle, ResourceUsageRecord::SubcommandType subcommand);

    ResourceUsageTag GetTagLimit() const override { return access_log_->size(); }
    VulkanTypedHandle Handle() const override {
        if (cb_state_) {
            return cb_state_->Handle();
        }
        return VulkanTypedHandle(static_cast<VkCommandBuffer>(VK_NULL_HANDLE), kVulkanObjectTypeCommandBuffer);
    }

    ResourceUsageTag NextCommandTag(vvl::Func command, NamedHandle &&handle,
                                    ResourceUsageRecord::SubcommandType subcommand = ResourceUsageRecord::SubcommandType::kNone);

    ResourceUsageTag NextCommandTag(vvl::Func command,
                                    ResourceUsageRecord::SubcommandType subcommand = ResourceUsageRecord::SubcommandType::kNone);
    ResourceUsageTag NextIndexedCommandTag(vvl::Func command, uint32_t index);

    // NamedHandle must be constructable from args
    template <class... Args>
    void AddHandle(ResourceUsageTag tag, Args &&...args) {
        assert(tag < access_log_->size());
        if (tag < access_log_->size()) {
            (*access_log_)[tag].AddHandle(std::forward<Args>(args)...);
        }
    }

    std::shared_ptr<const CMD_BUFFER_STATE> GetCBStateShared() const { return cb_state_->shared_from_this(); }

    const CMD_BUFFER_STATE &GetCBState() const {
        assert(cb_state_);
        return *cb_state_;
    }

    template <class T, class... Args>
    void RecordSyncOp(Args &&...args) {
        // T must be as derived from SyncOpBase or the compiler will flag the next line as an error.
        SyncOpPointer sync_op(std::make_shared<T>(std::forward<Args>(args)...));
        RecordSyncOp(std::move(sync_op));  // Call the non-template version
    }
    std::shared_ptr<AccessLog> GetAccessLogShared() const { return access_log_; }
    std::shared_ptr<CommandBufferSet> GetCBReferencesShared() const { return cbs_referenced_; }
    void InsertRecordedAccessLogEntries(const CommandBufferAccessContext &cb_context) override;
    const std::vector<SyncOpEntry> &GetSyncOps() const { return sync_ops_; };

  private:
    // As this is passing around a shared pointer to record, move to avoid needless atomics.
    void RecordSyncOp(SyncOpPointer &&sync_op);

    bool ValidateClearAttachment(const Location &loc, const ClearAttachmentInfo &info) const;
    void RecordClearAttachment(ResourceUsageTag tag, const ClearAttachmentInfo &clear_info);

    // Note: since every CommandBufferAccessContext is encapsulated in its CommandBuffer object,
    // a reference count is not needed here.
    CMD_BUFFER_STATE *cb_state_;

    std::shared_ptr<AccessLog> access_log_;
    std::shared_ptr<CommandBufferSet> cbs_referenced_;
    uint32_t command_number_;
    uint32_t subcommand_number_;
    uint32_t reset_count_;
    small_vector<NamedHandle, 1> command_handles_;

    AccessContext cb_access_context_;
    AccessContext *current_context_;
    SyncEventsContext events_context_;

    // Don't need the following for an active proxy cb context
    std::vector<std::unique_ptr<RenderPassAccessContext>> render_pass_contexts_;
    RenderPassAccessContext *current_renderpass_context_;
    std::vector<SyncOpEntry> sync_ops_;

    // State during dynamic rendering (dynamic rendering rendering passes must be
    // contained within a single command buffer)
    std::unique_ptr<syncval_state::DynamicRenderingInfo> dynamic_rendering_info_;
};

// Allow keep track of the exec contexts replay state
class ReplayState {
  public:
    struct RenderPassReplayState {
        // A minimal subset of the functionality present in the RenderPassAccessContext. Since the accesses are recorded in the
        // first_use information of the recorded access contexts, s.t. all we need to support is the barrier/resolve operations
        RenderPassReplayState() { Reset(); }
        AccessContext *Begin(VkQueueFlags queue_flags, const SyncOpBeginRenderPass &begin_op_,
                             const AccessContext &external_context);
        AccessContext *Next();
        void End(AccessContext &external_context);

        const SyncOpBeginRenderPass *begin_op = nullptr;
        const AccessContext *replay_context = nullptr;
        uint32_t subpass = VK_SUBPASS_EXTERNAL;
        std::vector<AccessContext> subpass_contexts;
        void Reset() {
            begin_op = nullptr;
            replay_context = nullptr;
            subpass = VK_SUBPASS_EXTERNAL;
            subpass_contexts.clear();
        }
        operator bool() const { return begin_op != nullptr; }
    };

    bool ValidateFirstUse();
    bool DetectFirstUseHazard(const ResourceUsageRange &first_use_range) const;

    ReplayState(CommandExecutionContext &exec_context, const CommandBufferAccessContext &recorded_context,
                const ErrorObject &error_object, uint32_t index);

    CommandExecutionContext &GetExecutionContext() const { return exec_context_; }
    ResourceUsageTag GetBaseTag() const { return base_tag_; }

    void BeginRenderPassReplaySetup(const SyncOpBeginRenderPass &begin_op) {
        exec_context_.BeginRenderPassReplaySetup(*this, begin_op);
    }
    void NextSubpassReplaySetup() { exec_context_.NextSubpassReplaySetup(*this); }
    void EndRenderPassReplayCleanup() { exec_context_.EndRenderPassReplayCleanup(*this); }

    AccessContext *ReplayStateRenderPassBegin(VkQueueFlags queue_flags, const SyncOpBeginRenderPass &begin_op,
                                              const AccessContext &external_context) {
        return rp_replay_.Begin(queue_flags, begin_op, external_context);
    }
    AccessContext *ReplayStateRenderPassNext() { return rp_replay_.Next(); }
    void ReplayStateRenderPassEnd(AccessContext &external_context) { rp_replay_.End(external_context); }

  protected:
    const AccessContext *GetRecordedAccessContext() const;

    CommandExecutionContext &exec_context_;
    const CommandBufferAccessContext &recorded_context_;
    const ErrorObject &error_obj_;
    const uint32_t index_;
    const ResourceUsageTag base_tag_;
    RenderPassReplayState rp_replay_;
};

namespace syncval_state {
class CommandBuffer : public CMD_BUFFER_STATE {
  public:
    CommandBufferAccessContext access_context;

    CommandBuffer(SyncValidator *dev, VkCommandBuffer cb, const VkCommandBufferAllocateInfo *pCreateInfo,
                  const COMMAND_POOL_STATE *pool);
    ~CommandBuffer() { Destroy(); }

    void NotifyInvalidate(const BASE_NODE::NodeList &invalid_nodes, bool unlink) override;

    void Destroy() override;
    void Reset() override;
};
}  // namespace syncval_state
VALSTATETRACK_DERIVED_STATE_OBJECT(VkCommandBuffer, syncval_state::CommandBuffer, CMD_BUFFER_STATE)

class QueueSyncState;

// Store references to ResourceUsageRecords with global tag range within a batch
class BatchAccessLog {
  public:
    struct BatchRecord {
        BatchRecord() = default;
        BatchRecord(const BatchRecord &other) = default;
        BatchRecord(BatchRecord &&other) = default;
        BatchRecord(const QueueSyncState &q, uint64_t submit, uint32_t batch)
            : queue(&q), submit_index(submit), batch_index(batch), cb_index(0), bias(0) {}
        BatchRecord &operator=(const BatchRecord &other) = default;
        const QueueSyncState *queue;
        uint64_t submit_index;
        uint32_t batch_index;
        uint32_t cb_index;
        ResourceUsageTag bias;
    };

    struct AccessRecord {
        const BatchRecord *batch;
        const ResourceUsageRecord *record;
        bool IsValid() const { return batch && record; }
    };

    struct CBSubmitLog {
      public:
        CBSubmitLog() = default;
        CBSubmitLog(const CBSubmitLog &batch) = default;
        CBSubmitLog(CBSubmitLog &&other) = default;
        CBSubmitLog &operator=(const CBSubmitLog &other) = default;
        CBSubmitLog &operator=(CBSubmitLog &&other) = default;
        CBSubmitLog(const BatchRecord &batch, std::shared_ptr<const CommandExecutionContext::CommandBufferSet> cbs,
                    std::shared_ptr<const CommandExecutionContext::AccessLog> log)
            : batch_(batch), cbs_(cbs), log_(log) {}
        CBSubmitLog(const BatchRecord &batch, const CommandBufferAccessContext &cb)
            : CBSubmitLog(batch, cb.GetCBReferencesShared(), cb.GetAccessLogShared()) {}

        size_t Size() const { return log_->size(); }
        AccessRecord operator[](ResourceUsageTag tag) const;

      private:
        BatchRecord batch_;
        std::shared_ptr<const CommandExecutionContext::CommandBufferSet> cbs_;
        std::shared_ptr<const CommandExecutionContext::AccessLog> log_;
    };

    ResourceUsageTag Import(const BatchRecord &batch, const CommandBufferAccessContext &cb_access);
    void Import(const BatchAccessLog &other);
    void Insert(const BatchRecord &batch, const ResourceUsageRange &range,
                std::shared_ptr<const CommandExecutionContext::AccessLog> log);

    void Trim(const ResourceUsageTagSet &used);
    // AccessRecord lookup is based on global tags
    AccessRecord operator[](ResourceUsageTag tag) const;
    BatchAccessLog() {}

  private:
    using CBSubmitLogRangeMap = sparse_container::range_map<ResourceUsageTag, CBSubmitLog>;
    CBSubmitLogRangeMap log_map_;
};

struct PresentedImageRecord {
    ResourceUsageTag tag;  // the global tag at presentation
    uint32_t image_index;
    uint32_t present_index;
    std::weak_ptr<const syncval_state::Swapchain> swapchain_state;
    std::shared_ptr<const syncval_state::ImageState> image;
};

struct PresentedImage : public PresentedImageRecord {
    std::shared_ptr<QueueBatchContext> batch;
    subresource_adapter::ImageRangeGenerator range_gen;

    PresentedImage() = default;
    void UpdateMemoryAccess(SyncStageAccessIndex usage, ResourceUsageTag tag, AccessContext &access_context) const;
    PresentedImage(const SyncValidator &sync_state, const std::shared_ptr<QueueBatchContext> batch, VkSwapchainKHR swapchain,
                   uint32_t image_index, uint32_t present_index, ResourceUsageTag present_tag_);
    // For non-previsously presented images..
    PresentedImage(std::shared_ptr<const syncval_state::Swapchain> swapchain, uint32_t at_index);
    bool Invalid() const { return BASE_NODE::Invalid(image); }
    void ExportToSwapchain(SyncValidator &);
    void SetImage(uint32_t at_index);
};
using PresentedImages = std::vector<PresentedImage>;

namespace syncval_state {
class Swapchain : public SWAPCHAIN_NODE {
  public:
    Swapchain(ValidationStateTracker *dev_data, const VkSwapchainCreateInfoKHR *pCreateInfo, VkSwapchainKHR swapchain);
    ~Swapchain() { Destroy(); }
    void RecordPresentedImage(PresentedImage &&presented_images);
    PresentedImage MovePresentedImage(uint32_t image_index);
    std::shared_ptr<const Swapchain> shared_from_this() const { return SharedFromThisImpl(this); }
    std::shared_ptr<Swapchain> shared_from_this() { return SharedFromThisImpl(this); }

  private:
    PresentedImages presented;  // Build this on demand
};
}  // namespace syncval_state
VALSTATETRACK_DERIVED_STATE_OBJECT(VkSwapchainKHR, syncval_state::Swapchain, SWAPCHAIN_NODE)

class QueueBatchContext : public CommandExecutionContext {
  public:
    class PresentResourceRecord : public AlternateResourceUsage::RecordBase {
      public:
        using Base_ = AlternateResourceUsage::RecordBase;
        Base_::Record MakeRecord() const override;
        ~PresentResourceRecord() override {}
        PresentResourceRecord(const PresentedImageRecord &presented) : presented_(presented) {}
        std::ostream &Format(std::ostream &out, const SyncValidator &sync_state) const override;

      private:
        PresentedImageRecord presented_;
    };

    class AcquireResourceRecord : public AlternateResourceUsage::RecordBase {
      public:
        using Base_ = AlternateResourceUsage::RecordBase;
        Base_::Record MakeRecord() const override;
        AcquireResourceRecord(const PresentedImageRecord &presented, ResourceUsageTag tag, vvl::Func command)
            : presented_(presented), acquire_tag_(tag), command_(command) {}
        std::ostream &Format(std::ostream &out, const SyncValidator &sync_state) const override;

      private:
        PresentedImageRecord presented_;
        ResourceUsageTag acquire_tag_;
        vvl::Func command_;
    };

    using ConstBatchSet = vvl::unordered_set<std::shared_ptr<const QueueBatchContext>>;
    using BatchSet = vvl::unordered_set<std::shared_ptr<QueueBatchContext>>;
    static constexpr bool TruePred(const std::shared_ptr<const QueueBatchContext> &) { return true; }
    struct CmdBufferEntry {
        uint32_t index = 0;
        std::shared_ptr<const syncval_state::CommandBuffer> cb;
        CmdBufferEntry(uint32_t index_, std::shared_ptr<const syncval_state::CommandBuffer> &&cb_)
            : index(index_), cb(std::move(cb_)) {}
    };

    using CommandBuffers = std::vector<CmdBufferEntry>;

    QueueBatchContext(const SyncValidator &sync_state, const QueueSyncState &queue_state, uint64_t submit_index,
                      uint32_t batch_index);
    QueueBatchContext(const SyncValidator &sync_state);
    QueueBatchContext() = delete;
    void Trim();

    std::string FormatUsage(ResourceUsageTag tag) const override;
    AccessContext *GetCurrentAccessContext() override { return current_access_context_; }
    const AccessContext *GetCurrentAccessContext() const override { return current_access_context_; }
    SyncEventsContext *GetCurrentEventsContext() override { return &events_context_; }
    const SyncEventsContext *GetCurrentEventsContext() const override { return &events_context_; }
    VkQueueFlags GetQueueFlags() const;
    QueueId GetQueueId() const override;

    void SetupBatchTags(const ResourceUsageRange &tag_range);
    void SetupBatchTags();
    void ResetEventsContext() { events_context_.Clear(); }
    ResourceUsageTag GetTagLimit() const override { return batch_.bias; }
    // begin is the tag bias  / .size() is the number of total records that should eventually be in access_log_
    ResourceUsageRange GetTagRange() const { return tag_range_; }
    void InsertRecordedAccessLogEntries(const CommandBufferAccessContext &cb_context) override;

    void SetTagBias(ResourceUsageTag);
    // For Submit
    void SetupAccessContext(const std::shared_ptr<const QueueBatchContext> &prev, const VkSubmitInfo2 &submit_info,
                            SignaledSemaphores &signaled_semaphores);
    void SetupCommandBufferInfo(const VkSubmitInfo2 &submit_info);
    bool DoQueueSubmitValidate(const SyncValidator &sync_state, QueueSubmitCmdState &cmd_state, const VkSubmitInfo2 &submit_info);
    void ResolveSubmittedCommandBuffer(const AccessContext &recorded_context, ResourceUsageTag offset);

    // For Present
    void SetupAccessContext(const std::shared_ptr<const QueueBatchContext> &prev, const VkPresentInfoKHR &present_info,
                            const PresentedImages &presented_images, SignaledSemaphores &signaled);
    bool DoQueuePresentValidate(const Location &loc, const PresentedImages &presented_images);
    void DoPresentOperations(const PresentedImages &presented_images);
    void LogPresentOperations(const PresentedImages &presented_images);

    // For Acquire
    void SetupAccessContext(const PresentedImage &presented);
    void DoAcquireOperation(const PresentedImage &presented);
    void LogAcquireOperation(const PresentedImage &presented, vvl::Func command);

    VulkanTypedHandle Handle() const override;

    template <typename Predicate>
    void ApplyPredicatedWait(Predicate &predicate);
    void ApplyTaggedWait(QueueId queue_id, ResourceUsageTag tag);
    void ApplyAcquireWait(const AcquiredImage &acquired);

    void BeginRenderPassReplaySetup(ReplayState &replay, const SyncOpBeginRenderPass &begin_op) override;
    void NextSubpassReplaySetup(ReplayState &replay) override;
    void EndRenderPassReplayCleanup(ReplayState &replay) override;

    void Cleanup();

  private:
    void CommonSetupAccessContext(const std::shared_ptr<const QueueBatchContext> &prev,
                                  QueueBatchContext::ConstBatchSet &batches_resolved);
    std::shared_ptr<QueueBatchContext> ResolveOneWaitSemaphore(VkSemaphore sem, const PresentedImages &presented_images,
                                                               SignaledSemaphores &signaled);
    std::shared_ptr<QueueBatchContext> ResolveOneWaitSemaphore(VkSemaphore sem, VkPipelineStageFlags2 wait_mask,
                                                               SignaledSemaphores &signaled);

    void ImportSyncTags(const QueueBatchContext &from);
    const QueueSyncState *queue_state_ = nullptr;
    ResourceUsageRange tag_range_ = ResourceUsageRange(0, 0);  // Range of tags referenced by cbs_referenced

    AccessContext access_context_;
    AccessContext *current_access_context_;
    SyncEventsContext events_context_;
    BatchAccessLog batch_log_;
    std::vector<ResourceUsageTag> queue_sync_tag_;

    // Clear these after validation and import, not valid after.
    BatchAccessLog::BatchRecord batch_;  // Holds the cumulative tag bias, and command buffer counts for Import support.
    CommandBuffers command_buffers_;
    ConstBatchSet async_batches_;
};

class QueueSyncState {
  public:
    QueueSyncState(const std::shared_ptr<vvl::Queue> &queue_state, VkQueueFlags queue_flags, QueueId id)
        : submit_index_(0), queue_state_(queue_state), last_batch_(), queue_flags_(queue_flags), id_(id) {}

    VulkanTypedHandle Handle() const {
        if (queue_state_) {
            return queue_state_->Handle();
        }
        return VulkanTypedHandle(static_cast<VkQueue>(VK_NULL_HANDLE), kVulkanObjectTypeQueue);
    }
    std::shared_ptr<const QueueBatchContext> LastBatch() const { return last_batch_; }
    std::shared_ptr<QueueBatchContext> LastBatch() { return last_batch_; }
    void UpdateLastBatch(std::shared_ptr<QueueBatchContext> &&last);
    const vvl::Queue *GetQueueState() const { return queue_state_.get(); }
    VkQueueFlags GetQueueFlags() const { return queue_flags_; }
    QueueId GetQueueId() const { return id_; }

    uint64_t ReserveSubmitId() const;  // Method is const but updates mutable sumbit_index atomically.

  private:
    mutable std::atomic<uint64_t> submit_index_;
    std::shared_ptr<vvl::Queue> queue_state_;
    std::shared_ptr<QueueBatchContext> last_batch_;
    const VkQueueFlags queue_flags_;
    QueueId id_;
};

// The converter needs to be more complex than simply an array of VkSubmitInfo2 structures.
// In order to convert from Info->Info2, arrays of VkSemaphoreSubmitInfo and VkCommandBufferSubmitInfo
// structures must be created for the pWaitSemaphoreInfos, pCommandBufferInfos, and pSignalSemaphoreInfos
// which comprise the converted VkSubmitInfo information. The created VkSubmitInfo2 structure then references the storage
// of the arrays, which must have a lifespan longer than the conversion, s.t. the ensuing valdation/record operations
// can reference them.  The resulting VkSubmitInfo2 is then copied into an additional which takes the place of the pSubmits
// parameter.
struct SubmitInfoConverter {
    struct BatchStore {
        BatchStore(const VkSubmitInfo &info, VkQueueFlags queue_flags);

        static VkSemaphoreSubmitInfo WaitSemaphore(const VkSubmitInfo &info, uint32_t index);
        static VkCommandBufferSubmitInfo CommandBuffer(const VkSubmitInfo &info, uint32_t index);
        static VkSemaphoreSubmitInfo SignalSemaphore(const VkSubmitInfo &info, uint32_t index, VkQueueFlags queue_flags);

        std::vector<VkSemaphoreSubmitInfo> waits;
        std::vector<VkCommandBufferSubmitInfo> cbs;
        std::vector<VkSemaphoreSubmitInfo> signals;
        VkSubmitInfo2 info2;
    };

    SubmitInfoConverter(uint32_t count, const VkSubmitInfo *infos, VkQueueFlags queue_flags);

    std::vector<BatchStore> info_store;
    std::vector<VkSubmitInfo2> info2s;
};

class SyncValidator : public ValidationStateTracker, public SyncStageAccess {
  public:
    using ImageState = syncval_state::ImageState;
    using ImageViewState = syncval_state::ImageViewState;
    using StateTracker = ValidationStateTracker;
    using Func = vvl::Func;
    using Struct = vvl::Struct;
    using Field = vvl::Field;

    SyncValidator() { container_type = LayerObjectTypeSyncValidation; }

    // Global tag range for submitted command buffers resource usage logs
    // Started the global tag count at 1 s.t. zero are invalid and ResourceUsageTag normalization can just zero them.
    mutable std::atomic<ResourceUsageTag> tag_limit_{1};  // This is reserved in Validation phase, thus mutable and atomic
    ResourceUsageRange ReserveGlobalTagRange(size_t tag_count) const;  // Note that the tag_limit_ is mutable this has side effects

    vvl::unordered_map<VkQueue, std::shared_ptr<QueueSyncState>> queue_sync_states_;
    QueueId queue_id_limit_ = kQueueIdBase;
    SignaledSemaphores signaled_semaphores_;

    using SignaledFences = vvl::unordered_map<VkFence, FenceSyncState>;
    using SignaledFence = SignaledFences::value_type;
    SignaledFences waitable_fences_;

    void ApplyTaggedWait(QueueId queue_id, ResourceUsageTag tag);
    void ApplyAcquireWait(const AcquiredImage &acquired);
    template <typename BatchOp>
    void ForAllQueueBatchContexts(BatchOp &&op);

    void UpdateFenceWaitInfo(VkFence fence, QueueId queue_id, ResourceUsageTag tag);
    void UpdateFenceWaitInfo(VkFence fence, const PresentedImage &image, ResourceUsageTag tag);
    void UpdateFenceWaitInfo(std::shared_ptr<const vvl::Fence> &fence, FenceSyncState &&wait_info);

    void WaitForFence(VkFence fence);

    void UpdateSyncImageMemoryBindState(uint32_t count, const VkBindImageMemoryInfo *infos);

    const QueueSyncState *GetQueueSyncState(VkQueue queue) const;
    QueueSyncState *GetQueueSyncState(VkQueue queue);
    std::shared_ptr<const QueueSyncState> GetQueueSyncStateShared(VkQueue queue) const;
    std::shared_ptr<QueueSyncState> GetQueueSyncStateShared(VkQueue queue);
    QueueId GetQueueIdLimit() const { return queue_id_limit_; }

    QueueBatchContext::BatchSet GetQueueBatchSnapshot();

    template <typename Predicate>
    QueueBatchContext::ConstBatchSet GetQueueLastBatchSnapshot(Predicate &&pred) const;
    QueueBatchContext::ConstBatchSet GetQueueLastBatchSnapshot() const {
        return GetQueueLastBatchSnapshot(QueueBatchContext::TruePred);
    };

    template <typename Predicate>
    QueueBatchContext::BatchSet GetQueueLastBatchSnapshot(Predicate &&pred);
    QueueBatchContext::BatchSet GetQueueLastBatchSnapshot() { return GetQueueLastBatchSnapshot(QueueBatchContext::TruePred); };

    std::shared_ptr<CMD_BUFFER_STATE> CreateCmdBufferState(VkCommandBuffer cb, const VkCommandBufferAllocateInfo *pCreateInfo,
                                                           const COMMAND_POOL_STATE *cmd_pool) override;
    std::shared_ptr<SWAPCHAIN_NODE> CreateSwapchainState(const VkSwapchainCreateInfoKHR *create_info,
                                                         VkSwapchainKHR swapchain) final;
    std::shared_ptr<IMAGE_STATE> CreateImageState(VkImage img, const VkImageCreateInfo *pCreateInfo,
                                                  VkFormatFeatureFlags2KHR features) final;

    std::shared_ptr<IMAGE_STATE> CreateImageState(VkImage img, const VkImageCreateInfo *pCreateInfo, VkSwapchainKHR swapchain,
                                                  uint32_t swapchain_index, VkFormatFeatureFlags2KHR features) final;
    std::shared_ptr<IMAGE_VIEW_STATE> CreateImageViewState(const std::shared_ptr<IMAGE_STATE> &image_state, VkImageView iv,
                                                           const VkImageViewCreateInfo *ci, VkFormatFeatureFlags2KHR ff,
                                                           const VkFilterCubicImageViewImageFormatPropertiesEXT &cubic_props) final;

    void RecordCmdBeginRenderPass(VkCommandBuffer commandBuffer, const VkRenderPassBeginInfo *pRenderPassBegin,
                                  const VkSubpassBeginInfo *pSubpassBeginInfo, Func command);
    void RecordCmdNextSubpass(VkCommandBuffer commandBuffer, const VkSubpassBeginInfo *pSubpassBeginInfo,
                              const VkSubpassEndInfo *pSubpassEndInfo, Func command);
    void RecordCmdEndRenderPass(VkCommandBuffer commandBuffer, const VkSubpassEndInfo *pSubpassEndInfo, Func command);
    bool SupressedBoundDescriptorWAW(const HazardResult &hazard) const;

    void CreateDevice(const VkDeviceCreateInfo *pCreateInfo) override;

    bool ValidateBeginRenderPass(VkCommandBuffer commandBuffer, const VkRenderPassBeginInfo *pRenderPassBegin,
                                 const VkSubpassBeginInfo *pSubpassBeginInfo, const ErrorObject &error_obj) const;

    bool PreCallValidateCmdBeginRenderPass(VkCommandBuffer commandBuffer, const VkRenderPassBeginInfo *pRenderPassBegin,
                                           VkSubpassContents contents, const ErrorObject &error_obj) const override;

    bool PreCallValidateCmdBeginRenderPass2KHR(VkCommandBuffer commandBuffer, const VkRenderPassBeginInfo *pRenderPassBegin,
                                               const VkSubpassBeginInfo *pSubpassBeginInfo,
                                               const ErrorObject &error_obj) const override;

    bool PreCallValidateCmdBeginRenderPass2(VkCommandBuffer commandBuffer, const VkRenderPassBeginInfo *pRenderPassBegin,
                                            const VkSubpassBeginInfo *pSubpassBeginInfo,
                                            const ErrorObject &error_obj) const override;

    bool PreCallValidateCmdCopyBuffer(VkCommandBuffer commandBuffer, VkBuffer srcBuffer, VkBuffer dstBuffer, uint32_t regionCount,
                                      const VkBufferCopy *pRegions, const ErrorObject &error_obj) const override;

    void PreCallRecordCmdCopyBuffer(VkCommandBuffer commandBuffer, VkBuffer srcBuffer, VkBuffer dstBuffer, uint32_t regionCount,
                                    const VkBufferCopy *pRegions, const RecordObject &record_obj) override;

    bool PreCallValidateCmdCopyBuffer2KHR(VkCommandBuffer commandBuffer, const VkCopyBufferInfo2KHR *pCopyBufferInfo,
                                          const ErrorObject &error_obj) const override;
    bool PreCallValidateCmdCopyBuffer2(VkCommandBuffer commandBuffer, const VkCopyBufferInfo2 *pCopyBufferInfo,
                                       const ErrorObject &error_obj) const override;
    void RecordCmdCopyBuffer2(VkCommandBuffer commandBuffer, const VkCopyBufferInfo2KHR *pCopyBufferInfo, Func command);
    void PreCallRecordCmdCopyBuffer2KHR(VkCommandBuffer commandBuffer, const VkCopyBufferInfo2KHR *pCopyBufferInfo,
                                        const RecordObject &record_obj) override;
    void PreCallRecordCmdCopyBuffer2(VkCommandBuffer commandBuffer, const VkCopyBufferInfo2 *pCopyBufferInfo,
                                     const RecordObject &record_obj) override;

    bool PreCallValidateCmdCopyImage(VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout,
                                     VkImage dstImage, VkImageLayout dstImageLayout, uint32_t regionCount,
                                     const VkImageCopy *pRegions, const ErrorObject &error_obj) const override;

    void PreCallRecordCmdCopyImage(VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout, VkImage dstImage,
                                   VkImageLayout dstImageLayout, uint32_t regionCount, const VkImageCopy *pRegions,
                                   const RecordObject &record_obj) override;

    bool PreCallValidateCmdCopyImage2KHR(VkCommandBuffer commandBuffer, const VkCopyImageInfo2KHR *pCopyImageInfo,
                                         const ErrorObject &error_obj) const override;
    bool PreCallValidateCmdCopyImage2(VkCommandBuffer commandBuffer, const VkCopyImageInfo2 *pCopyImageInfo,
                                      const ErrorObject &error_obj) const override;

    void RecordCmdCopyImage2(VkCommandBuffer commandBuffer, const VkCopyImageInfo2 *pCopyImageInfo, Func command);
    void PreCallRecordCmdCopyImage2KHR(VkCommandBuffer commandBuffer, const VkCopyImageInfo2KHR *pCopyImageInfo,
                                       const RecordObject &record_obj) override;
    void PreCallRecordCmdCopyImage2(VkCommandBuffer commandBuffer, const VkCopyImageInfo2 *pCopyImageInfo,
                                    const RecordObject &record_obj) override;

    bool PreCallValidateCmdPipelineBarrier(VkCommandBuffer commandBuffer, VkPipelineStageFlags srcStageMask,
                                           VkPipelineStageFlags dstStageMask, VkDependencyFlags dependencyFlags,
                                           uint32_t memoryBarrierCount, const VkMemoryBarrier *pMemoryBarriers,
                                           uint32_t bufferMemoryBarrierCount, const VkBufferMemoryBarrier *pBufferMemoryBarriers,
                                           uint32_t imageMemoryBarrierCount, const VkImageMemoryBarrier *pImageMemoryBarriers,
                                           const ErrorObject &error_obj) const override;

    void PreCallRecordCmdPipelineBarrier(VkCommandBuffer commandBuffer, VkPipelineStageFlags srcStageMask,
                                         VkPipelineStageFlags dstStageMask, VkDependencyFlags dependencyFlags,
                                         uint32_t memoryBarrierCount, const VkMemoryBarrier *pMemoryBarriers,
                                         uint32_t bufferMemoryBarrierCount, const VkBufferMemoryBarrier *pBufferMemoryBarriers,
                                         uint32_t imageMemoryBarrierCount, const VkImageMemoryBarrier *pImageMemoryBarriers,
                                         const RecordObject &record_obj) override;

    bool PreCallValidateCmdPipelineBarrier2KHR(VkCommandBuffer commandBuffer, const VkDependencyInfoKHR *pDependencyInfo,
                                               const ErrorObject &error_obj) const override;
    bool PreCallValidateCmdPipelineBarrier2(VkCommandBuffer commandBuffer, const VkDependencyInfo *pDependencyInfo,
                                            const ErrorObject &error_obj) const override;
    void PreCallRecordCmdPipelineBarrier2KHR(VkCommandBuffer commandBuffer, const VkDependencyInfoKHR *pDependencyInfo,
                                             const RecordObject &record_obj) override;
    void PreCallRecordCmdPipelineBarrier2(VkCommandBuffer commandBuffer, const VkDependencyInfo *pDependencyInfo,
                                          const RecordObject &record_obj) override;

    void PostCallRecordBeginCommandBuffer(VkCommandBuffer commandBuffer, const VkCommandBufferBeginInfo *pBeginInfo,
                                          const RecordObject &record_obj) override;

    void PostCallRecordCmdBeginRenderPass(VkCommandBuffer commandBuffer, const VkRenderPassBeginInfo *pRenderPassBegin,
                                          VkSubpassContents contents, const RecordObject &record_obj) override;
    void PostCallRecordCmdBeginRenderPass2(VkCommandBuffer commandBuffer, const VkRenderPassBeginInfo *pRenderPassBegin,
                                           const VkSubpassBeginInfo *pSubpassBeginInfo, const RecordObject &record_obj) override;
    void PostCallRecordCmdBeginRenderPass2KHR(VkCommandBuffer commandBuffer, const VkRenderPassBeginInfo *pRenderPassBegin,
                                              const VkSubpassBeginInfo *pSubpassBeginInfo, const RecordObject &record_obj) override;

    bool ValidateCmdNextSubpass(VkCommandBuffer commandBuffer, const VkSubpassBeginInfo *pSubpassBeginInfo,
                                const VkSubpassEndInfo *pSubpassEndInfo, const ErrorObject &error_obj) const;
    bool PreCallValidateCmdNextSubpass(VkCommandBuffer commandBuffer, VkSubpassContents contents,
                                       const ErrorObject &error_obj) const override;
    bool PreCallValidateCmdNextSubpass2(VkCommandBuffer commandBuffer, const VkSubpassBeginInfo *pSubpassBeginInfo,
                                        const VkSubpassEndInfo *pSubpassEndInfo, const ErrorObject &error_obj) const override;
    bool PreCallValidateCmdNextSubpass2KHR(VkCommandBuffer commandBuffer, const VkSubpassBeginInfo *pSubpassBeginInfo,
                                           const VkSubpassEndInfo *pSubpassEndInfo, const ErrorObject &error_obj) const override;

    void PostCallRecordCmdNextSubpass(VkCommandBuffer commandBuffer, VkSubpassContents contents,
                                      const RecordObject &record_obj) override;
    void PostCallRecordCmdNextSubpass2(VkCommandBuffer commandBuffer, const VkSubpassBeginInfo *pSubpassBeginInfo,
                                       const VkSubpassEndInfo *pSubpassEndInfo, const RecordObject &record_obj) override;
    void PostCallRecordCmdNextSubpass2KHR(VkCommandBuffer commandBuffer, const VkSubpassBeginInfo *pSubpassBeginInfo,
                                          const VkSubpassEndInfo *pSubpassEndInfo, const RecordObject &record_obj) override;

    bool ValidateCmdEndRenderPass(VkCommandBuffer commandBuffer, const VkSubpassEndInfo *pSubpassEndInfo,
                                  const ErrorObject &error_obj) const;
    bool PreCallValidateCmdEndRenderPass(VkCommandBuffer commandBuffer, const ErrorObject &error_obj) const override;
    bool PreCallValidateCmdEndRenderPass2KHR(VkCommandBuffer commandBuffer, const VkSubpassEndInfo *pSubpassEndInfo,
                                             const ErrorObject &error_obj) const override;
    bool PreCallValidateCmdEndRenderPass2(VkCommandBuffer commandBuffer, const VkSubpassEndInfo *pSubpassEndInfo,
                                          const ErrorObject &error_obj) const override;

    void PostCallRecordCmdEndRenderPass(VkCommandBuffer commandBuffer, const RecordObject &record_obj) override;
    void PostCallRecordCmdEndRenderPass2(VkCommandBuffer commandBuffer, const VkSubpassEndInfo *pSubpassEndInfo,
                                         const RecordObject &record_obj) override;
    void PostCallRecordCmdEndRenderPass2KHR(VkCommandBuffer commandBuffer, const VkSubpassEndInfo *pSubpassEndInfo,
                                            const RecordObject &record_obj) override;

    bool PreCallValidateCmdBeginRenderingKHR(VkCommandBuffer commandBuffer, const VkRenderingInfoKHR *pRenderingInfo,
                                             const ErrorObject &error_obj) const override;
    bool PreCallValidateCmdBeginRendering(VkCommandBuffer commandBuffer, const VkRenderingInfo *pRenderingInfo,
                                          const ErrorObject &error_obj) const override;
    void PreCallRecordCmdBeginRenderingKHR(VkCommandBuffer commandBuffer, const VkRenderingInfoKHR *pRenderingInfo,
                                           const RecordObject &record_obj) override;
    void PreCallRecordCmdBeginRendering(VkCommandBuffer commandBuffer, const VkRenderingInfo *pRenderingInfo,
                                        const RecordObject &record_obj) override;

    bool PreCallValidateCmdEndRenderingKHR(VkCommandBuffer commandBuffer, const ErrorObject &error_obj) const override;
    bool PreCallValidateCmdEndRendering(VkCommandBuffer commandBuffer, const ErrorObject &error_obj) const override;
    void PreCallRecordCmdEndRenderingKHR(VkCommandBuffer commandBuffer, const RecordObject &record_obj) override;
    void PreCallRecordCmdEndRendering(VkCommandBuffer commandBuffer, const RecordObject &record_obj) override;

    template <typename RegionType>
    bool ValidateCmdCopyBufferToImage(VkCommandBuffer commandBuffer, VkBuffer srcBuffer, VkImage dstImage,
                                      VkImageLayout dstImageLayout, uint32_t regionCount, const RegionType *pRegions,
                                      const Location &loc) const;
    bool PreCallValidateCmdCopyBufferToImage(VkCommandBuffer commandBuffer, VkBuffer srcBuffer, VkImage dstImage,
                                             VkImageLayout dstImageLayout, uint32_t regionCount, const VkBufferImageCopy *pRegions,
                                             const ErrorObject &error_obj) const override;
    bool PreCallValidateCmdCopyBufferToImage2KHR(VkCommandBuffer commandBuffer,
                                                 const VkCopyBufferToImageInfo2KHR *pCopyBufferToImageInfo,
                                                 const ErrorObject &error_obj) const override;
    bool PreCallValidateCmdCopyBufferToImage2(VkCommandBuffer commandBuffer, const VkCopyBufferToImageInfo2 *pCopyBufferToImageInfo,
                                              const ErrorObject &error_obj) const override;

    template <typename RegionType>
    void RecordCmdCopyBufferToImage(VkCommandBuffer commandBuffer, VkBuffer srcBuffer, VkImage dstImage,
                                    VkImageLayout dstImageLayout, uint32_t regionCount, const RegionType *pRegions, Func command);
    void PreCallRecordCmdCopyBufferToImage(VkCommandBuffer commandBuffer, VkBuffer srcBuffer, VkImage dstImage,
                                           VkImageLayout dstImageLayout, uint32_t regionCount, const VkBufferImageCopy *pRegions,
                                           const RecordObject &record_obj) override;
    void PreCallRecordCmdCopyBufferToImage2KHR(VkCommandBuffer commandBuffer,
                                               const VkCopyBufferToImageInfo2KHR *pCopyBufferToImageInfo,
                                               const RecordObject &record_obj) override;
    void PreCallRecordCmdCopyBufferToImage2(VkCommandBuffer commandBuffer, const VkCopyBufferToImageInfo2 *pCopyBufferToImageInfo,
                                            const RecordObject &record_obj) override;

    template <typename RegionType>
    bool ValidateCmdCopyImageToBuffer(VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout,
                                      VkBuffer dstBuffer, uint32_t regionCount, const RegionType *pRegions,
                                      const Location &loc) const;
    bool PreCallValidateCmdCopyImageToBuffer(VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout,
                                             VkBuffer dstBuffer, uint32_t regionCount, const VkBufferImageCopy *pRegions,
                                             const ErrorObject &error_obj) const override;
    bool PreCallValidateCmdCopyImageToBuffer2KHR(VkCommandBuffer commandBuffer,
                                                 const VkCopyImageToBufferInfo2KHR *pCopyImageToBufferInfo,
                                                 const ErrorObject &error_obj) const override;
    bool PreCallValidateCmdCopyImageToBuffer2(VkCommandBuffer commandBuffer, const VkCopyImageToBufferInfo2 *pCopyImageToBufferInfo,
                                              const ErrorObject &error_obj) const override;

    template <typename RegionType>
    void RecordCmdCopyImageToBuffer(VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout,
                                    VkBuffer dstBuffer, uint32_t regionCount, const RegionType *pRegions, Func command);
    void PreCallRecordCmdCopyImageToBuffer(VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout,
                                           VkBuffer dstBuffer, uint32_t regionCount, const VkBufferImageCopy *pRegions,
                                           const RecordObject &record_obj) override;
    void PreCallRecordCmdCopyImageToBuffer2KHR(VkCommandBuffer commandBuffer,
                                               const VkCopyImageToBufferInfo2KHR *pCopyImageToBufferInfo,
                                               const RecordObject &record_obj) override;
    void PreCallRecordCmdCopyImageToBuffer2(VkCommandBuffer commandBuffer, const VkCopyImageToBufferInfo2 *pCopyImageToBufferInfo,
                                            const RecordObject &record_obj) override;

    template <typename RegionType>
    bool ValidateCmdBlitImage(VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout, VkImage dstImage,
                              VkImageLayout dstImageLayout, uint32_t regionCount, const RegionType *pRegions, VkFilter filter,
                              const Location &loc) const;

    bool PreCallValidateCmdBlitImage(VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout,
                                     VkImage dstImage, VkImageLayout dstImageLayout, uint32_t regionCount,
                                     const VkImageBlit *pRegions, VkFilter filter, const ErrorObject &error_obj) const override;
    bool PreCallValidateCmdBlitImage2KHR(VkCommandBuffer commandBuffer, const VkBlitImageInfo2KHR *pBlitImageInfo,
                                         const ErrorObject &error_obj) const override;
    bool PreCallValidateCmdBlitImage2(VkCommandBuffer commandBuffer, const VkBlitImageInfo2 *pBlitImageInfo,
                                      const ErrorObject &error_obj) const override;

    template <typename RegionType>
    void RecordCmdBlitImage(VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout, VkImage dstImage,
                            VkImageLayout dstImageLayout, uint32_t regionCount, const RegionType *pRegions, VkFilter filter,
                            Func command);
    void PreCallRecordCmdBlitImage(VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout, VkImage dstImage,
                                   VkImageLayout dstImageLayout, uint32_t regionCount, const VkImageBlit *pRegions, VkFilter filter,
                                   const RecordObject &record_obj) override;
    void PreCallRecordCmdBlitImage2KHR(VkCommandBuffer commandBuffer, const VkBlitImageInfo2KHR *pBlitImageInfo,
                                       const RecordObject &record_obj) override;
    void PreCallRecordCmdBlitImage2(VkCommandBuffer commandBuffer, const VkBlitImageInfo2 *pBlitImageInfo,
                                    const RecordObject &record_obj) override;

    bool ValidateIndirectBuffer(const CommandBufferAccessContext &cb_context, const AccessContext &context,
                                VkCommandBuffer commandBuffer, const VkDeviceSize struct_size, const VkBuffer buffer,
                                const VkDeviceSize offset, const uint32_t drawCount, const uint32_t stride,
                                const Location &loc) const;
    void RecordIndirectBuffer(AccessContext &context, ResourceUsageTag tag, const VkDeviceSize struct_size, const VkBuffer buffer,
                              const VkDeviceSize offset, const uint32_t drawCount, uint32_t stride);

    bool ValidateCountBuffer(const CommandBufferAccessContext &cb_context, const AccessContext &context,
                             VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset, const Location &loc) const;
    void RecordCountBuffer(AccessContext &context, ResourceUsageTag tag, VkBuffer buffer, VkDeviceSize offset);

    bool PreCallValidateCmdDispatch(VkCommandBuffer commandBuffer, uint32_t x, uint32_t y, uint32_t z,
                                    const ErrorObject &error_obj) const override;
    void PreCallRecordCmdDispatch(VkCommandBuffer commandBuffer, uint32_t x, uint32_t y, uint32_t z,
                                  const RecordObject &record_obj) override;

    bool PreCallValidateCmdDispatchIndirect(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset,
                                            const ErrorObject &error_obj) const override;
    void PreCallRecordCmdDispatchIndirect(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset,
                                          const RecordObject &record_obj) override;

    bool PreCallValidateCmdDraw(VkCommandBuffer commandBuffer, uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex,
                                uint32_t firstInstance, const ErrorObject &error_obj) const override;
    void PreCallRecordCmdDraw(VkCommandBuffer commandBuffer, uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex,
                              uint32_t firstInstance, const RecordObject &record_obj) override;

    bool PreCallValidateCmdDrawIndexed(VkCommandBuffer commandBuffer, uint32_t indexCount, uint32_t instanceCount,
                                       uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance,
                                       const ErrorObject &error_obj) const override;
    void PreCallRecordCmdDrawIndexed(VkCommandBuffer commandBuffer, uint32_t indexCount, uint32_t instanceCount,
                                     uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance,
                                     const RecordObject &record_obj) override;

    bool PreCallValidateCmdDrawIndirect(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset, uint32_t drawCount,
                                        uint32_t stride, const ErrorObject &error_obj) const override;
    void PreCallRecordCmdDrawIndirect(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset, uint32_t drawCount,
                                      uint32_t stride, const RecordObject &record_obj) override;

    bool PreCallValidateCmdDrawIndexedIndirect(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset,
                                               uint32_t drawCount, uint32_t stride, const ErrorObject &error_obj) const override;
    void PreCallRecordCmdDrawIndexedIndirect(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset,
                                             uint32_t drawCount, uint32_t stride, const RecordObject &record_obj) override;

    bool PreCallValidateCmdDrawIndirectCount(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset,
                                             VkBuffer countBuffer, VkDeviceSize countBufferOffset, uint32_t maxDrawCount,
                                             uint32_t stride, const ErrorObject &error_obj) const override;
    void RecordCmdDrawIndirectCount(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset, VkBuffer countBuffer,
                                    VkDeviceSize countBufferOffset, uint32_t maxDrawCount, uint32_t stride, Func command);
    void PreCallRecordCmdDrawIndirectCount(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset,
                                           VkBuffer countBuffer, VkDeviceSize countBufferOffset, uint32_t maxDrawCount,
                                           uint32_t stride, const RecordObject &record_obj) override;
    bool PreCallValidateCmdDrawIndirectCountKHR(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset,
                                                VkBuffer countBuffer, VkDeviceSize countBufferOffset, uint32_t maxDrawCount,
                                                uint32_t stride, const ErrorObject &error_obj) const override;
    void PreCallRecordCmdDrawIndirectCountKHR(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset,
                                              VkBuffer countBuffer, VkDeviceSize countBufferOffset, uint32_t maxDrawCount,
                                              uint32_t stride, const RecordObject &record_obj) override;
    bool PreCallValidateCmdDrawIndirectCountAMD(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset,
                                                VkBuffer countBuffer, VkDeviceSize countBufferOffset, uint32_t maxDrawCount,
                                                uint32_t stride, const ErrorObject &error_obj) const override;
    void PreCallRecordCmdDrawIndirectCountAMD(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset,
                                              VkBuffer countBuffer, VkDeviceSize countBufferOffset, uint32_t maxDrawCount,
                                              uint32_t stride, const RecordObject &record_obj) override;

    bool PreCallValidateCmdDrawIndexedIndirectCount(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset,
                                                    VkBuffer countBuffer, VkDeviceSize countBufferOffset, uint32_t maxDrawCount,
                                                    uint32_t stride, const ErrorObject &error_obj) const override;
    void RecordCmdDrawIndexedIndirectCount(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset,
                                           VkBuffer countBuffer, VkDeviceSize countBufferOffset, uint32_t maxDrawCount,
                                           uint32_t stride, Func command);
    void PreCallRecordCmdDrawIndexedIndirectCount(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset,
                                                  VkBuffer countBuffer, VkDeviceSize countBufferOffset, uint32_t maxDrawCount,
                                                  uint32_t stride, const RecordObject &record_obj) override;
    bool PreCallValidateCmdDrawIndexedIndirectCountKHR(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset,
                                                       VkBuffer countBuffer, VkDeviceSize countBufferOffset, uint32_t maxDrawCount,
                                                       uint32_t stride, const ErrorObject &error_obj) const override;
    void PreCallRecordCmdDrawIndexedIndirectCountKHR(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset,
                                                     VkBuffer countBuffer, VkDeviceSize countBufferOffset, uint32_t maxDrawCount,
                                                     uint32_t stride, const RecordObject &record_obj) override;
    bool PreCallValidateCmdDrawIndexedIndirectCountAMD(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset,
                                                       VkBuffer countBuffer, VkDeviceSize countBufferOffset, uint32_t maxDrawCount,
                                                       uint32_t stride, const ErrorObject &error_obj) const override;
    void PreCallRecordCmdDrawIndexedIndirectCountAMD(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset,
                                                     VkBuffer countBuffer, VkDeviceSize countBufferOffset, uint32_t maxDrawCount,
                                                     uint32_t stride, const RecordObject &record_obj) override;

    bool PreCallValidateCmdClearColorImage(VkCommandBuffer commandBuffer, VkImage image, VkImageLayout imageLayout,
                                           const VkClearColorValue *pColor, uint32_t rangeCount,
                                           const VkImageSubresourceRange *pRanges, const ErrorObject &error_obj) const override;
    void PreCallRecordCmdClearColorImage(VkCommandBuffer commandBuffer, VkImage image, VkImageLayout imageLayout,
                                         const VkClearColorValue *pColor, uint32_t rangeCount,
                                         const VkImageSubresourceRange *pRanges, const RecordObject &record_obj) override;

    bool PreCallValidateCmdClearDepthStencilImage(VkCommandBuffer commandBuffer, VkImage image, VkImageLayout imageLayout,
                                                  const VkClearDepthStencilValue *pDepthStencil, uint32_t rangeCount,
                                                  const VkImageSubresourceRange *pRanges,
                                                  const ErrorObject &error_obj) const override;
    void PreCallRecordCmdClearDepthStencilImage(VkCommandBuffer commandBuffer, VkImage image, VkImageLayout imageLayout,
                                                const VkClearDepthStencilValue *pDepthStencil, uint32_t rangeCount,
                                                const VkImageSubresourceRange *pRanges, const RecordObject &record_obj) override;

    bool PreCallValidateCmdClearAttachments(VkCommandBuffer commandBuffer, uint32_t attachmentCount,
                                            const VkClearAttachment *pAttachments, uint32_t rectCount, const VkClearRect *pRects,
                                            const ErrorObject &error_obj) const override;
    void PreCallRecordCmdClearAttachments(VkCommandBuffer commandBuffer, uint32_t attachmentCount,
                                          const VkClearAttachment *pAttachments, uint32_t rectCount, const VkClearRect *pRects,
                                          const RecordObject &record_obj) override;

    bool PreCallValidateCmdCopyQueryPoolResults(VkCommandBuffer commandBuffer, VkQueryPool queryPool, uint32_t firstQuery,
                                                uint32_t queryCount, VkBuffer dstBuffer, VkDeviceSize dstOffset,
                                                VkDeviceSize stride, VkQueryResultFlags flags,
                                                const ErrorObject &error_obj) const override;
    void PreCallRecordCmdCopyQueryPoolResults(VkCommandBuffer commandBuffer, VkQueryPool queryPool, uint32_t firstQuery,
                                              uint32_t queryCount, VkBuffer dstBuffer, VkDeviceSize dstOffset, VkDeviceSize stride,
                                              VkQueryResultFlags flags, const RecordObject &record_obj) override;

    bool PreCallValidateCmdFillBuffer(VkCommandBuffer commandBuffer, VkBuffer dstBuffer, VkDeviceSize dstOffset, VkDeviceSize size,
                                      uint32_t data, const ErrorObject &error_obj) const override;
    void PreCallRecordCmdFillBuffer(VkCommandBuffer commandBuffer, VkBuffer dstBuffer, VkDeviceSize dstOffset, VkDeviceSize size,
                                    uint32_t data, const RecordObject &record_obj) override;

    bool PreCallValidateCmdResolveImage(VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout,
                                        VkImage dstImage, VkImageLayout dstImageLayout, uint32_t regionCount,
                                        const VkImageResolve *pRegions, const ErrorObject &error_obj) const override;

    void PreCallRecordCmdResolveImage(VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout,
                                      VkImage dstImage, VkImageLayout dstImageLayout, uint32_t regionCount,
                                      const VkImageResolve *pRegions, const RecordObject &record_obj) override;

    bool PreCallValidateCmdResolveImage2KHR(VkCommandBuffer commandBuffer, const VkResolveImageInfo2KHR *pResolveImageInfo,
                                            const ErrorObject &error_obj) const override;
    bool PreCallValidateCmdResolveImage2(VkCommandBuffer commandBuffer, const VkResolveImageInfo2 *pResolveImageInfo,
                                         const ErrorObject &error_obj) const override;
    void PreCallRecordCmdResolveImage2KHR(VkCommandBuffer commandBuffer, const VkResolveImageInfo2KHR *pResolveImageInfo,
                                          const RecordObject &record_obj) override;
    void PreCallRecordCmdResolveImage2(VkCommandBuffer commandBuffer, const VkResolveImageInfo2 *pResolveImageInfo,
                                       const RecordObject &record_obj) override;

    bool PreCallValidateCmdUpdateBuffer(VkCommandBuffer commandBuffer, VkBuffer dstBuffer, VkDeviceSize dstOffset,
                                        VkDeviceSize dataSize, const void *pData, const ErrorObject &error_obj) const override;
    void PreCallRecordCmdUpdateBuffer(VkCommandBuffer commandBuffer, VkBuffer dstBuffer, VkDeviceSize dstOffset,
                                      VkDeviceSize dataSize, const void *pData, const RecordObject &record_obj) override;

    bool PreCallValidateCmdWriteBufferMarkerAMD(VkCommandBuffer commandBuffer, VkPipelineStageFlagBits pipelineStage,
                                                VkBuffer dstBuffer, VkDeviceSize dstOffset, uint32_t marker,
                                                const ErrorObject &error_obj) const override;
    void PreCallRecordCmdWriteBufferMarkerAMD(VkCommandBuffer commandBuffer, VkPipelineStageFlagBits pipelineStage,
                                              VkBuffer dstBuffer, VkDeviceSize dstOffset, uint32_t marker,
                                              const RecordObject &record_obj) override;

    bool PreCallValidateCmdSetEvent(VkCommandBuffer commandBuffer, VkEvent event, VkPipelineStageFlags stageMask,
                                    const ErrorObject &error_obj) const override;
    void PostCallRecordCmdSetEvent(VkCommandBuffer commandBuffer, VkEvent event, VkPipelineStageFlags stageMask,
                                   const RecordObject &record_obj) override;

    bool PreCallValidateCmdSetEvent2KHR(VkCommandBuffer commandBuffer, VkEvent event, const VkDependencyInfoKHR *pDependencyInfo,
                                        const ErrorObject &error_obj) const override;
    bool PreCallValidateCmdSetEvent2(VkCommandBuffer commandBuffer, VkEvent event, const VkDependencyInfo *pDependencyInfo,
                                     const ErrorObject &error_obj) const override;
    void PostCallRecordCmdSetEvent2KHR(VkCommandBuffer commandBuffer, VkEvent event, const VkDependencyInfoKHR *pDependencyInfo,
                                       const RecordObject &record_obj) override;
    void PostCallRecordCmdSetEvent2(VkCommandBuffer commandBuffer, VkEvent event, const VkDependencyInfo *pDependencyInfo,
                                    const RecordObject &record_obj) override;

    bool PreCallValidateCmdResetEvent(VkCommandBuffer commandBuffer, VkEvent event, VkPipelineStageFlags stageMask,
                                      const ErrorObject &error_obj) const override;
    void PostCallRecordCmdResetEvent(VkCommandBuffer commandBuffer, VkEvent event, VkPipelineStageFlags stageMask,
                                     const RecordObject &record_obj) override;

    bool PreCallValidateCmdResetEvent2KHR(VkCommandBuffer commandBuffer, VkEvent event, VkPipelineStageFlags2KHR stageMask,
                                          const ErrorObject &error_obj) const override;
    bool PreCallValidateCmdResetEvent2(VkCommandBuffer commandBuffer, VkEvent event, VkPipelineStageFlags2 stageMask,
                                       const ErrorObject &error_obj) const override;
    void PostCallRecordCmdResetEvent2KHR(VkCommandBuffer commandBuffer, VkEvent event, VkPipelineStageFlags2KHR stageMask,
                                         const RecordObject &record_obj) override;
    void PostCallRecordCmdResetEvent2(VkCommandBuffer commandBuffer, VkEvent event, VkPipelineStageFlags2 stageMask,
                                      const RecordObject &record_obj) override;

    bool PreCallValidateCmdWaitEvents(VkCommandBuffer commandBuffer, uint32_t eventCount, const VkEvent *pEvents,
                                      VkPipelineStageFlags sourceStageMask, VkPipelineStageFlags dstStageMask,
                                      uint32_t memoryBarrierCount, const VkMemoryBarrier *pMemoryBarriers,
                                      uint32_t bufferMemoryBarrierCount, const VkBufferMemoryBarrier *pBufferMemoryBarriers,
                                      uint32_t imageMemoryBarrierCount, const VkImageMemoryBarrier *pImageMemoryBarriers,
                                      const ErrorObject &error_obj) const override;
    void PostCallRecordCmdWaitEvents(VkCommandBuffer commandBuffer, uint32_t eventCount, const VkEvent *pEvents,
                                     VkPipelineStageFlags sourceStageMask, VkPipelineStageFlags dstStageMask,
                                     uint32_t memoryBarrierCount, const VkMemoryBarrier *pMemoryBarriers,
                                     uint32_t bufferMemoryBarrierCount, const VkBufferMemoryBarrier *pBufferMemoryBarriers,
                                     uint32_t imageMemoryBarrierCount, const VkImageMemoryBarrier *pImageMemoryBarriers,
                                     const RecordObject &record_obj) override;
    bool PreCallValidateCmdWaitEvents2KHR(VkCommandBuffer commandBuffer, uint32_t eventCount, const VkEvent *pEvents,
                                          const VkDependencyInfoKHR *pDependencyInfos, const ErrorObject &error_obj) const override;
    void PostCallRecordCmdWaitEvents2KHR(VkCommandBuffer commandBuffer, uint32_t eventCount, const VkEvent *pEvents,
                                         const VkDependencyInfoKHR *pDependencyInfos, const RecordObject &record_obj) override;
    bool PreCallValidateCmdWaitEvents2(VkCommandBuffer commandBuffer, uint32_t eventCount, const VkEvent *pEvents,
                                       const VkDependencyInfo *pDependencyInfos, const ErrorObject &error_obj) const override;
    void PostCallRecordCmdWaitEvents2(VkCommandBuffer commandBuffer, uint32_t eventCount, const VkEvent *pEvents,
                                      const VkDependencyInfo *pDependencyInfos, const RecordObject &record_obj) override;
    bool PreCallValidateCmdWriteBufferMarker2AMD(VkCommandBuffer commandBuffer, VkPipelineStageFlags2KHR stage, VkBuffer dstBuffer,
                                                 VkDeviceSize dstOffset, uint32_t marker,
                                                 const ErrorObject &error_obj) const override;
    void PreCallRecordCmdWriteBufferMarker2AMD(VkCommandBuffer commandBuffer, VkPipelineStageFlags2KHR stage, VkBuffer dstBuffer,
                                               VkDeviceSize dstOffset, uint32_t marker, const RecordObject &record_obj) override;
    bool PreCallValidateCmdExecuteCommands(VkCommandBuffer commandBuffer, uint32_t commandBufferCount,
                                           const VkCommandBuffer *pCommandBuffers, const ErrorObject &error_obj) const override;
    void PreCallRecordCmdExecuteCommands(VkCommandBuffer commandBuffer, uint32_t commandBufferCount,
                                         const VkCommandBuffer *pCommandBuffers, const RecordObject &record_obj) override;
    void PostCallRecordBindImageMemory(VkDevice device, VkImage image, VkDeviceMemory mem, VkDeviceSize memoryOffset,
                                       const RecordObject &record_obj) override;
    void PostCallRecordBindImageMemory2(VkDevice device, uint32_t bindInfoCount, const VkBindImageMemoryInfo *pBindInfos,
                                        const RecordObject &record_obj) override;
    void PostCallRecordBindImageMemory2KHR(VkDevice device, uint32_t bindInfoCount, const VkBindImageMemoryInfo *pBindInfos,
                                           const RecordObject &record_obj) override;
    void PostCallRecordQueueWaitIdle(VkQueue queue, const RecordObject &record_obj) override;
    void PostCallRecordDeviceWaitIdle(VkDevice device, const RecordObject &record_obj) override;

    bool PreCallValidateQueuePresentKHR(VkQueue queue, const VkPresentInfoKHR *pPresentInfo,
                                        const ErrorObject &error_obj) const override;
    ResourceUsageRange SetupPresentInfo(const VkPresentInfoKHR &present_info, std::shared_ptr<QueueBatchContext> &batch,
                                        PresentedImages &presented_images) const;
    void PostCallRecordQueuePresentKHR(VkQueue queue, const VkPresentInfoKHR *pPresentInfo,
                                       const RecordObject &record_obj) override;
    void PostCallRecordAcquireNextImageKHR(VkDevice device, VkSwapchainKHR swapchain, uint64_t timeout, VkSemaphore semaphore,
                                           VkFence fence, uint32_t *pImageIndex, const RecordObject &record_obj) override;
    void PostCallRecordAcquireNextImage2KHR(VkDevice device, const VkAcquireNextImageInfoKHR *pAcquireInfo, uint32_t *pImageIndex,
                                            const RecordObject &record_obj) override;
    void RecordAcquireNextImageState(VkDevice device, VkSwapchainKHR swapchain, uint64_t timeout, VkSemaphore semaphore,
                                     VkFence fence, uint32_t *pImageIndex, const RecordObject &record_obj);
    bool ValidateQueueSubmit(VkQueue queue, uint32_t submitCount, const VkSubmitInfo2 *pSubmits, VkFence fence,
                             const ErrorObject &error_obj) const;
    bool PreCallValidateQueueSubmit(VkQueue queue, uint32_t submitCount, const VkSubmitInfo *pSubmits, VkFence fence,
                                    const ErrorObject &error_obj) const override;
    void RecordQueueSubmit(VkQueue queue, VkFence fence, const RecordObject &record_obj);
    void PostCallRecordQueueSubmit(VkQueue queue, uint32_t submitCount, const VkSubmitInfo *pSubmits, VkFence fence,
                                   const RecordObject &record_obj) override;
    bool PreCallValidateQueueSubmit2KHR(VkQueue queue, uint32_t submitCount, const VkSubmitInfo2KHR *pSubmits, VkFence fence,
                                        const ErrorObject &error_obj) const override;
    void PostCallRecordQueueSubmit2KHR(VkQueue queue, uint32_t submitCount, const VkSubmitInfo2KHR *pSubmits, VkFence fence,
                                       const RecordObject &record_obj) override;
    bool PreCallValidateQueueSubmit2(VkQueue queue, uint32_t submitCount, const VkSubmitInfo2KHR *pSubmits, VkFence fence,
                                     const ErrorObject &error_obj) const override;
    void PostCallRecordQueueSubmit2(VkQueue queue, uint32_t submitCount, const VkSubmitInfo2KHR *pSubmits, VkFence fence,
                                    const RecordObject &record_obj) override;
    void PostCallRecordGetFenceStatus(VkDevice device, VkFence fence, const RecordObject &record_obj) override;
    void PostCallRecordWaitForFences(VkDevice device, uint32_t fenceCount, const VkFence *pFences, VkBool32 waitAll,
                                     uint64_t timeout, const RecordObject &record_obj) override;
    void PostCallRecordGetSwapchainImagesKHR(VkDevice device, VkSwapchainKHR swapchain, uint32_t *pSwapchainImageCount,
                                             VkImage *pSwapchainImages, const RecordObject &record_obj) override;
};
