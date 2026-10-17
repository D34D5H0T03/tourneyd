# Performance Benchmarks

## Baseline: Fork Concurrency Model (v0.5)

**Environment Parameters:**
* **OS:** Ubuntu Linux (64-bit)
* **Hardware:** 11th Gen Intel(R) Core(TM) i5-1135G7 @ 2.40GHz, 8GB RAM
* **Network:** Loopback interface (`127.0.0.1`)
* **Target Payload:** 2,556 byte static HTML file (`/index.html`)

**Server Configuration (`config/bench.conf`):**
* `max_children` = 64
* `idle_timeout_sec` = 5
* `max_requests_per_conn` = 100
* `trust_proxy` = false
* Logging routed to `/dev/null`

---

### Test A: Standard Load (Under Worker Cap)
* **Command:** `ab -k -n 20000 -c 50 http://127.0.0.1:8080/index.html`
* **Requests per second (Mean):** 115,703.91 req/sec
* **Transfer Rate:** 343.37 MB/sec
* **Mean Time Per Request (Concurrent):** 0.009 ms
* **99th Percentile Latency:** 1 ms

### Test B: Cap Saturation (Over Worker Cap)
* **Command:** `ab -k -n 20000 -c 200 http://127.0.0.1:8080/index.html`
* **Requests per second (Mean):** 3,505.78 req/sec
* **Transfer Rate:** 10.37 MB/sec
* **Mean Time Per Request (Concurrent):** 0.285 ms
* **99th Percentile Latency:** 121 ms
* **Maximum Latency:** 5,631 ms (5.6 seconds)

**Analysis:** Once the 64 `max_children` cap is reached, the parent process stops calling `accept()`. The remaining 136 concurrent connections are forced into the Linux kernel's TCP backlog queue, causing RPS to drop by 97% and maximum latency to spike to 5.6 seconds as clients wait for idle timeouts to free up workers.

### Test C: Sustained High Throughput (`wrk`)
* **Command:** `wrk -t2 -c100 -d15s http://127.0.0.1:8080/index.html`
* **Total Requests Processed:** 2,207,347
* **Requests per second:** 146,172.35 req/sec
* **Transfer Rate:** 423.63 MB/sec
* **Average Latency:** 3.77 ms

**Architectural Conclusion:** The `fork` model is highly resilient and fast under low-to-medium concurrency. However, its memory overhead prevents scaling `max_children` high enough to mitigate the extreme latency spikes seen during saturation (Test B). This confirms the necessity of migrating to an asynchronous event-loop architecture (`epoll`).