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

#include <srs_app_http3_conn.hpp>

using namespace std;

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>

#include <sstream>

#include <srs_core_autofree.hpp>
#include <srs_kernel_buffer.hpp>
#include <srs_kernel_rtc_rtp.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_log.hpp>
#include <srs_rtc_stun_stack.hpp>
#include <srs_rtmp_stack.hpp>
#include <srs_rtmp_msg_array.hpp>
#include <srs_app_utility.hpp>
#include <srs_app_config.hpp>
#include <srs_app_rtc_queue.hpp>
#include <srs_app_source.hpp>
#include <srs_app_server.hpp>
#include <srs_service_utility.hpp>
#include <srs_http_stack.hpp>
#include <srs_app_http_api.hpp>
#include <srs_app_statistic.hpp>
#include <srs_app_pithy_print.hpp>
#include <srs_service_st.hpp>
#include <srs_app_rtc_server.hpp>
#include <srs_app_rtc_source.hpp>
#include <srs_app_rtc_conn.hpp>
#include <srs_protocol_utility.hpp>
#include <srs_app_quic_client.hpp>
#include <srs_app_quic_conn.hpp>

int cb_http3_acked_stream_data(nghttp3_conn *conn, int64_t stream_id,
                              size_t datalen, void *conn_user_data,
                              void *stream_user_data) {
}

int cb_http3_stream_close(nghttp3_conn *conn, int64_t stream_id,
                          uint64_t app_error_code,
                          void *conn_user_data,
                          void *stream_user_data) {
}

int cb_http3_recv_data(nghttp3_conn *conn, int64_t stream_id,
                       const uint8_t *data, size_t datalen,
                       void *conn_user_data, void *stream_user_data) {
}

int cb_http3_deferred_consume(nghttp3_conn *conn, int64_t stream_id,
                              size_t consumed, void *conn_user_data,
                              void *stream_user_data) {
}

int cb_http3_begin_headers(nghttp3_conn *conn, int64_t stream_id,
                           void *conn_user_data,
                           void *stream_user_data) {
}

int cb_http3_recv_header(nghttp3_conn *conn, int64_t stream_id,
                         int32_t token, nghttp3_rcbuf *name,
                         nghttp3_rcbuf *value, uint8_t flags,
                         void *conn_user_data,
                         void *stream_user_data) {
}

int cb_http3_end_headers(nghttp3_conn *conn, int64_t stream_id,
                         void *conn_user_data,
                         void *stream_user_data) {
}

int cb_http3_begin_trailers(nghttp3_conn *conn, int64_t stream_id,
                           void *conn_user_data,
                           void *stream_user_data) {
}

int cb_http3_recv_trailer(nghttp3_conn *conn, int64_t stream_id,
                         int32_t token, nghttp3_rcbuf *name,
                         nghttp3_rcbuf *value, uint8_t flags,
                         void *conn_user_data,
                         void *stream_user_data) {
}

int cb_http3_end_trailers(nghttp3_conn *conn, int64_t stream_id,
                         void *conn_user_data,
                         void *stream_user_data) {
}

int cb_http3_begin_push_promise(nghttp3_conn *conn, int64_t stream_id,
                                int64_t push_id, void *conn_user_data,
                                void *stream_user_data) {
}

int cb_http3_recv_push_promise(nghttp3_conn *conn, int64_t stream_id,
                               int64_t push_id, int32_t token,
                               nghttp3_rcbuf *name,
                               nghttp3_rcbuf *value, uint8_t flags,
                               void *conn_user_data,
                               void *stream_user_data) {
}

int cb_http3_end_push_promise(nghttp3_conn *conn, int64_t stream_id,
                              int64_t push_id, void *conn_user_data,
                              void *stream_user_data) {
}

int cb_http3_end_stream(nghttp3_conn *conn, int64_t stream_id,
                        void *conn_user_data, void *stream_user_data) {
}

int cb_http3_cancel_push(nghttp3_conn *conn, int64_t push_id,
                         int64_t stream_id, void *conn_user_data,
                         void *stream_user_data) {
}

int cb_http3_send_stop_sending(nghttp3_conn *conn, int64_t stream_id,
                               uint64_t app_error_code,
                               void *conn_user_data,
                               void *stream_user_data) {
}

int cb_http3_push_stream(nghttp3_conn *conn, int64_t push_id,
                         int64_t stream_id, void *conn_user_data) {
}

int cb_http3_reset_stream(nghttp3_conn *conn, int64_t stream_id,
                          uint64_t app_error_code,
                          void *conn_user_data,
                          void *stream_user_data) {
}

SrsHttp3QuicConn::SrsHttp3QuicConn(SrsQuicServer* server, SrsQuicConnection* quic_conn)
{
    server_ = server;
    quic_conn_ = quic_conn;
    trd_ = NULL;
}

