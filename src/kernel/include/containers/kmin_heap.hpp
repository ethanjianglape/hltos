#pragma once

#include "kvector.hpp"
#include <kassert/kassert.hpp>

#include <concepts>
#include <cstddef>

template <std::integral TKey, kvector_storable TValue>
class kmin_heap final {
private:
    class node final {
    public:
        TKey key;
        TValue value;
    };

    kvector<node> _array;

    std::size_t get_left(std::size_t i) const { return (i * 2) + 1; }
    std::size_t get_right(std::size_t i) const { return (i * 2) + 2; }
    std::size_t get_parent(std::size_t i) const { return (i - 1) / 2; }

    TKey get_key(std::size_t i) const { return _array[i].key; }
    TValue get_value(std::size_t i) const { return _array[i].value; }

    void swap(std::size_t i, std::size_t j)
    {
        node temp = _array[i];
        _array[i] = _array[j];
        _array[j] = temp;
    }

    void min_heapify(std::size_t root)
    {
        std::size_t l = get_left(root);
        std::size_t r = get_right(root);
        std::size_t smallest = root;

        if (l < size() && get_key(l) < get_key(root)) {
            smallest = l;
        }

        if (r < size() && get_key(r) < get_key(smallest)) {
            smallest = r;
        }

        if (smallest == root) {
            return;
        }

        swap(root, smallest);
        min_heapify(smallest);
    }

public:
    kmin_heap()
    {
    }

    std::size_t size() const { return _array.size(); }

    bool empty() const { return _array.empty(); }

    TValue peak() const
    {
        kassert(!empty());

        return _array.front().value;
    }

    void insert(TKey key, TValue value)
    {
        _array.emplace_back(key, value);

        std::size_t i = _array.size() - 1;

        while (i != 0) {
            std::size_t parent = get_parent(i);

            if (get_key(parent) <= get_key(i)) {
                break;
            }

            swap(i, parent);
            i = parent;
        }
    }

    void pop()
    {
        kassert(!empty());

        if (size() == 1) {
            _array.pop_back();
            return;
        }

        node back = _array.back();
        _array[0] = back;
        _array.pop_back();

        min_heapify(0);
    }
};
