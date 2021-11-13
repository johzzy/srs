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
#include <srs_kernel_error.hpp>
#include <srs_kernel_log.hpp>
#include <srs_rtmp_stack.hpp>
#include <srs_rtmp_msg_array.hpp>
#include <srs_app_utility.hpp>
#include <srs_app_config.hpp>
#include <srs_app_source.hpp>
#include <srs_app_server.hpp>
#include <srs_service_utility.hpp>
#include <srs_http_stack.hpp>
#include <srs_app_http_api.hpp>
#include <srs_app_statistic.hpp>
#include <srs_app_pithy_print.hpp>
#include <srs_service_st.hpp>
#include <srs_protocol_utility.hpp>
#include <srs_app_quic_client.hpp>
#include <srs_app_quic_conn.hpp>

int cb_http3_acked_stream_data(nghttp3_conn *conn, int64_t stream_id,
                              size_t datalen, void *conn_user_data,
                              void *stream_user_data) 
{
    SrsHttp3StreamThread* http3_stream = static_cast<SrsHttp3StreamThread*>(stream_user_data);
    return http3_stream->acked_stream_data(stream_id, datalen);
}

int cb_http3_stream_close(nghttp3_conn *conn, int64_t stream_id,
                          uint64_t app_error_code,
                          void *conn_user_data,
                          void *stream_user_data) 
{
    return 0;
}

int cb_http3_recv_data(nghttp3_conn *conn, int64_t stream_id,
                       const uint8_t *data, size_t datalen,
                       void *conn_user_data, void *stream_user_data) 
{
    SrsHttp3StreamThread* http3_stream = static_cast<SrsHttp3StreamThread*>(stream_user_data);
    return http3_stream->recv_data(data, datalen);
}

int cb_http3_deferred_consume(nghttp3_conn *conn, int64_t stream_id,
                              size_t consumed, void *conn_user_data,
                              void *stream_user_data) 
{
    return 0;
}

int cb_http3_begin_headers(nghttp3_conn *conn, int64_t stream_id,
                           void *conn_user_data,
                           void *stream_user_data) 
{
    SrsHttp3QuicConn* http3_conn = static_cast<SrsHttp3QuicConn*>(conn_user_data);
    return http3_conn->begin_request_headers(stream_id);
}

int cb_http3_recv_header(nghttp3_conn *conn, int64_t stream_id,
                         int32_t token, nghttp3_rcbuf *name,
                         nghttp3_rcbuf *value, uint8_t flags,
                         void *conn_user_data,
                         void *stream_user_data) 
{
    SrsHttp3StreamThread* http3_stream = static_cast<SrsHttp3StreamThread*>(stream_user_data);
    return http3_stream->recv_header(token, name, value, flags);
}

int cb_http3_end_headers(nghttp3_conn *conn, int64_t stream_id,
                         void *conn_user_data,
                         void *stream_user_data) 
{
    SrsHttp3StreamThread* http3_stream = static_cast<SrsHttp3StreamThread*>(stream_user_data);
    return http3_stream->end_request_headers();
}

int cb_http3_begin_trailers(nghttp3_conn *conn, int64_t stream_id,
                           void *conn_user_data,
                           void *stream_user_data) 
{
    return 0;
}

int cb_http3_recv_trailer(nghttp3_conn *conn, int64_t stream_id,
                         int32_t token, nghttp3_rcbuf *name,
                         nghttp3_rcbuf *value, uint8_t flags,
                         void *conn_user_data,
                         void *stream_user_data) 
{
    return 0;
}

int cb_http3_end_trailers(nghttp3_conn *conn, int64_t stream_id,
                         void *conn_user_data,
                         void *stream_user_data) 
{
    return 0;
}

int cb_http3_begin_push_promise(nghttp3_conn *conn, int64_t stream_id,
                                int64_t push_id, void *conn_user_data,
                                void *stream_user_data) 
{
    return 0;
}

