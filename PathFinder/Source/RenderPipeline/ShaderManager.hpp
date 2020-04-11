#pragma once

#include "../HardwareAbstractionLayer/Shader.hpp"
#include "../HardwareAbstractionLayer/ShaderCompiler.hpp"
#include "../IO/CommandLineParser.hpp"
#include "../Foundation/Event.hpp"

#include <vector>
#include <list>
#include <unordered_map>
#include <unordered_set>
#include <filesystem>
#include <functional>
#include <filewatch/FileWatcher.h>

namespace PathFinder
{

    class ShaderManager : private FW::FileWatchListener
    {
    public:
        using ShaderEvent = Foundation::Event<ShaderManager, std::string, void(const HAL::Shader*, const HAL::Shader*)>;
        using LibraryEvent = Foundation::Event<ShaderManager, std::string, void(const HAL::Library*, const HAL::Library*)>;

        ShaderManager(const CommandLineParser& commandLineParser);

        HAL::Shader* LoadShader(HAL::Shader::Stage pipelineStage, const std::string& entryPoint, const std::filesystem::path& relativePath);
        HAL::Library* LoadLibrary(const std::filesystem::path& relativePath);

        void BeginFrame();
        void EndFrame();

    private:
        using EntryPointName = Foundation::Name;
        using ShaderListIterator = std::list<HAL::Shader>::iterator;
        using LibraryListIterator = std::list<HAL::Library>::iterator;

        struct CompiledObjectsInFile
        {
            std::unordered_map<EntryPointName, ShaderListIterator> Shaders;
            std::optional<LibraryListIterator> Library;
        };

        HAL::Shader* GetShader(HAL::Shader::Stage pipelineStage, const std::string& entryPoint, const std::filesystem::path& relativePath);
        HAL::Shader* FindCachedShader(Foundation::Name entryPointName, const std::filesystem::path& relativePath);
        HAL::Shader* LoadAndCacheShader(HAL::Shader::Stage pipelineStage, const std::string& entryPoint, const std::filesystem::path& relativePath);

        HAL::Library* GetLibrary(const std::filesystem::path& relativePath);
        HAL::Library* FindCachedLibrary(const std::filesystem::path& relativePath);
        HAL::Library* LoadAndCacheLibrary(const std::filesystem::path& relativePath);

        std::filesystem::path ConstructShaderRootPath(const CommandLineParser& commandLineParser) const;
        void FindAndAddEntryPointShaderFileForRecompilation(const std::string& modifiedFile);
        void RecompileShader(ShaderListIterator oldShaderIt, const std::string& shaderFile);
        void RecompileLibrary(LibraryListIterator oldLibraryIt, const std::string& libraryFile);
        void RecompileModifiedShaders();
        void handleFileAction(FW::WatchID watchid, const FW::String& dir, const FW::String& filename, FW::Action action) override;

        CommandLineParser mCommandLineParser;
        FW::FileWatcher mFileWatcher;
        HAL::ShaderCompiler mCompiler;
        std::filesystem::path mShaderRootPath;

        std::list<HAL::Shader> mShaders;
        std::list<HAL::Library> mLibraries;

        std::unordered_set<std::string> mEntryPointShaderFilesToRecompile;
        std::unordered_map<std::string, CompiledObjectsInFile> mEntryPointFilePathToCompiledObjectAssociations;
        std::unordered_map<std::string, std::unordered_set<std::string>> mIncludedFilePathToEntryPointFilePathAssociations;

        ShaderEvent mShaderRecompilationEvent;
        LibraryEvent mLibraryRecompilationEvent;

    public:
        inline ShaderEvent& ShaderRecompilationEvent() { return mShaderRecompilationEvent; }
        inline LibraryEvent& LibraryRecompilationEvent() { return mLibraryRecompilationEvent; }
    };

}
