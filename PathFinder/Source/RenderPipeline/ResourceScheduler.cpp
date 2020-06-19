#include "ResourceScheduler.hpp"

#include "../Foundation/Assert.hpp"

namespace PathFinder
{

    ResourceScheduler::ResourceScheduler(PipelineResourceStorage* manager, RenderPassUtilityProvider* utilityProvider, RenderPassGraph* passGraph)
        : mResourceStorage{ manager }, mUtilityProvider{ utilityProvider }, mRenderPassGraph{ passGraph } {}

    void ResourceScheduler::NewRenderTarget(Foundation::Name resourceName, std::optional<NewTextureProperties> properties)
    {
        NewTextureProperties props = FillMissingFields(properties);
        mCurrentlySchedulingPassNode->AddWriteDependency(resourceName, 1);

        bool canBeReadAcrossFrames = EnumMaskBitSet(properties->Flags, ReadFlags::CrossFrameRead);

        HAL::ResourceFormat::FormatVariant format = *props.ShaderVisibleFormat;
        if (props.TypelessFormat) format = *props.TypelessFormat;

        mResourceStorage->QueueTexturesAllocationIfNeeded(
            resourceName, format, *props.Kind, *props.Dimensions, *props.ClearValues, props.MipCount, 

            [canBeReadAcrossFrames,
            typelessFormat = props.TypelessFormat,
            shaderVisibleFormat = props.ShaderVisibleFormat, 
            passName = mCurrentlySchedulingPassNode->PassMetadata().Name]
            (PipelineResourceSchedulingInfo& schedulingInfo)
            {
                PipelineResourceSchedulingInfo::PassInfo& passInfo = schedulingInfo.AllocateInfoForPass(passName);
                passInfo.SubresourceInfos[0] = PipelineResourceSchedulingInfo::SubresourceInfo{};
                passInfo.SubresourceInfos[0]->SetTextureRTRequested();
                passInfo.SubresourceInfos[0]->RequestedState = HAL::ResourceState::RenderTarget;

                schedulingInfo.CanBeAliased = !canBeReadAcrossFrames;

                if (typelessFormat)
                {
                    passInfo.SubresourceInfos[0]->ShaderVisibleFormat = shaderVisibleFormat;
                }
            }
        );
    }

    void ResourceScheduler::NewDepthStencil(Foundation::Name resourceName, std::optional<NewDepthStencilProperties> properties)
    {
        NewDepthStencilProperties props = FillMissingFields(properties);
        mCurrentlySchedulingPassNode->AddWriteDependency(resourceName, 1);

        bool canBeReadAcrossFrames = EnumMaskBitSet(properties->Flags, ReadFlags::CrossFrameRead);

        HAL::DepthStencilClearValue clearValue{ 1.0, 0 };

        mResourceStorage->QueueTexturesAllocationIfNeeded(
            resourceName, *props.Format, HAL::TextureKind::Texture2D, *props.Dimensions, clearValue, props.MipCount,

            [canBeReadAcrossFrames,
            passName = mCurrentlySchedulingPassNode->PassMetadata().Name]
            (PipelineResourceSchedulingInfo& schedulingInfo)
            {
                PipelineResourceSchedulingInfo::PassInfo& passInfo = schedulingInfo.AllocateInfoForPass(passName);
                passInfo.SubresourceInfos[0] = PipelineResourceSchedulingInfo::SubresourceInfo{};
                passInfo.SubresourceInfos[0]->SetTextureDSRequested();
                passInfo.SubresourceInfos[0]->RequestedState = HAL::ResourceState::DepthWrite;

                schedulingInfo.CanBeAliased = !canBeReadAcrossFrames;
            }
        );
    }

    void ResourceScheduler::NewTexture(Foundation::Name resourceName, std::optional<NewTextureProperties> properties)
    {
        NewTextureProperties props = FillMissingFields(properties);
        mCurrentlySchedulingPassNode->AddWriteDependency(resourceName, 1);

        bool canBeReadAcrossFrames = EnumMaskBitSet(properties->Flags, ReadFlags::CrossFrameRead);

        HAL::ResourceFormat::FormatVariant format = *props.ShaderVisibleFormat;
        if (props.TypelessFormat) format = *props.TypelessFormat;

        mResourceStorage->QueueTexturesAllocationIfNeeded(
            resourceName, format, *props.Kind, *props.Dimensions, *props.ClearValues, props.MipCount,

            [canBeReadAcrossFrames,
            typelessFormat = props.TypelessFormat,
            shaderVisibleFormat = props.ShaderVisibleFormat,
            passName = mCurrentlySchedulingPassNode->PassMetadata().Name]
            (PipelineResourceSchedulingInfo& schedulingInfo)
            {
                PipelineResourceSchedulingInfo::PassInfo& passInfo = schedulingInfo.AllocateInfoForPass(passName);
                passInfo.SubresourceInfos[0] = PipelineResourceSchedulingInfo::SubresourceInfo{};
                passInfo.SubresourceInfos[0]->SetTextureUARequested();
                passInfo.SubresourceInfos[0]->RequestedState = HAL::ResourceState::UnorderedAccess;

                schedulingInfo.CanBeAliased = !canBeReadAcrossFrames;

                if (typelessFormat)
                {
                    passInfo.SubresourceInfos[0]->ShaderVisibleFormat = shaderVisibleFormat;
                }
            }
        );
    }

