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

#include <srs_app_http3_conn.hpp>

using namespace std;

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <unistd.h>

#include <srs_app_config.hpp>
#include <srs_app_http_api.hpp>
#include <srs_app_pithy_print.hpp>
#include <srs_app_quic_client.hpp>
#include <srs_app_quic_conn.hpp>
#include <srs_app_server.hpp>
#include <srs_app_source.hpp>
#include <srs_app_statistic.hpp>
#include <srs_app_utility.hpp>
#include <srs_core_autofree.hpp>
#include <srs_http_stack.hpp>
#include <srs_kernel_buffer.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_log.hpp>
#include <srs_protocol_utility.hpp>
#include <srs_rtmp_msg_array.hpp>
#include <srs_rtmp_stack.hpp>
#include <srs_service_st.hpp>
#include <srs_service_utility.hpp>
#include <sstream>

SrsHttp3Conn::SrsHttp3Conn(SrsQuicServer *server, SrsHttp3QuicTransport *quic_conn, ISrsHttpServeMux *http_mux,
                           ISrsHttpConnOwner *handler)
{
    http_mux_ = http_mux;
    handler_ = handler;

    quic_conn_ = quic_conn;
    trd_ = NULL;
}

SrsHttp3Conn::~SrsHttp3Conn()
{
    srs_freep(quic_conn_);
    srs_freep(trd_);
}

srs_error_t SrsHttp3Conn::start()
{
    srs_error_t err = srs_success;

    if ((err = start_ctrl_stream_thread()) != srs_success) {
        return srs_error_wrap(err, "start ctrl stream thread failed");
    }

    if ((err = start_qpack_dec_stream_thread()) != srs_success) {
        return srs_error_wrap(err, "start qpack dec stream thread failed");
    }

    if ((err = start_qpack_enc_stream_thread()) != srs_success) {
        return srs_error_wrap(err, "start qpack enc stream thread failed");
    }

    if ((err = quic_conn_->bind_qpack_stream()) != srs_success) {
        return srs_error_wrap(err, "bind qpack stream failed");
    }

    trd_ = new SrsSTCoroutine("h3_quic_conn", this);
    if ((err = trd_->start()) != srs_success) {
        return srs_error_wrap(err, "start http3 conn thread failed");
    }

    return err;
}

srs_error_t SrsHttp3Conn::start_ctrl_stream_thread()
{
    srs_error_t err = srs_success;
    if ((err = quic_conn_->open_ctrl_stream()) != srs_success) {
        return srs_error_wrap(err, "open ctrl stream failed");
    }

    int64_t ctrl_stream_id = quic_conn_->get_ctrl_stream_id();
    SrsHttp3StreamThread *ctrl_stream_trd = new SrsHttp3StreamThread(this, ctrl_stream_id);
    if ((err = ctrl_stream_trd->start()) != srs_success) {
        srs_freep(ctrl_stream_trd);
        return srs_error_wrap(err, "start ctrl stream thread failed");
    }

    stream_trds_.insert(make_pair(ctrl_stream_id, ctrl_stream_trd));

    return srs_success;
}

srs_error_t SrsHttp3Conn::start_qpack_enc_stream_thread()
{
    srs_error_t err = srs_success;
    if ((err = quic_conn_->open_qpack_enc_stream()) != srs_success) {
        return srs_error_wrap(err, "open qpack enc stream failed");
    }

    int64_t qpack_enc_stream_id = quic_conn_->get_qpack_enc_stream_id();
    SrsHttp3StreamThread *qpack_enc_stream_trd = new SrsHttp3StreamThread(this, qpack_enc_stream_id);
    if ((err = qpack_enc_stream_trd->start()) != srs_success) {
        srs_freep(qpack_enc_stream_trd);
        return srs_error_wrap(err, "start qpack enc stream thread failed");
    }

    stream_trds_.insert(make_pair(qpack_enc_stream_id, qpack_enc_stream_trd));

    return srs_success;
}

