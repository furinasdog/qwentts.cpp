// tts-webui.cpp: WebUI HTTP server for Qwen3-TTS.
//
// Serves a single-page HTML frontend and a REST API for TTS synthesis,
// voice cloning (from raw audio or pre-encoded .spk/.rvq), and speaker
// embedding extraction. All GPU inference is serialised through a single
// mutex so the GPU context is never shared across concurrent requests.
//
// Endpoints:
//   GET  /                   HTML single-page app
//   GET  /api/info           model type, speaker list
//   POST /api/generate        base / custom_voice / voice_design synthesis
//   POST /api/clone           voice clone from reference WAV
//   POST /api/encode          encode WAV -> spk + rvq (base64 JSON)
//   POST /api/clone_from_spk  voice clone from pre-encoded .spk / .rvq files
//   GET  /health              liveness probe

#include "webui-html.h"

#include "audio-io.h"
#include "backend.h"
#include "bpe.h"
#include "gguf-weights.h"
#include "pipeline-codec.h"
#include "pipeline-tts.h"
#include "qwen.h"
#include "rvq-file.h"
#include "speaker-encoder-extract.h"
#include "speaker-encoder-weights.h"
#include "version.h"

#include "../vendor/cpp-httplib/httplib.h"
#include "../vendor/yyjson/yyjson.h"

#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// qt_context internal layout mirror (statically linked, layout is guaranteed)
// ---------------------------------------------------------------------------

// Forward declaration matching qwen.cpp internal layout for direct field access.
// This file links qwen-core statically, so the layout is guaranteed identical.
struct qt_context {
    BackendPair  bp;
    PipelineTTS  pt;
    BPETokenizer tok;
};

// ---------------------------------------------------------------------------
// Globals
// ---------------------------------------------------------------------------

static std::mutex        g_infer_mutex;
static httplib::Server * g_svr = nullptr;

static void webui_on_signal(int) {
    if (g_svr) {
        g_svr->stop();
    }
}

// ---------------------------------------------------------------------------
// Utility: base64 encode
// ---------------------------------------------------------------------------

static const char B64_TABLE[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static std::string base64_encode(const void * data, size_t len) {
    const uint8_t * src = (const uint8_t *) data;
    std::string     out;
    out.reserve(((len + 2) / 3) * 4);
    for (size_t i = 0; i < len; i += 3) {
        uint32_t b = (uint32_t) src[i] << 16;
        if (i + 1 < len) b |= (uint32_t) src[i + 1] << 8;
        if (i + 2 < len) b |= (uint32_t) src[i + 2];
        out.push_back(B64_TABLE[(b >> 18) & 0x3f]);
        out.push_back(B64_TABLE[(b >> 12) & 0x3f]);
        out.push_back(i + 1 < len ? B64_TABLE[(b >> 6) & 0x3f] : '=');
        out.push_back(i + 2 < len ? B64_TABLE[(b >> 0) & 0x3f] : '=');
    }
    return out;
}

// ---------------------------------------------------------------------------
// Utility: JSON error response
// ---------------------------------------------------------------------------

static void json_error(httplib::Response & res, int status, const char * message) {
    yyjson_mut_doc * doc  = yyjson_mut_doc_new(NULL);
    yyjson_mut_val * root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);
    yyjson_mut_obj_add_str(doc, root, "error", message);
    char * json = yyjson_mut_write(doc, 0, NULL);
    res.status  = status;
    res.set_content(json ? json : "{}", "application/json");
    if (json) {
        free(json);
    }
    yyjson_mut_doc_free(doc);
}

// ---------------------------------------------------------------------------
// Utility: multipart field helpers
// ---------------------------------------------------------------------------

// Return the string value of a named text field, or "" if absent.
static std::string mp_get_str(const httplib::MultipartFormData & form, const char * name) {
    if (!form.has_field(name)) {
        return "";
    }
    return form.get_field(name);
}

// Return true if a named file field is present and non-empty.
static bool mp_has_file(const httplib::MultipartFormData & form, const char * name) {
    return form.has_file(name) && !form.get_file(name).content.empty();
}

