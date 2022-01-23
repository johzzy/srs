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

#include <srs_app_http3_quic_transport.hpp>

using namespace std;

#include <ngtcp2/ngtcp2_crypto.h>

#include <srs_app_config.hpp>
#include <srs_app_quic_client.hpp>
#include <srs_app_quic_io_loop.hpp>
#include <srs_app_quic_tls.hpp>
#include <srs_app_quic_util.hpp>
#include <srs_app_utility.hpp>
#include <srs_core_autofree.hpp>
#include <srs_kernel_buffer.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_log.hpp>
#include <srs_protocol_utility.hpp>
#include <srs_service_st.hpp>
#include <srs_service_utility.hpp>

static void nghttp3_debug_log_handler(const char *format, va_list args)
{
    static char buf[4096];
    int nb = vsnprintf(buf, sizeof(buf), format, args);
    if (nb > 0) {
        buf[nb - 1] = '\0';
        srs_trace("nghtt3 debug log # %s", buf);
    }
}

static nghttp3_nv make_http3_header(const std::string &key, const std::string &value)
{
    nghttp3_nv nv;
    nv.name = (uint8_t *)key.data();
    nv.value = (uint8_t *)value.data();
    nv.namelen = key.size();
    nv.valuelen = value.size();
    nv.flags = NGHTTP3_NV_FLAG_NONE;
    return nv;
}

static nghttp3_ssize dump_http3_data(nghttp3_conn *conn, int64_t stream_id, nghttp3_vec *vec, size_t veccnt,
                                     uint32_t *pflags, void *user_data, void *stream_user_data)
{
    SrsHttp3QuicStream *stream = static_cast<SrsHttp3QuicStream *>(stream_user_data);
    SrsQuicStreamWriteBuffer *write_buffer = stream->get_write_buffer();

    srs_trace("@john, stream=%ld, size_unsend=%d", stream_id, write_buffer->size_unsend());
    if (write_buffer->size_unsend() == 0) {
        srs_trace("@john, stream=%ld block because of write buffer", stream_id);
        return NGHTTP3_ERR_WOULDBLOCK;
    }

    vec[0].base = (uint8_t *)write_buffer->data_unsend();
    vec[0].len = write_buffer->size_unsend();

    if (stream->eof()) {
        *pflags |= NGHTTP3_DATA_FLAG_EOF;
    }

    return 1;
}

static int cb_http3_acked_stream_data(nghttp3_conn *conn, int64_t stream_id, uint64_t datalen, void *conn_user_data,
                                      void *stream_user_data)
{
    SrsHttp3QuicStream *http3_stream = static_cast<SrsHttp3QuicStream *>(stream_user_data);
    return http3_stream->acked_stream_data(stream_id, datalen);
}

static int cb_http3_stream_close(nghttp3_conn *conn, int64_t stream_id, uint64_t app_error_code, void *conn_user_data,
                                 void *stream_user_data)
{
    srs_trace("h3 stream %lld closed, error code=%lu", stream_id, app_error_code);
    return 0;
}

static int cb_http3_recv_data(nghttp3_conn *conn, int64_t stream_id, const uint8_t *data, size_t datalen,
                              void *conn_user_data, void *stream_user_data)
{
    SrsHttp3QuicStream *http3_stream = static_cast<SrsHttp3QuicStream *>(stream_user_data);
    return http3_stream->recv_data(data, datalen);
}

static int cb_http3_deferred_consume(nghttp3_conn *conn, int64_t stream_id, size_t consumed, void *conn_user_data,
                                     void *stream_user_data)
{
    return 0;
}

static int cb_http3_begin_headers(nghttp3_conn *conn, int64_t stream_id, void *conn_user_data, void *stream_user_data)
{
    SrsHttp3QuicTransport *http3_conn = static_cast<SrsHttp3QuicTransport *>(conn_user_data);
    return http3_conn->begin_request_headers(stream_id);
}

static int cb_http3_recv_header(nghttp3_conn *conn, int64_t stream_id, int32_t token, nghttp3_rcbuf *name,
                                nghttp3_rcbuf *value, uint8_t flags, void *conn_user_data, void *stream_user_data)
{
    SrsHttp3QuicStream *http3_stream = static_cast<SrsHttp3QuicStream *>(stream_user_data);
    return http3_stream->recv_header(token, name, value, flags);
}

