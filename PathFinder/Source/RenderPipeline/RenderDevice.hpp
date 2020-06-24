#pragma once

#include "RenderSurfaceDescription.hpp"
#include "PipelineResourceStorage.hpp"
#include "PipelineStateManager.hpp"
#include "RenderPassMetadata.hpp"

#include "../Foundation/Name.hpp"

#include "../Geometry/Dimensions.hpp"

#include "../HardwareAbstractionLayer/ShaderRegister.hpp"
#include "../HardwareAbstractionLayer/Resource.hpp"
#include "../HardwareAbstractionLayer/CommandQueue.hpp"
#include "../HardwareAbstractionLayer/RayTracingAccelerationStructure.hpp"
#include "../HardwareAbstractionLayer/Viewport.hpp"
#include "../HardwareAbstractionLayer/RenderTarget.hpp"

#include "../Memory/PoolCommandListAllocator.hpp"
#include "../Memory/GPUResource.hpp"
#include "../Memory/ResourceStateTracker.hpp"

#include "DrawablePrimitive.hpp"

namespace PathFinder
{

    class RenderDevice
    {
    public:
        RenderDevice(
            const HAL::Device& device,
            const HAL::CBSRUADescriptorHeap* universalGPUDescriptorHeap,
            Memory::PoolCommandListAllocator* commandListAllocator,
            Memory::ResourceStateTracker* resourceStateTracker,
            PipelineResourceStorage* resourceStorage,
            PipelineStateManager* pipelineStateManager,
            const RenderPassGraph* renderPassGraph,
            const RenderSurfaceDescription& defaultRenderSurface
        );

        void ApplyPipelineState(const RenderPassGraph::Node* passNode, Foundation::Name psoName);

        void SetRenderTarget(const RenderPassGraph::Node* passNode, Foundation::Name rtName, std::optional<Foundation::Name> dsName = std::nullopt);
        void SetBackBufferAsRenderTarget(const RenderPassGraph::Node* passNode, std::optional<Foundation::Name> dsName = std::nullopt);
        void ClearRenderTarget(const RenderPassGraph::Node* passNode, Foundation::Name rtName);
        void ClearDepth(const RenderPassGraph::Node* passNode, Foundation::Name dsName);
        void SetViewport(const RenderPassGraph::Node* passNode, const HAL::Viewport& viewport);
        void Draw(const RenderPassGraph::Node* passNode, uint32_t vertexCount, uint32_t instanceCount = 1);
        void Draw(const RenderPassGraph::Node* passNode, const DrawablePrimitive& primitive);
        void Dispatch(const RenderPassGraph::Node* passNode, uint32_t groupCountX, uint32_t groupCountY = 1, uint32_t groupCountZ = 1);
        void DispatchRays(const RenderPassGraph::Node* passNode, uint32_t width, uint32_t height = 1, uint32_t depth = 1);
        void BindBuffer(const RenderPassGraph::Node* passNode, Foundation::Name resourceName, uint16_t shaderRegister, uint16_t registerSpace, HAL::ShaderRegister registerType);
        void BindExternalBuffer(const RenderPassGraph::Node* passNode, const Memory::Buffer& resource, uint16_t shaderRegister, uint16_t registerSpace, HAL::ShaderRegister registerType);

        template <class T>
        void SetRootConstants(const RenderPassGraph::Node* passNode, const T& constants, uint16_t shaderRegister, uint16_t registerSpace);

        template <size_t RTCount>
        void SetRenderTargets(const RenderPassGraph::Node* passNode, const std::array<Foundation::Name, RTCount>& rtNames, std::optional<Foundation::Name> dsName = std::nullopt);

        void SetBackBuffer(Memory::Texture* backBuffer);

        void AllocateUploadCommandList();
        void AllocateRTASBuildsCommandList();
        void AllocateWorkerCommandLists();

        void BatchCommandLists();
        void ExetuteCommandLists();
        void UploadPassConstants();

    private:
        using GraphicsCommandListPtr = Memory::PoolCommandListAllocator::GraphicsCommandListPtr;
        using ComputeCommandListPtr = Memory::PoolCommandListAllocator::ComputeCommandListPtr;
        using CommandListPtrVariant = std::variant<GraphicsCommandListPtr, ComputeCommandListPtr>;
        using HALCommandListPtrVariant = std::variant<HAL::GraphicsCommandList*, HAL::ComputeCommandList*>;

        struct PassCommandLists
        {
            CommandListPtrVariant TransitionsCommandList = GraphicsCommandListPtr{ nullptr };
            CommandListPtrVariant WorkCommandList = GraphicsCommandListPtr{ nullptr };
        };

        struct PassHelpers
        {
            // Keep a set of UAV barriers to be inserted between draws/dispatches
            HAL::ResourceBarrierCollection UAVBarriers;

            // Storage for pass constant buffer and its information
            PipelineResourceStoragePass* ResourceStoragePassData = nullptr;

            // Helpers for correct bindings and sanity checks
            const HAL::RayDispatchInfo* LastAppliedRTStateDispatchInfo = nullptr;
            std::optional<HAL::Viewport> LastAppliedViewport;
            uint64_t ExecutedRenderCommandsCount = 0;
            const HAL::RootSignature* LastSetRootSignature = nullptr;
            HAL::GPUAddress LastBoundRootConstantBufferAddress = 0;
            std::optional<PipelineStateManager::PipelineStateVariant> LastSetPipelineState;
        };

        struct CommandListBatch
        {
            std::vector<HALCommandListPtrVariant> CommandLists;
            std::unordered_set<const HAL::Fence*> FencesToWait;
            HAL::Fence* FenceToSignal = nullptr;
        };

