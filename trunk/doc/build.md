## achive

cd srs/trunk
./configure --osx --with-hds && make

CANDIDATE=$(ifconfig en0 inet| grep inet|awk '{print $2}')
192.168.200.133
ulimit -HSn 1107
objs/srs -c conf/rtc.conf
./etc/init.d/srs status

ffmpeg -hide_banner -re -i doc/source.200kbps.768x320.flv -c copy -f flv -y rtmp://${CANDIDATE}/live/livestream
ffmpeg -hide_banner -stream_loop -1  -re -i ~/Music/Yanni-Nightingale.mkv -acodec aac -vcodec copy -f flv -y rtmp://${CANDIDATE}/live/livestream

http://${CANDIDATE}:8080/players/rtc_player.html
ffplay rtmp://${CANDIDATE}/live/livestream




## note
server side:

    objs/srs -c conf/johnny.conf

publisher side:

    ffmpeg -hide_banner -re -i doc/source.200kbps.768x320.flv -c copy -f flv -y rtmp://127.0.0.1/live/livestream

play side:
    
    # rtmp
    ffplay rtmp://localhost/live/livestream

    # http flv (http_remux, mount       [vhost]/[app]/[stream].flv;)
    ffplay http://127.0.0.1:8080/live/livestream.flv
    
    # http ts (http_remux, mount       [vhost]/[app]/[stream].ts;)
    ffplay http://127.0.0.1:8080/live/livestream.ts

    # hls
    ffplay http://127.0.0.1:8080/live/livestream.m3u8

    # dash
    vlc http://127.0.0.1:8080/live/livestream.mpd
    
    # hds (https://github.com/ossrs/srs/issues/1535)
    unknown


## srs bug

```
[2021-01-05 21:26:42.508][Trace][75959][7j02yv15] HTTP #0 127.0.0.1:64657 OPTIONS http://127.0.0.1:1985/rtc/v1/play/, content-length=-1
[2021-01-05 21:26:42.514][Trace][75959][7j02yv15] HTTP #1 127.0.0.1:64657 POST http://127.0.0.1:1985/rtc/v1/play/, content-length=5307
[2021-01-05 21:26:42.514][Trace][75959][7j02yv15] RTC play webrtc://127.0.0.1/live/livestream, api=http://127.0.0.1:1985/rtc/v1/play/, clientip=, app=live, stream=livestream, offer=4885B, eip=, srtp=, dtls=
[2021-01-05 21:26:42.515][Warn][75959][7j02yv15][35][DTLS_HANG] DTLS: Hang, done=0, version=-1, arq=0
[2021-01-05 21:26:42.516][Warn][75959][7j02yv15][35] RTC error code=5018 : create session, dtls=1, srtp=1, eip= : create session : add player : no play relations
thread [75959][7j02yv15]: do_serve_http() [src/app/srs_app_rtc_api.cpp:195][errno=35]
thread [75959][7j02yv15]: create_session() [src/app/srs_app_rtc_server.cpp:423][errno=35]
thread [75959][7j02yv15]: do_create_session() [src/app/srs_app_rtc_server.cpp:445][errno=35]
thread [75959][7j02yv15]: add_player() [src/app/srs_app_rtc_conn.cpp:1827][errno=35]
[2021-01-05 21:26:42.517][Trace][75959][7j02yv15] TCP: before dispose resource(HttpConn)(0x7fb776920f00), conns=2, zombies=0, ign=0, inz=0, ind=0
[2021-01-05 21:26:42.517][Warn][75959][7j02yv15][54] client disconnect peer. ret=1007
[2021-01-05 21:26:42.518][Trace][75959][g220z04r] TCP: clear zombies=1 resources, conns=2, removing=0, unsubs=0
[2021-01-05 21:26:42.518][Trace][75959][7j02yv15] TCP: disposing #0 resource(HttpConn)(0x7fb776920f00), conns=2, disposing=1, zombies=0
[2021-01-05 21:26:49.720][Trace][75959][9vh4v479] <- CPB time=170120564, okbps=0,0,0, ikbps=1867,1562,0, mr=0/350, p1stpt=20000, pnt=5000
```