// Get file content string of a named field (for binary files).
static std::string mp_get_file_content(const httplib::MultipartFormData & form, const char * name) {
    if (!form.has_file(name)) {
        return "";
    }
    return form.get_file(name).content;
}

// ---------------------------------------------------------------------------
// Utility: parse sampling parameters from multipart form
// ---------------------------------------------------------------------------

static void parse_sampling_params(const httplib::MultipartFormData & form, struct qt_tts_params & p) {
    std::string seed_s = mp_get_str(form, "seed");
    if (!seed_s.empty()) {
        p.seed = (int64_t) std::atoll(seed_s.c_str());
    }
    std::string max_tok_s = mp_get_str(form, "max_new_tokens");
    if (!max_tok_s.empty()) {
        p.max_new_tokens = std::atoi(max_tok_s.c_str());
    }
    std::string do_sample_s = mp_get_str(form, "do_sample");
    if (!do_sample_s.empty()) {
        p.do_sample = (do_sample_s == "true" || do_sample_s == "1");
    }
    std::string temp_s = mp_get_str(form, "temperature");
    if (!temp_s.empty()) {
        p.temperature = (float) std::atof(temp_s.c_str());
    }
    std::string top_k_s = mp_get_str(form, "top_k");
    if (!top_k_s.empty()) {
        p.top_k = std::atoi(top_k_s.c_str());
    }
    std::string top_p_s = mp_get_str(form, "top_p");
    if (!top_p_s.empty()) {
        p.top_p = (float) std::atof(top_p_s.c_str());
    }
    std::string rep_pen_s = mp_get_str(form, "repetition_penalty");
    if (!rep_pen_s.empty()) {
        p.repetition_penalty = (float) std::atof(rep_pen_s.c_str());
    }
}

// ---------------------------------------------------------------------------
// Utility: decode WAV bytes to mono 24 kHz float buffer
// ---------------------------------------------------------------------------

static float * decode_wav_to_mono24k(const std::string & wav_bytes, int * n_samples_out, std::string & err) {
    *n_samples_out = 0;
    const uint8_t * data = (const uint8_t *) wav_bytes.data();
    size_t          size = wav_bytes.size();
    int             T    = 0;
    int             sr   = 0;
    float *         raw  = audio_io_read_wav_buf(data, size, &T, &sr);
    if (!raw || T <= 0) {
        err = "failed to decode WAV audio";
        return nullptr;
    }

    // raw is planar stereo [L:T][R:T], resample to 24kHz if needed
    float * stereo_rs = raw;
    int     T_rs      = T;
    if (sr != TOKENIZER_SAMPLE_RATE) {
        int     T_new     = 0;
        float * resampled = audio_resample(raw, T, sr, TOKENIZER_SAMPLE_RATE, 2, &T_new);
        free(raw);
        if (!resampled) {
            err = "audio resample failed";
            return nullptr;
        }
        stereo_rs = resampled;
        T_rs      = T_new;
    }

    // downmix planar [L:T][R:T] -> mono
    float *       mono  = (float *) malloc((size_t) T_rs * sizeof(float));
    const float * left  = stereo_rs;
    const float * right = stereo_rs + (size_t) T_rs;
    if (!mono) {
        free(stereo_rs);
        err = "out of memory";
        return nullptr;
    }
    for (int i = 0; i < T_rs; i++) {
        mono[i] = 0.5f * (left[i] + right[i]);
    }
    free(stereo_rs);

    *n_samples_out = T_rs;
    return mono;
}

// ---------------------------------------------------------------------------
// Utility: encode audio as WAV string in the requested format
// ---------------------------------------------------------------------------

static std::string encode_audio_wav(const float * samples, int n, const std::string & format) {
    WavFormat fmt = WAV_S16;
    if (format == "wav24") {
        fmt = WAV_S24;
    } else if (format == "wav32") {
        fmt = WAV_F32;
    }
    return audio_encode_wav(samples, n, TOKENIZER_SAMPLE_RATE, fmt);
}

// ---------------------------------------------------------------------------
// Handler: GET /
// ---------------------------------------------------------------------------

static void handle_index(const httplib::Request &, httplib::Response & res) {
    res.set_content(WEBUI_HTML, "text/html; charset=utf-8");
}

