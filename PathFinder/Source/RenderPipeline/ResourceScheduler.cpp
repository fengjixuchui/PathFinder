#include "ResourceScheduler.hpp"

#include "../Foundation/Assert.hpp"

namespace PathFinder
{

    ResourceScheduler::ResourceScheduler(PipelineResourceStorage* manager, const RenderSurfaceDescription& defaultRenderSurface)
        : mResourceStorage{ manager }, mDefaultRenderSurfaceDesc{ defaultRenderSurface } {}

    void ResourceScheduler::NewRenderTarget(Foundation::Name resourceName, std::optional<NewTextureProperties> properties)
    {
        EnsureSingleSchedulingRequestForCurrentPass(resourceName);

        assert_format(!mResourceStorage->IsResourceAllocationScheduled(resourceName), "New render target has already been scheduled");

        HAL::ColorClearValue clearValue{ 0.0, 0.0, 0.0, 1.0 };
        NewTextureProperties props = FillMissingFields(properties);

        HAL::ResourceFormat::FormatVariant format = *props.ShaderVisibleFormat;

        if (props.TypelessFormat) 
        {
            format = *props.TypelessFormat;
        }

        PipelineResourceSchedulingInfo* schedulingInfo = mResourceStorage->QueueTextureAllocationIfNeeded(
            resourceName, format, *props.Kind, *props.Dimensions, clearValue, *props.MipCount
        );

        auto& passData = schedulingInfo->AllocateMetadataForPass(mResourceStorage->CurrentPassGraphNode());
        passData.RequestedState = HAL::ResourceState::RenderTarget;
        passData.CreateTextureRTDescriptor = true;

        if (props.TypelessFormat)
        {
            passData.ShaderVisibleFormat = props.ShaderVisibleFormat;
        }

        mResourceStorage->RegisterResourceNameForCurrentPass(resourceName);
    }

    void ResourceScheduler::NewDepthStencil(Foundation::Name resourceName, std::optional<NewDepthStencilProperties> properties)
    {
        EnsureSingleSchedulingRequestForCurrentPass(resourceName);

        assert_format(!mResourceStorage->IsResourceAllocationScheduled(resourceName), "New depth-stencil texture has already been scheduled");

        HAL::DepthStencilClearValue clearValue{ 1.0, 0 };
        NewDepthStencilProperties props = FillMissingFields(properties);

        PipelineResourceSchedulingInfo* schedulingInfo = mResourceStorage->QueueTextureAllocationIfNeeded(
            resourceName, *props.Format, HAL::TextureKind::Texture2D, *props.Dimensions, clearValue, 1
        );

        auto& passData = schedulingInfo->AllocateMetadataForPass(mResourceStorage->CurrentPassGraphNode());
        passData.RequestedState = HAL::ResourceState::DepthWrite;
        passData.CreateTextureDSDescriptor = true;

        mResourceStorage->RegisterResourceNameForCurrentPass(resourceName);
    }

    void ResourceScheduler::NewTexture(Foundation::Name resourceName, std::optional<NewTextureProperties> properties)
    {
        EnsureSingleSchedulingRequestForCurrentPass(resourceName);

        assert_format(!mResourceStorage->IsResourceAllocationScheduled(resourceName), "Texture creation has already been scheduled");

        HAL::ColorClearValue clearValue{ 0.0, 0.0, 0.0, 1.0 };
        NewTextureProperties props = FillMissingFields(properties);

        HAL::ResourceFormat::FormatVariant format = *props.ShaderVisibleFormat;

        if (props.TypelessFormat)
        {
            format = *props.TypelessFormat;
        }

        PipelineResourceSchedulingInfo* schedulingInfo = mResourceStorage->QueueTextureAllocationIfNeeded(
            resourceName, format, *props.Kind, *props.Dimensions, clearValue, *props.MipCount
        );

        auto& passData = schedulingInfo->AllocateMetadataForPass(mResourceStorage->CurrentPassGraphNode());
        passData.RequestedState = HAL::ResourceState::UnorderedAccess;
        passData.CreateTextureUADescriptor = true;

        if (props.TypelessFormat) 
        {
            passData.ShaderVisibleFormat = props.ShaderVisibleFormat;
        }

        mResourceStorage->RegisterResourceNameForCurrentPass(resourceName);
    }

    void ResourceScheduler::UseRenderTarget(Foundation::Name resourceName, std::optional<HAL::ColorFormat> concreteFormat)
    {
        EnsureSingleSchedulingRequestForCurrentPass(resourceName);

        assert_format(mResourceStorage->IsResourceAllocationScheduled(resourceName), "Cannot use non-scheduled render target");

        PipelineResourceSchedulingInfo* schedulingInfo = mResourceStorage->GetResourceSchedulingInfo(resourceName);
        bool isTypeless = std::holds_alternative<HAL::TypelessColorFormat>(*schedulingInfo->ResourceFormat().DataType());

        assert_format(concreteFormat || !isTypeless, "Redefinition of Render target format is not allowed");
        assert_format(!concreteFormat || isTypeless, "Render target is typeless and concrete color format was not provided");

        auto& passData = schedulingInfo->AllocateMetadataForPass(mResourceStorage->CurrentPassGraphNode());
        passData.RequestedState = HAL::ResourceState::RenderTarget;
        passData.CreateTextureRTDescriptor = true;

        if (isTypeless) passData.ShaderVisibleFormat = concreteFormat;

        mResourceStorage->RegisterResourceNameForCurrentPass(resourceName);
    }

