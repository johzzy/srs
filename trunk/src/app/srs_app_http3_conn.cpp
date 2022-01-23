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

static nghttp3_ssize dump_http3_data(nghttp3_conn *conn, int64_t stream_id, nghttp3_vec *vec,
                             size_t veccnt, uint32_t *pflags, void *user_data,
                             void *stream_user_data) 
{
    SrsHttp3StreamThread* http3_stream = static_cast<SrsHttp3StreamThread*>(stream_user_data);
    void* buf = NULL;
    ssize_t nb = 0;
    bool eof = false;
    int ret = http3_stream->dump_data(&buf, &nb, eof);
    srs_trace("@john dump %d bytes, eof=%d", nb, eof);
    if (ret == 0) {
        return NGHTTP3_ERR_WOULDBLOCK;
    }

    if (eof) {
        *pflags |= NGHTTP3_DATA_FLAG_EOF;
    }

    vec[0].base = (uint8_t*)buf;
    vec[0].len = nb;
    return 1;
}


int cb_http3_acked_stream_data(nghttp3_conn *conn, int64_t stream_id,
                              uint64_t datalen, void *conn_user_data,
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
    srs_trace("@john, h3 stream %lld closed, error code=%lu", stream_id, app_error_code);
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

int cb_http3_end_stream(nghttp3_conn *conn, int64_t stream_id,
                        void *conn_user_data, void *stream_user_data) 
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

int cb_http3_shutdown(nghttp3_conn *conn, int64_t id,
                        void *conn_user_data)
{
    return 0;
}

SrsHttp3QuicConn::SrsHttp3QuicConn(SrsQuicServer* server, SrsQuicTransport* quic_conn, ISrsHttpServeMux* http_mux, ISrsHttpConnOwner* handler)
{
    http_mux_ = http_mux;
    handler_ = handler;

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
  	http3_cb_.end_stream = cb_http3_end_stream;
  	http3_cb_.reset_stream = cb_http3_reset_stream;
    http3_cb_.shutdown = cb_http3_shutdown;
    
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

            srs_trace("@john, try send stream_id=%ld %d bytes, ret=%d, nb=%d, fin=%d", stream_id, vec[i].len, ret, nb, fin);
            srs_assert(nb >= 0);
            nghttp3_conn_add_write_offset(http3_conn_, stream_id, nb);
        }
    }

    return err;
}

SrsHttp3ResponseWriter::SrsHttp3ResponseWriter(SrsHttp3StreamThread* stream, ISrsProtocolReadWriter* io)
    : SrsHttpResponseWriter(io)
    , http3_stream_(stream)
{
    set_chunked(false);
}

SrsHttp3ResponseWriter::~SrsHttp3ResponseWriter()
{
}

srs_error_t SrsHttp3ResponseWriter::final_request()
{
    srs_error_t err = srs_success;

    http3_stream_->data_eof_ = true;

    srs_trace("@john, final request");

    return err;
}

srs_error_t SrsHttp3ResponseWriter::send_header(char* data, int size)
{
    srs_error_t err = srs_success;
    
    if (header_sent) {
        return err;
    }
    header_sent = true;
    
    // detect content type
    if (srs_go_http_body_allowd(status)) {
        if (data && hdr->content_type().empty()) {
            hdr->set_content_type(srs_go_http_detect(data, size));
        }
    }
    
    // set server if not set.
    if (hdr->get("Server").empty()) {
        hdr->set("Server", RTMP_SIG_SRS_SERVER);
    }
    
    // Filter the header before writing it.
    if (hf && ((err = hf->filter(hdr)) != srs_success)) {
        return srs_error_wrap(err, "filter header");
    }

    vector<nghttp3_nv> http3_rsp_headers;

    std::map<std::string, std::string> headers = hdr->get_headers();
    std::stringstream ss;
    ss << status;
    http3_rsp_headers.push_back(make_http3_header(":status", ss.str()));
    srs_trace("status=%d", status);
    for (std::map<std::string, std::string>::iterator iter = headers.begin(); iter != headers.end(); ++iter) {
        if (iter->first == "Connection") {
            continue;
        }
        http3_rsp_headers.push_back(make_http3_header(iter->first, iter->second));
        srs_trace("http rsp header %s=%s", iter->first.c_str(), iter->second.c_str());
    }

    nghttp3_data_reader dr;
    dr.read_data = dump_http3_data;
    int ret = 0;
    if ((ret = nghttp3_conn_submit_response(http3_stream_->conn_->http3_conn_, http3_stream_->stream_id_, http3_rsp_headers.data(), 
                                            http3_rsp_headers.size(), &dr)) != 0) {
        return srs_error_new(ERROR_HTTP3, "nghttp3_conn_submit_response failed, ret=%d, err=%s", ret, nghttp3_strerror(ret));
    }

    srs_trace("stream_id=%ld http3 submit ret=%d", http3_stream_->stream_id_, ret);

    return err;
}

SrsHttp3StreamThread::SrsHttp3StreamThread(SrsHttp3QuicConn* conn, int64_t stream_id)
{
    header_completed_ = false;
    data_eof_ = false;
    cors_ = new SrsHttpCorsMux();

    trd_ = NULL;

    conn_ = conn;
    quic_conn_ = conn->quic_conn_;
    stream_id_ = stream_id;

    timeout_ = 1 * SRS_UTIME_SECONDS;

    buffer_ = new SrsQuicStreamWriteBuffer(8 * 1024 * 1024);
}

