#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <VoxelCube/Core/Base.hpp>

namespace vc
{
    enum class FileSystemChangeType : std::uint8_t
    {
        Added = 0,
        Modified,
        Removed
    };

    struct FileSystemChange
    {
        Path path;
        FileSystemChangeType type = FileSystemChangeType::Modified;
    };

    class FileSystem
    {
    public:
        [[nodiscard]] static bool Exists(const Path& path);
        [[nodiscard]] static bool IsFile(const Path& path);
        [[nodiscard]] static bool IsDirectory(const Path& path);

        [[nodiscard]] static Path GetWorkingDirectory();
        [[nodiscard]] static Path GetAbsolutePath(const Path& path);
        [[nodiscard]] static Path Normalize(const Path& path);

        static void CreateDirectories(const Path& path);

        [[nodiscard]] static std::vector<std::uint8_t> ReadBytes(const Path& path);
        [[nodiscard]] static std::string ReadText(const Path& path);
        static void WriteBytes(const Path& path, const std::vector<std::uint8_t>& bytes);
        static void WriteText(const Path& path, std::string_view text);

        [[nodiscard]] static std::vector<Path> ListFiles(const Path& path, bool recursive = false);
    };

    class FileSystemWatcher
    {
    public:
        explicit FileSystemWatcher(Path targetPath, bool recursive = true);
        ~FileSystemWatcher();

        FileSystemWatcher(const FileSystemWatcher&) = delete;
        FileSystemWatcher& operator=(const FileSystemWatcher&) = delete;
        FileSystemWatcher(FileSystemWatcher&&) noexcept;
        FileSystemWatcher& operator=(FileSystemWatcher&&) noexcept;

        [[nodiscard]] const Path& GetTargetPath() const noexcept;
        [[nodiscard]] bool IsRecursive() const noexcept;
        [[nodiscard]] std::vector<FileSystemChange> PollChanges();
        void ResetSnapshot();

    private:
        class Impl;
        Scope<Impl> m_impl;
    };
}