static int cb_http3_end_headers(nghttp3_conn *conn, int64_t stream_id, void *conn_user_data, void *stream_user_data)
{
    SrsHttp3QuicStream *http3_stream = static_cast<SrsHttp3QuicStream *>(stream_user_data);
    return http3_stream->end_request_headers();
}

static int cb_http3_begin_trailers(nghttp3_conn *conn, int64_t stream_id, void *conn_user_data, void *stream_user_data)
{
    srs_trace("http3 begin trailers, stream_id=%ld", stream_id);
    return 0;
}

static int cb_http3_recv_trailer(nghttp3_conn *conn, int64_t stream_id, int32_t token, nghttp3_rcbuf *name,
                                 nghttp3_rcbuf *value, uint8_t flags, void *conn_user_data, void *stream_user_data)
{
    srs_trace("http3 recv trailers, stream_id=%ld", stream_id);
    return 0;
}

static int cb_http3_end_trailers(nghttp3_conn *conn, int64_t stream_id, void *conn_user_data, void *stream_user_data)
{
    srs_trace("http3 end trailers, stream_id=%ld", stream_id);
    return 0;
}

static int cb_http3_end_stream(nghttp3_conn *conn, int64_t stream_id, void *conn_user_data, void *stream_user_data)
{
    srs_trace("http3 stream end, stream_id=%ld", stream_id);
    return 0;
}

static int cb_http3_reset_stream(nghttp3_conn *conn, int64_t stream_id, uint64_t app_error_code, void *conn_user_data,
                                 void *stream_user_data)
{
    srs_trace("http3 stream reset, stream_id=%ld", stream_id);
    return 0;
}

static int cb_http3_shutdown(nghttp3_conn *conn, int64_t id, void *conn_user_data)
{
    srs_trace("http3 shutdown, id=%ld", id);
    return 0;
}

SrsHttp3QuicStream::SrsHttp3QuicStream(int64_t stream_id, const SrsQuicStreamDirection &direction,
                                       const SrsQuicStreamState &state, SrsHttp3QuicTransport *transport)
    : SrsQuicStream(stream_id, direction, state, transport)
{
    msg_ = new SrsHttpMessage();
    header_ = new SrsHttpHeader();
    header_completed_ = false;
    header_completed_cond_ = srs_cond_new();
    qpack_stream_ = false;
}

SrsHttp3QuicStream::~SrsHttp3QuicStream()
{
    srs_freep(msg_);
    srs_freep(header_);
    srs_cond_destroy(header_completed_cond_);
}

nghttp3_conn *SrsHttp3QuicStream::get_nghttp3_conn()
{
    return dynamic_cast<SrsHttp3QuicTransport *>(quic_transport_)->http3_conn_;
}

srs_error_t SrsHttp3QuicStream::read_header(SrsHttpHeader **header, SrsHttpMessage **msg, srs_utime_t timeout)
{
    srs_error_t err = srs_success;

    if (header_completed_) {
        *header = header_;
        *msg = msg_;

        msg_ = new SrsHttpMessage();
        header_ = new SrsHttpHeader();

        header_completed_ = false;
        return err;
    }

    int ret = srs_cond_timedwait(header_completed_cond_, timeout);
    if (ret != 0) {
        return srs_error_new(ERROR_QUIC_TIMEOUT, "quic accept stream timeout");
    }

    return err;
}

int SrsHttp3QuicStream::acked_stream_data(int64_t stream_id, uint64_t datalen)
{
    int nb = write_buffer_->acked(datalen);
    srs_assert(nb == (int)datalen);

    return 0;
}

int SrsHttp3QuicStream::resume()
{
    return nghttp3_conn_resume_stream(dynamic_cast<SrsHttp3QuicTransport *>(quic_transport_)->http3_conn_, stream_id_);
}

int SrsHttp3QuicStream::recv_data(const uint8_t *data, size_t datalen)
{
    return 0;
}

