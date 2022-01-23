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

#ifndef SRS_APP_QUIC_IO_LOOP_HPP
#define SRS_APP_QUIC_IO_LOOP_HPP

#include <srs_core.hpp>

#include <srs_app_listener.hpp>
#include <srs_app_st.hpp>
#include <srs_app_reload.hpp>
#include <srs_app_hourglass.hpp>
#include <srs_app_hybrid.hpp>

#include <string>

class SrsQuicTransport;
class ISrsResource;
class SrsResourceManager;
class SrsQuicMultiplexer;

enum SrsQuicListenerType
{
    // QUIC client multiplexer
    SrsQuicListenerClient = 0,
	// RTC server forward
    SrsQuicListenerRtcForward = 1,
    // HTTP3 API
    SrsQuicListenerHttpApi = 2,
    // HTTP3 Stream
    SrsQuicListenerHttpStream = 3,
};

class ISrsQuicHandler
{
public:
    ISrsQuicHandler() {}
    virtual ~ISrsQuicHandler() {}
public:
    virtual srs_error_t on_quic_client(SrsQuicTransport* conn, SrsQuicListenerType type) = 0;
};

// The QUIC listen, recv udp packet and pass to SrsQuicMultiplexer.
class SrsQuicListener : virtual public ISrsUdpMuxHandler
{
public:
    SrsQuicListener(ISrsQuicHandler* handler, SrsQuicListenerType type);
    ~SrsQuicListener();
public:
    srs_error_t listen(const std::string& ip, int port);
public:
    // Get SSL key to initlize QUIC tls context.
    std::string get_key();
    // Get SSL cert to initlize QUIC tls context.
    std::string get_cert();
public:
    virtual srs_error_t on_udp_packet(SrsUdpMuxSocket* skt);
    srs_error_t on_accept_quic_conn(SrsQuicTransport* quic_session);
    sockaddr_in* local_addr() { return &listen_sa_; }
    socklen_t local_addrlen() { return sizeof(listen_sa_); }
    srs_netfd_t get_mux_netfd() { return listener_->stfd(); }
    SrsQuicMultiplexer* get_multiplexer() { return multiplexer_; }
private:
    SrsQuicMultiplexer* multiplexer_;
    // Handle when accept new quic conneciont(in application layer).
    ISrsQuicHandler* handler_;
    // Udp listener.
    SrsUdpMuxListener* listener_;
    SrsQuicListenerType listen_type_;
    struct sockaddr_in listen_sa_;
};

// The QUIC server instance, handle UDP packet, manage QUIC connections(in transport layer).
class SrsQuicMultiplexer
{
public:
    SrsQuicMultiplexer(SrsQuicListener* listener);
    virtual ~SrsQuicMultiplexer();
public:
    virtual srs_error_t initialize();
    void subscribe(SrsQuicTransport* quic_session);
    void unsubscribe(SrsQuicTransport* quic_session);
    void remove(ISrsResource* resource);
public:
    srs_error_t on_udp_packet(SrsUdpMuxSocket* skt, SrsQuicListener* listener);
    SrsQuicListener* get_listener() { return listener_; }
    srs_error_t add_transport(SrsQuicTransport* quic_session);
private:
    srs_error_t new_connection(SrsUdpMuxSocket* skt, SrsQuicListener* listener, SrsQuicTransport** p_conn);
    srs_error_t send_version_negotiation(SrsUdpMuxSocket* skt, const uint8_t version, 
        const uint8_t* dcid, const size_t dcid_len, const uint8_t* scid, const size_t scid_len);
private:
    // Manage QUIC connection(in transport layer).
    SrsResourceManager* quic_conn_map_;
    // Which listener it belong to.
    SrsQuicListener*  listener_;
};

#endif
