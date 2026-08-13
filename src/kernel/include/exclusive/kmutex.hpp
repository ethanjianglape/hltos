#pragma once

#include "katomic.hpp"

class kmutex final {
private:
    katomic<int> _lock;

public:
    kmutex()
        : _lock{1}
    {
    }

    void lock();
    void unlock();
};
