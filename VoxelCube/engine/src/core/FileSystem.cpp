#include <VoxelCube/Core/FileSystem.hpp>

#include <algorithm>
#include <fstream>
#include <iterator>
#include <map>
#include <string_view>
#include <system_error>

namespace
{
    using SnapshotMap = std::map<vc::Path, std::filesystem::file_time_type, std::less<>>;

    [[nodiscard]] vc::Path NormalizePath(const vc::Path& path)
    {
        std::error_code errorCode;
        const vc::Path weaklyCanonical = std::filesystem::weakly_canonical(path, errorCode);
        if (!errorCode)
        {
            return weaklyCanonical.lexically_normal();
        }

        const vc::Path absolute = std::filesystem::absolute(path, errorCode);
        if (!errorCode)
        {
            return absolute.lexically_normal();
        }

        return path.lexically_normal();
    }

    void EnsureNoError(const std::error_code& errorCode, std::string_view message)
    {
        if (errorCode)
        {
            throw std::filesystem::filesystem_error(std::string(message), errorCode);
        }
    }

    [[nodiscard]] SnapshotMap BuildSnapshot(const vc::Path& targetPath, bool recursive)
    {
        SnapshotMap snapshot;
        std::error_code errorCode;

        const vc::Path normalizedTarget = NormalizePath(targetPath);
        const bool exists = std::filesystem::exists(normalizedTarget, errorCode);
        EnsureNoError(errorCode, "Failed to inspect filesystem path.");
        if (!exists)
        {
            return snapshot;
        }

        const bool isDirectory = std::filesystem::is_directory(normalizedTarget, errorCode);
        EnsureNoError(errorCode, "Failed to inspect filesystem path type.");

        if (!isDirectory)
        {
            const auto timestamp = std::filesystem::last_write_time(normalizedTarget, errorCode);
            EnsureNoError(errorCode, "Failed to query file timestamp.");
            snapshot.emplace(normalizedTarget, timestamp);
            return snapshot;
        }

        if (recursive)
        {
            for (std::filesystem::recursive_directory_iterator iterator(normalizedTarget, errorCode), end; iterator != end; iterator.increment(errorCode))
            {
                EnsureNoError(errorCode, "Failed to enumerate directory contents.");
                const auto& entry = *iterator;
                if (!entry.is_regular_file(errorCode))
                {
                    EnsureNoError(errorCode, "Failed to inspect directory entry.");
                    continue;
                }

                const vc::Path normalizedPath = NormalizePath(entry.path());
                snapshot.emplace(normalizedPath, entry.last_write_time(errorCode));
                EnsureNoError(errorCode, "Failed to query file timestamp.");
            }
        }
        else
        {
            for (std::filesystem::directory_iterator iterator(normalizedTarget, errorCode), end; iterator != end; iterator.increment(errorCode))
            {
                EnsureNoError(errorCode, "Failed to enumerate directory contents.");
                const auto& entry = *iterator;
                if (!entry.is_regular_file(errorCode))
                {
                    EnsureNoError(errorCode, "Failed to inspect directory entry.");
                    continue;
                }

                const vc::Path normalizedPath = NormalizePath(entry.path());
                snapshot.emplace(normalizedPath, entry.last_write_time(errorCode));
                EnsureNoError(errorCode, "Failed to query file timestamp.");
            }
        }

        return snapshot;
    }
}

namespace vc
{
    bool FileSystem::Exists(const Path& path)
    {
        std::error_code errorCode;
        const bool exists = std::filesystem::exists(path, errorCode);
        EnsureNoError(errorCode, "Failed to check if a path exists.");
        return exists;
    }

    bool FileSystem::IsFile(const Path& path)
    {
        std::error_code errorCode;
        const bool isRegularFile = std::filesystem::is_regular_file(path, errorCode);
        EnsureNoError(errorCode, "Failed to check if a path is a regular file.");
        return isRegularFile;
    }

    bool FileSystem::IsDirectory(const Path& path)
    {
        std::error_code errorCode;
        const bool isDirectory = std::filesystem::is_directory(path, errorCode);
        EnsureNoError(errorCode, "Failed to check if a path is a directory.");
        return isDirectory;
    }

    Path FileSystem::GetWorkingDirectory()
    {
        std::error_code errorCode;
        const Path currentPath = std::filesystem::current_path(errorCode);
        EnsureNoError(errorCode, "Failed to get the working directory.");
        return currentPath;
    }

    Path FileSystem::GetAbsolutePath(const Path& path)
    {
        std::error_code errorCode;
        const Path absolutePath = std::filesystem::absolute(path, errorCode);
        EnsureNoError(errorCode, "Failed to compute an absolute path.");
        return absolutePath;
    }

    Path FileSystem::Normalize(const Path& path)
    {
        return NormalizePath(path);
    }

    void FileSystem::CreateDirectories(const Path& path)
    {
        if (path.empty())
        {
            return;
        }

        std::error_code errorCode;
        std::filesystem::create_directories(path, errorCode);
        EnsureNoError(errorCode, "Failed to create directories.");
    }

    std::vector<std::uint8_t> FileSystem::ReadBytes(const Path& path)
    {
        std::ifstream input(path, std::ios::binary);
        if (!input.is_open())
        {
            throw std::runtime_error("Failed to open file for reading: " + path.string());
        }

        return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    }

