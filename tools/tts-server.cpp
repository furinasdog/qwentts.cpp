// tts-server.cpp: OpenAI-compatible HTTP server backed by the qwentts
// ABI. Loads a talker + codec once, GPU resident, and serves synthesis over
// POST /v1/audio/speech. The shared core lives in src/tts-server.h ; this
// file only wires the qt_* ABI into the generic adapter.

#include "tts-server.h"

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

#include <cstdio>
#include <cstring>
#include <string>

// qt_context internal layout mirror (statically linked, layout is guaranteed)
// Same approach as tts-webui.cpp: define the opaque struct to access internal
// pipeline fields for reference audio encoding (speaker embedding + RVQ codes).
struct qt_context {
    BackendPair  bp;
    PipelineTTS  pt;
    BPETokenizer tok;
};

static void print_usage(const char * prog) {
    fprintf(stderr, "qwentts.cpp %s\n\n", QWEN_VERSION);
    fprintf(stderr,
            "Usage: %s --model <gguf> --codec <gguf> [options]\n\n"
            "Required:\n"
            "  --model <gguf>          Talker LM GGUF (qwen-talker-*.gguf)\n"
            "  --codec <gguf>          Codec GGUF (qwen-tokenizer-*.gguf)\n\n"
            "Optional:\n"
            "  --host <ip>             Listen address (default: 127.0.0.1)\n"
            "  --port <n>              Listen port (default: 8080)\n"
            "  --lang <name>           Language label (default: auto)\n"
            "  --cache-dir <dir>       Reference audio cache directory (default: qwentts_server_cache)\n"
            "  --no-fa                 Disable flash attention\n"
            "  --clamp-fp16            Clamp hidden states to FP16 range\n",
            prog);
}

// Trim a path down to its file name for the reported model id.
static std::string basename_of(const char * path) {
    std::string s = path;
    size_t      p = s.find_last_of("/\\");
    return p == std::string::npos ? s : s.substr(p + 1);
}

