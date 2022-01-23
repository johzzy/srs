/**
 * The MIT License (MIT)
 *
 * Copyright (c) 2013-2020 John
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef SRS_APP_QUIC_BUFFER_HPP
#define SRS_APP_QUIC_BUFFER_HPP

#include <srs_kernel_utility.hpp>

class SrsQuicStreamReadBuffer
{
public:
    SrsQuicStreamReadBuffer(int size);
    ~SrsQuicStreamReadBuffer();

public:
    int write(const void *data, int size);
    int read(void *data, int size);
    size_t size() const { return static_cast<size_t>(size_); }
    bool empty() const { return size_ == 0; }
    bool full() const { return size_ == capacity_; }

private:
    // Capacity of the buffer.
    int capacity_;
    // Current size of the buffer.
    int size_;
    // Start of  write position.
    int write_pos_;
    // Start of  read position.
    int read_pos_;
    // Ring buffer.
    uint8_t *buffer_;
};

class SrsQuicStreamWriteBuffer
{
public:
    SrsQuicStreamWriteBuffer(int size);
    ~SrsQuicStreamWriteBuffer();

public:
    int write(const void *data, int size);
    int acked(int size);
    int sent(int data_size);
    size_t size() const { return static_cast<size_t>(size_); }
    size_t size_unsend() const { return static_cast<size_t>(size_unsend_); }
    bool empty() const { return size_ == 0; }
    bool full() const { return size_ == capacity_; }
    uint8_t *data_unsend();
    int consecutive_size_unsend();

private:
    // Capacity of the buffer.
    int capacity_;
    // Current size of the buffer.
    int size_;
    // Current unsend size in buffer.
    int size_unsend_;
    // Next write position.
    int write_pos_;
    // Next send position.
    int send_pos_;
    // Current acked position.
    int acked_pos_;
    // Ring buffer.
    uint8_t *buffer_;
};

#endif
