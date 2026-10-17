#!/bin/bash
cd "$(dirname "$0")" && mkdir -p corpus && cd corpus
printf 'GET / HTTP/1.1\r\nHost: x\r\n\r\n' > get
printf 'GET /a/b.html?x=1&y=%%20z HTTP/1.1\r\nHost: x\r\nUser-Agent: t\r\nAccept: */*\r\nConnection: keep-alive\r\n\r\n' > get_query
printf 'POST /api HTTP/1.1\r\nHost: x\r\nContent-Length: 5\r\n\r\nhello' > post
printf 'POST /api HTTP/1.1\r\nHost: x\r\nContent-Length: 0\r\n\r\n' > post_empty
printf 'GET / HTTP/1.0\r\n\r\n' > http10
printf 'GET / HTTP/1.1\r\nHost: x\r\n\r\nGET /b HTTP/1.1\r\nHost: x\r\n\r\n' > pipelined
printf 'POST / HTTP/1.1\r\nHost: x\r\nContent-Length: 3\r\nContent-Length: 3\r\n\r\nabc' > dup_cl
printf 'HEAD /i.html HTTP/1.1\r\nHost: x\r\nExpect: 100-continue\r\n\r\n' > head_expect
printf 'POST / HTTP/1.1\r\nHost: x\r\nTransfer-Encoding: chunked\r\n\r\n' > chunked
printf 'BREW / HTTP/9.9\r\n\r\n' > bad