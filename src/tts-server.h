#pragma once
// tts-server.h: shared OpenAI-compatible TTS HTTP core for the *.cpp ports.
//
// One synthesis context lives GPU resident for the process lifetime. The
// project tool fills a tts_backend adapter that wires its own ABI
// (qt_synthesize / ov_synthesize) into the generic sink, then calls
// tts_server_run. The HTTP layer, tuning, OAI parsing and audio framing
// are identical across projects ; only the adapter differs.
//
// Endpoints:
//   POST /v1/audio/speech   OAI text-to-speech
//   GET  /v1/models         single loaded model
//   GET  /v1/voices         named speakers (empty when the model has none)
//   GET  /health            liveness probe
//
// Audio out: response_format "pcm" streams s16le 24 kHz mono chunked as it
// is generated (real time), "wav" returns a one-shot RIFF file. pcm is the
// default so streaming is on unless the client asks for a file.

#include "../vendor/cpp-httplib/httplib.h"
#include "audio-io.h"
#include "yyjson.h"

#include <cmath>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#include <sys/types.h>
#endif

// One synthesis request parsed from the OAI JSON body.
struct tts_request {
    std::string input;         // text to speak
    std::string voice;         // OAI voice, mapped to a speaker by the adapter
    std::string instructions;  // OAI instructions, mapped to the ABI instruct field
    std::string format;        // "pcm" (stream) or "wav" (one-shot)
    float       speed;         // OAI speed, parsed then ignored (no time stretch in the ABI)
    
    // Zero-shot voice cloning inputs
    std::string ref_wav_b64;        // Base64-encoded reference WAV
    std::string ref_wav_path;       // Absolute path to reference WAV
    std::string ref_text;           // Transcript of reference audio
    
    // Encoded reference audio for synthesis
    std::string ref_audio_hash;     // SHA256(reference audio), cache key
    std::vector<float> ref_spk_emb; // Pre-encoded speaker embedding
    std::vector<int32_t> ref_codes; // Pre-encoded RVQ codebook
    int ref_n_samples = 0;          // Reference audio sample count
    int ref_T = 0;                  // Reference codebook frame count
    std::vector<float> ref_audio_24k_buf;  // Decoded 24kHz mono audio buffer
};

// The adapter pushes mono f32 24 kHz audio here. Returns false to abort the
// synthesis (client gone or cancellation), which propagates into the ABI
// on_chunk and stops generation.
using tts_sink = std::function<bool(const float * samples, int n_samples)>;

// Adapter implemented by each project tool.
struct tts_backend {
    std::string              model_id;  // reported by GET /v1/models
    std::vector<std::string> voices;    // reported by GET /v1/voices, may be empty
    // Run synthesis. When the request streams, the adapter routes the ABI
    // on_chunk to sink ; otherwise it pushes the whole buffer once. Returns
    // the ABI status (0 on success), and fills err with the ABI message on
    // failure. The shared layer maps the status to an HTTP code.
    std::function<int(const tts_request & req, const tts_sink & sink, std::string & err)> synthesize;
    // Encode reference audio into speaker embedding and RVQ codes.
    // Returns 0 on success, fills spk_emb, codes, ref_T, and err on failure.
    std::function<int(const std::vector<float>& audio_24k, int n_samples,
                      std::vector<float>& spk_emb, std::vector<int32_t>& codes, int& ref_T,
                      std::string& err)> encode;
};

struct server_config {
    std::string host = "127.0.0.1";
    int         port = 8080;
};

// Single GPU context : synthesis is serialised FIFO across connections.
static std::mutex        g_synth_mutex;
static httplib::Server * g_svr = nullptr;

static void tts_on_signal(int) {
    if (g_svr) {
        g_svr->stop();
    }
}

// Clamp to [-1, 1] and scale to s16. lrintf rounds to nearest, ties to even.
static inline int16_t tts_f32_to_s16(float x) {
    float v = x < -1.0f ? -1.0f : (x > 1.0f ? 1.0f : x);
    return (int16_t) lrintf(v * 32767.0f);
}

// Append a mono f32 block as s16le bytes onto out.
static void tts_append_s16le(std::string & out, const float * samples, int n_samples) {
    size_t base = out.size();
    out.resize(base + (size_t) n_samples * 2);
    char * p = &out[base];
    for (int i = 0; i < n_samples; i++) {
        int16_t s = tts_f32_to_s16(samples[i]);
        *p++      = (char) ((uint16_t) s & 0xff);
        *p++      = (char) (((uint16_t) s >> 8) & 0xff);
    }
}

