#include "PipelineResourceStorageResource.hpp"

#include "../Foundation/StringUtils.hpp"
#include "../Foundation/Assert.hpp"
#include "../Foundation/STDHelpers.hpp"

namespace PathFinder
{

    PipelineResourceStorageResource::PipelineResourceStorageResource(Foundation::Name resourceName, const HAL::ResourceFormat& format)
        : mResourceName{ resourceName }, SchedulingInfo{ resourceName, format } {}

    const Memory::GPUResource* PipelineResourceStorageResource::GetGPUResource() const
    {
        if (Texture) return Texture.get();
        else if (Buffer) return Buffer.get();
        else return nullptr;
    }

    Memory::GPUResource* PipelineResourceStorageResource::GetGPUResource()
    {
        if (Texture) return Texture.get();
        else if (Buffer) return Buffer.get();
        else return nullptr;
    }

    PipelineResourceStorageResource::DiffEntry PipelineResourceStorageResource::GetDiffEntry() const
    {
        return { mResourceName, SchedulingInfo.TotalRequiredMemory(), 0, 0 };
    }

    bool PipelineResourceStorageResource::DiffEntry::operator==(const DiffEntry& that) const
    {
        // Pipeline Resource is identified by its name, memory footprint and lifetime,
        // which is sufficient to understand when
        // resource allocation, reallocation or deallocation is required.
        return this->ResourceName == that.ResourceName && 
            this->MemoryFootprint == that.MemoryFootprint &&
            this->LifetimeStart == that.LifetimeStart &&
            this->LifetimeEnd == that.LifetimeEnd;
    }

}
