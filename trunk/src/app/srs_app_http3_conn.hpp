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

#ifndef SRS_APP_HTTP3_CONN_HPP
#define SRS_APP_HTTP3_CONN_HPP

#include <srs_core.hpp>
#include <srs_app_listener.hpp>
#include <srs_service_st.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_rtmp_stack.hpp>
#include <srs_app_hybrid.hpp>
#include <srs_app_hourglass.hpp>
#include <srs_app_rtc_sdp.hpp>
#include <srs_app_reload.hpp>
#include <srs_kernel_rtc_rtp.hpp>
#include <srs_kernel_rtc_rtcp.hpp>
#include <srs_app_rtc_queue.hpp>
#include <srs_app_rtc_source.hpp>
#include <srs_app_rtc_dtls.hpp>
#include <srs_service_conn.hpp>
#include <srs_app_conn.hpp>
#include <srs_app_rtc_conn.hpp>
#include <srs_app_quic_conn.hpp>
#include <srs_app_quic_server.hpp>

#include <string>
#include <map>
#include <vector>
#include <sys/socket.h>

#include <nghttp3/nghttp3.h>

class SrsLiveReader;
class SrsQuicConnection;
class SrsHttp3StreamThread;

// TODO: FIXME: rename it.
// Process pull rtc stream requet, and send rtc stream over quic.
class SrsHttp3QuicConn : public ISrsResource, virtual public ISrsCoroutineHandler
                       , virtual public ISrsHourGlass
{
    friend class SrsHttp3StreamThread;
public:
    SrsHttp3QuicConn(SrsQuicServer* server, SrsQuicConnection* quic_conn);
    ~SrsHttp3QuicConn();

    srs_error_t start();
    virtual srs_error_t cycle();
private:
    srs_error_t do_cycle();
// Interface for ISrsResource
public:
    virtual const SrsContextId& get_id();
    virtual std::string desc();
// Interface for ISrsHourGlass
public:
    virtual srs_error_t notify(int event, srs_utime_t interval, srs_utime_t tick);
private:
    srs_error_t accept_stream();
    void clean_zombie_stream_thread();
private:
    srs_error_t start_ctrl_stream_thread();
    srs_error_t start_qpack_enc_stream_thread();
    srs_error_t start_qpack_dec_stream_thread();
    srs_error_t flush_h3_stream();
public:
    int begin_request_headers(int64_t stream_id);
private:
    SrsSTCoroutine* trd_;
    SrsHourGlass* timer_;
    SrsQuicServer* server_;
    SrsQuicConnection* quic_conn_;
    int64_t ctrl_stream_id_;
    int64_t qpack_enc_stream_id_;
    int64_t qpack_dec_stream_id_;
    nghttp3_conn* http3_conn_;
    nghttp3_callbacks http3_cb_;
    nghttp3_settings http3_settings_;
    std::map<int64_t, SrsHttp3StreamThread*> stream_trds_;
};

// TODO: FIXME: rename it.
// Process pull rtc stream requet, and send rtc stream over quic.
class SrsHttp3StreamThread : virtual public ISrsCoroutineHandler
{
public:
    SrsHttp3StreamThread(SrsHttp3QuicConn* conn, int64_t stream_id);
    ~SrsHttp3StreamThread();
public:
    srs_error_t start();
    srs_error_t pull();
    virtual srs_error_t cycle();
private:
    srs_error_t do_cycle();
public:
    int end_request_headers();
    int acked_stream_data(int64_t stream_id, uint64_t datalen);
    int resume();
    int recv_data(const uint8_t* data, size_t datalen);
    int recv_header(int32_t token, nghttp3_rcbuf *name, nghttp3_rcbuf *value, uint8_t flags);
    int read(void** buf, ssize_t* nb);
private:
    SrsHttp3QuicConn* conn_;
    SrsQuicConnection* quic_conn_;
    int64_t stream_id_;
    SrsSTCoroutine* trd_;
    srs_utime_t timeout_;

private:
    SrsLiveReader* live_reader_;
};

class SrsLiveReader : public ISrsCoroutineHandler, public ISrsLiveSourceHandler, public ISrsWriter
{
    friend class SrsHttp3StreamThread;
public:
    SrsLiveReader(SrsHttp3StreamThread* stream, SrsRequest* req);
    ~SrsLiveReader();
public:
    srs_error_t start();

// Interface for ISrsCoroutineHandler
public:
    virtual srs_error_t cycle();
// Interface for ISrsLiveSourceHandler
public:
    virtual srs_error_t on_publish(SrsLiveSource* s, SrsRequest* r)  { return srs_success; }
    virtual void on_unpublish(SrsLiveSource* s, SrsRequest* r) {}
// Interface for ISrsWriter
public:
   	virtual srs_error_t write(void* buf, size_t size, ssize_t* nwrite);
    virtual srs_error_t writev(const iovec *iov, int iov_size, ssize_t* nwrite);
public:

public:
    int read(void** buf, ssize_t* nb);
    int acked(uint64_t datalen);

private:
    SrsSTCoroutine* trd_;
    SrsHttp3StreamThread* stream_;
    SrsRequest* req_;

    SrsQuicStreamWriteBuffer* buffer_;
};

#endif