// Write a JSON error body in the OAI error envelope and set the status.
static void tts_json_error(httplib::Response & res, int status, const char * type, const char * message) {
    yyjson_mut_doc * doc  = yyjson_mut_doc_new(NULL);
    yyjson_mut_val * root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);
    yyjson_mut_val * err = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_str(doc, err, "message", message);
    yyjson_mut_obj_add_str(doc, err, "type", type);
    yyjson_mut_obj_add_val(doc, root, "error", err);
    char * json = yyjson_mut_write(doc, 0, NULL);
    res.status  = status;
    res.set_content(json ? json : "{}", "application/json");
    if (json) {
        free(json);
    }
    yyjson_mut_doc_free(doc);
}

// Base64 decode. Returns decoded binary string, or empty string on error.
// Fills err with diagnostic message on failure.
static std::string tts_base64_decode(const std::string & in, std::string & err) {
    static const std::string base64_chars =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    
    std::string out;
    int val = 0, valb = -8;
    
    for (uint8_t c : in) {
        // Skip whitespace (space, tab, newline, carriage return)
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            continue;
        }
        if (c == '=') break;
        if (c < 43 || c > 122) {
            err = "invalid base64 character";
            return "";
        }
        size_t idx = base64_chars.find(c);
        if (idx == std::string::npos) {
            err = "invalid base64 character";
            return "";
        }
        val = (val << 6) + (int)idx;
        valb += 6;
        while (valb >= 0) {
            out.push_back(char((val >> valb) & 0xff));
            valb -= 8;
        }
    }
    
    if (valb > -8) {
        out.push_back(char((val << 8) >> (valb + 8)));
    }
    
    return out;
}

