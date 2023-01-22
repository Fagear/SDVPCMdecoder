/**************************************************************************************************************************************************************
circbuffer.h

Copyright © 2023 Maksim Kryukov <fagear@mail.ru>

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.

Created: 2020-09

**************************************************************************************************************************************************************/

#ifndef CIRCBUFFER_H
#define CIRCBUFFER_H

#include <memory>
#include <mutex>
#include <vector>

// Circular buffer abstraction based on std::vector.
// Overflow overwrites oldest element.
template<class T, size_t N>
class circarray
{
public:
    explicit circarray()
    {
        arr_size = N;
        circ_data.resize(N);
        fill_cnt = head_i = tail_i = 0;
        is_full = false;
    }

    ~circarray()
    {
        circ_data.clear();
    }

//------------------------ Reset logic.
    void clear()
    {
        fill_cnt = head_i = tail_i = 0;
        is_full = false;
    }

//------------------------ Check if buffer is empty.
    bool empty()
    {
        return (fill_cnt==0);
    }

//------------------------ Check if buffer is full.
    bool full()
    {
        return is_full;
    }

//------------------------ Get maximum number of elements that can fit in the buffer.
    size_t max_size()
    {
        return arr_size;
    }

//------------------------ Get current number of used/filled elements.
    size_t size()
    {
        return fill_cnt;
    }

//------------------------ Add element into buffer (overwriting oldest element if full).
    void push(T in_obj)
    {
        std::lock_guard<std::mutex> lock(mtx);
        // Copy data into the front position.
        circ_data[head_i] = in_obj;
        // Check if buffer was full.
        if(is_full==true)
        {
            // Overwrite oldest element, advancing tail pointer with wrapping.
            tail_i = (tail_i+1)%arr_size;
            // Advance head pointer with wrapping.
            head_i = tail_i;
        }
        else
        {
            // Buffer was not full, increase fill counter.
            fill_cnt++;
            // Advance head pointer with wrapping.
            head_i = (head_i+1)%arr_size;
            // Check if buffer is full.
            is_full = (head_i==tail_i);
        }
    }

//------------------------ Remove oldest element from the buffer.
    T pop()
    {
        std::lock_guard<std::mutex> lock(mtx);
        // Check if buffer is empty.
        if(empty()==true)
        {
            return T();
        }
        // Copy data from the tail.
        T val = circ_data[tail_i];
        // There was something in the buffer, decrease fill counter.
        fill_cnt--;
        // Something was removed, buffer is 100% not full now.
        is_full = false;
        // Advance tail pointer with wrapping.
        tail_i = (tail_i+1)%arr_size;

        return val;
    }

//------------------------ Get a copy of a newest element.
    T front()
    {
        if(empty()==true)
        {
            return T();
        }
        else
        {
            return circ_data[head_i];
        }
    }

//------------------------ Get a copy of a oldest element.
    T back()
    {
        if(empty()==true)
        {
            return T();
        }
        else
        {
            return circ_data[tail_i];
        }
    }

//------------------------ Pick element at an index.
    T at(size_t ind)
    {
        if(ind<fill_cnt)
        {
            return this[ind];
        }
        else
        {
            return T();
        }
    }

    T operator[] (size_t ind)
    {
        ind = (ind+tail_i)%arr_size;
        return circ_data[ind];
    }

//------------------------ Fill the whole buffer.
    void fill(T in_obj)
    {
        clear();
        while(full()==false)
        {
            push(in_obj);
        }
    }

private:
    std::mutex mtx;
    std::vector<T> circ_data;
    size_t arr_size;
    size_t fill_cnt;
    size_t head_i;
    size_t tail_i;
    bool is_full;
};

#endif // CIRCBUFFER_H