int cb_http3_recv_push_promise(nghttp3_conn *conn, int64_t stream_id,
                               int64_t push_id, int32_t token,
                               nghttp3_rcbuf *name,
                               nghttp3_rcbuf *value, uint8_t flags,
                               void *conn_user_data,
                               void *stream_user_data) 
{
    return 0;
}

int cb_http3_end_push_promise(nghttp3_conn *conn, int64_t stream_id,
                              int64_t push_id, void *conn_user_data,
                              void *stream_user_data) 
{
    return 0;
}

int cb_http3_end_stream(nghttp3_conn *conn, int64_t stream_id,
                        void *conn_user_data, void *stream_user_data) 
{
    return 0;
}

int cb_http3_cancel_push(nghttp3_conn *conn, int64_t push_id,
                         int64_t stream_id, void *conn_user_data,
                         void *stream_user_data) 
{
    return 0;
}

int cb_http3_send_stop_sending(nghttp3_conn *conn, int64_t stream_id,
                               uint64_t app_error_code,
                               void *conn_user_data,
                               void *stream_user_data) 
{
    return 0;
}

int cb_http3_push_stream(nghttp3_conn *conn, int64_t push_id,
                         int64_t stream_id, void *conn_user_data) 
{
    return 0;
}

int cb_http3_reset_stream(nghttp3_conn *conn, int64_t stream_id,
                          uint64_t app_error_code,
                          void *conn_user_data,
                          void *stream_user_data) 
{
    return 0;
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

    srs_freep(timer_);
}

static void nghttp3_debug_log_handler(const char *format, va_list args)
{
    static char buf[4096];
    int nb = vsnprintf(buf, sizeof(buf), format, args);
    if (nb > 0) {
        buf[nb - 1] = '\0';
        srs_trace("nghtt3 debug log # %s", buf);
    }
}

srs_error_t SrsHttp3QuicConn::start()
{
    srs_error_t err = srs_success;

    nghttp3_set_debug_vprintf_callback(nghttp3_debug_log_handler);

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
    http3_settings_.qpack_max_table_capacity = 1024*16;
    http3_settings_.qpack_blocked_streams = 20;

    int ret = 0;
    const nghttp3_mem* mem = nghttp3_mem_default();
    if ((ret = nghttp3_conn_server_new(&http3_conn_, &http3_cb_, &http3_settings_, mem, this)) != 0) {
        return srs_error_new(ERROR_HTTP3, "nghttp3_conn_server_new failed, ret=%d", ret);
    }

    quic_conn_->set_http3_conn(http3_conn_);

    ngtcp2_transport_params local_params;
    ngtcp2_conn_get_local_transport_params(quic_conn_->conn(), &local_params);

    // TODO: FIXME: just for debug
    if (true) {
        ngtcp2_transport_params remote_params;
        ngtcp2_conn_get_remote_transport_params(quic_conn_->conn(), &remote_params);
        srs_trace("local transport params, max_idle_timeout=%ld", local_params.max_idle_timeout);
        srs_trace("remote transport params, max_idle_timeout=%ld", remote_params.max_idle_timeout);
    }

    nghttp3_conn_set_max_client_streams_bidi(http3_conn_, local_params.initial_max_streams_bidi);

    if ((err = start_ctrl_stream_thread()) != srs_success) {
        return srs_error_wrap(err, "start ctrl stream thread failed");
    }

    if ((err = start_qpack_dec_stream_thread()) != srs_success) {
        return srs_error_wrap(err, "start qpack dec stream thread failed");
    }

    if ((err = start_qpack_enc_stream_thread()) != srs_success) {
        return srs_error_wrap(err, "start qpack enc stream thread failed");
    }

    if ((ret = nghttp3_conn_bind_qpack_streams(http3_conn_, qpack_enc_stream_id_, qpack_dec_stream_id_)) != 0) {
        return srs_error_new(ERROR_HTTP3, "bind qpack stream failed, ret=%d", ret);
    }

    trd_ = new SrsSTCoroutine("h3_quic_conn", this);
    if ((err = trd_->start()) != srs_success) {
        return srs_error_wrap(err, "start http3 conn thread failed");
    }

    timer_ = new SrsHourGlass("http3", this, 5 * SRS_UTIME_MILLISECONDS);
    if ((err = timer_->start()) != srs_success) {
        return srs_error_wrap(err, "start http3 timer failed");
    }

    timer_->tick(1, 5 * SRS_UTIME_MILLISECONDS);

    return err;
}

