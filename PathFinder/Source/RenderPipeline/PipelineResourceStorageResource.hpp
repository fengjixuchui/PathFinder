#pragma once

#include "PipelineResourceSchedulingInfo.hpp"

#include "../Foundation/Name.hpp"
#include "../Memory/GPUResourceProducer.hpp"


namespace PathFinder
{

    struct PipelineResourceStorageResource
    {
    public:
        struct DiffEntry
        {
        public:
            bool operator==(const DiffEntry& that) const;

            Foundation::Name ResourceName;
            uint64_t MemoryFootprint = 0;
            uint64_t LifetimeStart = 0;
            uint64_t LifetimeEnd = 0;
        };

        PipelineResourceStorageResource(Foundation::Name resourceName, const HAL::ResourceFormat& format);

        // An array of resources can be requested per resource name
        PipelineResourceSchedulingInfo SchedulingInfo;
        Memory::GPUResourceProducer::TexturePtr Texture;
        Memory::GPUResourceProducer::BufferPtr Buffer;

        const Memory::GPUResource* GetGPUResource() const;
        Memory::GPUResource* GetGPUResource();

        DiffEntry GetDiffEntry() const;

    private:
        Foundation::Name mResourceName;

    public:
        inline Foundation::Name ResourceName() const { return mResourceName; }
    };

}