#include "RenderPassGraph.hpp"

#include "RenderPass.hpp"

#include "../Foundation/Assert.hpp"

namespace PathFinder
{

    std::pair<Foundation::Name, uint32_t> RenderPassGraph::DecodeSubresourceName(SubresourceName name)
    {
        return { Foundation::Name{ name >> 32 }, name & 0x0000FFFF };
    }

    uint64_t RenderPassGraph::NodeCountForQueue(uint64_t queueIndex) const
    {
        auto countIt = mQueueNodeCounters.find(queueIndex);
        return countIt != mQueueNodeCounters.end() ? countIt->second : 0;
    }

    const RenderPassGraph::ResourceUsageTimeline& RenderPassGraph::GetResourceUsageTimeline(Foundation::Name resourceName) const
    {
        auto timelineIt = mResourceUsageTimelines.find(resourceName);
        assert_format(timelineIt != mResourceUsageTimelines.end(), "Resource timeline doesn't exist");
        return timelineIt->second;
    }

    void RenderPassGraph::AddPass(const RenderPassMetadata& passMetadata)
    {
        EnsureRenderPassUniqueness(passMetadata.Name);
        mPassNodes.emplace_back(Node{ passMetadata, &mGlobalWriteDependencyRegistry });
    }

    void RenderPassGraph::RemovePass(NodeListIterator it)
    {
        mPassNodes.erase(it);
    }

    void RenderPassGraph::Build()
    {
        BuildDependencyLevels();
        FinalizeDependencyLevels();
        CullRedundantSynchronizations();
    }

    void RenderPassGraph::Clear()
    {
        mGlobalWriteDependencyRegistry.clear();
        mDependencyLevels.clear();
        mResourceUsageTimelines.clear();
        mQueueNodeCounters.clear();
        mOrderedNodes.clear();
        mFirstNodeThatUsesRayTracing = nullptr;
        mDetectedQueueCount = 1;

        for (Node& node : mPassNodes)
        {
            node.Clear();
        }
    }

    void RenderPassGraph::IterateNodesInExecutionOrder(const std::function<void(const RenderPassGraph::Node&)>& iterator) const
    {
        for (const DependencyLevel& dependencyLevel : mDependencyLevels)
        {
            for (const Node* node : dependencyLevel.Nodes())
            {
                iterator(*node);
            }
        }
    }

    void RenderPassGraph::EnsureRenderPassUniqueness(Foundation::Name passName)
    {
        assert_format(mRenderPassRegistry.find(passName) == mRenderPassRegistry.end(),
            "Render pass ", passName.ToString(), " is already added to the graph.");

        mRenderPassRegistry.insert(passName);
    }