// ---------------------------------------------------------------------------
// Handler: GET /health
// ---------------------------------------------------------------------------

static void handle_health(const httplib::Request &, httplib::Response & res) {
    res.set_content("{\"status\":\"ok\"}", "application/json");
}

// ---------------------------------------------------------------------------
// Handler: GET /api/info
// ---------------------------------------------------------------------------

static void handle_info(qt_context * q, const httplib::Request &, httplib::Response & res) {
    const std::string & model_type = q->pt.model_type;
    int                 n_spk      = qt_n_speakers(q);

    yyjson_mut_doc * doc  = yyjson_mut_doc_new(NULL);
    yyjson_mut_val * root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);
    yyjson_mut_obj_add_str(doc, root, "model_type", model_type.c_str());
    yyjson_mut_obj_add_int(doc, root, "n_speakers", n_spk);
    yyjson_mut_val * arr = yyjson_mut_arr(doc);
    for (int i = 0; i < n_spk; i++) {
        const char * name = qt_speaker_name(q, i);
        yyjson_mut_arr_add_str(doc, arr, name ? name : "");
    }
    yyjson_mut_obj_add_val(doc, root, "speakers", arr);
    char * json = yyjson_mut_write(doc, 0, NULL);
    res.set_content(json ? json : "{}", "application/json");
    if (json) {
        free(json);
    }
    yyjson_mut_doc_free(doc);
}

// ---------------------------------------------------------------------------
// Handler: POST /api/generate
// ---------------------------------------------------------------------------

static void handle_generate(qt_context * q, const httplib::Request & req, httplib::Response & res) {
    const auto & form = req.form;

    std::string text    = mp_get_str(form, "text");
    std::string lang    = mp_get_str(form, "lang");
    std::string format  = mp_get_str(form, "format");
    std::string instruct = mp_get_str(form, "instruct");
    std::string speaker = mp_get_str(form, "speaker");

    if (text.empty()) {
        json_error(res, 400, "'text' field is required");
        return;
    }
    if (format.empty()) {
        format = "wav16";
    }

    struct qt_tts_params p;
    qt_tts_default_params(&p);
    p.text     = text.c_str();
    p.lang     = lang.empty() ? nullptr : lang.c_str();
    p.instruct = instruct.empty() ? nullptr : instruct.c_str();
    p.speaker  = speaker.empty() ? nullptr : speaker.c_str();
    parse_sampling_params(form, p);

    struct qt_audio out = {};
    enum qt_status  rc;
    {
        std::lock_guard<std::mutex> lock(g_infer_mutex);
        rc = qt_synthesize(q, &p, &out);
    }
    if (rc != QT_STATUS_OK) {
        json_error(res, 500, qt_last_error());
        qt_audio_free(&out);
        return;
    }

    std::string wav = encode_audio_wav(out.samples, out.n_samples, format);
    qt_audio_free(&out);
    res.set_content(std::move(wav), "audio/wav");
}

// ---------------------------------------------------------------------------
// Handler: POST /api/clone
// ---------------------------------------------------------------------------

static void handle_clone(qt_context * q, const httplib::Request & req, httplib::Response & res) {
    const auto & form = req.form;

    std::string text     = mp_get_str(form, "text");
    std::string ref_text = mp_get_str(form, "ref_text");
    std::string lang     = mp_get_str(form, "lang");
    std::string format   = mp_get_str(form, "format");

    if (text.empty()) {
        json_error(res, 400, "'text' field is required");
        return;
    }
    if (!mp_has_file(form, "ref_wav")) {
        json_error(res, 400, "'ref_wav' file is required");
        return;
    }

    std::string wav_bytes = mp_get_file_content(form, "ref_wav");
    if (wav_bytes.empty()) {
        json_error(res, 400, "failed to read 'ref_wav'");
        return;
    }

    if (format.empty()) {
        format = "wav16";
    }

    std::string err;
    int         n_ref    = 0;
    float *     ref_mono = decode_wav_to_mono24k(wav_bytes, &n_ref, err);
    if (!ref_mono) {
        json_error(res, 400, err.c_str());
        return;
    }

    struct qt_tts_params p;
    qt_tts_default_params(&p);
    p.text           = text.c_str();
    p.lang           = lang.empty() ? nullptr : lang.c_str();
    p.ref_audio_24k  = ref_mono;
    p.ref_n_samples  = n_ref;
    p.ref_text       = ref_text.empty() ? nullptr : ref_text.c_str();
    parse_sampling_params(form, p);

    struct qt_audio out = {};
    enum qt_status  rc;
    {
        std::lock_guard<std::mutex> lock(g_infer_mutex);
        rc = qt_synthesize(q, &p, &out);
    }
    free(ref_mono);

    if (rc != QT_STATUS_OK) {
        json_error(res, 500, qt_last_error());
        qt_audio_free(&out);
        return;
    }

    std::string wav = encode_audio_wav(out.samples, out.n_samples, format);
    qt_audio_free(&out);
    res.set_content(std::move(wav), "audio/wav");
}

