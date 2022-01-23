/**
 * The MIT License (MIT)
 *
 * Copyright (c) 2013-2020 John
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef SRS_APP_HTTP3_QUIC_TRANSPORT_HPP
#define SRS_APP_HTTP3_QUIC_TRANSPORT_HPP

#include <nghttp3/nghttp3.h>
#include <ngtcp2/ngtcp2.h>
#include <sys/socket.h>

#include <deque>
#include <map>
#include <set>
#include <srs_app_conn.hpp>
#include <srs_app_hourglass.hpp>
#include <srs_app_http_conn.hpp>
#include <srs_app_listener.hpp>
#include <srs_app_quic_buffer.hpp>
#include <srs_app_quic_transport.hpp>
#include <srs_app_reload.hpp>
#include <srs_core.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_service_conn.hpp>
#include <srs_service_http_conn.hpp>
#include <srs_service_st.hpp>
#include <string>
#include <vector>

class SrsHttp3QuicTransport;

class SrsHttp3QuicStream : public SrsQuicStream
{
public:
    SrsHttp3QuicStream(int64_t stream_id, const SrsQuicStreamDirection& direction, const SrsQuicStreamState& state,
                       SrsHttp3QuicTransport* quic_transport);
    ~SrsHttp3QuicStream();

public:
    srs_error_t read_header(SrsHttpHeader** header, SrsHttpMessage** msg, srs_utime_t timeout);

public:
    bool is_qpack_stream()
    {
        return qpack_stream_;
    }
    void set_qpack_stream(bool b)
    {
        qpack_stream_ = b;
    }
    nghttp3_conn* get_nghttp3_conn();

public:
    int acked_stream_data(int64_t stream_id, uint64_t datalen);
    int resume();
    int recv_data(const uint8_t* data, size_t datalen);
    int recv_header(int32_t token, nghttp3_rcbuf* name, nghttp3_rcbuf* value, uint8_t flags);
    int end_request_headers();

private:
    SrsHttpMessage* msg_;
    SrsHttpHeader* header_;
    bool header_completed_;
    srs_cond_t header_completed_cond_;
    bool qpack_stream_;
};

// Quic transport base class, process quic packets.
class SrsHttp3QuicTransport : public SrsQuicTransport
{
    friend class SrsHttp3QuicStream;

public:
    SrsHttp3QuicTransport(SrsQuicMultiplexer* multiplexer, const SrsContextId& ctx_id);
    virtual ~SrsHttp3QuicTransport();

public:
    srs_error_t read_header(int64_t stream_id, SrsHttpHeader** header, SrsHttpMessage** msg, srs_utime_t timeout);

protected:
    virtual SrsQuicStream* create_new_stream(int64_t stream_id, const SrsQuicStreamDirection& direction,
                                             const SrsQuicStreamState& state);
    virtual srs_error_t write_data();
    virtual srs_error_t write_stream_data(int64_t stream_id, SrsQuicStreamWriteBuffer* buffer);

public:
    nghttp3_conn* get_nghttp3_conn()
    {
        return http3_conn_;
    }
    int64_t get_ctrl_stream_id() const
    {
        return ctrl_stream_id_;
    }
    int64_t get_qpack_enc_stream_id() const
    {
        return qpack_enc_stream_id_;
    }
    int64_t get_qpack_dec_stream_id() const
    {
        return qpack_dec_stream_id_;
    }

public:
    virtual int recv_stream_data(uint32_t flags, int64_t stream_id, uint64_t offset, const uint8_t* data,
                                 size_t datalen);
    virtual int acked_stream_data_offset(int64_t stream_id, uint64_t offset, uint64_t datalen);
    virtual int extend_max_remote_streams_bidi(uint64_t max_streams);
    virtual int extend_max_stream_data(int64_t stream_id, uint64_t max_data);
    int begin_request_headers(int64_t stream_id);

public:
    srs_error_t open_ctrl_stream();
    srs_error_t open_qpack_enc_stream();
    srs_error_t open_qpack_dec_stream();
    srs_error_t bind_qpack_stream();

protected:
    int64_t ctrl_stream_id_;
    int64_t qpack_enc_stream_id_;
    int64_t qpack_dec_stream_id_;
    nghttp3_conn* http3_conn_;
    nghttp3_callbacks http3_cb_;
    nghttp3_settings http3_settings_;
};

class SrsHttp3StreamReadWriter : public ISrsProtocolReadWriter
{
public:
    SrsHttp3StreamReadWriter(SrsHttp3QuicTransport* quic_transport, int64_t stream_id);
    ~SrsHttp3StreamReadWriter();
    // Interface ISrsProtocolReadWriter
public:
    virtual void set_recv_timeout(srs_utime_t tm);
    virtual srs_utime_t get_recv_timeout();
    virtual srs_error_t read_fully(void* buf, size_t size, ssize_t* nread);
    virtual int64_t get_recv_bytes();
    virtual int64_t get_send_bytes();
    virtual srs_error_t read(void* buf, size_t size, ssize_t* nread);
    virtual void set_send_timeout(srs_utime_t tm);
    virtual srs_utime_t get_send_timeout();
    virtual srs_error_t write(void* buf, size_t size, ssize_t* nwrite);
    virtual srs_error_t writev(const iovec* iov, int iov_size, ssize_t* nwrite);

private:
    srs_utime_t send_timeout_;
    srs_utime_t recv_timeout_;
    SrsHttp3QuicTransport* quic_transport_;
    int64_t stream_id_;
};

class SrsHttp3QuicResponseWriter : public SrsHttpResponseWriter
{
public:
    SrsHttp3QuicResponseWriter(SrsHttp3QuicTransport* quic_transport, int64_t stream_id, ISrsProtocolReadWriter* io);
    virtual ~SrsHttp3QuicResponseWriter();

public:
    virtual srs_error_t final_request();
    virtual srs_error_t send_header(char* data, int size);

private:
    SrsHttp3QuicTransport* quic_transport_;
    int64_t stream_id_;
};

#endif