    void RenderPassGraph::BuildDependencyLevels()
    {
        DependencyLevel& baseDependencyLevel = mDependencyLevels.emplace_back(mDependencyLevels.size());

        mDetectedQueueCount = 1;

        // Fill base dependency level to start from
        for (Node& node : mPassNodes)
        {
            baseDependencyLevel.AddNode(&node);
            mDetectedQueueCount = std::max(mDetectedQueueCount, node.ExecutionQueueIndex + 1);
        }

        std::vector<DependencyLevel::NodeIterator> nodesToMoveToNextLevel;

        do
        {
            assert_format(mDependencyLevels.size() <= mPassNodes.size(), "Graph builder is going into infinite loop");

            DependencyLevel& nextDependencyLevel = mDependencyLevels.emplace_back(mDependencyLevels.size());
            DependencyLevel& currentDependencyLevel = mDependencyLevels[mDependencyLevels.size() - 2];

            nodesToMoveToNextLevel.clear();

            // Take a node from the current dependency level
            for (auto nodeIt = currentDependencyLevel.mNodes.begin(); nodeIt != currentDependencyLevel.mNodes.end(); nodeIt++)
            {
                Node* node = *nodeIt;

                // Go through every other node in this dependency level and see if current node depends on any of the other ones
                for (auto otherNodeIt = currentDependencyLevel.mNodes.begin(); otherNodeIt != currentDependencyLevel.mNodes.end(); otherNodeIt++)
                {
                    // Do not check dependencies on itself
                    if (nodeIt == otherNodeIt) continue;

                    Node* otherNode = *otherNodeIt;

                    for (SubresourceName otherNodeWrittenResource : otherNode->WrittenSubresources())
                    {
                        // If current node reads resource that is written by other node in current dependency level,
                        // then it depends on the other node and should be moved to the next dependency level
                        bool thisNodeReadsOtherNodeResource = node->ReadSubresources().find(otherNodeWrittenResource) != node->ReadSubresources().end();

                        // If this node is not dependent on the other, check the next one
                        if (!thisNodeReadsOtherNodeResource)
                        {
                            continue;
                        }

                        // Signal only for cross-queue dependencies
                        if (node->ExecutionQueueIndex != otherNode->ExecutionQueueIndex)
                        {
                            otherNode->mSyncSignalRequired = true;
                        }

                        // Adding dependency even from the same queue is convenient 
                        // for dependency optimization pass later.
                        node->mNodesToSyncWith.push_back(otherNode);

                        // Check for circular dependencies
                        bool otherNodeReadsCurrentNodeResources = false;

                        for (SubresourceName currentNodeWrittenResource : node->WrittenSubresources())
                        {
                            if (otherNode->ReadSubresources().find(currentNodeWrittenResource) != otherNode->ReadSubresources().end())
                            {
                                otherNodeReadsCurrentNodeResources = true;
                                break;
                            }
                        }

                        auto [resourceName, subresourceIndex] = DecodeSubresourceName(otherNodeWrittenResource);

                        assert_format(!otherNodeReadsCurrentNodeResources,
                            "Detected a circular dependency between render passes: ",
                            node->PassMetadata().Name.ToString(), " and ",
                            otherNode->PassMetadata().Name.ToString(), ". \n",
                            "Dependency is: Resource ", resourceName.ToString(), ", Subresource ", subresourceIndex);

                        // One found dependency is enough 
                        nodesToMoveToNextLevel.push_back(nodeIt);
                        break;
                    }
                }
            }

            // Move nodes with dependencies to next level
            if (!nodesToMoveToNextLevel.empty())
            {
                for (auto it : nodesToMoveToNextLevel)
                {
                    Node* node = currentDependencyLevel.RemoveNode(it);
                    node->mDependencyLevelIndex++;
                    nextDependencyLevel.AddNode(node);
                }
            }
            else
            {
                mDependencyLevels.pop_back();
            }

        } while (!nodesToMoveToNextLevel.empty());

        for (DependencyLevel& level : mDependencyLevels)
        {
            OutputDebugString(StringFormat("Level %d: \n\n", level.LevelIndex()).c_str());
            for (Node* node : level.Nodes())
            {
                OutputDebugString(StringFormat("Pass %s: \n", node->PassMetadata().Name.ToString().c_str()).c_str());
            }
        }
    }