SrsHttp3QuicConn::~SrsHttp3QuicConn()
{
    srs_freep(quic_conn_);
    srs_freep(trd_);
}

srs_error_t SrsHttp3QuicConn::start()
{
    srs_error_t err = srs_success;

  	http3_cb_.acked_stream_data = cb_http3_acked_stream_data;
  	http3_cb_.stream_close = cb_http3_stream_close;
  	http3_cb_.recv_data = cb_http3_recv_data;
  	http3_cb_.deferred_consume = cb_http3_deferred_consume;
  	http3_cb_.begin_headers = cb_http3_begin_headers;
  	http3_cb_.recv_header = cb_http3_recv_header;
  	http3_cb_.end_headers = cb_http3_end_headers;
  	http3_cb_.begin_trailers = cb_http3_begin_trailers;
  	http3_cb_.recv_trailer = cb_http3_recv_trailer;
  	http3_cb_.end_trailers = cb_http3_end_trailers;
  	http3_cb_.begin_push_promise = cb_http3_begin_push_promise;
  	http3_cb_.recv_push_promise = cb_http3_recv_push_promise;
  	http3_cb_.end_push_promise = cb_http3_end_push_promise;
  	http3_cb_.cancel_push = cb_http3_cancel_push;
  	http3_cb_.send_stop_sending = cb_http3_send_stop_sending;
  	http3_cb_.push_stream = cb_http3_push_stream;
  	http3_cb_.end_stream = cb_http3_end_stream;
  	http3_cb_.reset_stream = cb_http3_reset_stream;
    
    nghttp3_settings_default(&http3_settings_);
    http3_settings_.qpack_max_table_capacity = 4096;
    http3_settings_.qpack_blocked_streams = 100;

    int ret = 0;
    if ((ret = nghttp3_conn_server_new(&http3_conn_, &http3_cb_, &http3_settings_, NULL, this)) != 0) {
        return srs_error_new(ERROR_HTTP3, "nghttp3_conn_server_new failed, ret=%d", ret);
    }

    ngtcp2_transport_params params;
    ngtcp2_conn_get_local_transport_params(quic_conn_->conn(), &params);

    nghttp3_conn_set_max_client_streams_bidi(http3_conn_,
                                             params.initial_max_streams_bidi);

    if ((err = start_ctrl_stream_thread()) != srs_success) {
        return srs_error_wrap(err, "start ctrl stream thread failed");
    }

    if ((err = start_qpack_enc_stream_thread()) != srs_success) {
        return srs_error_wrap(err, "start qpack enc stream thread failed");
    }

    if ((err = start_qpack_dec_stream_thread()) != srs_success) {
        return srs_error_wrap(err, "start qpack dec stream thread failed");
    }

    trd_ = new SrsSTCoroutine("rtc_forward_quic_conn", this);
    if ((err = trd_->start()) != srs_success) {
        return srs_error_wrap(err, "start rtc forward conn thread failed");
    }

    return err;
}

srs_error_t SrsHttp3QuicConn::start_ctrl_stream_thread() {
    srs_error_t err = srs_success;
    if ((err = quic_conn_->open_stream(&ctrl_stream_id_)) != srs_success) {
        return srs_error_wrap(err, "open ctrl stream failed");
    }

    nghttp3_conn_bind_control_stream(http3_conn_, ctrl_stream_id_);

    SrsHttp3StreamThread* ctrl_stream_trd = new SrsHttp3StreamThread(this, ctrl_stream_id_);
    if ((err = ctrl_stream_trd->start()) != srs_success) {
        srs_freep(ctrl_stream_trd);
        return srs_error_wrap(err, "start ctrl stream thread failed");
    }

    stream_trds_.insert(make_pair(ctrl_stream_id_, ctrl_stream_trd));
    
    return srs_success;
}

srs_error_t SrsHttp3QuicConn::start_qpack_enc_stream_thread() {
    srs_error_t err = srs_success;
    if ((err = quic_conn_->open_stream(&qpack_enc_stream_id_)) != srs_success) {
        return srs_error_wrap(err, "open ctrl stream failed");
    }

    nghttp3_conn_bind_control_stream(http3_conn_, qpack_enc_stream_id_);

    SrsHttp3StreamThread* qpack_enc_stream_trd = new SrsHttp3StreamThread(this, qpack_enc_stream_id_);
    if ((err = qpack_enc_stream_trd->start()) != srs_success) {
        srs_freep(qpack_enc_stream_trd);
        return srs_error_wrap(err, "start ctrl stream thread failed");
    }

    stream_trds_.insert(make_pair(qpack_enc_stream_id_, qpack_enc_stream_trd));
    
    return srs_success;
}

