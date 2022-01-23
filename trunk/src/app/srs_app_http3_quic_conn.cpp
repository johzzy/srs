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

SrsHttp3QuicConnection::SrsHttp3QuicConnection(SrsQuicMultiplexer* multiplexer,
                                               const SrsContextId& ctx_id)
    : SrsHttp3QuicTransport(multiplexer, ctx_id)
{
}

SrsHttp3QuicConnection::~SrsHttp3QuicConnection() {}

srs_error_t SrsHttp3QuicConnection::init_http3()
{
    srs_error_t err = srs_success;

    nghttp3_settings_default(&http3_settings_);
    http3_settings_.qpack_max_table_capacity = 1024 * 16;
    http3_settings_.qpack_blocked_streams = 20;

    int ret = 0;
    const nghttp3_mem* mem = nghttp3_mem_default();
    if ((ret = nghttp3_conn_server_new(&http3_conn_, &http3_cb_,
                                       &http3_settings_, mem, this)) != 0) {
        return srs_error_new(ERROR_HTTP3,
                             "nghttp3_conn_server_new failed, ret=%d", ret);
    }

    ngtcp2_transport_params local_params;
    ngtcp2_conn_get_local_transport_params(conn_, &local_params);

    if (true) {
        ngtcp2_transport_params remote_params;
        ngtcp2_conn_get_remote_transport_params(conn_, &remote_params);
        srs_trace("local transport params, max_idle_timeout=%ld",
                  local_params.max_idle_timeout);
        srs_trace("remote transport params, max_idle_timeout=%ld",
                  remote_params.max_idle_timeout);
    }

    nghttp3_conn_set_max_client_streams_bidi(
        http3_conn_, local_params.initial_max_streams_bidi);

    return err;
}