    void RenderPassGraph::FinalizeDependencyLevels()
    {
        uint64_t globalExecutionIndex = 0;
        uint64_t localExecutionIndex = 0;

        bool firstRayTracingUserDetected = false;
        
        for (DependencyLevel& dependencyLevel : mDependencyLevels)
        {
            std::unordered_map<SubresourceName, std::unordered_set<Node::QueueIndex>> resourceReadingQueueTracker;
            dependencyLevel.mNodesPerQueue.resize(mDetectedQueueCount);

            for (Node* node : dependencyLevel.mNodes)
            {
                // Track which resource is read by which queue in this dependency level
                for (SubresourceName subresourceName : node->ReadSubresources())
                {
                    resourceReadingQueueTracker[subresourceName].insert(node->ExecutionQueueIndex);
                }

                node->mGlobalExecutionIndex = globalExecutionIndex;
                node->mLocalToDependencyLevelExecutionIndex = localExecutionIndex;
                node->mLocalToQueueExecutionIndex = mQueueNodeCounters[node->ExecutionQueueIndex]++;

                mOrderedNodes.push_back(node);
                dependencyLevel.mNodesPerQueue[node->ExecutionQueueIndex].push_back(node);

                for (SubresourceName subresourceName : node->AllSubresources())
                {
                    auto [resourceName, subresourceIndex] = DecodeSubresourceName(subresourceName);

                    auto timelineIt = mResourceUsageTimelines.find(resourceName);
                    bool timelineExists = timelineIt != mResourceUsageTimelines.end();

                    if (timelineExists) 
                    {
                        // Update "end" 
                        timelineIt->second.second = node->GlobalExecutionIndex();
                    }
                    else {
                        // Create "start"
                        mResourceUsageTimelines[resourceName].first = node->GlobalExecutionIndex();
                    }
                }

                // Track first RT-using node to sync BVH builds with
                if (node->UsesRayTracing && !firstRayTracingUserDetected)
                {
                    mFirstNodeThatUsesRayTracing = node;
                    firstRayTracingUserDetected = true;
                }

                localExecutionIndex++;
                globalExecutionIndex++;
            }

            // Record queue indices that are detected to read common resources
            for (auto& [subresourceName, queueIndices] : resourceReadingQueueTracker)
            {
                // If resource is read by more than one queue
                if (queueIndices.size() > 1)
                {
                    for (Node::QueueIndex queueIndex : queueIndices)
                    {
                        dependencyLevel.mQueuesInvoledInCrossQueueResourceReads.insert(queueIndex);
                        dependencyLevel.mSubresourcesReadByMultipleQueues.insert(subresourceName);
                    }
                }
            }

            localExecutionIndex = 0;
        }
    }