srs_error_t SrsHttp3QuicConn::start_qpack_dec_stream_thread() {
    srs_error_t err = srs_success;
    if ((err = quic_conn_->open_stream(&qpack_dec_stream_id_)) != srs_success) {
        return srs_error_wrap(err, "open ctrl stream failed");
    }

    nghttp3_conn_bind_control_stream(http3_conn_, qpack_dec_stream_id_);

    SrsHttp3StreamThread* qpack_dec_stream_trd = new SrsHttp3StreamThread(this, qpack_dec_stream_id_);
    if ((err = qpack_dec_stream_trd->start()) != srs_success) {
        srs_freep(qpack_dec_stream_trd);
        return srs_error_wrap(err, "start ctrl stream thread failed");
    }

    stream_trds_.insert(make_pair(qpack_dec_stream_id_, qpack_dec_stream_trd));
    
    return srs_success;
}

srs_error_t SrsHttp3QuicConn::cycle()
{
    srs_error_t err = srs_success;

    if ((err = do_cycle()) != srs_success) {
        srs_error("do rtc forward quic conn cycle failed, err=%s", srs_error_desc(err).c_str());
    }

    for (std::map<int64_t, SrsHttp3StreamThread*>::iterator iter = stream_trds_.begin();
            iter != stream_trds_.end(); ++iter) {
        SrsHttp3StreamThread* stream_trd = iter->second;
        srs_freep(stream_trd);
    }

    server_->remove(this);

    return err;
}

srs_error_t SrsHttp3QuicConn::do_cycle()
{
    srs_error_t err = srs_success;

    while (true) {
        if ((err = trd_->pull()) != srs_success) {
            return srs_error_wrap(err, "rtc forward quic conn thread failed");
        }

        if ((err = accept_stream()) != srs_success) {
            return srs_error_wrap(err, "quic accept stream failed");
        }

        clean_zombie_stream_thread();
    }

    return err;
}

srs_error_t SrsHttp3QuicConn::accept_stream()
{
    srs_error_t err = srs_success;

    int64_t stream_id = -1;
    if ((err = quic_conn_->accept_stream(SRS_UTIME_SECONDS, stream_id)) != srs_success) {
        if (srs_error_code(err) != ERROR_QUIC_TIMEOUT) {
            return srs_error_wrap(err, "accept stream failed");
        }
        srs_freep(err);
        return srs_success;
    }

    srs_trace("accept new stream %ld", stream_id);

    SrsHttp3StreamThread* trd = new SrsHttp3StreamThread(this, stream_id);
    if ((err = trd->start()) != srs_success) {
        srs_freep(trd);
        return srs_error_wrap(err, "http3 stream thread start failed");
    }

    stream_trds_.insert(make_pair(stream_id, trd));

    return err;
}

void SrsHttp3QuicConn::clean_zombie_stream_thread()
{
    std::map<int64_t, SrsHttp3StreamThread*>::iterator iter = stream_trds_.begin();
    while (iter != stream_trds_.end()) {
        SrsHttp3StreamThread* stream_trd = iter->second;
        srs_error_t err = srs_success;
        if ((err = stream_trd->pull()) != srs_success) {
            srs_freep(err);
            srs_freep(stream_trd);
            stream_trds_.erase(iter++);
        } else {
            ++iter;
        }
    }
}

const SrsContextId& SrsHttp3QuicConn::get_id()
{
    return quic_conn_->get_id();
}

std::string SrsHttp3QuicConn::desc()
{
    return "RtcForwardQuicConn";
}

SrsHttp3StreamThread::SrsHttp3StreamThread(SrsHttp3QuicConn* conn, int64_t stream_id)
{
    trd_ = NULL;

    quic_conn_ = conn->quic_conn_;
    stream_id_ = stream_id;

    timeout_ = 5 * SRS_UTIME_SECONDS;
}

SrsHttp3StreamThread::~SrsHttp3StreamThread()
{
    srs_freep(trd_);
}

srs_error_t SrsHttp3StreamThread::start()
{
    srs_error_t err = srs_success;

    trd_ = new SrsSTCoroutine("rtc_forward_quic_stream_thread", this);
    if ((err = trd_->start()) != srs_success) {
        return srs_error_wrap(err, "start rtc forward send thread failed");
    }

    return err;
}

srs_error_t SrsHttp3StreamThread::pull()
{
    if (trd_ == NULL) {
        return srs_error_new(ERROR_HTTP3, "null thread");
    }
    return trd_->pull();
}

srs_error_t SrsHttp3StreamThread::cycle()
{
    srs_error_t err = srs_success;

    if ((err = do_cycle()) != srs_success) {
        srs_error("http3 quic stream cycle failed, err=%s", srs_error_desc(err).c_str());
    }

    return quic_conn_->close(srs_error_code(err));
}

srs_error_t SrsHttp3StreamThread::do_cycle()
{
    srs_error_t err = srs_success;

    return err;
}
