#include <algo/algo.hpp>
#include <containers/kstring.hpp>
#include <exclusive/kspinlock_irqsave.hpp>
#include <fs/devfs/devfs.hpp>
#include <fs/fs.hpp>
#include <fs/procfs/procfs.hpp>
#include <fs/tmpfs/tmpfs.hpp>
#include <kpanic/kpanic.hpp>
#include <log/log.hpp>
#include <process/process.hpp>

namespace fs {

static MountPoint* g_root_mountpoint;
static kvector<MountPoint*> g_mountpoints;

static kspinlock_irqsave g_fs_spinlock;

void init()
{
    auto* devfs = new devfs::DevFileSystem{};
    auto* tmpfs = new tmpfs::TmpFileSystem{};
    auto* procfs = new procfs::ProcFileSystem{};

    mount("/dev", devfs, nullptr);
    mount("/tmp", tmpfs, nullptr);
    mount("/proc", procfs, nullptr);
}

Inode::Inode(MountPoint* mp)
    : mountpoint{mp}
{
}

ReadOnlyInode::ReadOnlyInode(MountPoint* mp)
    : Inode{mp}
{
}

DirectoryInode::DirectoryInode(MountPoint* mp)
    : Inode{mp}
{
    type = FileType::DIRECTORY;
    size = 0;
}

kstring getcwd(const Inode* inode)
{
    klist<kstring> path{};

    while (inode != nullptr) {
        if (inode->name.length() > 0) {
            path.push_front(inode->name);
        }

        inode = inode->parent;
    }

    return "/" + algo::join(path, '/');
}

MountPoint* find_mount_at(Inode* inode)
{
    for (auto* mp : g_mountpoints) {
        if (mp->mounted_on == inode) {
            return mp;
        }
    }

    return nullptr;
}

Inode* resolve_path(kstring_view path, const kvector<kstring>& tokens)
{
    if (!g_root_mountpoint) {
        kpanic("!! VFS: Root is NULL !!");
        return nullptr;
    }

    process::Process* proc = arch::percpu::current_process();
    Inode* current = nullptr;

    if (path.front() == '/' || proc == nullptr || proc->cwd_inode == nullptr) {
        current = g_root_mountpoint->root_inode;
    } else {
        current = proc->cwd_inode;
    }

    for (const kstring& token : tokens) {
        if (token.empty() || token == ".") {
            continue;
        }

        if (token == "..") {
            if (current == current->mountpoint->root_inode && current->mountpoint->mounted_on != nullptr) {
                current = current->mountpoint->mounted_on->parent;
            } else if (current->parent != nullptr) {
                current = current->parent;
            }

            continue;
        }

        Inode* next = current->lookup(token.c_str());

        if (!next) {
            return nullptr;
        }

        MountPoint* mp = find_mount_at(next);

        if (mp) {
            current = mp->root_inode;
        } else {
            current = next;
        }
    }

    return current;
}

Inode* resolve_path(kstring_view path)
{
    kvector<kstring> tokens = algo::split(path, '/');

    return resolve_path(path, tokens);
}

void register_mount(const char* path, MountPoint* mp)
{
    g_fs_spinlock.lock();
    mp->path = path;

    if (g_root_mountpoint == nullptr) {
        log::infof("VFS: root mounted at {}", path);
        g_root_mountpoint = mp;
    }

    g_mountpoints.push_back(mp);
    g_fs_spinlock.unlock();
}

void mount(const char* path, FileSystem* fs, const char* source)
{
    MountPoint* mp = fs->mount(source);
    mp->mounted_on = resolve_path(path);

    mp->root_inode->parent = mp->mounted_on;
    register_mount(path, mp);
}

FileDescriptor* open(kstring_view path, int flags)
{
    Inode* inode = resolve_path(path);

    if (!inode) {
        return nullptr;
    }

    auto* fd = new FileDescriptor{};

    fd->flags = flags;
    fd->inode = inode;
    fd->path = path;
    fd->offset = 0;

    inode->open(fd, flags);

    return fd;
}

int stat(const kstring& path, Stat* out)
{
    Inode* inode = resolve_path(path);

    if (!inode) {
        return -1;
    }

    return inode->stat(out);
}

int readdir(const kstring& path, kvector<DirEntry>& out)
{
    Inode* inode = resolve_path(path);

    if (!inode) {
        return -1;
    }

    if (inode->type != FileType::DIRECTORY) {
        return -1;
    }

    auto* dir = static_cast<DirectoryInode*>(inode);

    return dir->readdir(out);
}

int mkdir(const kstring& path, int mode)
{
    kvector<kstring> tokens = algo::split(path, '/');
    kstring new_dir = tokens.back();

    tokens.pop_back();

    Inode* inode = resolve_path(path, tokens);

    if (!inode) {
        return -1;
    }

    if (inode->type != FileType::DIRECTORY) {
        return -1;
    }

    auto* dir = static_cast<DirectoryInode*>(inode);

    return dir->mkdir(new_dir.c_str(), mode);
}

}
