# Security & Hardening Posture (v0.5)

This document outlines the security mechanisms, enforced boundaries, and known architectural limits of `tourneyd` following the Phase A5 hardening and fuzzing validation.

## 1. Enforced Resource Limits
To prevent Denial of Service (DoS) via resource exhaustion, strict compile-time and kernel-enforced limits are applied to every connection and worker process.

* **Worker Process Cap:** The server concurrency is strictly capped (e.g., 64 concurrent children via `max_children`) to prevent fork-bombing and RAM exhaustion[cite: 2].
* **CPU Sandbox:** The Linux kernel `RLIMIT_CPU` strictly limits each worker child to 60 seconds of active compute time (soft limit) and 65 seconds (hard limit via `SIGKILL`) to defeat infinite-loop protocol parsing attacks[cite: 2].
* **File Descriptor Cap:** `RLIMIT_NOFILE` is capped at 64 per worker, guaranteeing that a rogue worker cannot exhaust the global system file descriptor pool[cite: 2].
* **Memory Cap:** `RLIMIT_AS` limits the virtual address space of each worker to 256 MB (disabled when AddressSanitizer is active)[cite: 2].
* **Static File Size Cap:** Requests for static assets exceeding 8 MB (`STATIC_MAX_FILE`) are rejected to prevent Out-Of-Memory (OOM) kills on massive disk reads[cite: 2].
* **Protocol Framing Limits:** The HTTP parser explicitly enforces maximum buffer constraints:
  * Maximum line length: 4,096 bytes[cite: 2].
  * Maximum URI path/query length: 1,024 bytes[cite: 2].
  * Maximum headers: 50 headers, up to a total block size of 16 KB[cite: 2].
  * Maximum request body size: 1 MB (`MAX_BODY_SIZE`)[cite: 2].

## 2. Implemented Defenses
* **Path Traversal & Null-Byte Injection:** 
  * The `http_url_decode` function explicitly rejects `%00` null-byte injections with a `400 Bad Request`[cite: 2].
  * Decoded paths containing `/.` are aggressively rejected with a `404 Not Found` before disk access, neutralizing `../` traversal tricks and hiding system dotfiles (e.g., `.git`, `.env`)[cite: 2].
  * Safe resolution is finalized via POSIX `realpath()`, verifying the normalized path resides strictly inside the `doc_root`[cite: 2].
* **Fuzzing & Memory Safety:** The incremental HTTP state-machine parser has been mathematically verified for split-invariance and has successfully passed 2,000,000+ iterations under `libFuzzer` combined with AddressSanitizer (ASan) and UndefinedBehaviorSanitizer (UBSan) with zero crashes or leaks[cite: 2].
* **XSS & Browser Sandboxing:** A centralized response builder injects strict framing and execution policies on all responses, including:
  * `X-Content-Type-Options: nosniff`[cite: 2].
  * `X-Frame-Options: DENY`[cite: 2].
  * `Referrer-Policy: no-referrer`[cite: 2].
  * `Permissions-Policy: camera=(), microphone=(), geolocation=()`[cite: 2].
  * `Content-Security-Policy: default-src 'self'; img-src 'self' data:; base-uri 'none'; form-action 'self'; frame-ancestors 'none'`[cite: 2].
* **Log Forging / Sanitization:** Untrusted client inputs (such as the URI target and `User-Agent` headers) are passed through `log_clean()` to scrub control characters (`< 0x20`, `0x7f`), tabs, and quotes (`"`) before hitting `stdout`, protecting downstream JSON parsers and log ingestion tools[cite: 2].
* **Socket Hijacking:** Client TCP sockets are accepted using `accept4()` with the `SOCK_CLOEXEC` flag[cite: 2]. If a worker process is forced to `exec` a malicious binary, the kernel will instantly close the socket, preventing the attacker from maintaining the active connection[cite: 2].

## 3. Known Limitations
* **No Native TLS:** The server operates strictly on plaintext HTTP/1.1[cite: 2]. Transport Layer Security (HTTPS) must be terminated externally via a reverse proxy (e.g., Cloudflare Tunnels, Nginx).
* **Chunked Transfer Encoding:** Chunked request bodies (`Transfer-Encoding: chunked`) are not supported and safely trigger a `501 Not Implemented` response[cite: 2].
* **Pipelined Request Limits:** While `max_requests_per_conn` enforces a soft limit by gracefully closing the connection after the maximum threshold is met, a malicious client sending an infinite, unbroken pipeline block is currently parsed in full until the `request_timeout_sec` deadline is breached[cite: 2].
* **Global Rate Limiting:** The `fork()` architecture isolates memory per connection. Consequently, strict per-IP rate limiting cannot be enforced natively without shared memory or a centralized datastore, relying instead on the reverse proxy layer for IP throttling.