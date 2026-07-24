#include "log/log.hpp"
#include <fs/procfs/proc_self.hpp>
#include <fs/procfs/procfs.hpp>

namespace fs::procfs {

ProcDirectoryInode::ProcDirectoryInode(ProcMountPoint* mp, int ino)
    : DirectoryInode{mp}
{
    this->ino = ino;
}

Inode* ProcDirectoryInode::lookup(const char* name)
{
    kstring name_str{name};

    auto proc_mp = static_cast<ProcMountPoint*>(mountpoint);

    if (name_str == "self") {
        return proc_mp->self_inode;
    }

    return nullptr;
}

int ProcDirectoryInode::readdir(kvector<DirEntry>& entries)
{
    entries.emplace_back("self", FileType::REGULAR);

    return entries.size();
}

int ProcDirectoryInode::mkdir(const char*, int)
{
    return 0;
}

int ProcDirectoryInode::create(const char*, int)
{
    return 0;
}

int ProcDirectoryInode::open(FileDescriptor*, int)
{
    return 0;
}

int ProcDirectoryInode::close(FileDescriptor*)
{
    return 0;
}

int ProcDirectoryInode::stat(Stat*)
{
    return 0;
}

ProcMountPoint::ProcMountPoint()
{
    int ino = PROCFS_ROOT_INODE;

    root_inode = new ProcDirectoryInode{this, ino++};
    self_inode = new ProcSelfInode{this, root_inode, ino++};
}

const char* ProcFileSystem::name()
{
    return "procfs";
}

MountPoint* ProcFileSystem::mount(const char*)
{
    return new ProcMountPoint{};
}

}
