#include "Scene.hpp"

namespace PathFinder 
{

    Mesh& Scene::AddMesh(Mesh&& mesh)
    {
        mMeshes.emplace_back(std::move(mesh));
        return mMeshes.back();
    }

    MeshInstance& Scene::AddMeshInstance(MeshInstance&& instance)
    {
        mMeshInstances.emplace_back(std::move(instance));
        return mMeshInstances.back();
    }

    PathFinder::Material& Scene::AddMaterial(Material&& material)
    {
        mMaterials.emplace_back(std::move(material));
        return mMaterials.back();
    }

    void Scene::IterateMeshInstances(const std::function<void(const MeshInstance& instance)>& functor) const
    {
        for (const MeshInstance& instance : mMeshInstances)
        {
            functor(instance);
        }
    }

    void Scene::IterateSubMeshes(const Mesh& mesh, const std::function<void(const SubMesh& subMesh)>& functor) const
    {
        for (const SubMesh& subMesh : mesh.SubMeshes())
        {
            functor(subMesh);
        }
    }

    void Scene::IterateMeshInstances(const std::function<void(MeshInstance & instance)>& functor)
    {
        for (MeshInstance& instance : mMeshInstances)
        {
            functor(instance);
        }
    }

    void Scene::IterateSubMeshes(Mesh& mesh, const std::function<void(SubMesh & subMesh)>& functor)
    {
        for (SubMesh& subMesh : mesh.SubMeshes())
        {
            functor(subMesh);
        }
    }

}
