#include "BackBufferOutputPass.hpp"

namespace PathFinder
{

    BackBufferOutputPass::BackBufferOutputPass()
        : RenderPass("BackBufferOutput") {}

    void BackBufferOutputPass::SetupPipelineStates(PipelineStateCreator* stateCreator, RootSignatureCreator* rootSignatureCreator)
    {
        stateCreator->CreateGraphicsState(PSONames::BackBufferOutput, [](GraphicsStateProxy& state)
        {
            state.ShaderFileNames.VertexShaderFileName = L"BackBufferOutput.hlsl";
            state.ShaderFileNames.PixelShaderFileName = L"BackBufferOutput.hlsl";
            state.PrimitiveTopology = HAL::PrimitiveTopology::TriangleStrip;
            state.DepthStencilState.SetDepthTestEnabled(false);
            state.RenderTargetFormats = { HAL::ColorFormat::RGBA8_Usigned_Norm };
        });
    }
     
    void BackBufferOutputPass::ScheduleResources(ResourceScheduler* scheduler)
    { 
        scheduler->ReadTexture(ResourceNames::ToneMappingOutput);
    } 

    void BackBufferOutputPass::Render(RenderContext<RenderPassContentMediator>* context)
    {
        context->GetCommandRecorder()->ApplyPipelineState(PSONames::BackBufferOutput);
        context->GetCommandRecorder()->SetBackBufferAsRenderTarget();
    
        BackBufferOutputPassData cbContent;
        cbContent.SourceTextureIndex = context->GetResourceProvider()->GetTextureDescriptorTableIndex(ResourceNames::ToneMappingOutput);

        context->GetConstantsUpdater()->UpdateRootConstantBuffer(cbContent);
        context->GetCommandRecorder()->Draw(DrawablePrimitive::Quad());
    }

}