int SrsHttp3QuicStream::recv_header(int32_t token, nghttp3_rcbuf *name, nghttp3_rcbuf *value, uint8_t flags)
{
    nghttp3_vec n = nghttp3_rcbuf_get_buf(name);
    nghttp3_vec v = nghttp3_rcbuf_get_buf(value);

    std::string field_name(n.base, n.base + n.len);
    std::string field_value(v.base, v.base + v.len);
    srs_trace("field %s=%s", field_name.c_str(), field_value.c_str());
    switch (token) {
        case NGHTTP3_QPACK_TOKEN__PATH:
            srs_trace("@john #h3, uri=%s", field_value.c_str());
            msg_->set_url(field_value, false);
            break;
        case NGHTTP3_QPACK_TOKEN__METHOD:
            srs_trace("@john #h3, method=%s", field_value.c_str());
            break;
        case NGHTTP3_QPACK_TOKEN__AUTHORITY:
            srs_trace("@john #h3, authority=%s", field_value.c_str());
            break;
        default:
            header_->set(field_name, field_value);
            break;
    }

    return 0;
}

int SrsHttp3QuicStream::end_request_headers()
{
    srs_trace("stream=%ld, header complete", stream_id_);
    header_completed_ = true;
    srs_cond_signal(header_completed_cond_);

    // TODO: FIXME:
    msg_->set_header(header_, 0);
    // msg_.set_connection(this);

    return 0;
}

SrsHttp3QuicTransport::SrsHttp3QuicTransport(SrsQuicMultiplexer *multiplexer, const SrsContextId &ctx_id)
    : SrsQuicTransport(multiplexer, ctx_id)
{
    ctrl_stream_id_ = -1;
    qpack_enc_stream_id_ = -1;
    qpack_dec_stream_id_ = -1;
    http3_conn_ = NULL;

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
}

SrsHttp3QuicTransport::~SrsHttp3QuicTransport()
{
    nghttp3_conn_del(http3_conn_);
}

int SrsHttp3QuicTransport::recv_stream_data(uint32_t flags, int64_t stream_id, uint64_t offset, const uint8_t *data,
                                            size_t datalen)
{
    SrsQuicStream *stream = find_stream(stream_id);
    if (stream == NULL) {
        return -1;
    }

    int nconsumed = nghttp3_conn_read_stream(http3_conn_, stream_id, data, datalen, 0);
    if (nconsumed < 0) {
        srs_warn("nghttp3_conn_read_stream failed, err=%s", nghttp3_strerror(nconsumed));
        return -1;
    }

    int nb = stream->on_data(data, nconsumed);
    if (nb <= 0) {
        srs_warn("quic conn %s stream %ld no room to store incoming packet", get_conn_name().c_str(), stream_id);
        return -1;
    }

    if (nb < (int)datalen) {
        srs_warn("quic conn %s stream %ld partial data ack", get_conn_name().c_str(), stream_id);
    }

    // Quic stream level flow control.
    ngtcp2_conn_extend_max_stream_offset(conn_, stream_id, nb);
    ngtcp2_conn_extend_max_offset(conn_, nb);

    return 0;
}

int SrsHttp3QuicTransport::acked_stream_data_offset(int64_t stream_id, uint64_t offset, uint64_t datalen)
{
    int ret = nghttp3_conn_add_ack_offset(http3_conn_, stream_id, datalen);
    if (ret != 0) {
        return NGTCP2_ERR_CALLBACK_FAILURE;
    }

    SrsQuicStream *stream = find_stream(stream_id);
    if (stream == NULL) {
        return NGTCP2_ERR_CALLBACK_FAILURE;
    }

    if (stream->acked_stream_data_offset(offset, datalen) != 0) {
        return NGTCP2_ERR_CALLBACK_FAILURE;
    }

    stream->notify_writeable();

    return 0;
}

int SrsHttp3QuicTransport::extend_max_remote_streams_bidi(uint64_t max_streams)
{
    nghttp3_conn_set_max_client_streams_bidi(http3_conn_, max_streams);
    return 0;
}

int SrsHttp3QuicTransport::extend_max_stream_data(int64_t stream_id, uint64_t max_data)
{
    int ret = nghttp3_conn_unblock_stream(http3_conn_, stream_id);
    if (ret != 0) {
        srs_error("nghttp3_conn_unblock_stream %ld failed", stream_id);
        return -1;
    }
    return 0;
}