        struct SubresourceTransitionInfo
        {
            RenderPassGraph::SubresourceName SubresourceName;
            HAL::ResourceTransitionBarrier TransitionBarrier;
            const HAL::Resource* Resource;
        };

        struct SubresourcePreviousTransitionInfo
        {
            const RenderPassGraph::Node* Node;
            uint64_t CommandListBatchIndex = 0;
        };

        void ApplyState(const RenderPassGraph::Node* passNode, const HAL::GraphicsPipelineState* state);
        void ApplyState(const RenderPassGraph::Node* passNode, const HAL::ComputePipelineState* state);
        void ApplyState(const RenderPassGraph::Node* passNode, const HAL::RayTracingPipelineState* state, const HAL::RayDispatchInfo* dispatchInfo);

        void BindGraphicsCommonResources(const RenderPassGraph::Node* passNode, const HAL::RootSignature* rootSignature, HAL::GraphicsCommandListBase* cmdList);
        void BindComputeCommonResources(const RenderPassGraph::Node* passNode, const HAL::RootSignature* rootSignature, HAL::ComputeCommandListBase* cmdList);
        void BindGraphicsPassRootConstantBuffer(const RenderPassGraph::Node* passNode, HAL::GraphicsCommandListBase* cmdList);
        void BindComputePassRootConstantBuffer(const RenderPassGraph::Node* passNode, HAL::ComputeCommandListBase* cmdList);

        void CreatePassHelpers();
        void GatherResourceTransitionKnowledge(const RenderPassGraph::DependencyLevel& dependencyLevel);
        void CollectNodeTransitions(const RenderPassGraph::Node* node, uint64_t currentCommandListBatchIndex, HAL::ResourceBarrierCollection& collection);
        void BatchCommandListsWithTransitionRerouting(const RenderPassGraph::DependencyLevel& dependencyLevel);
        void BatchCommandListsWithoutTransitionRerouting(const RenderPassGraph::DependencyLevel& dependencyLevel);
        void RecordBeginBarriers();

        bool IsStateTransitionSupportedOnQueue(uint64_t queueIndex, HAL::ResourceState beforeState, HAL::ResourceState afterState) const;
        bool IsStateTransitionSupportedOnQueue(uint64_t queueIndex, HAL::ResourceState afterState) const;
        HAL::CommandQueue& GetCommandQueue(uint64_t queueIndex);
        uint64_t FindMostCompetentQueueIndex(const std::unordered_set<RenderPassGraph::Node::QueueIndex>& queueIndices) const;
        CommandListPtrVariant AllocateCommandListForQueue(uint64_t queueIndex) const;
        HAL::ComputeCommandListBase* GetComputeCommandListBase(CommandListPtrVariant& variant) const;
        HALCommandListPtrVariant GetHALCommandListVariant(CommandListPtrVariant& variant) const;

        HAL::Fence& FenceForQueueIndex(uint64_t index);

        const HAL::CBSRUADescriptorHeap* mUniversalGPUDescriptorHeap;
        Memory::PoolCommandListAllocator* mCommandListAllocator;
        Memory::ResourceStateTracker* mResourceStateTracker;
        PipelineResourceStorage* mResourceStorage;
        PipelineStateManager* mPipelineStateManager;
        const RenderPassGraph* mRenderPassGraph;
        RenderSurfaceDescription mDefaultRenderSurface;

        Memory::Texture* mBackBuffer = nullptr;
        Memory::PoolCommandListAllocator::GraphicsCommandListPtr mPreRenderUploadsCommandList;
        Memory::PoolCommandListAllocator::ComputeCommandListPtr mRTASBuildsCommandList;
        std::vector<PassCommandLists> mPassCommandLists;
        std::vector<CommandListPtrVariant> mReroutedTransitionsCommandLists;
        std::vector<std::vector<CommandListBatch>> mCommandListBatches;
        std::vector<PassHelpers> mPassHelpers;
        HAL::GraphicsCommandQueue mGraphicsQueue;
        HAL::ComputeCommandQueue mComputeQueue;

        HAL::Fence mGraphicsQueueFence;
        HAL::Fence mComputeQueueFence;
        uint64_t mQueueCount = 2;
        uint64_t mBVHBuildsQueueIndex = 1;

        // Keep track of nodes where transitions previously occurred to insert Begin part of split barriers there
        std::unordered_map<RenderPassGraph::SubresourceName, SubresourcePreviousTransitionInfo> mSubresourcesPreviousTransitionInfo;

        // Keep list of separate barriers gathered for dependency level so we could cull them, if conditions are met, when command list batches are determined
        std::vector<std::vector<SubresourceTransitionInfo>> mDependencyLevelTransitionBarriers;

        // Gather aliasing barriers required by each node in a dependency level
        std::vector<std::vector<HAL::ResourceAliasingBarrier>> mDependencyLevelAliasingBarriers;

        // Keep track of queues inside a graph dependency layer that require transition rerouting
        std::unordered_set<RenderPassGraph::Node::QueueIndex> mDependencyLevelQueuesThatRequireTransitionRerouting;

        // Collect begin barriers for passes that may issue them to be applied in batches after all nodes are processed
        std::vector<HAL::ResourceBarrierCollection> mPerNodeBeginBarriers;

    public:
        inline HAL::GraphicsCommandQueue& GraphicsCommandQueue() { return mGraphicsQueue; }
        inline HAL::ComputeCommandQueue& ComputeCommandQueue() { return mComputeQueue; }
        inline HAL::GraphicsCommandList* PreRenderUploadsCommandList() { return mPreRenderUploadsCommandList.get(); }
        inline HAL::ComputeCommandList* RTASBuildsCommandList() { return mRTASBuildsCommandList.get(); }
    };

}

#include "RenderDevice.inl"