    std::string FileSystem::ReadText(const Path& path)
    {
        std::ifstream input(path, std::ios::binary);
        if (!input.is_open())
        {
            throw std::runtime_error("Failed to open file for reading: " + path.string());
        }

        return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    }

    void FileSystem::WriteBytes(const Path& path, const std::vector<std::uint8_t>& bytes)
    {
        const Path parentPath = path.parent_path();
        if (!parentPath.empty())
        {
            CreateDirectories(parentPath);
        }

        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        if (!output.is_open())
        {
            throw std::runtime_error("Failed to open file for writing: " + path.string());
        }

        if (!bytes.empty())
        {
            output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        }

        if (!output.good())
        {
            throw std::runtime_error("Failed to write binary file: " + path.string());
        }
    }

    void FileSystem::WriteText(const Path& path, std::string_view text)
    {
        const Path parentPath = path.parent_path();
        if (!parentPath.empty())
        {
            CreateDirectories(parentPath);
        }

        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        if (!output.is_open())
        {
            throw std::runtime_error("Failed to open file for writing: " + path.string());
        }

        output.write(text.data(), static_cast<std::streamsize>(text.size()));
        if (!output.good())
        {
            throw std::runtime_error("Failed to write text file: " + path.string());
        }
    }

    std::vector<Path> FileSystem::ListFiles(const Path& path, bool recursive)
    {
        std::vector<Path> files;
        std::error_code errorCode;

        if (!std::filesystem::exists(path, errorCode))
        {
            EnsureNoError(errorCode, "Failed to inspect filesystem path.");
            return files;
        }
        EnsureNoError(errorCode, "Failed to inspect filesystem path.");

        if (recursive)
        {
            for (std::filesystem::recursive_directory_iterator iterator(path, errorCode), end; iterator != end; iterator.increment(errorCode))
            {
                EnsureNoError(errorCode, "Failed to enumerate directory contents.");
                if (iterator->is_regular_file(errorCode))
                {
                    files.push_back(NormalizePath(iterator->path()));
                }
                EnsureNoError(errorCode, "Failed to inspect directory entry.");
            }
        }
        else
        {
            for (std::filesystem::directory_iterator iterator(path, errorCode), end; iterator != end; iterator.increment(errorCode))
            {
                EnsureNoError(errorCode, "Failed to enumerate directory contents.");
                if (iterator->is_regular_file(errorCode))
                {
                    files.push_back(NormalizePath(iterator->path()));
                }
                EnsureNoError(errorCode, "Failed to inspect directory entry.");
            }
        }

        return files;
    }

    class FileSystemWatcher::Impl
    {
    public:
        explicit Impl(Path targetPath, bool recursive)
            : m_targetPath(FileSystem::Normalize(std::move(targetPath)))
            , m_recursive(recursive)
        {
            m_snapshot = BuildSnapshot(m_targetPath, m_recursive);
        }

        [[nodiscard]] const Path& GetTargetPath() const noexcept
        {
            return m_targetPath;
        }

        [[nodiscard]] bool IsRecursive() const noexcept
        {
            return m_recursive;
        }

        [[nodiscard]] std::vector<FileSystemChange> PollChanges()
        {
            std::vector<FileSystemChange> changes;
            const SnapshotMap currentSnapshot = BuildSnapshot(m_targetPath, m_recursive);

            for (const auto& [path, timestamp] : currentSnapshot)
            {
                const auto previous = m_snapshot.find(path);
                if (previous == m_snapshot.end())
                {
                    changes.push_back({ path, FileSystemChangeType::Added });
                    continue;
                }

                if (previous->second != timestamp)
                {
                    changes.push_back({ path, FileSystemChangeType::Modified });
                }
            }

            for (const auto& [path, timestamp] : m_snapshot)
            {
                (void)timestamp;
                if (!currentSnapshot.contains(path))
                {
                    changes.push_back({ path, FileSystemChangeType::Removed });
                }
            }

            m_snapshot = currentSnapshot;
            return changes;
        }

        void ResetSnapshot()
        {
            m_snapshot = BuildSnapshot(m_targetPath, m_recursive);
        }

    private:
        Path m_targetPath;
        bool m_recursive = true;
        SnapshotMap m_snapshot;
    };

    FileSystemWatcher::FileSystemWatcher(Path targetPath, bool recursive)
        : m_impl(CreateScope<Impl>(std::move(targetPath), recursive))
    {
    }

    FileSystemWatcher::~FileSystemWatcher() = default;

    FileSystemWatcher::FileSystemWatcher(FileSystemWatcher&& other) noexcept = default;

    FileSystemWatcher& FileSystemWatcher::operator=(FileSystemWatcher&& other) noexcept = default;

    const Path& FileSystemWatcher::GetTargetPath() const noexcept
    {
        return m_impl->GetTargetPath();
    }

    bool FileSystemWatcher::IsRecursive() const noexcept
    {
        return m_impl->IsRecursive();
    }

    std::vector<FileSystemChange> FileSystemWatcher::PollChanges()
    {
        return m_impl->PollChanges();
    }

    void FileSystemWatcher::ResetSnapshot()
    {
        m_impl->ResetSnapshot();
    }
}
