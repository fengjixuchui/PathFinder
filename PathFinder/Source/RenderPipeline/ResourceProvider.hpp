#pragma once

#include "PipelineResourceStorage.hpp"
#include "RenderPassGraph.hpp"

#include "../Foundation/Name.hpp"
#include "../HardwareAbstractionLayer/ShaderRegister.hpp"

namespace PathFinder
{

    class ResourceProvider
    {
    public:
        ResourceProvider(const PipelineResourceStorage* storage, const RenderPassGraph::Node* passNode);
       
        uint32_t GetUATextureIndex(Foundation::Name textureName, uint8_t mipLevel = 0) const;
        uint32_t GetSRTextureIndex(Foundation::Name textureName, uint8_t mipLevel = 0) const;
        const HAL::Texture::Properties& GetTextureProperties(Foundation::Name resourceName) const;

    private:
        const PipelineResourceStorage* mResourceStorage;
        const RenderPassGraph::Node* mPassNode;
    };

}