    void RenderPassGraph::CullRedundantSynchronizations()
    {
        // Initialize synchronization index sets
        for (Node& node : mPassNodes)
        {
            node.mSynchronizationIndexSet.resize(mDetectedQueueCount, 0);
        }

        std::vector<std::vector<const Node*>> nodesPerQueue{ mDetectedQueueCount };

        for (DependencyLevel& dependencyLevel : mDependencyLevels)
        {
            // First pass: find closest nodes to sync with, compute initial SSIS (sufficient synchronization index set)
            for (Node* node : dependencyLevel.mNodes)
            {
                // Closest node to sync with on each queue
                std::vector<const Node*> closestNodesToSyncWith{ mDetectedQueueCount, nullptr };

                // Find closest dependencies from other queues for the current node
                for (const Node* dependencyNode : node->mNodesToSyncWith)
                {
                    uint64_t closestDependencyNodeIndexInQueue = dependencyNode->LocalToQueueExecutionIndex();

                    if (const Node* closestNode = closestNodesToSyncWith[node->ExecutionQueueIndex])
                    {
                        closestDependencyNodeIndexInQueue = std::max(closestNode->LocalToQueueExecutionIndex(), closestDependencyNodeIndexInQueue);
                    }

                    closestNodesToSyncWith[node->ExecutionQueueIndex] = dependencyNode;
                }

                // Get rid of nodes to sync that may have had redundancies
                node->mNodesToSyncWith.clear();

                for (const Node* closestNode : closestNodesToSyncWith)
                {
                    if (!closestNode)
                    {
                        continue;
                    }

                    // Update SSIS using closest nodes' indices
                    if (closestNode->ExecutionQueueIndex != node->ExecutionQueueIndex)
                    {
                        node->mSynchronizationIndexSet[closestNode->ExecutionQueueIndex] = closestNode->LocalToQueueExecutionIndex();
                    }

                    // Store only closest nodes to sync with
                    node->mNodesToSyncWith.push_back(closestNode);
                }

                // Use node's execution index as synchronization index on its own queue
                node->mSynchronizationIndexSet[node->ExecutionQueueIndex] = node->LocalToQueueExecutionIndex();

                nodesPerQueue[node->ExecutionQueueIndex].push_back(node);
            }

            // Second pass: cull redundant dependencies by searching for indirect synchronizations
            for (Node* node : dependencyLevel.mNodes)
            {
                // Keep track of queues we still need to sync with
                std::unordered_set<uint64_t> queueToSyncWithIndices;

                // Store nodes and queue syncs they cover
                std::vector<std::pair<const Node*, std::vector<uint64_t>>> syncCoverage;

                // Final optimized list of nodes without redundant dependencies
                std::vector<const Node*> optimalNodesToSyncWith;

                for (const Node* nodeToSyncWith : node->mNodesToSyncWith)
                {
                    // Having node on the same queue as a dependency to sync with is useful 
                    // to detect indirect syncs performed through nodes on the same queue,
                    // but we don't actually want the same queue for direct synchronization,
                    // so skip it here.

                    if (nodeToSyncWith->ExecutionQueueIndex == node->ExecutionQueueIndex)
                    {
                        continue;
                    }

                    queueToSyncWithIndices.insert(nodeToSyncWith->ExecutionQueueIndex);
                }

                while (!queueToSyncWithIndices.empty())
                {
                    uint64_t maxNumberOfSyncsCoveredBySingleNode = 0;

                    for (const Node* dependencyNode : node->mNodesToSyncWith)
                    {
                        // Take a dependency node and check how many queues we would sync with 
                        // if we would only sync with this one node. We very well may encounter a case
                        // where by synchronizing with just one node we will sync with more then one queue
                        // or even all of them through indirect synchronizations, 
                        // which will make other synchronizations previously detected for this node redundant.

                        uint64_t numberOfSyncsCoveredByDependency = 0;

                        for (uint64_t queueIndex : queueToSyncWithIndices)
                        {
                            uint64_t currentNodeDesiredSyncIndex = node->mSynchronizationIndexSet[queueIndex];
                            uint64_t dependencyNodeSyncIndex = dependencyNode->mSynchronizationIndexSet[queueIndex];

                            if (dependencyNodeSyncIndex >= currentNodeDesiredSyncIndex)
                            {
                                ++numberOfSyncsCoveredByDependency;
                            }
                        }

                        syncCoverage.emplace_back(dependencyNode, numberOfSyncsCoveredByDependency);
                        maxNumberOfSyncsCoveredBySingleNode = std::max(maxNumberOfSyncsCoveredBySingleNode, numberOfSyncsCoveredByDependency);
                    }

                    for (auto& [node, syncedQueues] : syncCoverage)
                    {
                        auto coveredSyncCount = syncedQueues.size();

                        if (coveredSyncCount >= maxNumberOfSyncsCoveredBySingleNode)
                        {
                            optimalNodesToSyncWith.push_back(node);

                            // Remove covered queues from the list of queues we need to sync with
                            for (uint64_t syncedQueueIndex : syncedQueues)
                            {
                                queueToSyncWithIndices.erase(syncedQueueIndex);
                            }
                        }
                    }
                }

                // Finally, assign an optimal list of nodes to sync with to the current node
                node->mNodesToSyncWith = optimalNodesToSyncWith;
            }
        }
    }

    RenderPassGraph::Node::Node(const RenderPassMetadata& passMetadata, WriteDependencyRegistry* writeDependencyRegistry)
        : mPassMetadata{ passMetadata }, mWriteDependencyRegistry{ writeDependencyRegistry } {}

    void RenderPassGraph::Node::AddReadDependency(Foundation::Name resourceName, uint32_t firstSubresourceIndex, uint32_t lastSubresourceIndex)
    {
        for (auto i = firstSubresourceIndex; i <= lastSubresourceIndex; ++i)
        {
            SubresourceName name = CreateSubresourceName(resourceName, i);
            mReadSubresources.insert(name);
            mAllSubresources.insert(name);
            mAllResources.insert(resourceName);
        }
    }

