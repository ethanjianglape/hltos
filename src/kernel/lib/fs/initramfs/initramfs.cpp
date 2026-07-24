#include "algo/algo.hpp"
#include "arch/x64/memory/vmm.hpp"
#include "containers/kstring_view.hpp"
#include "containers/kvector.hpp"
#include "tar.hpp"

#include <cerrno>
#include <cstdint>
#include <fs/fs.hpp>
#include <fs/fs_file_ops.hpp>
#include <fs/initramfs/initramfs.hpp>
#include <log/log.hpp>
#include <memory/memory.hpp>

namespace fs::initramfs {

using namespace tar;

InitramfsDirectoryInode::InitramfsDirectoryInode(MountPoint* mp)
    : DirectoryInode{mp}
{
    mountpoint = mp;
    type = FileType::DIRECTORY;
}

Inode* InitramfsDirectoryInode::lookup(const char* name)
{
    for (std::size_t i = 0; i < children.size(); i++) {
        Inode* child = children[i];

        if (child->name == name) {
            return child;
        }
    }

    return nullptr;
}

int InitramfsDirectoryInode::readdir(kvector<DirEntry>& entries)
{
    for (std::size_t i = 0; i < children.size(); i++) {
        Inode* child = children[i];

        entries.emplace_back(child->name, child->type);
    }

    return children.size();
}

int InitramfsDirectoryInode::mkdir(const char*, int) { return -EROFS; }

int InitramfsDirectoryInode::create(const char*, int) { return -EROFS; }

int InitramfsDirectoryInode::open(FileDescriptor*, int) { return 0; }

int InitramfsDirectoryInode::close(FileDescriptor*) { return 0; }

int InitramfsDirectoryInode::stat(Stat*) { return 0; }

InitramfsFileInode::InitramfsFileInode(MountPoint* mp, std::uint8_t* data)
    : Inode{mp}
    , tar_data{data}
{
    type = FileType::REGULAR;
}

int InitramfsFileInode::open(FileDescriptor*, int) { return 0; }

int InitramfsFileInode::read(FileDescriptor*, void* buf, std::size_t count)
{
    if (arch::vmm::is_user_addr(buf)) {
        kcopy_to_user(buf, tar_data, count);
    } else {
        memcpy(buf, tar_data, count);
    }

    return count;
}

int InitramfsFileInode::write(FileDescriptor*, const void*, std::size_t) { return 0; }

int InitramfsFileInode::close(FileDescriptor*) { return 0; }

int InitramfsFileInode::lseek(FileDescriptor*, int, int) { return 0; }

int InitramfsFileInode::stat(Stat*) { return 0; }

bool InitramfsMountPoint::is_empty_header(TarHeader* header)
{
    return header->filename[0] == '\0';
}

void InitramfsMountPoint::insert_inode(const char* path, Inode* inode)
{
    auto* current = static_cast<InitramfsDirectoryInode*>(root_inode);

    kvector<kstring> tokens = algo::split(kstring_view{path}, '/');

    for (std::size_t i = 0; i < tokens.size() - 1; i++) {
        kstring& token = tokens[i];

        Inode* existing = current->lookup(token.c_str());

        if (existing) {
            // static_cast here is safe because only fs::initramfs has access
            // to the internal implementation of these data structures, so we trust
            // that `existing` is a valid directory inode
            current = static_cast<InitramfsDirectoryInode*>(existing);
        } else {
            // this token is part of a path that doesn't have an inode yet, maybe because
            // the tar file didn't include a tar entry for it
            auto* dir = new InitramfsDirectoryInode{this};
            dir->ino = next_ino++;
            dir->parent = current;
            dir->name = token;

            current->children.push_back(dir);

            current = dir;
        }
    }

    const kstring& last = tokens.back();

    inode->parent = current;
    inode->name = last;

    current->children.push_back(inode);
}

void InitramfsMountPoint::parse_tar_headers()
{
    log::info("initramfs: parsing tar headers");

    root_inode = new InitramfsDirectoryInode{this};
    root_inode->ino = next_ino++;
    root_inode->parent = nullptr;

    std::uint8_t* addr = tar_buffer;
    std::uint8_t* end = addr + tar_size;

    log::infof("initramfs: tar located at [{} - {}]", fmt::hex{addr}, fmt::hex{end});

    while (addr < end) {
        TarHeader* header = reinterpret_cast<TarHeader*>(addr);
        Inode* new_inode = nullptr;

        std::uintmax_t size = fmt::parse_uint((const char*)header->size, 12, fmt::NumberFormat::OCT);
        std::uintmax_t num_blocks = (size + 511) / 512;
        const char* filename = header->filename;

        if (is_empty_header(header)) {
            goto next;
        }

        if (filename[0] == '.' && filename[1] == '\0') {
            goto next;
        }

        if (filename[0] == '.' && filename[1] == '/') {
            filename += 2;
        }

        if (filename[0] == '\0') {
            goto next;
        }

        log::infof("initramfs: tar header");
        log::infof("  addr = {}", fmt::hex{header});
        log::infof("  size = {}", size);
        log::infof("  blocks = {}", num_blocks);
        log::infof("  name = {}", filename);

        if (header->typeflag == TYPEFLAG_DIR) {
            new_inode = new InitramfsDirectoryInode{this};

            new_inode->ino = next_ino++;
        } else {
            new_inode = new InitramfsFileInode{this, addr + 512};

            new_inode->size = size;
            new_inode->ino = next_ino++;
        }

        insert_inode(filename, new_inode);

    next:
        addr += 512 + (num_blocks * 512);
    }
}

const char* InitramfsFileSystem::name() { return "initramfs"; }

MountPoint* InitramfsFileSystem::mount(const char*) { return nullptr; }

MountPoint* InitramfsFileSystem::mount_from_mem(std::uint8_t* buffer, std::size_t size)
{
    return new InitramfsMountPoint{buffer, size};
}

}
