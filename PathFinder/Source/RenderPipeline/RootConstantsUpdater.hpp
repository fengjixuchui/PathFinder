#pragma once

#include "PipelineResourceStorage.hpp"
#include "RenderPassGraph.hpp"

namespace PathFinder
{

    class RootConstantsUpdater
    {
    public:
        RootConstantsUpdater(PipelineResourceStorage* storage, const RenderPassGraph::Node* passNode);

        /// Uploads data to current pass' constant buffer.
        /// Data is versioned between each draw/dispatch call
        template <class RootCBufferContent>
        void UpdateRootConstantBuffer(const RootCBufferContent& data);

    private:
        PipelineResourceStorage* mResourceStorage;
        const RenderPassGraph::Node* mPassNode;
    };

    template <class RootCBufferContent>
    void RootConstantsUpdater::UpdateRootConstantBuffer(const RootCBufferContent& data)
    {
        mResourceStorage->UpdatePassRootConstants(data, *mPassNode);
    }

}
