/* proxycheck.c
 *
 * Minimal Docker HEALTHCHECK helper for this image. Makes one real,
 * authenticated HTTP request through the local tinyproxy instance to a
 * stable external target and checks for a genuine 200 response — not just
 * "the port is open." No shell, no other tools: this plus tinyproxy are the
 * only two binaries in the final image.
 *
 * Reads the healthcheck-only credential from HEALTHCHECK_AUTH
 * ("user:password"), supplied via the container's environment by whatever
 * deploys this image. This is never the real user-facing credential — the
 * consumer is expected to configure a dedicated, low-value BasicAuth user
 * in tinyproxy.conf purely for this check.
 *
 * Exit 0 = healthy. Exit 1 = anything else (missing env, connect failure,
 * timeout, non-200 response) — Docker's own HEALTHCHECK --retries handles
 * smoothing over a single transient failure; this binary just reports the
 * truth for one attempt.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define PROXY_HOST "127.0.0.1"
#define PROXY_PORT 8888
#define TARGET_HOST "example.com"
#define IO_TIMEOUT_SEC 5
#define MAX_AUTH_LEN 300

static void b64_encode(const unsigned char *in, size_t in_len, char *out) {
    static const char tbl[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t i, o = 0;
    for (i = 0; i < in_len; i += 3) {
        unsigned int n = (unsigned int)in[i] << 16;
        if (i + 1 < in_len) n |= (unsigned int)in[i + 1] << 8;
        if (i + 2 < in_len) n |= (unsigned int)in[i + 2];

        out[o++] = tbl[(n >> 18) & 0x3F];
        out[o++] = tbl[(n >> 12) & 0x3F];
        out[o++] = (i + 1 < in_len) ? tbl[(n >> 6) & 0x3F] : '=';
        out[o++] = (i + 2 < in_len) ? tbl[n & 0x3F] : '=';
    }
    out[o] = '\0';
}

static int fail(const char *why) {
    fprintf(stderr, "proxycheck: %s\n", why);
    return 1;
}

int main(void) {
    const char *auth = getenv("HEALTHCHECK_AUTH");
    if (!auth || !*auth) {
        return fail("HEALTHCHECK_AUTH not set");
    }
    if (strlen(auth) > MAX_AUTH_LEN) {
        return fail("HEALTHCHECK_AUTH too long");
    }

    char b64[512];
    b64_encode((const unsigned char *)auth, strlen(auth), b64);

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return fail("socket() failed");
    }

    struct timeval tv;
    tv.tv_sec = IO_TIMEOUT_SEC;
    tv.tv_usec = 0;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PROXY_PORT);
    if (inet_pton(AF_INET, PROXY_HOST, &addr.sin_addr) != 1) {
        close(fd);
        return fail("inet_pton() failed");
    }

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        close(fd);
        return fail("connect() failed");
    }

    char req[1024];
    int req_len = snprintf(req, sizeof(req),
        "GET http://%s/ HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Proxy-Authorization: Basic %s\r\n"
        "Connection: close\r\n"
        "\r\n",
        TARGET_HOST, TARGET_HOST, b64);

    if (req_len < 0 || (size_t)req_len >= sizeof(req)) {
        close(fd);
        return fail("request did not fit in buffer");
    }

    if (write(fd, req, (size_t)req_len) != req_len) {
        close(fd);
        return fail("write() failed");
    }

    char resp[512];
    ssize_t n = read(fd, resp, sizeof(resp) - 1);
    close(fd);

    if (n <= 0) {
        return fail("read() failed or connection closed early");
    }
    resp[n] = '\0';

    /* Status line looks like "HTTP/1.1 200 OK\r\n..." — check the start of
     * the response specifically, not just "200" anywhere in it, so a 200
     * appearing incidentally in a header/body can't produce a false pass. */
    if (strncmp(resp, "HTTP/1.1 200", 12) != 0 &&
        strncmp(resp, "HTTP/1.0 200", 12) != 0) {
        return fail("non-200 response from proxied request");
    }

    return 0;
}
