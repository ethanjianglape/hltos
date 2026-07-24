#pragma once

#include "containers/klist.hpp"
#include <cerrno>
#include <containers/kstring.hpp>
#include <containers/kvector.hpp>

#include <cstddef>
#include <cstdint>

namespace fs {
enum class FileType : std::uint8_t {
    NONE = 0,
    REGULAR = 1,
    DIRECTORY = 2,
    CHAR_DEVICE = 3,
};

constexpr int O_RDONLY = 0x00;
constexpr int O_WRONLY = 0x01;
constexpr int O_RDWR = 0x02;

constexpr int SEEK_SET = 0;
constexpr int SEEK_CUR = 1;
constexpr int SEEK_END = 2;

struct FileDescriptor;
struct Stat;
struct DirEntry;

class Inode;
class DirectoryInode;
class FileSystem;
class MountPoint;

/**
 * @brief File metadata (for stat() without opening).
 */
struct Stat {
    FileType type;
    std::size_t size;
};

/**
 * @brief Directory listing entry.
 */
struct DirEntry {
    kstring name;
    FileType type;

    DirEntry() {}
    DirEntry(kstring n, FileType t)
        : name{n}
        , type{t}
    {
    }
};

/**
 * @brief An open file handle (per-process).
 */
struct FileDescriptor {
    Inode* inode;
    kstring path;
    std::size_t offset;
    int flags;
};

class FileSystem {
public:
    virtual const char* name() = 0;
    virtual MountPoint* mount(const char* dir) = 0;

    FileSystem() {};
    virtual ~FileSystem() = default;
    FileSystem(const FileSystem&) = delete;
    FileSystem(FileSystem&&) = delete;
    FileSystem& operator=(const FileSystem&) = delete;
    FileSystem& operator=(FileSystem&&) = delete;
};

class MountPoint {
public:
    const char* path;
    FileSystem* fs;
    Inode* mounted_on;
    Inode* root_inode;
};

class Inode {
public:
    MountPoint* mountpoint;
    Inode* parent;
    std::uint64_t ino;
    std::uint64_t size;
    FileType type;
    kstring name;

    explicit Inode(MountPoint* mp);
    Inode() = delete;
    virtual ~Inode() = default;
    Inode(const Inode&) = delete;
    Inode(Inode&&) = delete;
    Inode& operator=(const Inode&) = delete;
    Inode& operator=(Inode&&) = delete;

    virtual int open(FileDescriptor* fd, int flags) = 0;
    virtual int read(FileDescriptor* fd, void* buf, std::size_t count) = 0;
    virtual int write(FileDescriptor* fd, const void* buf, std::size_t count) = 0;
    virtual int close(FileDescriptor* fd) = 0;
    virtual int lseek(FileDescriptor* fd, int offset, int whence) = 0;
    virtual int stat(Stat* stat) = 0;

    virtual int ioctl(unsigned long, void*) { return -ENOTTY; }

    virtual Inode* lookup(const char*) { return nullptr; }
    virtual int readdir(kvector<DirEntry>&) { return -ENOTDIR; }
    virtual int mkdir(const char*, int) { return -ENOTDIR; }
    virtual int create(const char*, int) { return -ENOTDIR; }
};

class ReadOnlyInode : public Inode {
public:
    ReadOnlyInode(MountPoint* mp);
    virtual ~ReadOnlyInode() = default;

    int open(FileDescriptor*, int) override { return 0; }
    int write(FileDescriptor*, const void*, std::size_t) final override { return -EROFS; }
    int close(FileDescriptor*) override { return 0; }
    int lseek(FileDescriptor*, int, int) override { return 0; }
    int stat(Stat*) override { return 0; }
};

class DirectoryInode : public Inode {
public:
    klist<Inode*> children;

    explicit DirectoryInode(MountPoint* mpt);
    DirectoryInode() = delete;
    virtual ~DirectoryInode() = default;
    DirectoryInode(const DirectoryInode&) = delete;
    DirectoryInode(DirectoryInode&&) = delete;
    DirectoryInode& operator=(const DirectoryInode&) = delete;
    DirectoryInode& operator=(DirectoryInode&&) = delete;

    int read(FileDescriptor*, void*, std::size_t) final override { return -EISDIR; }
    int write(FileDescriptor*, const void*, std::size_t) final override { return -EISDIR; }
    int lseek(FileDescriptor*, int, int) final override { return -EISDIR; }
};

void init();

// ============================================================================
// VFS operations - global path-based file access
// ============================================================================

kstring getcwd(const Inode* inode);
void register_mount(const char* path, MountPoint* mp);
FileDescriptor* open(kstring_view path, int flags);
void mount(const char* path, FileSystem* fs, const char* source);

int stat(const kstring& path, Stat* out);
int readdir(const kstring& path, kvector<DirEntry>& out);
int mkdir(const kstring& path, int mode);
}
