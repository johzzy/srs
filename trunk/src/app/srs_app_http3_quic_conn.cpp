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

#include <srs_app_http3_quic_conn.hpp>

using namespace std;

#include <ngtcp2/ngtcp2_crypto.h>

#include <srs_app_config.hpp>
#include <srs_app_quic_client.hpp>
#include <srs_app_quic_conn.hpp>
#include <srs_app_quic_io_loop.hpp>
#include <srs_app_quic_server.hpp>
#include <srs_app_quic_tls.hpp>
#include <srs_app_quic_util.hpp>
#include <srs_app_server.hpp>
#include <srs_app_utility.hpp>
#include <srs_core_autofree.hpp>
#include <srs_kernel_buffer.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_log.hpp>
#include <srs_protocol_utility.hpp>
#include <srs_service_st.hpp>
#include <srs_service_utility.hpp>

SrsHttp3QuicConnection::SrsHttp3QuicConnection(SrsQuicMultiplexer* multiplexer, const SrsContextId& ctx_id)
    : SrsHttp3QuicTransport(multiplexer, ctx_id)
{}

SrsHttp3QuicConnection::~SrsHttp3QuicConnection()
{}

srs_error_t SrsHttp3QuicConnection::accept(SrsUdpMuxSocket* skt, ngtcp2_pkt_hd* hd)
{
    udp_fd_ = skt->stfd();

    local_addr_ = *(multiplexer_->get_listener()->local_addr());
    local_addr_len_ = multiplexer_->get_listener()->local_addrlen();

    remote_addr_ = *skt->peer_addr();
    remote_addr_len_ = skt->peer_addrlen();

    scid_.datalen = kServerCidLen;
    srs_generate_rand_data(scid_.data, scid_.datalen);

    dcid_ = hd->scid;
    origin_dcid_ = hd->dcid;

    return init(reinterpret_cast<sockaddr*>(&local_addr_), local_addr_len_, reinterpret_cast<sockaddr*>(&remote_addr_),
                remote_addr_len_, &scid_, &dcid_, hd->version, hd->token.base, hd->token.len);
}

srs_error_t SrsHttp3QuicConnection::init(sockaddr* local_addr, const socklen_t local_addrlen, sockaddr* remote_addr,
                                         const socklen_t remote_addrlen, ngtcp2_cid* scid, ngtcp2_cid* dcid,
                                         const uint32_t version, uint8_t* token, const size_t tokenlen)
{
    srs_error_t err = srs_success;

    settings_ = build_quic_settings(token, tokenlen);
    transport_params_ = build_quic_transport_params(&origin_dcid_);

    ngtcp2_path path = build_quic_path(local_addr, local_addrlen, remote_addr, remote_addrlen);

    int ret =
        ngtcp2_conn_server_new(&conn_, dcid, scid, &path, version, &cb_, &settings_, &transport_params_, NULL, this);

    if (ret != 0) {
        return srs_error_new(ERROR_QUIC_CONN, "new quic conn failed, err=%s", ngtcp2_strerror(ret));
    }

    tls_context_ = new SrsQuicTlsServerContext();
    string tls_key = multiplexer_->get_listener()->get_key();
    string tls_cert = multiplexer_->get_listener()->get_cert();
    if ((err = tls_context_->init(tls_key, tls_cert)) != srs_success) {
        return srs_error_wrap(err, "init quic tls server ctx failed");
    }

    tls_session_ = new SrsQuicTlsServerSession();
    if ((err = tls_session_->init(tls_context_, this)) != srs_success) {
        return srs_error_wrap(err, "tls session init failed");
    }

    quic_token_ = new SrsQuicToken();
    if ((err = quic_token_->init()) != srs_success) {
        return srs_error_wrap(err, "init quic token failed");
    }

    if ((err = init_timer()) != srs_success) {
        return srs_error_wrap(err, "init timer failed");
    }

    ngtcp2_conn_set_tls_native_handle(conn_, tls_session_->get_ssl());

    nghttp3_settings_default(&http3_settings_);
    http3_settings_.qpack_max_table_capacity = 1024 * 16;
    http3_settings_.qpack_blocked_streams = 20;

    const nghttp3_mem* mem = nghttp3_mem_default();
    if ((ret = nghttp3_conn_server_new(&http3_conn_, &http3_cb_, &http3_settings_, mem, this)) != 0) {
        return srs_error_new(ERROR_HTTP3, "nghttp3_conn_server_new failed, ret=%d", ret);
    }

    ngtcp2_transport_params local_params;
    ngtcp2_conn_get_local_transport_params(conn_, &local_params);

    if (true) {
        ngtcp2_transport_params remote_params;
        ngtcp2_conn_get_remote_transport_params(conn_, &remote_params);
        srs_trace("local transport params, max_idle_timeout=%ld", local_params.max_idle_timeout);
        srs_trace("remote transport params, max_idle_timeout=%ld", remote_params.max_idle_timeout);
    }

    nghttp3_conn_set_max_client_streams_bidi(http3_conn_, local_params.initial_max_streams_bidi);

    return err;
}

ngtcp2_settings SrsHttp3QuicConnection::build_quic_settings(uint8_t* token, size_t tokenlen)
{
    ngtcp2_settings settings;
    ngtcp2_settings_default(&settings);

    // TODO: FIXME: conf this values using struct like SrsQuicParam.
    settings.log_printf = ngtcp2_log_handle;
    settings.qlog.write = qlog_handle;
    settings.initial_ts = srs_get_system_time_for_quic();
    settings.token.base = token;
    settings.token.len = tokenlen;
    settings.max_udp_payload_size = NGTCP2_MAX_UDP_PAYLOAD_SIZE;
    settings.cc_algo = NGTCP2_CC_ALGO_BBR;
    return settings;
}

ngtcp2_transport_params SrsHttp3QuicConnection::build_quic_transport_params(ngtcp2_cid* original_dcid)
{
    ngtcp2_transport_params params;
    ngtcp2_transport_params_default(&params);

    params.initial_max_stream_data_bidi_local = kStreamDataSize;
    params.initial_max_stream_data_bidi_remote = kStreamDataSize;
    params.initial_max_stream_data_uni = kStreamDataSize;
    ;
    params.initial_max_data = 2 * kStreamDataSize;
    params.initial_max_streams_bidi = 4;
    params.initial_max_streams_uni = 4;
    params.max_idle_timeout = 15 * NGTCP2_SECONDS;
    params.stateless_reset_token_present = 1;
    params.active_connection_id_limit = 7;

    if (original_dcid) {
        params.original_dcid = *original_dcid;
    }

    return params;
}

int SrsHttp3QuicConnection::handshake_completed()
{
    srs_trace("quic connection handshake %s completed", get_conn_name().c_str());

    uint8_t token[NGTCP2_CRYPTO_MAX_REGULAR_TOKENLEN];
    size_t tokenlen = sizeof(token);
    if (quic_token_->generate_token(token, tokenlen, reinterpret_cast<const sockaddr*>(&remote_addr_),
                                    remote_addr_len_) != 0) {
        return 0;
    }

    int ret = ngtcp2_conn_submit_new_token(conn_, token, tokenlen);
    if (ret != 0) {
        srs_error("ngtcp2_conn_submit_new_token failed, ret=%d", ret);
        return -1;
    }

    if (multiplexer_->get_listener()) {
        multiplexer_->get_listener()->on_accept_quic_conn(this);
    }

    return 0;
}
