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

#ifndef SRS_APP_HTTP3_QUIC_CONN_HPP
#define SRS_APP_HTTP3_QUIC_CONN_HPP

#include <ngtcp2/ngtcp2.h>
#include <sys/socket.h>

#include <deque>
#include <map>
#include <srs_app_conn.hpp>
#include <srs_app_hourglass.hpp>
#include <srs_app_http3_quic_transport.hpp>
#include <srs_app_listener.hpp>
#include <srs_app_quic_conn.hpp>
#include <srs_app_reload.hpp>
#include <srs_core.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_service_conn.hpp>
#include <srs_service_st.hpp>
#include <string>
#include <vector>

class SrsQuicMultiplexer;
class SrsUdpMuxSocket;
class SrsQuicTlsServerSession;

// Quic connection which accept from client.
class SrsHttp3QuicConnection : public SrsHttp3QuicTransport, public ISrsQuicServerConn
{
public:
    SrsHttp3QuicConnection(SrsQuicMultiplexer* multiplexer, const SrsContextId& ctx_id);
    ~SrsHttp3QuicConnection();

private:
    virtual srs_error_t accept(SrsUdpMuxSocket* skt, ngtcp2_pkt_hd* hd);
    virtual srs_error_t init(sockaddr* local_addr, const socklen_t local_addrlen, sockaddr* remote_addr,
                             const socklen_t remote_addrlen, ngtcp2_cid* scid, ngtcp2_cid* dcid, const uint32_t version,
                             uint8_t* token, const size_t tokenlen);
    virtual ngtcp2_settings build_quic_settings(uint8_t* token, size_t tokenlen);
    virtual ngtcp2_transport_params build_quic_transport_params(ngtcp2_cid* original_dcid);
    virtual int handshake_completed();
};

#endif
