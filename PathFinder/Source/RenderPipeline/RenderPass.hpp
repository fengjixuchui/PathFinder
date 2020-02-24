#pragma once

#include "../Foundation/Name.hpp"

#include "RenderPassMetadata.hpp"
#include "ResourceScheduler.hpp"
#include "ResourceProvider.hpp"
#include "GraphicsDevice.hpp"
#include "RenderContext.hpp"
#include "PipelineStateCreator.hpp"

#include "RenderPasses/PipelineNames.hpp"

namespace PathFinder
{

    template <class ContentMediator>
    class RenderPass
    {
    public:
        RenderPass(Foundation::Name name, RenderPassPurpose purpose = RenderPassPurpose::Default)
            : mMetadata{ name, purpose } {};

        virtual ~RenderPass() = 0;

        virtual void SetupPipelineStates(PipelineStateCreator* stateCreator) {};
        virtual void ScheduleResources(ResourceScheduler* scheduler) {};
        virtual void Render(RenderContext<ContentMediator>* context) {};

    private:
        RenderPassMetadata mMetadata;

    public:
        inline const RenderPassMetadata& Metadata() const { return mMetadata; }
    };

    template <class ContentMediator>
    RenderPass<ContentMediator>::~RenderPass() {}

}