srs_error_t SrsHttp3QuicConn::start_ctrl_stream_thread() {
    srs_error_t err = srs_success;
    if ((err = quic_conn_->open_uni_stream(&ctrl_stream_id_)) != srs_success) {
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
    if ((err = quic_conn_->open_uni_stream(&qpack_enc_stream_id_)) != srs_success) {
        return srs_error_wrap(err, "open qpack enc stream failed");
    }

    SrsHttp3StreamThread* qpack_enc_stream_trd = new SrsHttp3StreamThread(this, qpack_enc_stream_id_);
    if ((err = qpack_enc_stream_trd->start()) != srs_success) {
        srs_freep(qpack_enc_stream_trd);
        return srs_error_wrap(err, "start qpack enc stream thread failed");
    }

    stream_trds_.insert(make_pair(qpack_enc_stream_id_, qpack_enc_stream_trd));
    
    return srs_success;
}

srs_error_t SrsHttp3QuicConn::start_qpack_dec_stream_thread() {
    srs_error_t err = srs_success;
    if ((err = quic_conn_->open_uni_stream(&qpack_dec_stream_id_)) != srs_success) {
        return srs_error_wrap(err, "open qpack dec stream failed");
    }

    SrsHttp3StreamThread* qpack_dec_stream_trd = new SrsHttp3StreamThread(this, qpack_dec_stream_id_);
    if ((err = qpack_dec_stream_trd->start()) != srs_success) {
        srs_freep(qpack_dec_stream_trd);
        return srs_error_wrap(err, "start qpack dec stream thread failed");
    }

    stream_trds_.insert(make_pair(qpack_dec_stream_id_, qpack_dec_stream_trd));
    
    return srs_success;
}

srs_error_t SrsHttp3QuicConn::cycle()
{
    srs_error_t err = srs_success;

    if ((err = do_cycle()) != srs_success) {
        srs_error("do http3 conn cycle failed, err=%s", srs_error_desc(err).c_str());
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
            return srs_error_wrap(err, "http3 quic conn thread failed");
        }

        if ((err = accept_stream()) != srs_success) {
            if (srs_error_code(err) != ERROR_QUIC_TIMEOUT) {
                return srs_error_wrap(err, "quic accept stream failed");
            }

            srs_freep(err);
        }

        clean_zombie_stream_thread();
    }

    return err;
}

const SrsContextId& SrsHttp3QuicConn::get_id()
{
    return quic_conn_->get_id();
}

