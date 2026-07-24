#ifdef KERNEL_TESTS

#include <cerrno>
#include <fs/devfs/dev_null.hpp>
#include <fs/devfs/devfs.hpp>
#include <fs/fs.hpp>
#include <fs/tmpfs/tmpfs.hpp>
#include <log/log.hpp>
#include <test/test.hpp>

namespace test_fs {

// =========================================================================
// tmpfs unit tests (no global VFS state required)
// =========================================================================

void test_tmpfs_mount_has_root_dir()
{
    fs::tmpfs::TmpMountPoint mp{};
    test::assert_not_null(mp.root_inode, "tmpfs: mount has root inode");
    test::assert_eq(mp.root_inode->type, fs::FileType::DIRECTORY, "tmpfs: root inode is DIRECTORY");
}

void test_tmpfs_mkdir_creates_child()
{
    fs::tmpfs::TmpMountPoint mp{};
    auto* root = static_cast<fs::tmpfs::TmpDirectoryInode*>(mp.root_inode);

    int result = root->mkdir("mydir", 0);
    test::assert_eq(result, 0, "tmpfs: mkdir returns 0");

    fs::Inode* child = root->lookup("mydir");
    test::assert_not_null(child, "tmpfs: lookup finds mkdir'd dir");
    test::assert_eq(child->type, fs::FileType::DIRECTORY, "tmpfs: mkdir'd entry is DIRECTORY");
}

void test_tmpfs_mkdir_duplicate_fails()
{
    fs::tmpfs::TmpMountPoint mp{};
    auto* root = static_cast<fs::tmpfs::TmpDirectoryInode*>(mp.root_inode);

    root->mkdir("dup", 0);
    int result = root->mkdir("dup", 0);
    test::assert_ne(result, 0, "tmpfs: mkdir duplicate name returns error");
}

void test_tmpfs_create_makes_file()
{
    fs::tmpfs::TmpMountPoint mp{};
    auto* root = static_cast<fs::tmpfs::TmpDirectoryInode*>(mp.root_inode);

    int result = root->create("file.txt", 0);
    test::assert_eq(result, 0, "tmpfs: create returns 0");

    fs::Inode* child = root->lookup("file.txt");
    test::assert_not_null(child, "tmpfs: lookup finds create'd file");
    test::assert_eq(child->type, fs::FileType::REGULAR, "tmpfs: create'd entry is REGULAR");
}

void test_tmpfs_create_duplicate_fails()
{
    fs::tmpfs::TmpMountPoint mp{};
    auto* root = static_cast<fs::tmpfs::TmpDirectoryInode*>(mp.root_inode);

    root->create("dup.txt", 0);
    int result = root->create("dup.txt", 0);
    test::assert_ne(result, 0, "tmpfs: create duplicate name returns error");
}

void test_tmpfs_lookup_missing_returns_null()
{
    fs::tmpfs::TmpMountPoint mp{};
    auto* root = static_cast<fs::tmpfs::TmpDirectoryInode*>(mp.root_inode);

    fs::Inode* result = root->lookup("doesnotexist");
    test::assert_null(result, "tmpfs: lookup missing name returns nullptr");
}

void test_tmpfs_readdir_empty()
{
    fs::tmpfs::TmpMountPoint mp{};
    auto* root = static_cast<fs::tmpfs::TmpDirectoryInode*>(mp.root_inode);

    kvector<fs::DirEntry> entries;
    int result = root->readdir(entries);
    test::assert_eq(result, 0, "tmpfs: readdir empty dir returns 0");
    test::assert_eq(entries.size(), 0ul, "tmpfs: readdir empty dir has no entries");
}

void test_tmpfs_readdir_lists_children()
{
    fs::tmpfs::TmpMountPoint mp{};
    auto* root = static_cast<fs::tmpfs::TmpDirectoryInode*>(mp.root_inode);

    root->mkdir("subdir", 0);
    root->create("file.txt", 0);

    kvector<fs::DirEntry> entries;
    root->readdir(entries);
    test::assert_eq(entries.size(), 2ul, "tmpfs: readdir lists both children");

    bool found_dir = false;
    bool found_file = false;
    for (std::size_t i = 0; i < entries.size(); i++) {
        if (entries[i].name == "subdir" && entries[i].type == fs::FileType::DIRECTORY)
            found_dir = true;
        if (entries[i].name == "file.txt" && entries[i].type == fs::FileType::REGULAR)
            found_file = true;
    }
    test::assert_true(found_dir, "tmpfs: readdir contains created directory entry");
    test::assert_true(found_file, "tmpfs: readdir contains created file entry");
}

void test_tmpfs_dir_read_returns_eisdir()
{
    fs::tmpfs::TmpMountPoint mp{};
    auto* root = static_cast<fs::tmpfs::TmpDirectoryInode*>(mp.root_inode);

    fs::FileDescriptor fd{};
    fd.inode = root;
    char buf[16];
    int result = root->read(&fd, buf, sizeof(buf));
    test::assert_eq(result, -EISDIR, "tmpfs: directory read returns -EISDIR");
}

void test_tmpfs_dir_write_returns_eisdir()
{
    fs::tmpfs::TmpMountPoint mp{};
    auto* root = static_cast<fs::tmpfs::TmpDirectoryInode*>(mp.root_inode);

    fs::FileDescriptor fd{};
    fd.inode = root;
    const char* data = "hello";
    int result = root->write(&fd, data, 5);
    test::assert_eq(result, -EISDIR, "tmpfs: directory write returns -EISDIR");
}

void test_tmpfs_file_stat()
{
    fs::tmpfs::TmpMountPoint mp{};
    auto* root = static_cast<fs::tmpfs::TmpDirectoryInode*>(mp.root_inode);
    root->create("statme.txt", 0);
    auto* file = static_cast<fs::tmpfs::TmpFileInode*>(root->lookup("statme.txt"));

    fs::Stat st{};
    int result = file->stat(&st);
    test::assert_eq(result, 0, "tmpfs: file stat returns 0");
    test::assert_eq(st.type, fs::FileType::REGULAR, "tmpfs: file stat reports REGULAR");
}

void test_tmpfs_dir_stat()
{
    fs::tmpfs::TmpMountPoint mp{};
    auto* root = static_cast<fs::tmpfs::TmpDirectoryInode*>(mp.root_inode);

    fs::Stat st{};
    int result = root->stat(&st);
    test::assert_eq(result, 0, "tmpfs: dir stat returns 0");
    test::assert_eq(st.type, fs::FileType::DIRECTORY, "tmpfs: dir stat reports DIRECTORY");
}

void test_tmpfs_nested_mkdir()
{
    fs::tmpfs::TmpMountPoint mp{};
    auto* root = static_cast<fs::tmpfs::TmpDirectoryInode*>(mp.root_inode);

    root->mkdir("a", 0);
    auto* a = static_cast<fs::tmpfs::TmpDirectoryInode*>(root->lookup("a"));
    test::assert_not_null(a, "tmpfs: nested mkdir - parent 'a' found");

    int result = a->mkdir("b", 0);
    test::assert_eq(result, 0, "tmpfs: nested mkdir in subdir returns 0");

    fs::Inode* b = a->lookup("b");
    test::assert_not_null(b, "tmpfs: nested mkdir - 'b' found in 'a'");
    test::assert_eq(b->type, fs::FileType::DIRECTORY, "tmpfs: nested 'b' is DIRECTORY");
}

void test_tmpfs_write_then_read_roundtrip()
{
    fs::tmpfs::TmpMountPoint mp{};
    auto* root = static_cast<fs::tmpfs::TmpDirectoryInode*>(mp.root_inode);
    root->create("rw.txt", 0);
    auto* file = static_cast<fs::tmpfs::TmpFileInode*>(root->lookup("rw.txt"));

    fs::FileDescriptor fd{};
    fd.inode = file;

    const char* msg = "hello";
    int written = file->write(&fd, msg, 5);
    test::assert_eq(written, 5, "tmpfs: write returns byte count");
    test::assert_eq(fd.offset, 5ul, "tmpfs: write advances fd offset");

    fd.offset = 0;
    char buf[5] = {};
    int read = file->read(&fd, buf, sizeof(buf));

    test::assert_eq(read, 5, "tmpfs: read returns byte count");
    test::assert_eq(kstring(buf, 5), kstring("hello"), "tmpfs: read returns previously written data");
    test::assert_eq(fd.offset, 5ul, "tmpfs: read advances fd offset");
}

void test_tmpfs_write_updates_size()
{
    fs::tmpfs::TmpMountPoint mp{};
    auto* root = static_cast<fs::tmpfs::TmpDirectoryInode*>(mp.root_inode);
    root->create("size.txt", 0);
    auto* file = static_cast<fs::tmpfs::TmpFileInode*>(root->lookup("size.txt"));

    fs::FileDescriptor fd{};
    fd.inode = file;
    file->write(&fd, "hello world", 11);

    fs::Stat st{};
    file->stat(&st);
    test::assert_eq(st.size, static_cast<std::size_t>(11), "tmpfs: write grows inode size");
}

void test_tmpfs_read_short_at_eof()
{
    fs::tmpfs::TmpMountPoint mp{};
    auto* root = static_cast<fs::tmpfs::TmpDirectoryInode*>(mp.root_inode);
    root->create("short.txt", 0);
    auto* file = static_cast<fs::tmpfs::TmpFileInode*>(root->lookup("short.txt"));

    fs::FileDescriptor fd{};
    fd.inode = file;
    file->write(&fd, "hello", 5);

    fd.offset = 0;
    char buf[1024] = {};
    int read = file->read(&fd, buf, sizeof(buf));
    test::assert_eq(read, 5, "tmpfs: read clamps to remaining bytes, not requested count");
    test::assert_eq(fd.offset, 5ul, "tmpfs: short read advances offset only by bytes read");
}

void test_tmpfs_read_past_eof_returns_zero()
{
    fs::tmpfs::TmpMountPoint mp{};
    auto* root = static_cast<fs::tmpfs::TmpDirectoryInode*>(mp.root_inode);
    root->create("eof.txt", 0);
    auto* file = static_cast<fs::tmpfs::TmpFileInode*>(root->lookup("eof.txt"));

    fs::FileDescriptor fd{};
    fd.inode = file;
    file->write(&fd, "hi", 2);

    char buf[16];
    int read = file->read(&fd, buf, sizeof(buf));
    test::assert_eq(read, 0, "tmpfs: read at EOF returns 0");
}

void test_tmpfs_lseek_set()
{
    fs::tmpfs::TmpMountPoint mp{};
    auto* root = static_cast<fs::tmpfs::TmpDirectoryInode*>(mp.root_inode);
    root->create("seekset.txt", 0);
    auto* file = static_cast<fs::tmpfs::TmpFileInode*>(root->lookup("seekset.txt"));

    fs::FileDescriptor fd{};
    fd.inode = file;
    file->write(&fd, "hello world", 11);

    int result = file->lseek(&fd, 3, fs::SEEK_SET);
    test::assert_eq(result, 0, "tmpfs: lseek SEEK_SET returns 0");
    test::assert_eq(fd.offset, 3ul, "tmpfs: lseek SEEK_SET sets absolute offset");
}

void test_tmpfs_lseek_cur()
{
    fs::tmpfs::TmpMountPoint mp{};
    auto* root = static_cast<fs::tmpfs::TmpDirectoryInode*>(mp.root_inode);
    root->create("seekcur.txt", 0);
    auto* file = static_cast<fs::tmpfs::TmpFileInode*>(root->lookup("seekcur.txt"));

    fs::FileDescriptor fd{};
    fd.inode = file;
    fd.offset = 5;

    int result = file->lseek(&fd, 2, fs::SEEK_CUR);
    test::assert_eq(result, 0, "tmpfs: lseek SEEK_CUR returns 0");
    test::assert_eq(fd.offset, 7ul, "tmpfs: lseek SEEK_CUR adds to current offset");

    result = file->lseek(&fd, -3, fs::SEEK_CUR);
    test::assert_eq(result, 0, "tmpfs: lseek SEEK_CUR backwards returns 0");
    test::assert_eq(fd.offset, 4ul, "tmpfs: lseek SEEK_CUR can move backwards");
}

void test_tmpfs_lseek_end()
{
    fs::tmpfs::TmpMountPoint mp{};
    auto* root = static_cast<fs::tmpfs::TmpDirectoryInode*>(mp.root_inode);
    root->create("seekend.txt", 0);
    auto* file = static_cast<fs::tmpfs::TmpFileInode*>(root->lookup("seekend.txt"));

    fs::FileDescriptor fd{};
    fd.inode = file;
    file->write(&fd, "hello world", 11);

    int result = file->lseek(&fd, -4, fs::SEEK_END);
    test::assert_eq(result, 0, "tmpfs: lseek SEEK_END returns 0");
    test::assert_eq(fd.offset, 7ul, "tmpfs: lseek SEEK_END is relative to file size");
}

void test_tmpfs_lseek_negative_result_returns_einval()
{
    fs::tmpfs::TmpMountPoint mp{};
    auto* root = static_cast<fs::tmpfs::TmpDirectoryInode*>(mp.root_inode);
    root->create("seekneg.txt", 0);
    auto* file = static_cast<fs::tmpfs::TmpFileInode*>(root->lookup("seekneg.txt"));

    fs::FileDescriptor fd{};
    fd.inode = file;
    fd.offset = 2;

    int result = file->lseek(&fd, -10, fs::SEEK_CUR);
    test::assert_eq(result, -EINVAL, "tmpfs: lseek SEEK_CUR below 0 returns -EINVAL");
    test::assert_eq(fd.offset, 2ul, "tmpfs: failed lseek leaves offset unchanged");
}

void test_tmpfs_lseek_invalid_whence_returns_einval()
{
    fs::tmpfs::TmpMountPoint mp{};
    auto* root = static_cast<fs::tmpfs::TmpDirectoryInode*>(mp.root_inode);
    root->create("seekbad.txt", 0);
    auto* file = static_cast<fs::tmpfs::TmpFileInode*>(root->lookup("seekbad.txt"));

    fs::FileDescriptor fd{};
    fd.inode = file;

    int result = file->lseek(&fd, 0, 99);
    test::assert_eq(result, -EINVAL, "tmpfs: lseek with invalid whence returns -EINVAL");
}

// =========================================================================
// getcwd tests
// =========================================================================

void test_getcwd_root()
{
    fs::tmpfs::TmpMountPoint mp{};
    kstring result = fs::getcwd(mp.root_inode);
    test::assert_eq(result, kstring{"/"}, "getcwd: root inode returns '/'");
}

void test_getcwd_single_level()
{
    fs::tmpfs::TmpMountPoint mp{};
    auto* root = static_cast<fs::tmpfs::TmpDirectoryInode*>(mp.root_inode);
    root->mkdir("usr", 0);
    fs::Inode* usr = root->lookup("usr");

    kstring result = fs::getcwd(usr);
    test::assert_eq(result, kstring{"/usr"}, "getcwd: single level returns '/usr'");
}

void test_getcwd_two_levels()
{
    fs::tmpfs::TmpMountPoint mp{};
    auto* root = static_cast<fs::tmpfs::TmpDirectoryInode*>(mp.root_inode);

    root->mkdir("usr", 0);
    auto* usr = static_cast<fs::tmpfs::TmpDirectoryInode*>(root->lookup("usr"));
    usr->mkdir("bin", 0);
    fs::Inode* bin = usr->lookup("bin");

    kstring result = fs::getcwd(bin);
    test::assert_eq(result, kstring{"/usr/bin"}, "getcwd: two levels returns '/usr/bin'");
}

// =========================================================================
// devfs unit tests (no global VFS state required)
// =========================================================================

void test_devfs_null_read_returns_eof()
{
    fs::devfs::DevMountPoint mp{};
    fs::FileDescriptor fd{};
    fd.inode = mp.null_inode;

    char buf[16];
    int result = mp.null_inode->read(&fd, buf, sizeof(buf));
    test::assert_eq(result, 0, "devfs: /dev/null read returns 0 (EOF)");
}

void test_devfs_null_write_returns_count()
{
    fs::devfs::DevMountPoint mp{};
    fs::FileDescriptor fd{};
    fd.inode = mp.null_inode;

    const char* data = "discard me";
    int result = mp.null_inode->write(&fd, data, 10);
    test::assert_eq(result, 10, "devfs: /dev/null write returns byte count");
}

void test_devfs_null_stat()
{
    fs::devfs::DevMountPoint mp{};
    fs::Stat st{};
    int result = mp.null_inode->stat(&st);
    test::assert_eq(result, 0, "devfs: /dev/null stat returns 0");
    test::assert_eq(st.type, fs::FileType::CHAR_DEVICE, "devfs: /dev/null stat reports CHAR_DEVICE");
    test::assert_eq(st.size, static_cast<std::size_t>(0), "devfs: /dev/null stat size is 0");
}

void test_devfs_dir_lookup_null()
{
    fs::devfs::DevMountPoint mp{};
    auto* root = static_cast<fs::devfs::DevDirectoryInode*>(mp.root_inode);

    fs::Inode* result = root->lookup("null");
    test::assert_not_null(result, "devfs: lookup 'null' returns non-null");
    test::assert_eq(result->type, fs::FileType::CHAR_DEVICE, "devfs: 'null' is CHAR_DEVICE");
}

void test_devfs_dir_lookup_tty1()
{
    fs::devfs::DevMountPoint mp{};
    auto* root = static_cast<fs::devfs::DevDirectoryInode*>(mp.root_inode);

    fs::Inode* result = root->lookup("tty1");
    test::assert_not_null(result, "devfs: lookup 'tty1' returns non-null");
    test::assert_eq(result->type, fs::FileType::CHAR_DEVICE, "devfs: 'tty1' is CHAR_DEVICE");
}

void test_devfs_dir_lookup_missing()
{
    fs::devfs::DevMountPoint mp{};
    auto* root = static_cast<fs::devfs::DevDirectoryInode*>(mp.root_inode);

    fs::Inode* result = root->lookup("nonexistent");
    test::assert_null(result, "devfs: lookup nonexistent returns nullptr");
}

void test_devfs_readdir_has_devices()
{
    fs::devfs::DevMountPoint mp{};
    auto* root = static_cast<fs::devfs::DevDirectoryInode*>(mp.root_inode);

    kvector<fs::DirEntry> entries;
    root->readdir(entries);
    test::assert_eq(entries.size(), 3ul, "devfs: readdir returns 3 entries");

    bool found_null = false;
    bool found_tty1 = false;
    bool found_tty2 = false;
    for (std::size_t i = 0; i < entries.size(); i++) {
        if (entries[i].name == "null")
            found_null = true;
        if (entries[i].name == "tty1")
            found_tty1 = true;
        if (entries[i].name == "tty2")
            found_tty2 = true;
    }
    test::assert_true(found_null, "devfs: readdir contains 'null'");
    test::assert_true(found_tty1, "devfs: readdir contains 'tty1'");
    test::assert_true(found_tty2, "devfs: readdir contains 'tty2'");
}

// =========================================================================
// VFS integration tests (require kernel VFS to be initialized)
// =========================================================================

void test_vfs_open_existing_file()
{
    fs::FileDescriptor* fd = fs::open("/bin/shell", fs::O_RDONLY);
    test::assert_not_null(fd, "vfs: open /bin/shell returns fd");
    delete fd;
}

void test_vfs_open_nonexistent_file()
{
    fs::FileDescriptor* fd = fs::open("/nonexistent/file/path", fs::O_RDONLY);
    test::assert_null(fd, "vfs: open nonexistent file returns nullptr");
}

void test_vfs_open_dev_null()
{
    fs::FileDescriptor* fd = fs::open("/dev/null", fs::O_RDONLY);
    test::assert_not_null(fd, "vfs: open /dev/null returns fd");
    if (fd) {
        test::assert_eq(fd->inode->type, fs::FileType::CHAR_DEVICE, "vfs: /dev/null inode is CHAR_DEVICE");
        delete fd;
    }
}

void test_vfs_dev_null_read_returns_eof()
{
    fs::FileDescriptor* fd = fs::open("/dev/null", fs::O_RDONLY);
    test::assert_not_null(fd, "vfs: open /dev/null for read test");
    if (fd) {
        char buf[16];
        int result = fd->inode->read(fd, buf, sizeof(buf));
        test::assert_eq(result, 0, "vfs: /dev/null read returns 0 (EOF)");
        delete fd;
    }
}

void test_vfs_dev_null_write_returns_count()
{
    fs::FileDescriptor* fd = fs::open("/dev/null", fs::O_RDONLY);
    test::assert_not_null(fd, "vfs: open /dev/null for write test");
    if (fd) {
        const char* data = "test data";
        int result = fd->inode->write(fd, data, 9);
        test::assert_eq(result, 9, "vfs: /dev/null write returns byte count");
        delete fd;
    }
}

void test_vfs_stat_existing_file()
{
    fs::Stat st{};
    int result = fs::stat("/bin/shell", &st);
    test::assert_eq(result, 0, "vfs: stat /bin/shell returns 0");
}

void test_vfs_stat_nonexistent_file()
{
    fs::Stat st{};
    int result = fs::stat("/nonexistent/file", &st);
    test::assert_ne(result, 0, "vfs: stat nonexistent file returns error");
}

void test_vfs_stat_dev_null()
{
    fs::Stat st{};
    int result = fs::stat("/dev/null", &st);
    test::assert_eq(result, 0, "vfs: stat /dev/null returns 0");
    test::assert_eq(st.type, fs::FileType::CHAR_DEVICE, "vfs: stat /dev/null is CHAR_DEVICE");
}

void test_vfs_stat_directory()
{
    fs::Stat st{};
    int result = fs::stat("/bin", &st);
    test::assert_eq(result, 0, "vfs: stat /bin returns 0");
}

void test_vfs_stat_dev_tty()
{
    fs::Stat st{};
    int result = fs::stat("/dev/tty1", &st);
    test::assert_eq(result, 0, "vfs: stat /dev/tty1 returns 0");
    test::assert_eq(st.type, fs::FileType::CHAR_DEVICE, "vfs: stat /dev/tty1 is CHAR_DEVICE");
}

void test_vfs_stat_with_dotdot()
{
    fs::Stat st{};
    int result = fs::stat("/bin/../bin/shell", &st);
    test::assert_eq(result, 0, "vfs: stat with '..' resolves correctly");
}

void test_vfs_readdir_root()
{
    kvector<fs::DirEntry> entries;
    int result = fs::readdir("/", entries);
    test::assert_true(result >= 0, "vfs: readdir '/' returns non-negative");
    test::assert_true(entries.size() > 0, "vfs: readdir '/' has entries");

    bool found_bin = false;
    for (std::size_t i = 0; i < entries.size(); i++) {
        if (entries[i].name == "bin" && entries[i].type == fs::FileType::DIRECTORY)
            found_bin = true;
    }
    test::assert_true(found_bin, "vfs: readdir '/' contains 'bin' as DIRECTORY");
}

void test_vfs_readdir_bin()
{
    kvector<fs::DirEntry> entries;
    int result = fs::readdir("/bin", entries);
    test::assert_true(result >= 0, "vfs: readdir /bin returns non-negative");
    test::assert_true(entries.size() > 0, "vfs: readdir /bin has entries");

    bool found_shell = false;
    for (std::size_t i = 0; i < entries.size(); i++) {
        if (entries[i].name == "shell") {
            found_shell = true;
            test::assert_eq(entries[i].type, fs::FileType::REGULAR, "vfs: /bin/shell entry is REGULAR");
            break;
        }
    }
    test::assert_true(found_shell, "vfs: readdir /bin contains 'shell'");
}

void test_vfs_readdir_dev()
{
    kvector<fs::DirEntry> entries;
    int result = fs::readdir("/dev", entries);
    test::assert_true(result >= 0, "vfs: readdir /dev returns non-negative");

    bool found_null = false;
    for (std::size_t i = 0; i < entries.size(); i++) {
        if (entries[i].name == "null") {
            found_null = true;
            test::assert_eq(entries[i].type, fs::FileType::CHAR_DEVICE, "vfs: /dev/null entry is CHAR_DEVICE");
            break;
        }
    }
    test::assert_true(found_null, "vfs: readdir /dev contains 'null'");
}

void test_vfs_readdir_nonexistent()
{
    kvector<fs::DirEntry> entries;
    int result = fs::readdir("/nonexistent", entries);
    test::assert_eq(result, -1, "vfs: readdir nonexistent returns -1");
}

void test_vfs_readdir_on_file()
{
    kvector<fs::DirEntry> entries;
    int result = fs::readdir("/bin/shell", entries);
    test::assert_eq(result, -1, "vfs: readdir on file returns -1");
}

void run()
{
    log::info("Running filesystem tests...");

    // tmpfs unit tests
    test_tmpfs_mount_has_root_dir();
    test_tmpfs_mkdir_creates_child();
    test_tmpfs_mkdir_duplicate_fails();
    test_tmpfs_create_makes_file();
    test_tmpfs_create_duplicate_fails();
    test_tmpfs_lookup_missing_returns_null();
    test_tmpfs_readdir_empty();
    test_tmpfs_readdir_lists_children();
    test_tmpfs_dir_read_returns_eisdir();
    test_tmpfs_dir_write_returns_eisdir();
    test_tmpfs_file_stat();
    test_tmpfs_dir_stat();
    test_tmpfs_nested_mkdir();
    test_tmpfs_write_then_read_roundtrip();
    test_tmpfs_write_updates_size();
    test_tmpfs_read_short_at_eof();
    test_tmpfs_read_past_eof_returns_zero();
    test_tmpfs_lseek_set();
    test_tmpfs_lseek_cur();
    test_tmpfs_lseek_end();
    test_tmpfs_lseek_negative_result_returns_einval();
    test_tmpfs_lseek_invalid_whence_returns_einval();

    // getcwd tests
    test_getcwd_root();
    test_getcwd_single_level();
    test_getcwd_two_levels();

    // devfs unit tests
    test_devfs_null_read_returns_eof();
    test_devfs_null_write_returns_count();
    test_devfs_null_stat();
    test_devfs_dir_lookup_null();
    test_devfs_dir_lookup_tty1();
    test_devfs_dir_lookup_missing();
    test_devfs_readdir_has_devices();

    // VFS integration tests
    test_vfs_open_existing_file();
    test_vfs_open_nonexistent_file();
    test_vfs_open_dev_null();
    test_vfs_dev_null_read_returns_eof();
    test_vfs_dev_null_write_returns_count();
    test_vfs_stat_existing_file();
    test_vfs_stat_nonexistent_file();
    test_vfs_stat_dev_null();
    test_vfs_stat_directory();
    test_vfs_stat_dev_tty();
    test_vfs_stat_with_dotdot();
    test_vfs_readdir_root();
    test_vfs_readdir_bin();
    test_vfs_readdir_dev();
    test_vfs_readdir_nonexistent();
    test_vfs_readdir_on_file();
}
}

#endif // KERNEL_TESTS
