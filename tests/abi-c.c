/* tests/abi-c.c : link-only ABI smoke test for qwen.h.
 *
 * Compiled in pure C99 with -Wall -Werror -pedantic. The purpose of this
 * test is NOT to run a full synthesis (no GGUF loaded, no model required);
 * it is to guarantee at every build that :
 *
 *   1. qwen.h parses with a C compiler (no <cstdio>, no std::*, no
 *      C++-only forward declarations leak in).
 *   2. Every public qwen_* symbol has C linkage and links from a C
 *      translation unit.
 *   3. The structs are POD and zero-initialisable with `{0}` from C.
 *   4. The qwen_log_set callback routes formatted messages from the lib
 *      to the user, and abi_version validation rejects future structs.
 *
 * If this test stops compiling or stops linking, the public ABI has
 * regressed and the build breaks before anything else.
 */

#include "qwen.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Counter incremented by the stub log callback. The probe checks that at
 * least one log line was routed through the callback by triggering a
 * qwen_init failure (which emits a [qwen] ERROR line via qt_log). */
static int               g_log_lines        = 0;
static enum qwen_log_level g_last_log_level = QWEN_LOG_DEBUG;
static char              g_last_log_msg[512] = { 0 };

static void stub_log(enum qwen_log_level level, const char * msg, void * user_data) {
    (void) user_data;
    g_log_lines++;
    g_last_log_level = level;
    if (msg) {
        size_t n = strlen(msg);
        if (n >= sizeof(g_last_log_msg)) {
            n = sizeof(g_last_log_msg) - 1;
        }
        memcpy(g_last_log_msg, msg, n);
        g_last_log_msg[n] = '\0';
    }
}

int main(void) {
    /* Static version string, always reachable. */
    const char * version = qwen_version();
    printf("[Probe] %s\n", version);

    /* Default-initialise the public structs from C. */
    struct qwen_init_params iparams;
    qwen_init_default_params(&iparams);

    struct qwen_tts_params params;
    qwen_tts_default_params(&params);

    /* Sanity-check a few default values, including the abi_version. */
    if (params.max_new_tokens != 2048 || params.temperature != 0.9f) {
        fprintf(stderr, "[Probe] default values do not match\n");
        return 1;
    }
    if (iparams.abi_version != QWEN_ABI_VERSION || params.abi_version != QWEN_ABI_VERSION) {
        fprintf(stderr, "[Probe] abi_version not set by qwen_*_default_params\n");
        return 1;
    }

    /* Touch every output struct field so the compiler validates the
     * layout end-to-end without ever needing a model. */
    struct qwen_audio audio = { 0 };
    qwen_audio_free(&audio);

    /* Install the log callback before the failing init so the [qwen]
     * ERROR line lands on stub_log instead of stderr. */
    qwen_log_set(stub_log, NULL);

    /* Call every entry through its early-return path. qwen_init returns
     * NULL on missing talker_path / codec_path, qwen_synthesize fails on
     * NULL handle, qwen_free is safe on NULL. None of these load a model,
     * but the linker must resolve every name to satisfy the call. */
    struct qwen_context * dummy = qwen_init(NULL);
    if (dummy != NULL) {
        fprintf(stderr, "[Probe] qwen_init(NULL) was supposed to return NULL\n");
        qwen_free(dummy);
        return 2;
    }

    /* qwen_init(NULL) just failed -> qwen_last_error() must point to a
     * non-empty thread-local string. Pointer is always valid (c_str on
     * an empty std::string still gives a NUL byte), so we only need to
     * check the first byte to confirm an error was actually recorded. */
    const char * err = qwen_last_error();
    if (err == NULL || err[0] == '\0') {
        fprintf(stderr, "[Probe] qwen_last_error() empty after a known failure\n");
        return 5;
    }

    /* The same failure must have surfaced through the log callback at
     * ERROR level. */
    if (g_log_lines == 0) {
        fprintf(stderr, "[Probe] qwen_log_set callback never invoked\n");
        return 6;
    }
    if (g_last_log_level != QWEN_LOG_ERROR) {
        fprintf(stderr, "[Probe] last log level was %d, expected %d\n", (int) g_last_log_level,
                (int) QWEN_LOG_ERROR);
        return 7;
    }
    printf("[Probe] qwen_log_set routed %d line(s), last: '%s'\n", g_log_lines, g_last_log_msg);
    printf("[Probe] qwen_last_error reads '%s'\n", err);

    /* abi_version validation : a struct claiming a future ABI must be
     * rejected up front, before any allocation. Both paths are filled
     * with placeholders so the NULL guard does not short-circuit the
     * abi_version branch. */
    struct qwen_init_params future_iparams;
    qwen_init_default_params(&future_iparams);
    future_iparams.talker_path = "irrelevant.gguf";
    future_iparams.codec_path  = "irrelevant.gguf";
    future_iparams.abi_version = QWEN_ABI_VERSION + 1;
    struct qwen_context * rejected = qwen_init(&future_iparams);
    if (rejected != NULL) {
        fprintf(stderr, "[Probe] qwen_init accepted a future abi_version\n");
        qwen_free(rejected);
        return 8;
    }

    enum qwen_status rc = qwen_synthesize(NULL, &params, &audio);
    if (rc != QWEN_STATUS_INVALID_PARAMS) {
        fprintf(stderr, "[Probe] qwen_synthesize(NULL) returned %d, expected %d\n", (int) rc,
                (int) QWEN_STATUS_INVALID_PARAMS);
        return 3;
    }

    /* Restore the default stderr fallback before exit so the trailing
     * [qwen] log lines from the cleanup paths land where the user
     * expects them. */
    qwen_log_set(NULL, NULL);

    qwen_free(NULL);
    qwen_audio_free(&audio);

    return 0;
}