// ---------------------------------------------------------------------------
// Handler: POST /api/encode
// ---------------------------------------------------------------------------

static void handle_encode(qt_context * q, const httplib::Request & req, httplib::Response & res) {
    const auto & form = req.form;

    if (!mp_has_file(form, "ref_wav")) {
        json_error(res, 400, "'ref_wav' file is required");
        return;
    }

    // Only supported for base models (has speaker encoder + codec encoder)
    if (q->pt.model_type != "base") {
        json_error(res, 400, "encode is only supported for base models");
        return;
    }

    std::string wav_bytes = mp_get_file_content(form, "ref_wav");
    if (wav_bytes.empty()) {
        json_error(res, 400, "failed to read 'ref_wav'");
        return;
    }

    std::string err;
    int         n_samples = 0;
    float *     mono      = decode_wav_to_mono24k(wav_bytes, &n_samples, err);
    if (!mono) {
        json_error(res, 400, err.c_str());
        return;
    }

    // Truncate to hop boundary for codec encode
    const int hop       = TOKENIZER_HOP_LENGTH;
    const int T_aligned = (n_samples / hop) * hop;
    if (T_aligned < hop) {
        free(mono);
        json_error(res, 400, "audio too short (need at least one codec hop = 1920 samples at 24kHz)");
        return;
    }

    // Codec encode (serialised)
    std::vector<int32_t> codes;
    {
        std::lock_guard<std::mutex> lock(g_infer_mutex);
        codes = pipeline_codec_encode(&q->pt.codec, mono, T_aligned);
    }
    if (codes.empty()) {
        free(mono);
        json_error(res, 500, "codec encode failed");
        return;
    }

    // Speaker embedding extraction (serialised)
    std::string        spk_b64;
    std::string        rvq_b64;
    std::vector<float> emb;

    if (q->pt.has_speaker_encoder) {
        ggml_backend_sched_t sched = backend_sched_new(q->pt.bp, 4096);
        bool                 ok;
        {
            std::lock_guard<std::mutex> lock(g_infer_mutex);
            ok = speaker_encoder_extract(&q->pt.speaker_encoder, sched, mono, n_samples, emb);
        }
        ggml_backend_sched_free(sched);
        if (!ok) {
            free(mono);
            json_error(res, 500, "speaker encoder extraction failed");
            return;
        }
        spk_b64 = base64_encode(emb.data(), emb.size() * sizeof(float));
    }
    free(mono);

    // Pack RVQ codes and base64 encode
    std::vector<uint8_t> packed = rvq_pack_codes(codes, TOKENIZER_CODE_BITS);
    rvq_b64                     = base64_encode(packed.data(), packed.size());

    yyjson_mut_doc * doc  = yyjson_mut_doc_new(NULL);
    yyjson_mut_val * root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);
    yyjson_mut_obj_add_str(doc, root, "spk", spk_b64.c_str());
    yyjson_mut_obj_add_str(doc, root, "rvq", rvq_b64.c_str());
    char * json = yyjson_mut_write(doc, 0, NULL);
    res.set_content(json ? json : "{}", "application/json");
    if (json) {
        free(json);
    }
    yyjson_mut_doc_free(doc);
}

// ---------------------------------------------------------------------------
// Handler: POST /api/clone_from_spk
// ---------------------------------------------------------------------------