    void ResourceScheduler::UseRenderTarget(Foundation::Name resourceName, const MipList& mips, std::optional<HAL::ColorFormat> concreteFormat)
    {
        mCurrentlySchedulingPassNode->AddWriteDependency(resourceName, mips);

        mResourceStorage->QueueResourceUsage(resourceName, [mips, concreteFormat, passName = mCurrentlySchedulingPassNode->PassMetadata().Name](PipelineResourceSchedulingInfo& schedulingInfo)
        {
            bool isTypeless = std::holds_alternative<HAL::TypelessColorFormat>(*schedulingInfo.ResourceFormat().DataType());

            assert_format(concreteFormat || !isTypeless, "Redefinition of Render target format is not allowed");
            assert_format(!concreteFormat || isTypeless, "Render target is typeless and concrete color format was not provided");

            for (auto mipLevel : mips)
            {
                PipelineResourceSchedulingInfo::PassInfo& passInfo = schedulingInfo.AllocateInfoForPass(passName);
                passInfo.SubresourceInfos[mipLevel] = PipelineResourceSchedulingInfo::SubresourceInfo{};
                passInfo.SubresourceInfos[mipLevel]->SetTextureRTRequested();
                passInfo.SubresourceInfos[mipLevel]->RequestedState = HAL::ResourceState::RenderTarget;

                if (isTypeless)
                {
                    passInfo.SubresourceInfos[mipLevel]->ShaderVisibleFormat = concreteFormat;
                }
            }
        });
    }

    void ResourceScheduler::UseDepthStencil(Foundation::Name resourceName)
    {
        mCurrentlySchedulingPassNode->AddWriteDependency(resourceName, 1);

        mResourceStorage->QueueResourceUsage(resourceName, [passName = mCurrentlySchedulingPassNode->PassMetadata().Name](PipelineResourceSchedulingInfo& schedulingInfo)
        {
            assert_format(std::holds_alternative<HAL::DepthStencilFormat>(*schedulingInfo.ResourceFormat().DataType()), "Cannot reuse non-depth-stencil texture");

            PipelineResourceSchedulingInfo::PassInfo& passInfo = schedulingInfo.AllocateInfoForPass(passName);
            passInfo.SubresourceInfos[0] = PipelineResourceSchedulingInfo::SubresourceInfo{};
            passInfo.SubresourceInfos[0]->SetTextureDSRequested();
            passInfo.SubresourceInfos[0]->RequestedState = HAL::ResourceState::DepthWrite;
        });
    }

    void ResourceScheduler::ReadTexture(Foundation::Name resourceName, const MipList& mips, std::optional<HAL::ColorFormat> concreteFormat)
    {
        mCurrentlySchedulingPassNode->AddReadDependency(resourceName, mips);
        
        mResourceStorage->QueueResourceUsage(resourceName,
            [mips,
            concreteFormat, 
            passName = mCurrentlySchedulingPassNode->PassMetadata().Name]
        (PipelineResourceSchedulingInfo& schedulingInfo)
        {
            bool isTypeless = std::holds_alternative<HAL::TypelessColorFormat>(*schedulingInfo.ResourceFormat().DataType());

            assert_format(concreteFormat || !isTypeless, "Redefinition of texture format is not allowed");

            for (auto mipLevel : mips)
            {
                PipelineResourceSchedulingInfo::PassInfo& passInfo = schedulingInfo.AllocateInfoForPass(passName);
                passInfo.SubresourceInfos[mipLevel] = PipelineResourceSchedulingInfo::SubresourceInfo{};
                passInfo.SubresourceInfos[mipLevel]->SetTextureSRRequested();
                passInfo.SubresourceInfos[mipLevel]->RequestedState = HAL::ResourceState::AnyShaderAccess;

                if (std::holds_alternative<HAL::DepthStencilFormat>(*schedulingInfo.ResourceFormat().DataType()))
                {
                    passInfo.SubresourceInfos[mipLevel]->RequestedState |= HAL::ResourceState::DepthRead;
                }

                if (isTypeless)
                {
                    passInfo.SubresourceInfos[mipLevel]->ShaderVisibleFormat = concreteFormat;
                }
            }
        });
    }