srs_error_t SrsHttp3QuicTransport::read_header(int64_t stream_id, SrsHttpHeader **header, SrsHttpMessage **msg,
                                               srs_utime_t timeout)
{
    srs_error_t err = srs_success;

    if (in_draininig()) {
        return srs_error_new(ERROR_QUIC_CLOSED, "quic conn closed");
    }

    SrsHttp3QuicStream *stream = dynamic_cast<SrsHttp3QuicStream *>(find_stream(stream_id));
    if (stream == NULL) {
        return srs_error_new(ERROR_QUIC_BAD_STREAM, "can not found quic stream %ld", stream_id);
    }

    if ((err = stream->read_header(header, msg, timeout)) != srs_success) {
        return srs_error_wrap(err, "read stream %ld faled", stream_id);
    }

    return srs_success;
}

SrsQuicStream *SrsHttp3QuicTransport::create_new_stream(int64_t stream_id, const SrsQuicStreamDirection &direction,
                                                        const SrsQuicStreamState &state)
{
    SrsQuicStream *new_stream = new SrsHttp3QuicStream(stream_id, direction, SrsQuicStreamStateOpened, this);
    return new_stream;
}

srs_error_t SrsHttp3QuicTransport::write_data()
{
    srs_error_t err = srs_success;
    srs_assert(conn_);

    if (ngtcp2_conn_is_in_closing_period(conn_)) {
        return srs_error_new(ERROR_QUIC_CLOSING, "quic conn in closing state");
    }

    if (in_draininig() || ngtcp2_conn_is_in_draining_period(conn_)) {
        return srs_error_new(ERROR_QUIC_DRAINING, "quic conn in draining state");
    }

    if ((err = write_stream_data(-1, NULL)) != srs_success) {
        srs_freep(err);
    }

    while (true) {
        int fin = 0;
        nghttp3_vec vec[1];
        int64_t stream_id = -1;
        int ret = nghttp3_conn_writev_stream(http3_conn_, &stream_id, &fin, vec, sizeof(vec) / sizeof(vec[0]));

        srs_trace("nghttp3_conn_writev_stream return %d", ret);

        if (ret <= 0) {
            break;
        }

        SrsHttp3QuicStream *stream = dynamic_cast<SrsHttp3QuicStream *>(find_stream(stream_id));
        srs_trace("@john, stream %ld, is_qpack_stream=%d, dump %d bytes data", stream_id, stream->is_qpack_stream(),
                  vec[0].len);
        if (stream->is_qpack_stream()) {
            stream->get_write_buffer()->write(vec[0].base, vec[0].len);
        }

        if ((err = stream->flush()) != srs_success) {
            srs_freep(err);
        }
    }

    return update_transport_timer();
}