static void handle_clone_from_spk(qt_context * q, const httplib::Request & req, httplib::Response & res) {
    const auto & form = req.form;

    std::string text     = mp_get_str(form, "text");
    std::string ref_text = mp_get_str(form, "ref_text");
    std::string lang     = mp_get_str(form, "lang");
    std::string format   = mp_get_str(form, "format");

    if (text.empty()) {
        json_error(res, 400, "'text' field is required");
        return;
    }
    if (!mp_has_file(form, "spk")) {
        json_error(res, 400, "'spk' file is required");
        return;
    }

    if (format.empty()) {
        format = "wav16";
    }

    // Parse spk: raw float32 binary
    std::string spk_bytes = mp_get_file_content(form, "spk");
    if (spk_bytes.empty()) {
        json_error(res, 400, "failed to read 'spk' file");
        return;
    }
    if (spk_bytes.size() % sizeof(float) != 0) {
        json_error(res, 400, "'spk' file size is not a multiple of 4 (expected raw float32)");
        return;
    }
    size_t              spk_dim = spk_bytes.size() / sizeof(float);
    std::vector<float>  spk_emb(spk_dim);
    std::memcpy(spk_emb.data(), spk_bytes.data(), spk_bytes.size());

    // Parse rvq (optional): packed codes
    std::vector<int32_t> ref_codes;
    int                  ref_T = 0;
    if (mp_has_file(form, "rvq")) {
        std::string rvq_bytes = mp_get_file_content(form, "rvq");
        if (!rvq_bytes.empty()) {
            const size_t total_bits = rvq_bytes.size() * 8;
            const size_t n_codes    = total_bits / (size_t) TOKENIZER_CODE_BITS;
            if (n_codes > 0 && (n_codes % TOKENIZER_NUM_CODEBOOKS) == 0) {
                std::vector<uint8_t> rvq_buf(rvq_bytes.begin(), rvq_bytes.end());
                ref_codes = rvq_unpack_codes(rvq_buf, n_codes, TOKENIZER_CODE_BITS);
                ref_T     = (int) (n_codes / TOKENIZER_NUM_CODEBOOKS);
            } else {
                json_error(res, 400, "'rvq' file does not contain a valid packed code stream");
                return;
            }
        }
    }

    struct qt_tts_params p;
    qt_tts_default_params(&p);
    p.text        = text.c_str();
    p.lang        = lang.empty() ? nullptr : lang.c_str();
    p.ref_text    = ref_text.empty() ? nullptr : ref_text.c_str();
    p.ref_spk_emb = spk_emb.data();
    p.ref_spk_dim = (int) spk_dim;
    if (!ref_codes.empty()) {
        p.ref_codes = ref_codes.data();
        p.ref_T     = ref_T;
    }
    parse_sampling_params(form, p);

    struct qt_audio out = {};
    enum qt_status  rc;
    {
        std::lock_guard<std::mutex> lock(g_infer_mutex);
        rc = qt_synthesize(q, &p, &out);
    }
    if (rc != QT_STATUS_OK) {
        json_error(res, 500, qt_last_error());
        qt_audio_free(&out);
        return;
    }

    std::string wav = encode_audio_wav(out.samples, out.n_samples, format);
    qt_audio_free(&out);
    res.set_content(std::move(wav), "audio/wav");
}

// ---------------------------------------------------------------------------
// CLI help
// ---------------------------------------------------------------------------

