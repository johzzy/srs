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

#include <srs_app_quic_buffer.hpp>
#include <srs_core.hpp>
#include <srs_kernel_log.hpp>

SrsQuicStreamReadBuffer::SrsQuicStreamReadBuffer(int capacity)
{
    capacity_ = capacity;
    size_ = 0;
    write_pos_ = 0;
    read_pos_ = 0;
    buffer_ = new uint8_t[capacity_];
}

SrsQuicStreamReadBuffer::~SrsQuicStreamReadBuffer() { srs_freepa(buffer_); }

int SrsQuicStreamReadBuffer::write(const void *buf, int buf_size)
{
    if (size_ == capacity_) {
        return 0;
    }

    int size_write = 0;
    if (write_pos_ >= read_pos_) {
        size_write = srs_min(capacity_ - (write_pos_ - read_pos_), buf_size);
        int write_size_to_buffer_end = srs_min(capacity_ - write_pos_, size_write);
        memcpy(buffer_ + write_pos_, buf, write_size_to_buffer_end);

        int write_size_from_buffer_begin = size_write - write_size_to_buffer_end;
        if (write_size_from_buffer_begin > 0) {
            memcpy(buffer_, static_cast<const uint8_t *>(buf) + write_size_to_buffer_end, write_size_from_buffer_begin);
        }

        write_pos_ += size_write;
        write_pos_ %= capacity_;
    } else {
        size_write = srs_min(read_pos_ - write_pos_, buf_size);
        memcpy(buffer_ + write_pos_, buf, size_write);
        write_pos_ += size_write;
    }

    size_ += size_write;

    return size_write;
}

int SrsQuicStreamReadBuffer::read(void *buf, int buf_size)
{
    if (size_ == 0) {
        return 0;
    }

    int size_read = 0;
    if (write_pos_ == read_pos_) {
        size_read = srs_min(size_ - read_pos_, buf_size);
        if (buf) {
            memcpy(buf, buffer_ + read_pos_, size_read);
        }
        read_pos_ += size_read;
    } else if (write_pos_ > read_pos_) {
        size_read = srs_min((write_pos_ - read_pos_), buf_size);
        if (buf) {
            memcpy(buf, buffer_ + read_pos_, size_read);
        }
        read_pos_ += size_read;
    } else {
        int size_read_to_buffer_end = srs_min(capacity_ - read_pos_, buf_size);
        if (buf) {
            memcpy(buf, buffer_ + read_pos_, size_read_to_buffer_end);
        }

        int size_read_from_buffer_begin = srs_min(buf_size - size_read_to_buffer_end, write_pos_);
        if (size_read_from_buffer_begin && buf) {
            memcpy(static_cast<uint8_t *>(buf) + size_read_to_buffer_end, buffer_, size_read_from_buffer_begin);
        }

        size_read = size_read_to_buffer_end + size_read_from_buffer_begin;
        read_pos_ += size_read;
    }

    read_pos_ %= capacity_;
    size_ -= size_read;

    return size_read;
}

SrsQuicStreamWriteBuffer::SrsQuicStreamWriteBuffer(int capacity)
{
    capacity_ = capacity;
    size_ = 0;
    size_unsend_ = 0;
    write_pos_ = 0;
    send_pos_ = 0;
    acked_pos_ = 0;
    buffer_ = new uint8_t[capacity_];
}

SrsQuicStreamWriteBuffer::~SrsQuicStreamWriteBuffer() { srs_freepa(buffer_); }

int SrsQuicStreamWriteBuffer::write(const void *buf, int buf_size)
{
    if (size_ == capacity_) {
        return 0;
    }

    int size_write = 0;
    if (write_pos_ >= acked_pos_) {
        size_write = srs_min(capacity_ - (write_pos_ - acked_pos_), buf_size);
        int write_size_to_buffer_end = srs_min(capacity_ - write_pos_, size_write);
        memcpy(buffer_ + write_pos_, buf, write_size_to_buffer_end);

        int write_size_from_buffer_begin = size_write - write_size_to_buffer_end;
        if (write_size_from_buffer_begin > 0) {
            memcpy(buffer_, static_cast<const uint8_t *>(buf) + write_size_to_buffer_end, write_size_from_buffer_begin);
        }

        write_pos_ += size_write;
        write_pos_ %= capacity_;
    } else {
        size_write = srs_min(acked_pos_ - write_pos_, buf_size);
        memcpy(buffer_ + write_pos_, buf, size_write);
        write_pos_ += size_write;
    }

    size_ += size_write;
    size_unsend_ += size_write;

    return size_write;
}

uint8_t *SrsQuicStreamWriteBuffer::data_unsend() { return buffer_ + send_pos_; }

int SrsQuicStreamWriteBuffer::consecutive_size_unsend()
{
    if (size_unsend_ == 0) {
        return 0;
    }

    if (write_pos_ > send_pos_) {
        return write_pos_ - send_pos_;
    }

    return capacity_ - send_pos_;
}

int SrsQuicStreamWriteBuffer::sent(int data_size)
{
    if (size_unsend_ == 0) {
        return 0;
    }

    int size_sent = 0;
    if (write_pos_ == send_pos_) {
        size_sent = srs_min(size_unsend_ - send_pos_, data_size);
        send_pos_ += size_sent;
    } else if (write_pos_ > send_pos_) {
        size_sent = srs_min((write_pos_ - send_pos_), data_size);
        send_pos_ += size_sent;
    } else {
        int size_read_to_buffer_end = srs_min(capacity_ - send_pos_, data_size);
        int size_read_from_buffer_begin = srs_min(data_size - size_read_to_buffer_end, write_pos_);
        size_sent = size_read_to_buffer_end + size_read_from_buffer_begin;
        send_pos_ += size_sent;
    }
    send_pos_ %= capacity_;
    size_unsend_ -= size_sent;

    return size_sent;
}

int SrsQuicStreamWriteBuffer::acked(int data_size)
{
    if (size_ == 0) {
        return 0;
    }

    int size_acked = 0;
    if (send_pos_ == acked_pos_) {
        size_acked = srs_min(size_, data_size);
        acked_pos_ += size_acked;
    } else if (send_pos_ > acked_pos_) {
        size_acked = srs_min((send_pos_ - acked_pos_), data_size);
        acked_pos_ += size_acked;
    } else {
        int size_read_to_buffer_end = srs_min(capacity_ - acked_pos_, data_size);
        int size_read_from_buffer_begin = srs_min(data_size - size_read_to_buffer_end, send_pos_);
        size_acked = size_read_to_buffer_end + size_read_from_buffer_begin;
        acked_pos_ += size_acked;
    }

    acked_pos_ %= capacity_;
    size_ -= size_acked;

    return size_acked;
}