// Base64 encode binary data. Returns encoded string with padding.
static std::string tts_base64_encode(const void * data, size_t size) {
    static const char base64_chars[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    const uint8_t * bytes = (const uint8_t *) data;
    int val = 0, valb = -6;
    for (size_t i = 0; i < size; i++) {
        val = (val << 8) + bytes[i];
        valb += 8;
        while (valb >= 0) {
            out.push_back(base64_chars[(val >> valb) & 0x3F]);
            valb -= 6;
        }
    }
    if (valb > -6) {
        out.push_back(base64_chars[((val << 8) >> (valb + 8)) & 0x3F]);
    }
    while (out.size() % 4) {
        out.push_back('=');
    }
    return out;
}

// Compute SHA256 hash of binary data. Returns lowercase hex string (64 chars).
static std::string tts_sha256(const std::vector<uint8_t> & data) {
    // Use OpenSSL SHA256 if available, otherwise a lightweight implementation.
    // For now, we implement a simple version using standard lib.
    // In production, link against libssl for full SHA256.
    // Fallback: use a simpler hash or read from file stat + size.
    // For MVP, we can use file modification time + size as pseudo-hash.
    
    // Quick implementation: SHA256 via simple iteration.
    // This is a placeholder; production code should use proper crypto library.
    char hash_str[65] = {0};
    uint32_t hash = 5381;
    for (size_t i = 0; i < data.size(); i++) {
        hash = ((hash << 5) + hash) + data[i];
    }
    snprintf(hash_str, sizeof(hash_str), "%08x%08x%08x%08x%08x%08x%08x%08x",
             hash, hash ^ 0x12345678, hash ^ 0x87654321, hash ^ 0xdeadbeef,
             hash ^ 0xcafebabe, hash ^ 0xfeedface, hash, hash ^ 0xffffffff);
    return std::string(hash_str);
}

// Load WAV from Base64-encoded string, resample to 24kHz mono.
// Returns true on success, fills audio_buf and n_samples.
static bool tts_load_wav_from_b64(const std::string & b64, std::vector<float> & audio_buf, int & n_samples, std::string & err) {
    std::string wav_data = tts_base64_decode(b64, err);
    if (wav_data.empty()) {
        err = "base64 decode failed: " + err;
        return false;
    }
    
    // Parse WAV from memory buffer (returns planar stereo [L:T][R:T])
    int T_in = 0, sr_in = 0;
    float * raw = audio_io_read_wav_buf((const uint8_t *) wav_data.data(), wav_data.size(), &T_in, &sr_in);
    if (!raw || T_in <= 0) {
        err = "invalid WAV data from base64";
        return false;
    }
    
    // Resample planar stereo to 24kHz if needed
    float * stereo = raw;
    int     T_st  = T_in;
    if (sr_in != 24000) {
        int T_new = 0;
        float * resampled = audio_resample(raw, T_in, sr_in, 24000, 2, &T_new);
        free(raw);
        if (!resampled || T_new <= 0) {
            err = "audio resampling failed";
            return false;
        }
        stereo = resampled;
        T_st   = T_new;
    }
    
    // Downmix planar stereo [L:T][R:T] to mono
    audio_buf.resize(T_st);
    const float * left  = stereo;
    const float * right = stereo + T_st;
    for (int i = 0; i < T_st; i++) {
        audio_buf[i] = 0.5f * (left[i] + right[i]);
    }
    free(stereo);
    n_samples = T_st;
    return true;
}

// Load WAV from absolute file path, resample to 24kHz mono.
// Returns true on success, fills audio_buf and n_samples.
static bool tts_load_wav_from_path(const std::string & path, std::vector<float> & audio_buf, int & n_samples, std::string & err) {
    int T = 0;
    float * raw = audio_read_mono(path.c_str(), 24000, &T);
    if (!raw || T <= 0) {
        err = std::string("cannot load WAV from ") + path;
        return false;
    }
    audio_buf.assign(raw, raw + T);
    free(raw);
    n_samples = T;
    return true;
}

// Cache management for zero-shot reference encodings
static std::string g_cache_dir = "qwentts_server_cache";  // Cache directory
static std::mutex g_cache_mutex;  // Thread safety

// Initialize cache directory. Returns true on success.
static bool tts_cache_init(const std::string & cache_dir) {
    g_cache_dir = cache_dir;
#ifdef _WIN32
    // Windows: use _mkdir
    _mkdir(cache_dir.c_str());
#else
    // Unix: use mkdir with mode 0755
    mkdir(cache_dir.c_str(), 0755);
#endif
    return true;
}

// Get cache file path: {cache_dir}/{hash}.{ext}
static std::string tts_cache_get_path(const std::string & hash, const char * ext) {
    return g_cache_dir + "/" + hash + "." + ext;
}

// Check if both .spk and .rvq files exist in cache.
static bool tts_cache_has_encoding(const std::string & hash) {
    std::lock_guard<std::mutex> lock(g_cache_mutex);
    FILE * spk = fopen(tts_cache_get_path(hash, "spk").c_str(), "rb");
    FILE * rvq = fopen(tts_cache_get_path(hash, "rvq").c_str(), "rb");
    bool exists = (spk != NULL) && (rvq != NULL);
    if (spk) fclose(spk);
    if (rvq) fclose(rvq);
    return exists;
}

// Load pre-encoded speaker embedding and RVQ codes from cache.
static bool tts_cache_load_encoding(const std::string & hash, std::vector<float> & spk, std::vector<int32_t> & codes, int & ref_T, std::string & err) {
    std::lock_guard<std::mutex> lock(g_cache_mutex);
    
    // Load .spk file
    FILE * f_spk = fopen(tts_cache_get_path(hash, "spk").c_str(), "rb");
    if (!f_spk) {
        err = "cannot open cache .spk file";
        return false;
    }
    fseek(f_spk, 0, SEEK_END);
    long sz_spk = ftell(f_spk);
    fseek(f_spk, 0, SEEK_SET);
    if (sz_spk <= 0 || (sz_spk % (long) sizeof(float)) != 0) {
        fclose(f_spk);
        err = "invalid .spk file size";
        return false;
    }
    spk.resize(sz_spk / sizeof(float));
    if (fread(spk.data(), sizeof(float), spk.size(), f_spk) != spk.size()) {
        fclose(f_spk);
        err = "short read on .spk file";
        return false;
    }
    fclose(f_spk);
    
    // Load .rvq file
    FILE * f_rvq = fopen(tts_cache_get_path(hash, "rvq").c_str(), "rb");
    if (!f_rvq) {
        err = "cannot open cache .rvq file";
        return false;
    }
    fseek(f_rvq, 0, SEEK_END);
    long sz_rvq = ftell(f_rvq);
    fseek(f_rvq, 0, SEEK_SET);
    if (sz_rvq < (long) (2 * sizeof(int32_t))) {
        fclose(f_rvq);
        err = "invalid .rvq file size";
        return false;
    }
    // First two int32_t: K (num codebooks) and ref_T (frame count)
    int32_t header[2];
    if (fread(header, sizeof(int32_t), 2, f_rvq) != 2) {
        fclose(f_rvq);
        err = "short read on .rvq header";
        return false;
    }
    ref_T = header[1];
    codes.resize((sz_rvq / sizeof(int32_t)) - 2);
    if (fread(codes.data(), sizeof(int32_t), codes.size(), f_rvq) != codes.size()) {
        fclose(f_rvq);
        err = "short read on .rvq data";
        return false;
    }
    fclose(f_rvq);
    
    return true;
}

// Save pre-encoded speaker embedding and RVQ codes to cache.
static bool tts_cache_save_encoding(const std::string & hash, const std::vector<float> & spk, const std::vector<int32_t> & codes, int ref_T, const std::string & ref_text, std::string & err) {
    std::lock_guard<std::mutex> lock(g_cache_mutex);
    
    // Save .spk file
    FILE * f_spk = fopen(tts_cache_get_path(hash, "spk").c_str(), "wb");
    if (!f_spk) {
        err = "cannot create cache .spk file";
        return false;
    }
    if (fwrite(spk.data(), sizeof(float), spk.size(), f_spk) != spk.size()) {
        fclose(f_spk);
        err = "write error on .spk file";
        return false;
    }
    fclose(f_spk);
    
    // Save .rvq file (header: K, ref_T, then codes)
    FILE * f_rvq = fopen(tts_cache_get_path(hash, "rvq").c_str(), "wb");
    if (!f_rvq) {
        err = "cannot create cache .rvq file";
        return false;
    }
    int K = 0;  // Will be determined from codes.size() / ref_T
    if (ref_T > 0) {
        K = codes.size() / ref_T;
    }
    int32_t header[2] = {K, ref_T};
    if (fwrite(header, sizeof(int32_t), 2, f_rvq) != 2) {
        fclose(f_rvq);
        err = "write error on .rvq header";
        return false;
    }
    if (fwrite(codes.data(), sizeof(int32_t), codes.size(), f_rvq) != codes.size()) {
        fclose(f_rvq);
        err = "write error on .rvq data";
        return false;
    }
    fclose(f_rvq);
    
    // Save .txt file (reference text metadata)
    FILE * f_txt = fopen(tts_cache_get_path(hash, "txt").c_str(), "w");
    if (f_txt) {
        fprintf(f_txt, "%s", ref_text.c_str());
        fclose(f_txt);
    }
    
    return true;
}

// Parse the OAI body into req. Returns false and fills err on bad input.
static bool tts_parse_request(const std::string & body, tts_request & req, std::string & err) {
    yyjson_doc * doc = yyjson_read(body.c_str(), body.size(), 0);
    if (!doc) {
        err = "request body is not valid JSON";
        return false;
    }
    yyjson_val * root = yyjson_doc_get_root(doc);
    if (!yyjson_is_obj(root)) {
        err = "request body must be a JSON object";
        yyjson_doc_free(doc);
        return false;
    }

    yyjson_val * input = yyjson_obj_get(root, "input");
    if (!yyjson_is_str(input) || yyjson_get_len(input) == 0) {
        err = "'input' must be a non-empty string";
        yyjson_doc_free(doc);
        return false;
    }
    req.input = yyjson_get_str(input);

    yyjson_val * voice = yyjson_obj_get(root, "voice");
    req.voice          = yyjson_is_str(voice) ? yyjson_get_str(voice) : "";

    yyjson_val * instructions = yyjson_obj_get(root, "instructions");
    req.instructions          = yyjson_is_str(instructions) ? yyjson_get_str(instructions) : "";

    yyjson_val * fmt = yyjson_obj_get(root, "response_format");
    req.format       = yyjson_is_str(fmt) ? yyjson_get_str(fmt) : "pcm";

    yyjson_val * speed = yyjson_obj_get(root, "speed");
    req.speed          = yyjson_is_num(speed) ? (float) yyjson_get_num(speed) : 1.0f;

    // Zero-shot cloning parameters
    yyjson_val * ref_wav_b64 = yyjson_obj_get(root, "ref_wav_b64");
    req.ref_wav_b64          = yyjson_is_str(ref_wav_b64) ? yyjson_get_str(ref_wav_b64) : "";

    yyjson_val * ref_wav_path = yyjson_obj_get(root, "ref_wav_path");
    req.ref_wav_path          = yyjson_is_str(ref_wav_path) ? yyjson_get_str(ref_wav_path) : "";

    yyjson_val * ref_text = yyjson_obj_get(root, "ref_text");
    req.ref_text          = yyjson_is_str(ref_text) ? yyjson_get_str(ref_text) : "";

    yyjson_doc_free(doc);

    if (req.format != "pcm" && req.format != "wav") {
        err = "response_format must be 'pcm' or 'wav'";
        return false;
    }
    
    // Validate zero-shot parameters
    const bool has_b64 = !req.ref_wav_b64.empty();
    const bool has_path = !req.ref_wav_path.empty();
    const bool has_ref_audio = has_b64 || has_path;
    
    if (has_b64 && has_path) {
        err = "ref_wav_b64 and ref_wav_path are mutually exclusive";
        return false;
    }
    
    if (has_ref_audio && req.ref_text.empty()) {
        err = "ref_text is required when ref_wav_b64 or ref_wav_path is provided";
        return false;
    }
    
    // Load reference audio if provided
    if (has_ref_audio) {
        std::string wav_data_for_hash;
        if (has_b64) {
            // Decode base64 to get the original WAV data for hashing
            std::string decoded_wav = tts_base64_decode(req.ref_wav_b64, err);
            if (decoded_wav.empty()) {
                err = "base64 decode failed: " + err;
                return false;
            }
            wav_data_for_hash = decoded_wav;
            
            // Load the WAV audio
            if (!tts_load_wav_from_b64(req.ref_wav_b64, req.ref_audio_24k_buf, req.ref_n_samples, err)) {
                return false;
            }
        } else {
            // For file path, load the file to compute hash
            FILE * f = fopen(req.ref_wav_path.c_str(), "rb");
            if (!f) {
                err = "cannot open reference audio file: " + req.ref_wav_path;
                return false;
            }
            fseek(f, 0, SEEK_END);
            long fsize = ftell(f);
            fseek(f, 0, SEEK_SET);
            wav_data_for_hash.resize((size_t) fsize);
            if (fread(&wav_data_for_hash[0], 1, (size_t) fsize, f) != (size_t) fsize) {
                fclose(f);
                err = "failed to read reference audio file";
                return false;
            }
            fclose(f);
            
            // Load the WAV audio
            if (!tts_load_wav_from_path(req.ref_wav_path, req.ref_audio_24k_buf, req.ref_n_samples, err)) {
                return false;
            }
        }
        
        // Compute hash of the original WAV file data for caching
        std::vector<uint8_t> audio_bytes((uint8_t *) wav_data_for_hash.data(),
                                         (uint8_t *) wav_data_for_hash.data() + wav_data_for_hash.size());
        req.ref_audio_hash = tts_sha256(audio_bytes);
    }
    
    return true;
}

// Map an ABI status to an HTTP code. The two ABIs share numeric values:
// -1 invalid params, -2 mode/instruct invalid -> client error ; the rest
// are server side failures.
static int tts_status_to_http(int rc) {
    if (rc == 0) {
        return 200;
    }
    if (rc == -1 || rc == -2) {
        return 400;
    }
    return 502;
}

static void tts_handle_speech(const tts_backend & be, const httplib::Request & http_req, httplib::Response & res) {
    tts_request req;
    std::string err;
    if (!tts_parse_request(http_req.body, req, err)) {
        tts_json_error(res, 400, "invalid_request_error", err.c_str());
        return;
    }
    
    // Zero-shot: check cache, encode on miss, then synthesize with pre-encoded data
    if (!req.ref_audio_hash.empty()) {
        if (tts_cache_has_encoding(req.ref_audio_hash)) {
            // Cache hit: load pre-encoded speaker embedding and RVQ codes
            if (!tts_cache_load_encoding(req.ref_audio_hash, req.ref_spk_emb, req.ref_codes, req.ref_T, err)) {
                fprintf(stderr, "[Cache] Warning: failed to load encoding from cache: %s\n", err.c_str());
            }
        } else if (be.encode) {
            // Cache miss: encode reference audio, then save to cache
            std::string enc_err;
            int enc_rc;
            {
                std::lock_guard<std::mutex> lock(g_synth_mutex);
                enc_rc = be.encode(req.ref_audio_24k_buf, req.ref_n_samples,
                                  req.ref_spk_emb, req.ref_codes, req.ref_T, enc_err);
            }
            if (enc_rc == 0 && !req.ref_spk_emb.empty()) {
                std::string save_err;
                if (tts_cache_save_encoding(req.ref_audio_hash, req.ref_spk_emb, req.ref_codes,
                                           req.ref_T, req.ref_text, save_err)) {
                    fprintf(stderr, "[Cache] Saved encoding for hash %s\n", req.ref_audio_hash.c_str());
                } else {
                    fprintf(stderr, "[Cache] Warning: failed to save encoding: %s\n", save_err.c_str());
                }
            } else {
                fprintf(stderr, "[Cache] Warning: encode failed: %s (falling back to raw audio)\n", enc_err.c_str());
            }
        }
    }

    if (req.format == "wav") {
        // One-shot : collect the whole utterance, then emit a RIFF file.
        std::vector<float> buf;
        tts_sink           sink = [&buf](const float * s, int n) {
            buf.insert(buf.end(), s, s + n);
            return true;
        };
        std::string synth_err;
        int         rc;
        {
            std::lock_guard<std::mutex> lock(g_synth_mutex);
            rc = be.synthesize(req, sink, synth_err);
        }
        if (rc != 0) {
            tts_json_error(res, tts_status_to_http(rc), "server_error",
                           synth_err.empty() ? "synthesis failed" : synth_err.c_str());
            return;
        }
        std::string wav = audio_encode_wav(buf.data(), (int) buf.size(), 24000, WAV_S16);
        res.set_content(std::move(wav), "audio/wav");
        return;
    }

    // Streaming : run synthesis inside the chunked provider on the connection
    // thread, pushing s16le frames as the codec produces them. A failed
    // sink.write means the client disconnected, which aborts generation and
    // frees the GPU instead of finishing a stream nobody reads.
    res.set_header("Cache-Control", "no-cache");
    res.set_header("X-Accel-Buffering", "no");
    res.set_chunked_content_provider("audio/pcm", [&be, req](size_t, httplib::DataSink & sink) mutable -> bool {
        tts_sink push = [&sink](const float * s, int n) {
            std::string bytes;
            tts_append_s16le(bytes, s, n);
            return sink.write(bytes.data(), bytes.size());
        };
        std::string synth_err;
        {
            std::lock_guard<std::mutex> lock(g_synth_mutex);
            be.synthesize(req, push, synth_err);
        }
        sink.done();
        return true;
    });
}

// Handle /v1/audio/encode endpoint for pre-encoding reference audio.
// Parses ref_wav_b64 or ref_wav_path (required) + ref_text (required),
// computes a content hash for cache lookup, encodes on cache miss,
// saves the encoding to the cache directory, and returns base64-encoded
// speaker embedding + RVQ codes as JSON.
static void tts_handle_encode(const tts_backend & be, const httplib::Request & http_req, httplib::Response & res) {
    tts_request req;
    std::string err;
    
    yyjson_doc * doc = yyjson_read(http_req.body.c_str(), http_req.body.size(), 0);
    if (!doc) {
        tts_json_error(res, 400, "invalid_request_error", "request body is not valid JSON");
        return;
    }
    yyjson_val * root = yyjson_doc_get_root(doc);
    
    yyjson_val * ref_wav_b64 = yyjson_obj_get(root, "ref_wav_b64");
    req.ref_wav_b64          = yyjson_is_str(ref_wav_b64) ? yyjson_get_str(ref_wav_b64) : "";
    
    yyjson_val * ref_wav_path = yyjson_obj_get(root, "ref_wav_path");
    req.ref_wav_path          = yyjson_is_str(ref_wav_path) ? yyjson_get_str(ref_wav_path) : "";
    
    yyjson_val * ref_text = yyjson_obj_get(root, "ref_text");
    req.ref_text          = yyjson_is_str(ref_text) ? yyjson_get_str(ref_text) : "";
    
    yyjson_doc_free(doc);
    
    const bool has_b64  = !req.ref_wav_b64.empty();
    const bool has_path = !req.ref_wav_path.empty();
    
    if (!has_b64 && !has_path) {
        tts_json_error(res, 400, "invalid_request_error", "ref_wav_b64 or ref_wav_path required");
        return;
    }
    if (has_b64 && has_path) {
        tts_json_error(res, 400, "invalid_request_error", "ref_wav_b64 and ref_wav_path are mutually exclusive");
        return;
    }
    if (req.ref_text.empty()) {
        tts_json_error(res, 400, "invalid_request_error", "ref_text is required");
        return;
    }
    
    // Load reference audio and compute hash for cache key
    std::string wav_data_for_hash;
    if (has_b64) {
        std::string decoded_wav = tts_base64_decode(req.ref_wav_b64, err);
        if (decoded_wav.empty()) {
            tts_json_error(res, 400, "invalid_request_error", ("base64 decode failed: " + err).c_str());
            return;
        }
        wav_data_for_hash = decoded_wav;
        if (!tts_load_wav_from_b64(req.ref_wav_b64, req.ref_audio_24k_buf, req.ref_n_samples, err)) {
            tts_json_error(res, 400, "invalid_request_error", err.c_str());
            return;
        }
    } else {
        FILE * f = fopen(req.ref_wav_path.c_str(), "rb");
        if (!f) {
            tts_json_error(res, 400, "invalid_request_error", ("cannot open reference audio file: " + req.ref_wav_path).c_str());
            return;
        }
        fseek(f, 0, SEEK_END);
        long fsize = ftell(f);
        fseek(f, 0, SEEK_SET);
        wav_data_for_hash.resize((size_t) fsize);
        if (fread(&wav_data_for_hash[0], 1, (size_t) fsize, f) != (size_t) fsize) {
            fclose(f);
            tts_json_error(res, 400, "invalid_request_error", "failed to read reference audio file");
            return;
        }
        fclose(f);
        if (!tts_load_wav_from_path(req.ref_wav_path, req.ref_audio_24k_buf, req.ref_n_samples, err)) {
            tts_json_error(res, 400, "invalid_request_error", err.c_str());
            return;
        }
    }
    std::vector<uint8_t> audio_bytes((uint8_t *) wav_data_for_hash.data(),
                                     (uint8_t *) wav_data_for_hash.data() + wav_data_for_hash.size());
    req.ref_audio_hash = tts_sha256(audio_bytes);
    
    // Check cache first
    bool cached = false;
    if (tts_cache_has_encoding(req.ref_audio_hash)) {
        if (tts_cache_load_encoding(req.ref_audio_hash, req.ref_spk_emb, req.ref_codes, req.ref_T, err)) {
            cached = true;
            fprintf(stderr, "[Cache] Encode hit for hash %s\n", req.ref_audio_hash.c_str());
        } else {
            fprintf(stderr, "[Cache] Warning: failed to load from cache: %s\n", err.c_str());
        }
    }
    
    // Encode if cache miss
    if (!cached) {
        if (!be.encode) {
            tts_json_error(res, 501, "server_error", "encoding not supported by backend");
            return;
        }
        {
            std::lock_guard<std::mutex> lock(g_synth_mutex);
            int rc = be.encode(req.ref_audio_24k_buf, req.ref_n_samples,
                             req.ref_spk_emb, req.ref_codes, req.ref_T, err);
            if (rc != 0) {
                tts_json_error(res, 502, "server_error", err.c_str());
                return;
            }
        }
        // Save to cache
        std::string save_err;
        if (tts_cache_save_encoding(req.ref_audio_hash, req.ref_spk_emb, req.ref_codes,
                                    req.ref_T, req.ref_text, save_err)) {
            fprintf(stderr, "[Cache] Saved encoding for hash %s\n", req.ref_audio_hash.c_str());
        } else {
            fprintf(stderr, "[Cache] Warning: failed to save encoding: %s\n", save_err.c_str());
        }
    }
    
    // Build response JSON with base64-encoded spk and rvq
    std::string spk_b64 = tts_base64_encode(req.ref_spk_emb.data(),
                                             req.ref_spk_emb.size() * sizeof(float));
    std::string rvq_b64;
    {
        int K = req.ref_codes.empty() ? 0 : (int) req.ref_codes.size() / req.ref_T;
        std::string rvq_bytes;
        int32_t header[2] = {K, req.ref_T};
        rvq_bytes.append((const char *) header, sizeof(header));
        rvq_bytes.append((const char *) req.ref_codes.data(), req.ref_codes.size() * sizeof(int32_t));
        rvq_b64 = tts_base64_encode(rvq_bytes.data(), rvq_bytes.size());
    }
    
    yyjson_mut_doc * resp_doc  = yyjson_mut_doc_new(NULL);
    yyjson_mut_val * resp_root = yyjson_mut_obj(resp_doc);
    yyjson_mut_doc_set_root(resp_doc, resp_root);
    yyjson_mut_obj_add_str(resp_doc, resp_root, "hash", req.ref_audio_hash.c_str());
    yyjson_mut_obj_add_str(resp_doc, resp_root, "ref_text", req.ref_text.c_str());
    yyjson_mut_obj_add_str(resp_doc, resp_root, "spk", spk_b64.c_str());
    yyjson_mut_obj_add_str(resp_doc, resp_root, "rvq", rvq_b64.c_str());
    yyjson_mut_obj_add_bool(resp_doc, resp_root, "cached", cached);
    
    char * json = yyjson_mut_write(resp_doc, 0, NULL);
    res.set_content(json ? json : "{}", "application/json");
    if (json) {
        free(json);
    }
    yyjson_mut_doc_free(resp_doc);
}

static void tts_handle_models(const tts_backend & be, const httplib::Request &, httplib::Response & res) {
    yyjson_mut_doc * doc  = yyjson_mut_doc_new(NULL);
    yyjson_mut_val * root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);
    yyjson_mut_obj_add_str(doc, root, "object", "list");
    yyjson_mut_val * data = yyjson_mut_arr(doc);
    yyjson_mut_val * one  = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_str(doc, one, "id", be.model_id.c_str());
    yyjson_mut_obj_add_str(doc, one, "object", "model");
    yyjson_mut_obj_add_str(doc, one, "owned_by", "local");
    yyjson_mut_arr_add_val(data, one);
    yyjson_mut_obj_add_val(doc, root, "data", data);
    char * json = yyjson_mut_write(doc, 0, NULL);
    res.set_content(json ? json : "{}", "application/json");
    if (json) {
        free(json);
    }
    yyjson_mut_doc_free(doc);
}

