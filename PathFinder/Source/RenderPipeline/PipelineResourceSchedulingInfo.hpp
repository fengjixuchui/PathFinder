#pragma once

#include "../Foundation/Name.hpp"
#include "../HardwareAbstractionLayer/ResourceState.hpp"
#include "../HardwareAbstractionLayer/ResourceFormat.hpp"

#include "RenderPassGraph.hpp"

#include <functional>
#include <optional>

namespace PathFinder
{

    /// A helper class to hold all info necessary for resource allocation.
    /// Filled by resource scheduling infrastructure.
    class PipelineResourceSchedulingInfo
    {
    public:
        class SubresourceInfo
        {
        public:
            using RequestedAccessFlag = uint16_t;

            HAL::ResourceState RequestedState = HAL::ResourceState::Common;
            std::optional<HAL::ColorFormat> ShaderVisibleFormat;

            void SetTextureRTRequested() { mAccessFlag = 1 << 0; }
            void SetTextureDSRequested() { mAccessFlag = 1 << 1; }
            void SetTextureSRRequested() { mAccessFlag = 1 << 2; }
            void SetTextureUARequested() { mAccessFlag = 1 << 3; }
            void SetBufferCBRequested() { mAccessFlag = 1 << 4; }
            void SetBufferSRRequested() { mAccessFlag = 1 << 5; }
            void SetBufferUARequested() { mAccessFlag = 1 << 6; }

            bool IsTextureRTRequested() const { return mAccessFlag & 1 << 0; }
            bool IsTextureDSRequested() const { return mAccessFlag & 1 << 1; }
            bool IsTextureSRRequested() const { return mAccessFlag & 1 << 2; }
            bool IsTextureUARequested() const { return mAccessFlag & 1 << 3; }
            bool IsBufferCBRequested() const { return mAccessFlag & 1 << 4; }
            bool IsBufferSRRequested() const { return mAccessFlag & 1 << 5; }
            bool IsBufferUARequested() const { return mAccessFlag & 1 << 6; }

        private:
            RequestedAccessFlag mAccessFlag;
        };

        struct PassInfo
        {
            std::vector<std::optional<SubresourceInfo>> SubresourceInfos;
            bool NeedsUnorderedAccessBarrier = false;
            bool NeedsAliasingBarrier = false;
        };

        PipelineResourceSchedulingInfo(Foundation::Name resourceName, const HAL::ResourceFormat& format);

        void FinishScheduling();
        const PassInfo* GetInfoForPass(Foundation::Name passName) const;
        PassInfo* GetInfoForPass(Foundation::Name passName);
        PassInfo& AllocateInfoForPass(Foundation::Name passName);
        HAL::ResourceState GetSubresourceCombinedReadStates(uint64_t subresourceIndex) const;
        HAL::ResourceState GetSubresourceWriteState(uint64_t subresourceIndex) const;

        uint64_t HeapOffset = 0;
        bool CanBeAliased = true;

    private:
        std::unordered_map<Foundation::Name, PassInfo> mPassInfoMap;
        HAL::ResourceFormat mResourceFormat;
        HAL::ResourceState mExpectedStates = HAL::ResourceState::Common;
        Foundation::Name mResourceName;
        uint64_t mSubresourceCount = 0;

        // Since engine is designed to make only one write per subresource in a frame
        // we can store the single write state and batch all read states
        std::vector<HAL::ResourceState> mSubresourceCombinedReadStates;
        std::vector<HAL::ResourceState> mSubresourceWriteStates;

    public:
        inline const HAL::ResourceFormat& ResourceFormat() const { return mResourceFormat; }
        inline HAL::ResourceState ExpectedStates() const { return mExpectedStates; }
        inline Foundation::Name ResourceName() const { return mResourceName; }
        inline auto SubresourceCount() const { return mSubresourceCount; }
        inline auto TotalRequiredMemory() const { return mResourceFormat.ResourceSizeInBytes(); }
    };

}