int main(int argc, char ** argv) {
    const char * talker_path = "models/qwen-talker-1.7b-base-Q8_0.gguf";
    const char * codec_path  = "models/qwen-tokenizer-12hz-Q8_0.gguf";
    std::string   lang        = "auto";
    std::string   cache_dir   = "qwentts_server_cache";
    server_config cfg;
    bool          use_fa     = true;
    bool          clamp_fp16 = false;

    for (int i = 1; i < argc; i++) {
        const char * arg = argv[i];
        if (!std::strcmp(arg, "--model") && i + 1 < argc) {
            talker_path = argv[++i];
        } else if (!std::strcmp(arg, "--codec") && i + 1 < argc) {
            codec_path = argv[++i];
        } else if (!std::strcmp(arg, "--host") && i + 1 < argc) {
            cfg.host = argv[++i];
        } else if (!std::strcmp(arg, "--port") && i + 1 < argc) {
            cfg.port = std::atoi(argv[++i]);
        } else if (!std::strcmp(arg, "--lang") && i + 1 < argc) {
            lang = argv[++i];
        } else if (!std::strcmp(arg, "--cache-dir") && i + 1 < argc) {
            cache_dir = argv[++i];
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
    
    // Initialize reference audio cache
    tts_cache_init(cache_dir);

    struct qt_init_params iparams;
    qt_init_default_params(&iparams);
    iparams.talker_path = talker_path;
    iparams.codec_path  = codec_path;
    iparams.use_fa      = use_fa;
    iparams.clamp_fp16  = clamp_fp16;

    struct qt_context * q = qt_init(&iparams);
    if (!q) {
        fprintf(stderr, "[Server] FATAL: %s\n", qt_last_error());
        return 1;
    }

    tts_backend be;
    be.model_id = basename_of(talker_path);
    int n       = qt_n_speakers(q);
    for (int i = 0; i < n; i++) {
        be.voices.push_back(qt_speaker_name(q, i));
    }

    // The adapter always drives the streaming pipeline : on_chunk routes to
    // the shared sink, which either streams to the socket (pcm) or fills a
    // one-shot buffer (wav). Either way the audio path is identical.
    be.synthesize = [q, &lang](const tts_request & req, const tts_sink & sink, std::string & err) -> int {
        struct qt_tts_params p;
        qt_tts_default_params(&p);
        p.text = req.input.c_str();
        p.lang = lang.c_str();
        
        // Zero-shot voice cloning mode
        const bool has_ref_audio = !req.ref_wav_b64.empty() || !req.ref_wav_path.empty();
        if (has_ref_audio) {
            // Zero-shot mode constraints: base model only, no speaker/instruct
            if (qt_n_speakers(q) > 0) {
                err = "zero-shot cloning requires base model (current model has speakers)";
                return -1;
            }
            
            // Use pre-encoded data if available, otherwise raw audio
            if (!req.ref_spk_emb.empty()) {
                // Pre-encoded mode (cache hit)
                p.ref_spk_emb = req.ref_spk_emb.data();
                p.ref_spk_dim = (int) req.ref_spk_emb.size();
                if (!req.ref_codes.empty() && req.ref_T > 0) {
                    // Mode B: ICL with encoded codes
                    p.ref_codes = req.ref_codes.data();
                    p.ref_T = req.ref_T;
                    p.ref_text = req.ref_text.c_str();
                }
                // Otherwise Mode A: x-vector only
            } else if (req.ref_n_samples > 0) {
                // Raw audio mode (cache miss)
                p.ref_audio_24k = req.ref_audio_24k_buf.data();
                p.ref_n_samples = req.ref_n_samples;
                p.ref_text = req.ref_text.c_str();
            }
        } else {
            // Non-zero-shot mode: fixed voice / voice design
            if (!req.voice.empty() && qt_n_speakers(q) > 0) {
                p.speaker = req.voice.c_str();
            }
            if (!req.instructions.empty()) {
                p.instruct = req.instructions.c_str();
            }
        }

        // Trampoline : the C ABI on_chunk forwards to the C++ sink.
        const tts_sink * sink_ptr = &sink;
        p.on_chunk                = [](const float * s, int ns, void * u) -> bool {
            return (*static_cast<const tts_sink *>(u))(s, ns);
        };
        p.on_chunk_user_data = (void *) sink_ptr;

        struct qt_audio out = {};
        enum qt_status  rc  = qt_synthesize(q, &p, &out);
        qt_audio_free(&out);
        if (rc != QT_STATUS_OK) {
            err = qt_last_error();
            return (int) rc;
        }
        return 0;
    };
    
    // Encode reference audio into speaker embedding and RVQ codes.
    // Uses internal pipeline access (same pattern as tts-webui.cpp) to call
    // pipeline_codec_encode (RVQ) and speaker_encoder_extract (spk emb).
    be.encode = [q](const std::vector<float>& audio_24k, int n_samples,
                    std::vector<float>& spk_emb, std::vector<int32_t>& codes, int& ref_T,
                    std::string& err) -> int {
        if (!q->pt.has_speaker_encoder) {
            err = "speaker encoder not loaded (base model required for encoding)";
            return -1;
        }
        if (!audio_24k.data() || n_samples <= 0) {
            err = "empty audio buffer";
            return -1;
        }

        // Truncate to hop boundary for codec encode (1920 samples at 24kHz)
        const int hop       = TOKENIZER_HOP_LENGTH;
        const int T_aligned = (n_samples / hop) * hop;
        if (T_aligned < hop) {
            err = "audio too short (need at least 1920 samples at 24kHz for codec encode)";
            return -1;
        }

        // Codec encode: 24kHz mono audio -> RVQ codes [K=16, T] row-major
        codes = pipeline_codec_encode(&q->pt.codec, audio_24k.data(), T_aligned);
        if (codes.empty()) {
            err = "codec encode failed";
            return -1;
        }
        const int K = TOKENIZER_NUM_CODEBOOKS;
        ref_T       = (int) codes.size() / K;

        // Speaker embedding extraction: needs a dedicated scheduler
        ggml_backend_sched_t sched = backend_sched_new(q->pt.bp, 4096);
        bool                 ok    = speaker_encoder_extract(&q->pt.speaker_encoder, sched,
                                                            audio_24k.data(), n_samples, spk_emb);
        ggml_backend_sched_free(sched);
        if (!ok || spk_emb.empty()) {
            err = "speaker encoder extraction failed";
            return -1;
        }

        return 0;
    };

    int rc = tts_server_run(be, cfg);
    qt_free(q);
    return rc;
}