srs_error_t SrsHttp3Conn::start_qpack_dec_stream_thread()
{
    srs_error_t err = srs_success;
    if ((err = quic_conn_->open_qpack_dec_stream()) != srs_success) {
        return srs_error_wrap(err, "open qpack dec stream failed");
    }

    int64_t qpack_dec_stream_id = quic_conn_->get_qpack_dec_stream_id();
    SrsHttp3StreamThread *qpack_dec_stream_trd = new SrsHttp3StreamThread(this, qpack_dec_stream_id);
    if ((err = qpack_dec_stream_trd->start()) != srs_success) {
        srs_freep(qpack_dec_stream_trd);
        return srs_error_wrap(err, "start qpack dec stream thread failed");
    }

    stream_trds_.insert(make_pair(qpack_dec_stream_id, qpack_dec_stream_trd));

    return srs_success;
}

srs_error_t SrsHttp3Conn::cycle()
{
    srs_error_t err = srs_success;

    if ((err = do_cycle()) != srs_success) {
        srs_error("do http3 conn cycle failed, err=%s", srs_error_desc(err).c_str());
    }

    for (std::map<int64_t, SrsHttp3StreamThread *>::iterator iter = stream_trds_.begin(); iter != stream_trds_.end();
         ++iter) {
        SrsHttp3StreamThread *stream_trd = iter->second;
        srs_freep(stream_trd);
    }

    return err;
}

srs_error_t SrsHttp3Conn::do_cycle()
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

const SrsContextId &SrsHttp3Conn::get_id()
{
    return quic_conn_->get_id();
}

std::string SrsHttp3Conn::desc()
{
    return "Http3QuicConn";
}

srs_error_t SrsHttp3Conn::accept_stream()
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

    SrsHttp3StreamThread *trd = new SrsHttp3StreamThread(this, stream_id);
    if ((err = trd->start()) != srs_success) {
        srs_freep(trd);
        return srs_error_wrap(err, "http3 stream thread start failed");
    }

    stream_trds_.insert(make_pair(stream_id, trd));

    return err;
}

void SrsHttp3Conn::clean_zombie_stream_thread()
{
    std::map<int64_t, SrsHttp3StreamThread *>::iterator iter = stream_trds_.begin();
    while (iter != stream_trds_.end()) {
        SrsHttp3StreamThread *stream_trd = iter->second;
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

SrsHttp3StreamThread::SrsHttp3StreamThread(SrsHttp3Conn *conn, int64_t stream_id)
{
    cors_ = new SrsHttpCorsMux();

    trd_ = NULL;

    conn_ = conn;
    quic_conn_ = conn->quic_conn_;
    stream_id_ = stream_id;

    timeout_ = 1 * SRS_UTIME_SECONDS;
}

SrsHttp3StreamThread::~SrsHttp3StreamThread()
{
    srs_freep(cors_);
    srs_freep(trd_);
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

        // TODO: FIXME: add control/qpack stream thread.
        SrsHttpHeader *header = NULL;
        SrsHttpMessage *msg = NULL;
        SrsAutoFree(SrsHttpHeader, header);
        SrsAutoFree(SrsHttpMessage, msg);

        if ((err = quic_conn_->read_header(stream_id_, &header, &msg, timeout_)) != srs_success) {
            if (srs_error_code(err) != ERROR_QUIC_TIMEOUT) {
                return srs_error_wrap(err, "http3 stream read %ld failed", stream_id_);
            }
            srs_freep(err);
            continue;
        }

        srs_trace("@john, read http3 header");

        // TODO: FIXME:
        SrsHttp3StreamReadWriter quic_stream_rw(quic_conn_, stream_id_);
        SrsHttp3QuicResponseWriter h3_rsp_writer(quic_conn_, stream_id_, &quic_stream_rw);
        process_request(&h3_rsp_writer, msg);
    }

    return err;
}

srs_error_t SrsHttp3StreamThread::process_request(ISrsHttpResponseWriter *w, ISrsHttpMessage *r)
{
    srs_error_t err = srs_success;

    // use cors server mux to serve http request, which will proxy to
    // http_remux.
    if ((err = cors_->serve_http(w, r)) != srs_success) {
        return srs_error_wrap(err, "mux serve");
    }

    return err;
}
