// Aeroverse Wii U network test: checks that the console can reach https://aeroverse.lukwszystkorobicz.pl
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <coreinit/thread.h>
#include <coreinit/time.h>
#include <whb/proc.h>
#include <whb/log.h>
#include <whb/log_console.h>

#include <curl/curl.h>

#include "cacert_pem.h"   // generated from data/cacert.pem by the wut build rules

#define HOST "https://aeroverse.lukwszystkorobicz.pl"

typedef struct { char buf[600]; size_t len; } Out;

static size_t onData(char *p, size_t sz, size_t n, void *ud) {
    Out *o = (Out *)ud; size_t k = sz * n;
    if (o->len < sizeof(o->buf) - 1) {
        size_t c = k < sizeof(o->buf) - 1 - o->len ? k : sizeof(o->buf) - 1 - o->len;
        memcpy(o->buf + o->len, p, c); o->len += c; o->buf[o->len] = 0;
    }
    return k;
}

static void tryGet(const char *url, int verify) {
    Out out = { {0}, 0 };
    CURL *c = curl_easy_init();
    if (!c) { WHBLogPrintf("curl_easy_init failed"); return; }
    char err[CURL_ERROR_SIZE] = {0};
    curl_easy_setopt(c, CURLOPT_URL, url);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, onData);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &out);
    curl_easy_setopt(c, CURLOPT_ERRORBUFFER, err);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 20L);
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(c, CURLOPT_USERAGENT, "AeroverseWiiU/0.1");
    if (verify) {
        struct curl_blob blob;
        blob.data = (void *)cacert_pem; blob.len = cacert_pem_size; blob.flags = CURL_BLOB_COPY;
        curl_easy_setopt(c, CURLOPT_CAINFO_BLOB, &blob);
    } else {
        // DIAGNOSTIC ONLY: does the connection work at all if certificates are not checked?
        curl_easy_setopt(c, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(c, CURLOPT_SSL_VERIFYHOST, 0L);
    }
    CURLcode rc = curl_easy_perform(c);
    long code = 0; curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code);
    WHBLogPrintf("[%s] %s", verify ? "verified" : "NO CERT CHECK", url);
    if (rc == CURLE_OK) WHBLogPrintf("  OK, HTTP %ld, %u bytes", code, (unsigned)out.len);
    else WHBLogPrintf("  FAILED: curl %d: %s", (int)rc, err[0] ? err : curl_easy_strerror(rc));
    if (out.len) { out.buf[80] = 0; WHBLogPrintf("  %s", out.buf); }
    curl_easy_cleanup(c);
}

int main(int argc, char **argv) {
    WHBProcInit();
    WHBLogConsoleInit();
    WHBLogPrintf("Aeroverse network test");
    WHBLogPrintf("Press HOME to exit");
    WHBLogConsoleDraw();

    // networking is initialised automatically by wut (libwut), no explicit socket init needed
    curl_global_init(CURL_GLOBAL_DEFAULT);

    tryGet(HOST "/api/communities", 1);
    WHBLogConsoleDraw();
    tryGet(HOST "/api/communities", 0);
    WHBLogPrintf("Done. Take a photo of this screen.");

    while (WHBProcIsRunning()) {
        WHBLogConsoleDraw();
        OSSleepTicks(OSMillisecondsToTicks(100));
    }
    curl_global_cleanup();
    WHBLogConsoleFree();
    WHBProcShutdown();
    return 0;
}