std::string SrsHttp3QuicConn::desc()
{
    return "Http3QuicConn";
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

int SrsHttp3QuicConn::begin_request_headers(int64_t stream_id)
{
    std::map<int64_t, SrsHttp3StreamThread*>::iterator iter = stream_trds_.find(stream_id);
    if (iter == stream_trds_.end()) {
        return NGHTTP3_ERR_CALLBACK_FAILURE;
    }

    nghttp3_conn_set_stream_user_data(http3_conn_, stream_id, iter->second);
    return 0;
}

srs_error_t SrsHttp3QuicConn::notify(int event, srs_utime_t interval, srs_utime_t tick)
{
    return flush_h3_stream();
}

srs_error_t SrsHttp3QuicConn::flush_h3_stream()
{
    srs_error_t err = srs_success;
    int fin = 0;
    nghttp3_vec vec[8];
    int64_t stream_id = -1;
    int ret = nghttp3_conn_writev_stream(http3_conn_, &stream_id, &fin, vec, sizeof(vec) / sizeof(vec[0]));

    if (ret < 0) {
        srs_error("nghttp3_conn_writev_stream failed, err=%s", nghttp3_strerror(ret));
    } else if (ret > 0) {
        for (int i = 0; i < ret; ++i) {
            ssize_t nb = 0;
            srs_error_t err = quic_conn_->write(stream_id, vec[i].base, vec[i].len, &nb, 5 * SRS_UTIME_SECONDS);
            if (err != srs_success) {
                return srs_error_wrap(err, "quic conn write failed");
            }

            srs_assert(nb >= 0);
            nghttp3_conn_add_write_offset(http3_conn_, stream_id, nb);
        }
    }

    return err;
}

SrsHttp3StreamThread::SrsHttp3StreamThread(SrsHttp3QuicConn* conn, int64_t stream_id)
{
    trd_ = NULL;

    conn_ = conn;
    quic_conn_ = conn->quic_conn_;
    stream_id_ = stream_id;

    timeout_ = 1 * SRS_UTIME_SECONDS;

    live_reader_ = NULL;
}

SrsHttp3StreamThread::~SrsHttp3StreamThread()
{
    srs_freep(trd_);
    srs_freep(live_reader_);
}

srs_error_t SrsHttp3StreamThread::start()
{
    srs_error_t err = srs_success;

    trd_ = new SrsSTCoroutine("http3_quic_stream_thread", this);
    if ((err = trd_->start()) != srs_success) {
        return srs_error_wrap(err, "start http3 stream thread failed");
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

    while (true) {
        if ((err = trd_->pull()) != srs_success) {
            return srs_error_wrap(err, "http3 stream thread failed");
        }

        uint8_t buf[1500];
        ssize_t nb = 0;
        if ((err = quic_conn_->read(stream_id_, buf, sizeof(buf), &nb, timeout_)) != srs_success) {
            if (srs_error_code(err) != ERROR_QUIC_TIMEOUT) {
                return srs_error_wrap(err, "http3 stream read %ld failed", stream_id_);
            }
            srs_freep(err);
            continue;
        }

        int nconsumed = nghttp3_conn_read_stream(conn_->http3_conn_, stream_id_, buf, nb, 0);
        srs_trace("stream %ld recv %d bytes, consumed to h3 %d bytes", stream_id_, nb, nconsumed);
        if (nconsumed < 0) {
            return srs_error_new(ERROR_HTTP3, "nghttp3_conn_read_stream failed, err=%s", nghttp3_strerror(nconsumed));
        }
    }

    return err;
}

static std::string http3_rsp(1024*1024*64, 'x');

nghttp3_ssize read_data(nghttp3_conn *conn, int64_t stream_id, nghttp3_vec *vec,
                        size_t veccnt, uint32_t *pflags, void *user_data,
                        void *stream_user_data) 
{
  	vec[0].base = (uint8_t*)(http3_rsp.data());
  	vec[0].len = http3_rsp.size();

  	*pflags |= NGHTTP3_DATA_FLAG_EOF;

  	return 1;
}

nghttp3_ssize read_live_data(nghttp3_conn *conn, int64_t stream_id, nghttp3_vec *vec,
                             size_t veccnt, uint32_t *pflags, void *user_data,
                             void *stream_user_data) 
{
    SrsHttp3StreamThread* http3_stream = static_cast<SrsHttp3StreamThread*>(stream_user_data);
    void* buf = NULL;
    ssize_t nb = 0;
    int ret = http3_stream->read(&buf, &nb);
    if (ret == 0) {
        return NGHTTP3_ERR_WOULDBLOCK;
    }

    vec[0].base = (uint8_t*)buf;
    vec[0].len = nb;
    return 1;
}

static nghttp3_nv make_http3_header(const std::string& key, const std::string& value) 
{
    nghttp3_nv nv;
    nv.name = (uint8_t*)key.data();
    nv.value = (uint8_t*)value.data();
    nv.namelen = key.size();
    nv.valuelen = value.size();
    nv.flags = NGHTTP3_NV_FLAG_NONE;
    return nv;
}

int SrsHttp3StreamThread::end_request_headers()
{
    nghttp3_data_reader dr;
    string content_type = "text/plain";
    int64_t content_length = http3_rsp.size();
    static bool dynamic = true;
    if (! dynamic) {
        dr.read_data = read_data;
    } else {
        content_type = "application/octet-stream";
        content_length = -1;
        dr.read_data = read_live_data;
        SrsRequest req;
        req.vhost = SRS_CONSTS_RTMP_DEFAULT_VHOST;
        req.app = "live";
        req.stream = "livestream";
        live_reader_ = new SrsLiveReader(this, &req);
        srs_error_t err = live_reader_->start();
        if (err != srs_success) {
            srs_error("start live reader failed, err=%s", srs_error_desc(err).c_str());
            srs_freep(err);
            return -1;
        }
    }

    vector<nghttp3_nv> http3_rsp_headers;
    http3_rsp_headers.push_back(make_http3_header(":status", "200"));
    http3_rsp_headers.push_back(make_http3_header("server", "SRS"));
    http3_rsp_headers.push_back(make_http3_header("content-type", content_type));
    if (content_length > 0) {
        http3_rsp_headers.push_back(make_http3_header("content-length", std::to_string(content_length)));
    }

    int ret = 0;
    if ((ret = nghttp3_conn_submit_response(conn_->http3_conn_, stream_id_, http3_rsp_headers.data(), 
                                            http3_rsp_headers.size(), &dr)) != 0) {
        srs_error("nghttp3_conn_submit_response failed, ret=%d, err=%s", ret, nghttp3_strerror(ret));
        return -1;
    }

    srs_trace("http3 submit ret=%d", ret);

    return 0;
}

int SrsHttp3StreamThread::acked_stream_data(int64_t stream_id, uint64_t datalen)
{
    nghttp3_conn_resume_stream(conn_->http3_conn_, stream_id);
    // resume();

    live_reader_->acked(datalen);

    return 0;
}

int SrsHttp3StreamThread::resume()
{
    return nghttp3_conn_resume_stream(conn_->http3_conn_, stream_id_);
}

int SrsHttp3StreamThread::recv_data(const uint8_t* data, size_t datalen) 
{
    return 0;
}

int SrsHttp3StreamThread::recv_header(int32_t token, nghttp3_rcbuf *name, nghttp3_rcbuf *value, uint8_t flags) 
{
	nghttp3_vec v = nghttp3_rcbuf_get_buf(value);

    switch (token) {
        case NGHTTP3_QPACK_TOKEN__PATH:
            srs_trace("@john #h3, uri=%s", std::string(v.base, v.base + v.len).c_str());
            break;
        case NGHTTP3_QPACK_TOKEN__METHOD:
            srs_trace("@john #h3, method=%s", std::string(v.base, v.base + v.len).c_str());
            break;
        case NGHTTP3_QPACK_TOKEN__AUTHORITY:
            srs_trace("@john #h3, authority=%s", std::string(v.base, v.base + v.len).c_str());
            break;
        default:
            break;
    }

    return 0;
}

int SrsHttp3StreamThread::read(void** buf, ssize_t* nb)
{
    return live_reader_->read(buf, nb);
}

SrsLiveReader::SrsLiveReader(SrsHttp3StreamThread* stream, SrsRequest* req)
{
    req_ = req->copy();
    stream_ = stream;
    buffer_ = new SrsQuicStreamWriteBuffer(8 * 1024 * 1024);
}

SrsLiveReader::~SrsLiveReader()
{
    srs_freep(trd_);
    srs_freep(req_);
    srs_freep(buffer_);
}

srs_error_t SrsLiveReader::start()
{
    srs_error_t err = srs_success;
    trd_ = new SrsSTCoroutine("h3_read_live", this);
    if ((err = trd_->start()) != srs_success) {
        return srs_error_wrap(err, "start http3 read live thread failed");
    }
    return err;
}

srs_error_t SrsLiveReader::cycle()
{
    srs_error_t err = srs_success;

	SrsLiveSource* live_source = NULL;
    if ((err = _srs_sources->fetch_or_create(req_, this, &live_source)) != srs_success) {
        return srs_error_wrap(err, "create live_source");
    }
    
    srs_trace("h3 flv: source url=%s, source_id=%s/%s",
        req_->get_stream_url().c_str(), live_source->source_id().c_str(), live_source->pre_source_id().c_str());

    SrsLiveConsumer* live_consumer = new SrsLiveConsumer(live_source);
    SrsAutoFree(SrsLiveConsumer, live_consumer);

    if ((err = live_source->create_consumer(live_consumer)) != srs_success) {
        return srs_error_wrap(err, "create consumer failed");
    }

    if ((err = live_source->consumer_dumps(live_consumer)) != srs_success) {
        return srs_error_wrap(err, "consumer dumps failed");
    }

    SrsFlvTransmuxer* enc = new SrsFlvTransmuxer();
    SrsAutoFree(SrsFlvTransmuxer, enc);

    if ((err = enc->initialize(this)) != srs_success) {
        return srs_error_wrap(err, "initialize flv transmux");
    }

    int count = 0;
	SrsMessageArray msgs(SRS_PERF_MW_MSGS);

    bool flv_header_writed = false;

    while (true) {
        if ((err = trd_->pull()) != srs_success) {
            return srs_error_wrap(err, "http3 quic conn thread failed");
        }

        if ((err = live_consumer->dump_packets(&msgs, count)) != srs_success) {
            return srs_error_wrap(err, "consumer dump packets");
        }

        if (count <= 0) {
            srs_usleep(100 * SRS_UTIME_MILLISECONDS);
            continue;
        }

        if (! flv_header_writed) {
            flv_header_writed = true;
            enc->write_header(true, true);
        }

        for (int i = 0; i < count; i++) {
            SrsSharedPtrMessage* msg = msgs.msgs[i];
            
            if (msg->is_audio()) {
                err = enc->write_audio(msg->timestamp, msg->payload, msg->size);
            } else if (msg->is_video()) {
                err = enc->write_video(msg->timestamp, msg->payload, msg->size);
            } else {
                err = enc->write_metadata(msg->timestamp, msg->payload, msg->size);
            }
            
            if (err != srs_success) {
                srs_error("send messages failed, err=%s", srs_error_desc(err).c_str());
                srs_freep(err);
            }
            srs_freep(msg);
        }
    }

    return err;
}

srs_error_t SrsLiveReader::write(void* buf, size_t size, ssize_t* nwrite) 
{
    srs_error_t err = srs_success;
    if (buffer_->size_unsend() == 0) {
        stream_->resume();
    }

    int nb = buffer_->write(buf, size);
    if (nwrite) {
        *nwrite = nb;
    }
    return err;
}

srs_error_t SrsLiveReader::writev(const iovec *iov, int iov_size, ssize_t* nwrite)
{
    srs_error_t err = srs_success;

    if (buffer_->size_unsend() == 0) {
        stream_->resume();
    }

    int nb = 0;
    for (int i = 0; i < iov_size; ++i)
    {
        int n = buffer_->write(iov[i].iov_base, iov[i].iov_len);
        if (n <= 0) {
            break;
        }
        nb += n;
    }

    if (nwrite) {
        *nwrite = nb;
    }
    return err;
}

int SrsLiveReader::read(void** buf, ssize_t* nb)
{
    if (buffer_->size_unsend() == 0) {
        return 0;
    }

    ssize_t size_to_write = srs_min(64 * 1024, buffer_->consecutive_size_unsend());

    *buf = buffer_->data_unsend();
    *nb = size_to_write;
    
    buffer_->sent(size_to_write);

    return 1;
}

int SrsLiveReader::acked(uint64_t datalen)
{
    int nb = buffer_->acked(datalen);
    srs_assert(nb == (int)datalen);
    return 0;
}