    void RenderPassGraph::Node::AddReadDependency(Foundation::Name resourceName, const SubresourceList& subresources)
    {
        if (subresources.empty())
        {
            AddReadDependency(resourceName, 1);
        }
        else
        {
            for (auto subresourceIndex : subresources)
            {
                SubresourceName name = CreateSubresourceName(resourceName, subresourceIndex);
                mReadSubresources.insert(name);
                mAllSubresources.insert(name);
                mAllResources.insert(resourceName);
            }
        }
    }

    void RenderPassGraph::Node::AddReadDependency(Foundation::Name resourceName, uint32_t subresourceCount)
    {
        assert_format(subresourceCount > 0, "0 subresource count");
        AddReadDependency(resourceName, 0, subresourceCount - 1);
    }

    void RenderPassGraph::Node::AddWriteDependency(Foundation::Name resourceName, uint32_t firstSubresourceIndex, uint32_t lastSubresourceIndex)
    {
        for (auto i = firstSubresourceIndex; i <= lastSubresourceIndex; ++i)
        {
            SubresourceName name = CreateSubresourceName(resourceName, i);
            EnsureSingleWriteDependency(name);
            mWrittenSubresources.insert(name);
            mAllSubresources.insert(name);
            mAllResources.insert(resourceName);
        }
    }

    void RenderPassGraph::Node::AddWriteDependency(Foundation::Name resourceName, const SubresourceList& subresources)
    {
        if (subresources.empty())
        {
            AddWriteDependency(resourceName, 1);
        }
        else
        {
            for (auto subresourceIndex : subresources)
            {
                SubresourceName name = CreateSubresourceName(resourceName, subresourceIndex);
                EnsureSingleWriteDependency(name);
                mWrittenSubresources.insert(name);
                mAllSubresources.insert(name);
                mAllResources.insert(resourceName);
            }
        }
    }

    void RenderPassGraph::Node::AddWriteDependency(Foundation::Name resourceName, uint32_t subresourceCount)
    {
        assert_format(subresourceCount > 0, "0 subresource count");
        AddWriteDependency(resourceName, 0, subresourceCount - 1);
    }

    bool RenderPassGraph::Node::HasDependency(Foundation::Name resourceName, uint32_t subresourceIndex) const
    {
        return mAllSubresources.find(CreateSubresourceName(resourceName, subresourceIndex)) != mAllSubresources.end();
    }

    void RenderPassGraph::Node::Clear()
    {
        mReadSubresources.clear();
        mWrittenSubresources.clear();
        mAllSubresources.clear();
        mAllResources.clear();
        mNodesToSyncWith.clear();
        mSynchronizationIndexSet.clear();
        mDependencyLevelIndex = 0;
        mSyncSignalRequired = false;
        ExecutionQueueIndex = 0;
        UsesRayTracing = false;
        mGlobalExecutionIndex = 0;
        mLocalToDependencyLevelExecutionIndex = 0;
    }

    RenderPassGraph::SubresourceName RenderPassGraph::Node::CreateSubresourceName(Foundation::Name resourceName, uint32_t subresourceIndex) const
    {
        SubresourceName name = resourceName.ToId();
        name <<= 32;
        name |= subresourceIndex;
        return name;
    }

    void RenderPassGraph::Node::EnsureSingleWriteDependency(SubresourceName name)
    {
        auto [resourceName, subresourceIndex] = DecodeSubresourceName(name);

        assert_format(mWriteDependencyRegistry->find(name) == mWriteDependencyRegistry->end(),
            "Resource ", resourceName.ToString(), ", subresource ", subresourceIndex, " already has a write dependency. ",
            "Consider refactoring render passes to write to each sub resource of any resource only once in a frame.");

        mWriteDependencyRegistry->insert(name);
    }

    RenderPassGraph::DependencyLevel::DependencyLevel(uint64_t levelIndex)
        : mLevelIndex{ levelIndex } {}

    void RenderPassGraph::DependencyLevel::AddNode(Node* node)
    {
        mNodes.push_back(node);
    }

    RenderPassGraph::Node* RenderPassGraph::DependencyLevel::RemoveNode(NodeIterator it)
    {
        Node* node = *it;
        mNodes.erase(it);
        return node;
    }

}