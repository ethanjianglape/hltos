#include "kassert/kassert.hpp"
#include <arch.hpp>
#include <console/console.hpp>
#include <containers/kstring.hpp>
#include <containers/kstring_view.hpp>
#include <containers/kvector.hpp>
#include <crt/crt.h>
#include <fs/devfs/dev_tty.hpp>
#include <fs/fs.hpp>
#include <fs/fs_file_ops.hpp>
#include <linux/ioctl.hpp>
#include <log/log.hpp>
#include <memory/memory.hpp>
#include <process/process.hpp>
#include <scheduler/mechanism/scheduler_mechanism.hpp>

#include <cerrno>

namespace fs::devfs {

using ScanCode = keyboard::ScanCode;
using ExtendedScanCode = keyboard::ExtendedScanCode;

// Scancode Set 1 to ASCII lookup table (lowercase, unshifted)
// Index = scancode value, value = ASCII char (0 = non-printable)
static constexpr char scancode_to_ascii_table[128] = {
    0,
    0,
    '1',
    '2',
    '3',
    '4',
    '5',
    '6', // 0x00-0x07
    '7',
    '8',
    '9',
    '0',
    '-',
    '=',
    0,
    0, // 0x08-0x0F (0x0E=Backspace, 0x0F=Tab)
    'q',
    'w',
    'e',
    'r',
    't',
    'y',
    'u',
    'i', // 0x10-0x17
    'o',
    'p',
    '[',
    ']',
    0,
    0,
    'a',
    's', // 0x18-0x1F (0x1C=Enter, 0x1D=LCtrl)
    'd',
    'f',
    'g',
    'h',
    'j',
    'k',
    'l',
    ';', // 0x20-0x27
    '\'',
    '`',
    0,
    '\\',
    'z',
    'x',
    'c',
    'v', // 0x28-0x2F (0x2A=LShift)
    'b',
    'n',
    'm',
    ',',
    '.',
    '/',
    0,
    '*', // 0x30-0x37 (0x36=RShift, 0x37=Keypad*)
    0,
    ' ',
    0,
    0,
    0,
    0,
    0,
    0, // 0x38-0x3F (0x38=LAlt, 0x39=Space, 0x3A=Caps, 0x3B-0x3F=F1-F5)
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    '7', // 0x40-0x47 (0x40-0x44=F6-F10, 0x45=Num, 0x46=Scroll, 0x47=Kp7)
    '8',
    '9',
    '-',
    '4',
    '5',
    '6',
    '+',
    '1', // 0x48-0x4F (Keypad)
    '2',
    '3',
    '0',
    '.',
    0,
    0,
    0,
    0, // 0x50-0x57 (Keypad, 0x57=F11)
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0, // 0x58-0x5F (0x58=F12)
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0, // 0x60-0x67
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0, // 0x68-0x6F
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0, // 0x70-0x77
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0, // 0x78-0x7F
};

char scancode_to_ascii(ScanCode code, bool caps)
{
    auto index = static_cast<std::uint8_t>(code);

    if (index >= 128) {
        return 0;
    }

    char ascii = scancode_to_ascii_table[index];

    if (caps && ascii >= 'a' && ascii <= 'z') {
        return ascii - 32;
    }

    return ascii;
}

DevTtyInode::DevTtyInode(MountPoint* mp, Inode* parent, int ino)
    : Inode{mp}
{
    this->type = FileType::CHAR_DEVICE;
    this->parent = parent;
    this->ino = ino;
}

void DevTtyInode::insert_char(char c)
{
    buffer.insert(buffer_index, c);
    buffer_index++;

    console::erase_in_line(console::get_cursor_x(), console::get_screen_cols());
    console::put(c);

    for (std::size_t i = buffer_index; i < buffer.size(); i++) {
        console::put(buffer[i]);
    }

    int len = buffer.size() - buffer_index;

    console::move_cursor(-len, 0);
}

void DevTtyInode::delete_back()
{
    if (buffer_index == 0) {
        return;
    }

    buffer.erase(buffer_index - 1);
    buffer_index--;

    console::move_cursor(-1, 0);
    console::erase_in_line(console::get_cursor_x(), console::get_screen_cols());

    for (std::size_t i = buffer_index; i < buffer.size(); i++) {
        console::put(buffer[i]);
    }

    int len = buffer.size() - buffer_index;

    console::move_cursor(-len, 0);
}

void DevTtyInode::delete_forward()
{
    if (buffer_index == buffer.size()) {
        return;
    }

    buffer.erase(buffer_index);

    console::erase_in_line(console::get_cursor_x(), console::get_screen_cols());

    for (std::size_t i = buffer_index; i < buffer.size(); i++) {
        console::put(buffer[i]);
    }

    int len = buffer.size() - buffer_index;

    console::move_cursor(-len, 0);
}

void DevTtyInode::move_left()
{
    if (buffer_index > 0) {
        buffer_index--;
        console::move_cursor(-1, 0);
    }
}

void DevTtyInode::move_right()
{
    if (buffer_index < buffer.size()) {
        buffer_index++;
        console::move_cursor(1, 0);
    }
}

void DevTtyInode::move_to_start()
{
    if (buffer_index == 0) {
        return;
    }

    console::move_cursor(-buffer_index, 0);
    buffer_index = 0;
}

void DevTtyInode::move_to_end()
{
    if (buffer_index == buffer.size()) {
        return;
    }

    std::size_t len = buffer.size() - buffer_index;
    console::move_cursor(len, 0);

    buffer_index = buffer.size();
}

void DevTtyInode::delete_to_end()
{
    buffer.truncate(buffer_index);
    console::erase_in_line(console::get_cursor_x(), console::get_screen_cols());
}

void DevTtyInode::add_buffer_history()
{
    // do not add empty string or back to back duplicates to history
    if (buffer.empty()) {
        return;
    }

    if (!history.empty() && buffer == history.back()) {
        return;
    }

    history.push_back(buffer);
    history_index = history.size();
}

void DevTtyInode::buffer_history_up()
{
    if (!history.empty() && history_index > 0) {
        move_to_start();
        delete_to_end();

        buffer = history[--history_index];
        buffer_index = buffer.size();

        console::put(buffer);
    }
}

void DevTtyInode::buffer_history_down()
{
    move_to_start();
    delete_to_end();

    if (history_index + 1 < history.size()) {
        buffer = history[++history_index];
        buffer_index = buffer.size();
    } else {
        buffer = "";
        buffer_index = buffer.size();
        history_index = history.size();
    }

    console::put(buffer);
}

void DevTtyInode::process_ctrl(keyboard::ScanCode c, keyboard::ExtendedScanCode)
{
    if (c == ScanCode::A) {
        move_to_start();
    } else if (c == ScanCode::E) {
        move_to_end();
    } else if (c == ScanCode::K) {
        delete_to_end();
    } else if (c == ScanCode::B) {
        move_left();
    } else if (c == ScanCode::F) {
        move_right();
    } else if (c == ScanCode::D) {
        delete_forward();
    }
}

void DevTtyInode::page_up()
{
    console::scroll_up();
}

void DevTtyInode::page_down()
{
    console::scroll_down();
}

void run_tty_program(const kstring& name)
{
    fs::FileDescriptor* fd = fs::open(name.c_str(), 0);

    kassert_not_null(fd);

    auto size = fd->inode->size;
    auto* data = new std::uint8_t[size];

    fd->inode->read(fd, data, size);

    auto* proc = new process::Process{};

    kvector<kstring> argv{};
    kvector<kstring> envp{};

    proc->build_stdio();
    proc->exec_elf64(data, size, argv, envp);

    scheduler::mechanism::add_process(proc);
}

void init_tty()
{
    run_tty_program("/bin/shell");
    // run_tty_program("/bin/musl");
}

int DevTtyInode::open(FileDescriptor*, int)
{
    return 0;
}

int DevTtyInode::read(FileDescriptor*, void* buff, std::size_t count)
{
    buffer = "";
    buffer_index = 0;

    while (true) {
        while (keyboard::KeyEvent* event = keyboard::poll()) {
            keyboard::ScanCode scancode = event->scancode;
            keyboard::ExtendedScanCode extended = event->extended_scancode;

            if (event->released) {
                continue;
            }

            bool caps = event->shift_held || event->caps_lock_on;
            bool ctrl = event->control_held;

            char ascii = scancode_to_ascii(scancode, caps);

            if (ctrl) {
                process_ctrl(scancode, extended);
            } else if (ascii != '\0') {
                insert_char(ascii);
            } else if (scancode == ScanCode::Backspace) {
                delete_back();
            } else if (scancode == ScanCode::Enter) {
                add_buffer_history();

                std::size_t len = buffer.size();
                kcopy_to_user(buff, buffer.c_str(), len > count ? count : len);
                console::newline();

                return len;
            } else if (extended == ExtendedScanCode::LeftArrow) {
                move_left();
            } else if (extended == ExtendedScanCode::RightArrow) {
                move_right();
            } else if (extended == ExtendedScanCode::Delete) {
                delete_forward();
            } else if (extended == ExtendedScanCode::UpArrow) {
                buffer_history_up();
            } else if (extended == ExtendedScanCode::DownArrow) {
                buffer_history_down();
            } else if (extended == ExtendedScanCode::PageUp) {
                page_up();
            } else if (extended == ExtendedScanCode::PageDown) {
                page_down();
            }

            console::redraw();
        }

        scheduler::mechanism::yield_blocked(process::WaitReason::KEYBOARD);
    }
}

int DevTtyInode::write(FileDescriptor*, const void* buffer, std::size_t count)
{
    char* str_buffer = new char[count];
    kcopy_from_user(str_buffer, buffer, count);

    kstring_view str(str_buffer, count);

    console::put(str);
    console::redraw();

    return str.size();
}

int DevTtyInode::close(FileDescriptor*)
{
    // TTY inode is static, nothing to free
    return 0;
}

int DevTtyInode::lseek(FileDescriptor*, int, int)
{
    return -ESPIPE;
}

int DevTtyInode::stat(Stat* stat)
{
    stat->size = 0;
    stat->type = FileType::CHAR_DEVICE;

    return 0;
}

int DevTtyInode::ioctl(unsigned long request, void* arg)
{
    if (request == linux::TIOCGWINSZ) {
        linux::winsize ws{};

        ws.ws_row = console::get_screen_rows();
        ws.ws_col = console::get_screen_cols();
        ws.ws_xpixel = 0;
        ws.ws_ypixel = 0;

        kcopy_to_user(arg, &ws, sizeof(linux::winsize));

        return 0;
    }

    return -ENOTTY;
}

}
