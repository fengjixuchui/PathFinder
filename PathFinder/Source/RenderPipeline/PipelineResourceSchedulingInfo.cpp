#include "PipelineResourceSchedulingInfo.hpp"

#include "../Foundation/Assert.hpp"

namespace PathFinder
{

    PipelineResourceSchedulingInfo::PipelineResourceSchedulingInfo(Foundation::Name resourceName, const HAL::ResourceFormat& format)
        : mResourceFormat{ format }, mResourceName{ resourceName }, mSubresourceCount{ format.SubresourceCount() } {}

    void PipelineResourceSchedulingInfo::FinishScheduling()
    {
        HAL::ResourceState expectedStates = HAL::ResourceState::Common;

        mSubresourceCombinedReadStates.resize(mSubresourceCount);
        mSubresourceWriteStates.resize(mSubresourceCount);

        for (auto& [passName, info] : mPassInfoMap)
        {
            for (auto subresourceIdx = 0; subresourceIdx < mSubresourceCount; ++subresourceIdx)
            {
                std::optional<SubresourceInfo>& subresourceInfo = info.SubresourceInfos[subresourceIdx];

                if (!subresourceInfo)
                {
                    continue;
                }

                expectedStates |= subresourceInfo->RequestedState;

                if (EnumMaskBitSet(expectedStates, HAL::ResourceState::UnorderedAccess))
                {
                    info.NeedsUnorderedAccessBarrier = true;
                }

                if (IsResourceStateReadOnly(subresourceInfo->RequestedState))
                {
                    mSubresourceCombinedReadStates[subresourceIdx] |= subresourceInfo->RequestedState;
                }
                else
                {
                    assert_format(mSubresourceWriteStates[subresourceIdx] == HAL::ResourceState::Common,
                        "One write state is already requested. Engine architecture allows one write per frame.");

                    mSubresourceWriteStates[subresourceIdx] = subresourceInfo->RequestedState;
                }
            }

        }

        mExpectedStates = expectedStates;
        mResourceFormat.SetExpectedStates(expectedStates);
    }

    const PipelineResourceSchedulingInfo::PassInfo* PipelineResourceSchedulingInfo::GetInfoForPass(Foundation::Name passName) const
    {
        auto it = mPassInfoMap.find(passName);
        return it != mPassInfoMap.end() ? &it->second : nullptr;
    }

    PipelineResourceSchedulingInfo::PassInfo* PipelineResourceSchedulingInfo::GetInfoForPass(Foundation::Name passName)
    {
        auto it = mPassInfoMap.find(passName);
        return it != mPassInfoMap.end() ? &it->second : nullptr;
    }

    PipelineResourceSchedulingInfo::PassInfo& PipelineResourceSchedulingInfo::AllocateInfoForPass(Foundation::Name passName)
    {
        auto [it, success] = mPassInfoMap.emplace(passName, PassInfo{});
        it->second.SubresourceInfos.resize(mSubresourceCount);
        return it->second;
    }

    HAL::ResourceState PipelineResourceSchedulingInfo::GetSubresourceCombinedReadStates(uint64_t subresourceIndex) const
    {
        return mSubresourceCombinedReadStates[subresourceIndex];
    }

    HAL::ResourceState PipelineResourceSchedulingInfo::GetSubresourceWriteState(uint64_t subresourceIndex) const
    {
        return mSubresourceWriteStates[subresourceIndex];
    }

}
