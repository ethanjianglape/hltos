#include <fs/fs.hpp>
#include <log/log.hpp>
#include <process/process.hpp>
#include <scheduler/mechanism/scheduler_mechanism.hpp>
#include <userspace/userspace.hpp>

#include <cstdint>

namespace userspace {

void init()
{
    log::info("starting userspace at /bin/init");

    fs::FileDescriptor* fd = fs::open("/bin/init", 0);

    std::size_t size = fd->inode->size;
    std::uint8_t* data = new std::uint8_t[size];

    fd->inode->read(fd, data, size);

    auto* proc = new process::Process{};

    kvector<kstring> argv{};
    kvector<kstring> envp{};

    proc->build_stdio();
    proc->exec_elf64(data, size, argv, envp);

    scheduler::mechanism::add_process(proc);
}

}