    void ResourceScheduler::UseDepthStencil(Foundation::Name resourceName)
    {
        EnsureSingleSchedulingRequestForCurrentPass(resourceName);

        assert_format(mResourceStorage->IsResourceAllocationScheduled(resourceName), "Cannot reuse non-scheduled depth-stencil texture");

        PipelineResourceSchedulingInfo* schedulingInfo = mResourceStorage->GetResourceSchedulingInfo(resourceName);

        assert_format(std::holds_alternative<HAL::DepthStencilFormat>(*schedulingInfo->ResourceFormat().DataType()), "Cannot reuse non-depth-stencil texture");

        auto& passData = schedulingInfo->AllocateMetadataForPass(mResourceStorage->CurrentPassGraphNode());
        passData.RequestedState = HAL::ResourceState::DepthWrite;
        passData.CreateTextureDSDescriptor = true;

        mResourceStorage->RegisterResourceNameForCurrentPass(resourceName);
    }

    void ResourceScheduler::ReadTexture(Foundation::Name resourceName, std::optional<HAL::ColorFormat> concreteFormat)
    {
        EnsureSingleSchedulingRequestForCurrentPass(resourceName);

        assert_format(mResourceStorage->IsResourceAllocationScheduled(resourceName), "Cannot read non-scheduled texture");

        PipelineResourceSchedulingInfo* schedulingInfo = mResourceStorage->GetResourceSchedulingInfo(resourceName);

        bool isTypeless = std::holds_alternative<HAL::TypelessColorFormat>(*schedulingInfo->ResourceFormat().DataType());

        assert_format(concreteFormat || !isTypeless, "Redefinition of texture format is not allowed");
        assert_format(!concreteFormat || isTypeless, "Texture is typeless and concrete color format was not provided");

        auto& passData = schedulingInfo->AllocateMetadataForPass(mResourceStorage->CurrentPassGraphNode());
        passData.RequestedState = HAL::ResourceState::PixelShaderAccess | HAL::ResourceState::NonPixelShaderAccess;

        if (std::holds_alternative<HAL::DepthStencilFormat>(*schedulingInfo->ResourceFormat().DataType()))
        {
            passData.RequestedState |= HAL::ResourceState::DepthRead;
        } 

        if (isTypeless) passData.ShaderVisibleFormat = concreteFormat;

        passData.CreateTextureSRDescriptor = true;

        mResourceStorage->RegisterResourceNameForCurrentPass(resourceName);
    }

    void ResourceScheduler::ReadWriteTexture(Foundation::Name resourceName, std::optional<HAL::ColorFormat> concreteFormat)
    {
        EnsureSingleSchedulingRequestForCurrentPass(resourceName);

        assert_format(mResourceStorage->IsResourceAllocationScheduled(resourceName), "Cannot read/write non-scheduled texture");

        PipelineResourceSchedulingInfo* schedulingInfo = mResourceStorage->GetResourceSchedulingInfo(resourceName);
        bool isTypeless = std::holds_alternative<HAL::TypelessColorFormat>(*schedulingInfo->ResourceFormat().DataType());

        assert_format(concreteFormat || !isTypeless, "Redefinition of texture format is not allowed");
        assert_format(!concreteFormat || isTypeless, "Texture is typeless and concrete color format was not provided");

        auto& passData = schedulingInfo->AllocateMetadataForPass(mResourceStorage->CurrentPassGraphNode());
        passData.RequestedState = HAL::ResourceState::UnorderedAccess;
        passData.CreateTextureUADescriptor = true;

        if (isTypeless) passData.ShaderVisibleFormat = concreteFormat;

        mResourceStorage->RegisterResourceNameForCurrentPass(resourceName);
    }

    void ResourceScheduler::ReadBuffer(Foundation::Name resourceName, BufferReadContext readContext)
    {

    }

    void ResourceScheduler::ReadWriteBuffer(Foundation::Name resourceName)
    {

    }

    ResourceScheduler::NewTextureProperties ResourceScheduler::FillMissingFields(std::optional<NewTextureProperties> properties)
    {
        NewTextureProperties filledProperties{
            HAL::TextureKind::Texture2D,
            mDefaultRenderSurfaceDesc.Dimensions(),
            mDefaultRenderSurfaceDesc.RenderTargetFormat(),
            std::nullopt,
            1
        };

        if (properties)
        {
            if (properties->Kind) filledProperties.Kind = *properties->Kind;
            if (properties->Dimensions) filledProperties.Dimensions = *properties->Dimensions;
            if (properties->ShaderVisibleFormat) filledProperties.ShaderVisibleFormat = *properties->ShaderVisibleFormat;
            if (properties->TypelessFormat) filledProperties.TypelessFormat = *properties->TypelessFormat;
            if (properties->MipCount) filledProperties.MipCount = *properties->MipCount;
        }

        return filledProperties;
    }

    ResourceScheduler::NewDepthStencilProperties ResourceScheduler::FillMissingFields(std::optional<NewDepthStencilProperties> properties)
    {
        NewDepthStencilProperties filledProperties{
            mDefaultRenderSurfaceDesc.DepthStencilFormat(),
            mDefaultRenderSurfaceDesc.Dimensions()
        };

        if (properties)
        {
            if (properties->Format) filledProperties.Format = *properties->Format;
            if (properties->Dimensions) filledProperties.Dimensions = *properties->Dimensions;
        }

        return filledProperties;
    }

    void ResourceScheduler::EnsureSingleSchedulingRequestForCurrentPass(ResourceName resourceName)
    {
        const auto& names = mResourceStorage->ScheduledResourceNamesForCurrentPass();
        bool isResourceScheduledInCurrentPass = names.find(resourceName) != names.end();
        assert_format(!isResourceScheduledInCurrentPass, "Resource ", resourceName.ToString(), " is already scheduled for this pass. Resources can only be scheduled once per pass");
    }

}