    void ResourceScheduler::WriteTexture(Foundation::Name resourceName, const MipList& mips, std::optional<HAL::ColorFormat> concreteFormat)
    {
        mCurrentlySchedulingPassNode->AddWriteDependency(resourceName, mips);

        mResourceStorage->QueueResourceUsage(resourceName, [mips, concreteFormat, passName = mCurrentlySchedulingPassNode->PassMetadata().Name](PipelineResourceSchedulingInfo& schedulingInfo)
        {
            bool isTypeless = std::holds_alternative<HAL::TypelessColorFormat>(*schedulingInfo.ResourceFormat().DataType());

            assert_format(concreteFormat || !isTypeless, "Redefinition of texture format is not allowed");
            assert_format(!concreteFormat || isTypeless, "Texture is typeless and concrete color format was not provided");

            for (auto mipLevel : mips)
            {
                PipelineResourceSchedulingInfo::PassInfo& passInfo = schedulingInfo.AllocateInfoForPass(passName);
                passInfo.SubresourceInfos[mipLevel] = PipelineResourceSchedulingInfo::SubresourceInfo{};
                passInfo.SubresourceInfos[mipLevel]->SetTextureUARequested();
                passInfo.SubresourceInfos[mipLevel]->RequestedState = HAL::ResourceState::UnorderedAccess;

                if (isTypeless)
                {
                    passInfo.SubresourceInfos[mipLevel]->ShaderVisibleFormat = concreteFormat;
                }
            }
        });
    }

    void ResourceScheduler::ReadBuffer(Foundation::Name resourceName, BufferReadContext readContext)
    {
        assert_format(false, "Not implemented");
    }

    void ResourceScheduler::WriteBuffer(Foundation::Name resourceName)
    {
        assert_format(false, "Not implemented");
    }

    void ResourceScheduler::ExecuteOnQueue(RenderPassExecutionQueue queue)
    {
        mCurrentlySchedulingPassNode->ExecutionQueueIndex = std::underlying_type_t<RenderPassExecutionQueue>(queue);
    }

    void ResourceScheduler::UseRayTracing()
    {
        mCurrentlySchedulingPassNode->UsesRayTracing = true;
    }

    void ResourceScheduler::SetCurrentlySchedulingPassNode(RenderPassGraph::Node* node)
    {
        mCurrentlySchedulingPassNode = node;
    }

    ResourceScheduler::NewTextureProperties ResourceScheduler::FillMissingFields(std::optional<NewTextureProperties> properties)
    {
        NewTextureProperties filledProperties{
            mUtilityProvider->DefaultRenderSurfaceDescription.RenderTargetFormat(),
            HAL::TextureKind::Texture2D,
            mUtilityProvider->DefaultRenderSurfaceDescription.Dimensions(),
            std::nullopt,
            HAL::ColorClearValue{ 0, 0, 0, 1 },
            1
        };

        if (properties)
        {
            if (properties->Kind) filledProperties.Kind = properties->Kind;
            if (properties->Dimensions) filledProperties.Dimensions = properties->Dimensions;
            if (properties->ShaderVisibleFormat) filledProperties.ShaderVisibleFormat = properties->ShaderVisibleFormat;
            if (properties->TypelessFormat) filledProperties.TypelessFormat = properties->TypelessFormat;
            if (properties->ClearValues) filledProperties.ClearValues = properties->ClearValues;
            if (properties->MipCount) filledProperties.MipCount = properties->MipCount;
        }

        return filledProperties;
    }

    ResourceScheduler::NewDepthStencilProperties ResourceScheduler::FillMissingFields(std::optional<NewDepthStencilProperties> properties)
    {
        NewDepthStencilProperties filledProperties{
            mUtilityProvider->DefaultRenderSurfaceDescription.DepthStencilFormat(),
            mUtilityProvider->DefaultRenderSurfaceDescription.Dimensions(),
            1
        };

        if (properties)
        {
            if (properties->Format) filledProperties.Format = properties->Format;
            if (properties->Dimensions) filledProperties.Dimensions = properties->Dimensions;
            if (properties->MipCount) filledProperties.MipCount = properties->MipCount;
        }

        return filledProperties;
    }

}