srs_error_t SrsHttp3QuicTransport::write_stream_data(int64_t stream_id, SrsQuicStreamWriteBuffer *buffer)
{
    srs_error_t err = srs_success;

    ngtcp2_ssize ndatalen = 0;

    sockaddr_storage local_addr_storage;
    sockaddr_storage remote_addr_storage;
    ngtcp2_path path;
    path.local.addr = reinterpret_cast<sockaddr *>(&local_addr_storage);
    path.remote.addr = reinterpret_cast<sockaddr *>(&remote_addr_storage);

    size_t max_udp_payload_size = ngtcp2_conn_get_path_max_udp_payload_size(conn_);

    while (true) {
        // No more stream data to write.
        if (buffer && buffer->size_unsend() == 0) {
            break;
        }

        // Merge write, ngtcp2 will append multi small quic packet into one udp
        // packet if possiblity.
        uint32_t flags = NGTCP2_WRITE_STREAM_FLAG_MORE;

        if (buffer && ngtcp2_conn_get_max_data_left(conn_) < max_udp_payload_size) {
            return srs_error_new(ERROR_QUIC_AGAIN, "no data left in quic conn");
        }

        const uint8_t *data = buffer ? buffer->data_unsend() : NULL;
        size_t size = buffer ? buffer->consecutive_size_unsend() : 0;
        ngtcp2_tstamp pkt_ts = srs_get_system_time_for_quic();
        int nwrite = ngtcp2_conn_write_stream(conn_, &path, NULL, udp_send_buffer_, udp_send_buffer_size_, &ndatalen,
                                              flags, stream_id, data, size, pkt_ts);

        ngtcp2_conn_update_pkt_tx_time(conn_, pkt_ts);
        if (nwrite == 0) {
            return srs_error_new(ERROR_QUIC_AGAIN, "quic conn congested");
        }

        if (nwrite < 0) {
            switch (nwrite) {
                // Write failed becasue stream flow control.
                case NGTCP2_ERR_STREAM_DATA_BLOCKED: {
                    int r0 = 0;
                    if ((r0 = nghttp3_conn_block_stream(http3_conn_, stream_id)) != 0) {
                        srs_error("nghttp3_conn_block_stream %ld failed, err=%s", stream_id, nghttp3_strerror(r0));
                        // TODO: FIXME: return error.
                    }
                    srs_error("nghttp3_conn_block_stream %ld", stream_id);

                    return srs_error_new(ERROR_QUIC_AGAIN, "quic conn stream %ld block", stream_id);
                }
                // Write failed becasuse stream in half close(write direction).
                case NGTCP2_ERR_STREAM_SHUT_WR: {
                    return srs_error_new(ERROR_QUIC_CONN, "quic conn shutdown");
                }
                // Data has been cached, try merge write with next packet.
                case NGTCP2_ERR_WRITE_MORE: {
                    srs_trace("stream %ld write %d bytes", stream_id, ndatalen);
                    if (buffer) {
                        buffer->sent(ndatalen);
                    }
                    nghttp3_conn_add_write_offset(http3_conn_, stream_id, ndatalen);
                    continue;
                }
                default: {
                    srs_error("quic conn %s write stream %ld failed, err=%s", get_conn_name().c_str(), stream_id,
                              ngtcp2_strerror(nwrite));
                    srs_error_t err = on_error();
                    if (err != srs_success) {
                        srs_freep(err);
                    }

                    return srs_error_new(ERROR_QUIC_CONN, "quic conn unknown error");
                }
            }
        }

        if (ndatalen > 0) {
            if (buffer) {
                buffer->sent(ndatalen);
                nghttp3_conn_add_write_offset(http3_conn_, stream_id, ndatalen);
                srs_trace("stream %ld write %d bytes", stream_id, ndatalen);
            }
        }

        if ((err = update_idle_timer()) != srs_success) {
            srs_warn("update idle timer failed, err=%s", srs_error_desc(err).c_str());
            srs_freep(err);
        }

        // nwrite is the length of quic packet, include data and header,
        // ndatalen is the length of data.
        if (send_packet(&path, udp_send_buffer_, nwrite) <= 0) {
            return srs_error_new(ERROR_QUIC_UDP_SEND, "quic conn send udp packet error");
        }
    }

    return err;
}

int SrsHttp3QuicTransport::begin_request_headers(int64_t stream_id)
{
    SrsHttp3QuicStream *stream = dynamic_cast<SrsHttp3QuicStream *>(find_stream(stream_id));
    if (stream == NULL) {
        return NGTCP2_ERR_CALLBACK_FAILURE;
    }

    nghttp3_conn_set_stream_user_data(http3_conn_, stream_id, stream);
    return 0;
}

srs_error_t SrsHttp3QuicTransport::open_ctrl_stream()
{
    srs_error_t err = srs_success;
    if ((err = open_uni_stream(&ctrl_stream_id_)) != srs_success) {
        return srs_error_wrap(err, "open ctrl stream failed");
    }

    SrsHttp3QuicStream *stream = dynamic_cast<SrsHttp3QuicStream *>(find_stream(ctrl_stream_id_));
    stream->set_qpack_stream(true);
    nghttp3_conn_bind_control_stream(http3_conn_, ctrl_stream_id_);

    return err;
}

srs_error_t SrsHttp3QuicTransport::open_qpack_enc_stream()
{
    srs_error_t err = srs_success;
    if ((err = open_uni_stream(&qpack_enc_stream_id_)) != srs_success) {
        return srs_error_wrap(err, "open qpack enc stream failed");
    }

    SrsHttp3QuicStream *stream = dynamic_cast<SrsHttp3QuicStream *>(find_stream(qpack_enc_stream_id_));
    stream->set_qpack_stream(true);

    return err;
}

srs_error_t SrsHttp3QuicTransport::open_qpack_dec_stream()
{
    srs_error_t err = srs_success;
    if ((err = open_uni_stream(&qpack_dec_stream_id_)) != srs_success) {
        return srs_error_wrap(err, "open qpack dec stream failed");
    }

    SrsHttp3QuicStream *stream = dynamic_cast<SrsHttp3QuicStream *>(find_stream(qpack_dec_stream_id_));
    stream->set_qpack_stream(true);

    return err;
}