static void print_usage(const char * prog) {
    fprintf(stderr, "qwentts.cpp %s\n\n", QWEN_VERSION);
    fprintf(stderr,
            "Usage: %s --model <gguf> --codec <gguf> [options]\n\n"
            "Required:\n"
            "  --model <gguf>          Talker LM GGUF (qwen-talker-*.gguf)\n"
            "  --codec <gguf>          Codec GGUF (qwen-tokenizer-*.gguf)\n\n"
            "Optional:\n"
            "  --host <ip>             Listen address (default: 127.0.0.1)\n"
            "  --port <n>              Listen port (default: 7860)\n"
            "  --no-fa                 Disable flash attention\n"
            "  --clamp-fp16            Clamp hidden states to FP16 range\n",
            prog);
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main(int argc, char ** argv) {
    const char * talker_path = "models/qwen-talker-1.7b-base-Q8_0.gguf";
    const char * codec_path  = "models/qwen-tokenizer-12hz-Q8_0.gguf";
    std::string  host        = "127.0.0.1";
    int          port        = 7860;
    bool         use_fa      = true;
    bool         clamp_fp16  = false;

    for (int i = 1; i < argc; i++) {
        const char * arg = argv[i];
        if (!std::strcmp(arg, "--model") && i + 1 < argc) {
            talker_path = argv[++i];
        } else if (!std::strcmp(arg, "--codec") && i + 1 < argc) {
            codec_path = argv[++i];
        } else if (!std::strcmp(arg, "--host") && i + 1 < argc) {
            host = argv[++i];
        } else if (!std::strcmp(arg, "--port") && i + 1 < argc) {
            port = std::atoi(argv[++i]);
        } else if (!std::strcmp(arg, "--no-fa")) {
            use_fa = false;
        } else if (!std::strcmp(arg, "--clamp-fp16")) {
            clamp_fp16 = true;
        } else if (!std::strcmp(arg, "--help") || !std::strcmp(arg, "-h")) {
            print_usage(argv[0]);
            return 0;
        } else {
            fprintf(stderr, "[CLI] ERROR: unknown arg: %s\n", arg);
            print_usage(argv[0]);
            return 1;
        }
    }

    if (!talker_path || !codec_path) {
        print_usage(argv[0]);
        return 0;
    }

    struct qt_init_params iparams;
    qt_init_default_params(&iparams);
    iparams.talker_path = talker_path;
    iparams.codec_path  = codec_path;
    iparams.use_fa      = use_fa;
    iparams.clamp_fp16  = clamp_fp16;

    struct qt_context * q = qt_init(&iparams);
    if (!q) {
        fprintf(stderr, "[WebUI] FATAL: %s\n", qt_last_error());
        return 1;
    }

    // Setup server
    httplib::Server svr;
    g_svr = &svr;

    svr.set_read_timeout(60);
    svr.set_write_timeout(120);
    svr.set_payload_max_length(64 * 1024 * 1024);
    svr.set_tcp_nodelay(true);

    svr.set_socket_options([](socket_t sock) {
        int one = 1;
#ifdef _WIN32
        setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (const char *) &one, sizeof(one));
#else
        setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
#endif
    });

    svr.set_default_headers({{"Access-Control-Allow-Origin", "*"}});
    svr.Options("/.*", [](const httplib::Request &, httplib::Response & res) {
        res.set_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
        res.set_header("Access-Control-Allow-Headers", "Content-Type");
    });

    // Routes
    svr.Get("/", handle_index);
    svr.Get("/health", handle_health);
    svr.Get("/api/info", [q](const httplib::Request & req, httplib::Response & res) {
        handle_info(q, req, res);
    });
    svr.Post("/api/generate", [q](const httplib::Request & req, httplib::Response & res) {
        handle_generate(q, req, res);
    });
    svr.Post("/api/clone", [q](const httplib::Request & req, httplib::Response & res) {
        handle_clone(q, req, res);
    });
    svr.Post("/api/encode", [q](const httplib::Request & req, httplib::Response & res) {
        handle_encode(q, req, res);
    });
    svr.Post("/api/clone_from_spk", [q](const httplib::Request & req, httplib::Response & res) {
        handle_clone_from_spk(q, req, res);
    });

    signal(SIGINT, webui_on_signal);
    signal(SIGTERM, webui_on_signal);

    fprintf(stderr, "[WebUI] model: %s\n", talker_path);
    fprintf(stderr, "[WebUI] model_type: %s\n", q->pt.model_type.c_str());
    fprintf(stderr, "[WebUI] speakers: %d\n", qt_n_speakers(q));
    fprintf(stderr, "[WebUI] listening on http://%s:%d\n", host.c_str(), port);

    if (!svr.listen(host.c_str(), port)) {
        fprintf(stderr, "[WebUI] FATAL: cannot bind %s:%d\n", host.c_str(), port);
        qt_free(q);
        return 1;
    }

    qt_free(q);
    return 0;
}