SrsHttp3StreamThread::~SrsHttp3StreamThread()
{
    srs_freep(cors_);
    srs_freep(trd_);
    srs_freep(buffer_);
}

srs_error_t SrsHttp3StreamThread::start()
{
    srs_error_t err = srs_success;

    trd_ = new SrsSTCoroutine("http3_quic_stream_thread", this);
    if ((err = trd_->start()) != srs_success) {
        return srs_error_wrap(err, "start http3 stream thread failed");
    }

    // initialize the cors, which will proxy to mux.
    // TODO: FIXME:
    bool v = true;
    if ((err = cors_->initialize(conn_->http_mux_, v)) != srs_success) {
        return srs_error_wrap(err, "init cors");
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

        srs_trace("stream_id=%ld, header_completed %d", stream_id_, header_completed_);
        if (header_completed_) {
            SrsHttp3ResponseWriter writer(this, this);
            // TODO: FIXME:
            process_request(&writer, &msg_);
        }
    }

    return err;
}

srs_error_t SrsHttp3StreamThread::process_request(ISrsHttpResponseWriter* w, ISrsHttpMessage* r)
{
    srs_error_t err = srs_success;
    
    /*
    srs_trace("HTTP #%d %s:%d %s %s, content-length=%" PRId64 "", rid, ip.c_str(), port,
        r->method_str().c_str(), r->url().c_str(), r->content_length());
    */
    
    // use cors server mux to serve http request, which will proxy to http_remux.
    if ((err = cors_->serve_http(w, r)) != srs_success) {
        return srs_error_wrap(err, "mux serve");
    }
    
    return err;
}

int SrsHttp3StreamThread::end_request_headers()
{
    srs_trace("stream=%ld, header complete", stream_id_);
    header_completed_ = true;

    // TODO: FIXME:
    // msg->set_basic(hp_header.type, hp_header.method, hp_header.status_code, hp_header.content_length);
    msg_.set_header(&header_, 0);
    msg_.set_connection(this);

    return 0;
}

int SrsHttp3StreamThread::acked_stream_data(int64_t stream_id, uint64_t datalen)
{
    int nb = buffer_->acked(datalen);
    srs_assert(nb == (int)datalen);

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
	nghttp3_vec n = nghttp3_rcbuf_get_buf(name);
	nghttp3_vec v = nghttp3_rcbuf_get_buf(value);

    std::string field_name(n.base, n.base + n.len);
    std::string field_value(v.base, v.base + v.len);
    srs_trace("field %s=%s", field_name.c_str(), field_value.c_str());
    switch (token) {
        case NGHTTP3_QPACK_TOKEN__PATH:
            srs_trace("@john #h3, uri=%s", field_value.c_str());
            msg_.set_url(field_value, false);
            break;
        case NGHTTP3_QPACK_TOKEN__METHOD:
            srs_trace("@john #h3, method=%s", field_value.c_str());
            break;
        case NGHTTP3_QPACK_TOKEN__AUTHORITY:
            srs_trace("@john #h3, authority=%s", field_value.c_str());
            break;
        default:
            header_.set(field_name, field_value);
            break;
    }

    return 0;
}

int SrsHttp3StreamThread::dump_data(void** buf, ssize_t* nb, bool& eof)
{
    srs_trace("@john, data_eof_=%d, unsend=%u", data_eof_, buffer_->size_unsend());
    if (buffer_->size_unsend() == 0) {
        return 0;
    }

    ssize_t size_to_write = srs_min(64 * 1024, buffer_->consecutive_size_unsend());

    *buf = buffer_->data_unsend();
    *nb = size_to_write;

    buffer_->sent(size_to_write);

    eof = data_eof_;

    return 1;
}

void SrsHttp3StreamThread::set_recv_timeout(srs_utime_t tm)
{
}

srs_utime_t SrsHttp3StreamThread::get_recv_timeout()
{
    return timeout_;
}

srs_error_t SrsHttp3StreamThread::read_fully(void* buf, size_t size, ssize_t* nread)
{
    return quic_conn_->read_fully(stream_id_, buf, size, nread, timeout_);
}

int64_t SrsHttp3StreamThread::get_recv_bytes()
{
    // TODO: FIXME:
    return 0;
}

int64_t SrsHttp3StreamThread::get_send_bytes()
{
    // TODO: FIXME:
    return 0;
}

srs_error_t SrsHttp3StreamThread::read(void* buf, size_t size, ssize_t* nread)
{
    return quic_conn_->read(stream_id_, buf, size, nread, timeout_);
}

void SrsHttp3StreamThread::set_send_timeout(srs_utime_t tm)
{
}

srs_utime_t SrsHttp3StreamThread::get_send_timeout()
{
    return timeout_;
}

srs_error_t SrsHttp3StreamThread::write(void* buf, size_t size, ssize_t* nwrite)
{
    srs_error_t err = srs_success;

    if (buffer_->size_unsend() == 0) {
        resume();
    }

    int nb = buffer_->write(buf, size);
    if (nwrite) {
        *nwrite = nb;
    }

    return err;
}

srs_error_t SrsHttp3StreamThread::writev(const iovec *iov, int iov_size, ssize_t* nwrite)
{
    srs_error_t err = srs_success;

    if (buffer_->size_unsend() == 0) {
        resume();
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

const SrsContextId& SrsHttp3StreamThread::get_id()
{
    return _srs_context->get_id(); 
}

std::string SrsHttp3StreamThread::desc() 
{ 
    return "h3"; 
}

std::string SrsHttp3StreamThread::remote_ip() 
{
    return "0.0.0.0"; 
}