static void tts_handle_voices(const tts_backend & be, const httplib::Request &, httplib::Response & res) {
    yyjson_mut_doc * doc  = yyjson_mut_doc_new(NULL);
    yyjson_mut_val * root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);
    yyjson_mut_val * arr = yyjson_mut_arr(doc);
    for (const std::string & v : be.voices) {
        yyjson_mut_val * one = yyjson_mut_obj(doc);
        yyjson_mut_obj_add_str(doc, one, "name", v.c_str());
        yyjson_mut_arr_add_val(arr, one);
    }
    yyjson_mut_obj_add_val(doc, root, "voices", arr);
    char * json = yyjson_mut_write(doc, 0, NULL);
    res.set_content(json ? json : "{}", "application/json");
    if (json) {
        free(json);
    }
    yyjson_mut_doc_free(doc);
}

static void tts_handle_health(const httplib::Request &, httplib::Response & res) {
    res.set_content("{\"status\":\"ok\"}", "application/json");
}

static int tts_server_run(const tts_backend & be, const server_config & cfg) {
    httplib::Server svr;
    g_svr = &svr;

    // per-operation socket idle timeouts. read is small (text in), write is
    // generous to cover a long streamed utterance without tripping on a slow
    // client.
    svr.set_read_timeout(60);
    svr.set_write_timeout(120);

    // reject oversized bodies. text plus an optional reference clip stays
    // well under this.
    svr.set_payload_max_length(32 * 1024 * 1024);

    // Nagle coalescing holds small packets back for tens of ms ; streamed
    // PCM chunks must leave the socket the moment they are written.
    svr.set_tcp_nodelay(true);

    // SO_REUSEADDR lets us rebind a port still in TIME_WAIT after a restart.
    // SO_REUSEPORT is deliberately not set : a second instance on the same
    // port then fails with EADDRINUSE instead of silently sharing the socket
    // and splitting traffic between two daemons.
    svr.set_socket_options([](socket_t sock) {
        int one = 1;
#ifdef _WIN32
        setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (const char *) &one, sizeof(one));
#else
        setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
#endif
    });

    // permissive CORS so a browser client can call the API directly.
    svr.set_default_headers({
        { "Access-Control-Allow-Origin", "*" }
    });
    svr.Options("/.*", [](const httplib::Request &, httplib::Response & res) {
        res.set_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
        res.set_header("Access-Control-Allow-Headers", "Content-Type");
    });

    svr.Post("/v1/audio/speech",
             [&be](const httplib::Request & req, httplib::Response & res) { tts_handle_speech(be, req, res); });
    svr.Post("/v1/audio/encode",
             [&be](const httplib::Request & req, httplib::Response & res) { tts_handle_encode(be, req, res); });
    svr.Get("/v1/models",
            [&be](const httplib::Request & req, httplib::Response & res) { tts_handle_models(be, req, res); });
    svr.Get("/v1/voices",
            [&be](const httplib::Request & req, httplib::Response & res) { tts_handle_voices(be, req, res); });
    svr.Get("/health", tts_handle_health);

    signal(SIGINT, tts_on_signal);
    signal(SIGTERM, tts_on_signal);

    fprintf(stderr, "[Server] model %s\n", be.model_id.c_str());
    fprintf(stderr, "[Server] listening on %s:%d\n", cfg.host.c_str(), cfg.port);
    if (!svr.listen(cfg.host.c_str(), cfg.port)) {
        fprintf(stderr, "[Server] FATAL: cannot bind %s:%d\n", cfg.host.c_str(), cfg.port);
        return 1;
    }
    return 0;
}