srs_error_t SrsHttp3QuicTransport::bind_qpack_stream()
{
    srs_error_t err = srs_success;
    int ret = 0;
    if ((ret = nghttp3_conn_bind_qpack_streams(http3_conn_, qpack_enc_stream_id_, qpack_dec_stream_id_)) != 0) {
        return srs_error_new(ERROR_HTTP3, "bind qpack stream failed, ret=%d", ret);
    }
    return err;
}

SrsHttp3StreamReadWriter::SrsHttp3StreamReadWriter(SrsHttp3QuicTransport *quic_transport, int64_t stream_id)
{
    quic_transport_ = quic_transport;
    stream_id_ = stream_id;
    send_timeout_ = SRS_UTIME_NO_TIMEOUT;
    recv_timeout_ = SRS_UTIME_NO_TIMEOUT;
}

SrsHttp3StreamReadWriter::~SrsHttp3StreamReadWriter()
{}

void SrsHttp3StreamReadWriter::set_recv_timeout(srs_utime_t tm)
{
    recv_timeout_ = tm;
}

srs_utime_t SrsHttp3StreamReadWriter::get_recv_timeout()
{
    return recv_timeout_;
}

srs_error_t SrsHttp3StreamReadWriter::read_fully(void *buf, size_t size, ssize_t *nread)
{
    return quic_transport_->read_fully(stream_id_, buf, size, nread, recv_timeout_);
}

int64_t SrsHttp3StreamReadWriter::get_recv_bytes()
{
    return 0;
}

int64_t SrsHttp3StreamReadWriter::get_send_bytes()
{
    return 0;
}

srs_error_t SrsHttp3StreamReadWriter::read(void *buf, size_t size, ssize_t *nread)
{
    return quic_transport_->read(stream_id_, buf, size, nread, recv_timeout_);
}

void SrsHttp3StreamReadWriter::set_send_timeout(srs_utime_t tm)
{
    send_timeout_ = tm;
}

srs_utime_t SrsHttp3StreamReadWriter::get_send_timeout()
{
    return send_timeout_;
}

srs_error_t SrsHttp3StreamReadWriter::write(void *buf, size_t size, ssize_t *nwrite)
{
    return quic_transport_->write(stream_id_, buf, size, nwrite, send_timeout_);
}

srs_error_t SrsHttp3StreamReadWriter::writev(const iovec *iov, int iov_size, ssize_t *nwrite)
{
    srs_error_t err = srs_success;
    for (int i = 0; i < iov_size; ++i) {
        ssize_t nb;
        if ((err = write(iov[i].iov_base, iov[i].iov_len, &nb)) != srs_success) {
            return srs_error_wrap(err, "quic write failed");
        }

        *nwrite += nb;
    }
    return err;
}

SrsHttp3QuicResponseWriter::SrsHttp3QuicResponseWriter(SrsHttp3QuicTransport *quic_transport, int64_t stream_id,
                                                       ISrsProtocolReadWriter *io)
    : SrsHttpResponseWriter(io), quic_transport_(quic_transport), stream_id_(stream_id)
{
    set_chunked(false);
}

SrsHttp3QuicResponseWriter::~SrsHttp3QuicResponseWriter()
{}

srs_error_t SrsHttp3QuicResponseWriter::final_request()
{
    srs_error_t err = srs_success;

    // TODO: FIXME: close stream

    srs_trace("@john, final request");

    return err;
}

srs_error_t SrsHttp3QuicResponseWriter::send_header(char *data, int size)
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
    if ((ret = nghttp3_conn_submit_response(quic_transport_->get_nghttp3_conn(), stream_id_, http3_rsp_headers.data(),
                                            http3_rsp_headers.size(), &dr)) != 0) {
        srs_error("nghttp3_conn_submit_response failed, ret=%d, err=%s", ret, nghttp3_strerror(ret));
        return srs_error_new(ERROR_HTTP3, "nghttp3_conn_submit_response failed, ret=%d, err=%s", ret,
                             nghttp3_strerror(ret));
    }

    srs_trace("stream_id=%ld http3 submit ret=%d", stream_id_, ret);

    return err;
}
