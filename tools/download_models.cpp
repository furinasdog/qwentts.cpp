// download_models.cpp: CLI tool to download Qwen3-TTS GGUF models.
//
// Downloads model files from HuggingFace or ModelScope mirrors.
// Uses the system curl executable for HTTP transfers so no extra
// C++ HTTP library dependency is needed. On Windows 10+ curl.exe
// is built-in; on Linux / macOS it is universally available.
//
// Usage:
//   download-models                          # default: tokenizer + base-1.7b (Q8_0)
//   download-models --quant Q4_K_M           # download Q4_K_M quantised models
//   download-models --variant base-0.6b      # download the 0.6B base model
//   download-models --variant customvoice-1.7b --speaker  # with speaker encoder
//   download-models --mirror modelscope      # use ModelScope mirror (China)
//   download-models --file custom.gguf       # download a specific file
//   download-models --list                   # list available model variants
//   download-models --all                    # download all variants

#include "version.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#ifdef _WIN32
#    define NOMINMAX
#    include <direct.h>
#    include <windows.h>
#    define MKDIR(path) _mkdir(path)
#    define POPEN _popen
#    define PCLOSE _pclose
#else
#    include <sys/stat.h>
#    include <sys/types.h>
#    define MKDIR(path) mkdir(path, 0755)
#    define POPEN popen
#    define PCLOSE pclose
#endif

// ---------------------------------------------------------------------------
// Model variant registry
// ---------------------------------------------------------------------------

struct ModelVariant {
    const char * id;            // CLI identifier  (e.g. "base-1.7b")
    const char * name_prefix;   // filename prefix (e.g. "qwen-talker-1.7b-base")
    const char * desc;          // human-readable description
    bool         is_tokenizer;  // tokenizer / codec model (always downloaded)
};

static const ModelVariant VARIANTS[] = {
    // id                    prefix                             desc                            tokenizer?
    { "tokenizer",          "qwen-tokenizer-12hz",             "12Hz audio codec / tokenizer",  true  },
    { "base-0.6b",          "qwen-talker-0.6b-base",           "0.6B Base talker",              false },
    { "base-1.7b",          "qwen-talker-1.7b-base",           "1.7B Base talker",              false },
    { "customvoice-0.6b",   "qwen-talker-0.6b-customvoice",    "0.6B CustomVoice talker",       false },
    { "customvoice-1.7b",   "qwen-talker-1.7b-customvoice",    "1.7B CustomVoice talker",       false },
    { "voicedesign-1.7b",   "qwen-talker-1.7b-voicedesign",    "1.7B VoiceDesign talker",       false },
};

static const int NUM_VARIANTS = (int) (sizeof(VARIANTS) / sizeof(VARIANTS[0]));

// Default variant set (tokenizer + base-1.7b talker)
static const char * DEFAULT_IDS[] = { "tokenizer", "base-1.7b" };
static const int   NUM_DEFAULTS   = 2;

// ---------------------------------------------------------------------------
// Mirror configuration
// ---------------------------------------------------------------------------

struct Mirror {
    const char * id;        // CLI identifier: "hf" or "modelscope"
    const char * url_tmpl;  // URL template: {repo} -> repo_id, {file} -> filename
    const char * branch;    // default branch name
};

// URL templates use printf-style: base_url/repo/resolve/branch/file
static const Mirror MIRRORS[] = {
    { "hf",
      "https://huggingface.co/%s/resolve/main/%s",
      "main" },
    { "modelscope",
      "https://modelscope.cn/models/%s/resolve/master/%s",
      "master" },
};

static const int NUM_MIRRORS = (int) (sizeof(MIRRORS) / sizeof(MIRRORS[0]));

// Default repos per mirror
static const char * DEFAULT_REPOS[] = {
    "Serveurperso/Qwen3-TTS-GGUF",       // hf
    "leletxh/Qwen3-TTS-GGUF",            // modelscope
};

// ---------------------------------------------------------------------------
// Argument parsing
// ---------------------------------------------------------------------------

struct Args {
    const char * dir;             // output directory    (default: "models")
    const char * repo;            // HF / MS repo id     (default: per-mirror)
    const char * mirror;          // mirror id           (default: "hf")
    const char * file;            // specific filename   (overrides --variant)
    const char * variant;         // model variant id    (default: "base-1.7b")
    const char * quant;           // quantisation type   (default: "Q8_0")
    bool         list_only;       // list models and exit
    bool         force;           // re-download existing files
    bool         download_all;    // download every variant
    bool         no_tokenizer;    // skip tokenizer download
};

static void print_usage(const char * prog) {
    fprintf(stderr, "qwentts.cpp %s\n\n", QWEN_VERSION);
    fprintf(stderr,
            "Usage: %s [options]\n\n"
            "Download Qwen3-TTS GGUF models from HuggingFace or ModelScope.\n\n"
            "Options:\n"
            "  --dir <path>          Output directory (default: models)\n"
            "  --repo <repo_id>      Repository ID (default: Serveurperso/Qwen3-TTS-GGUF)\n"
            "  --mirror <provider>   Download mirror: hf (default) or modelscope\n"
            "  --variant <name>      Model variant (default: base-1.7b). Use --list to see all.\n"
            "  --quant <type>        Quantisation type: F32, BF16, Q8_0 (default), Q4_K_M\n"
            "  --file <filename>     Download a specific file (ignores --variant / --quant)\n"
            "  --all                 Download all variants\n"
            "  --list                List available model variants\n"
            "  --force               Re-download even if file already exists\n"
            "  --no-tokenizer        Skip tokenizer download (only when using --variant)\n"
            "  -h, --help            Show this help\n\n"
            "Examples:\n"
            "  %s                              # download tokenizer + base-1.7b (Q8_0)\n"
            "  %s --quant Q4_K_M               # download Q4_K_M quantised models\n"
            "  %s --variant base-0.6b           # download the 0.6B base model\n"
            "  %s --mirror modelscope           # use ModelScope mirror (faster in China)\n"
            "  %s --file qwen-talker-1.7b-base-F32.gguf  # download a specific file\n",
            prog, prog, prog, prog, prog, prog);
}

static bool parse_args(int argc, char ** argv, Args & a) {
    a             = {};
    a.dir         = "models";
    a.mirror      = "hf";
    a.variant     = "base-1.7b";
    a.quant       = "Q8_0";

    for (int i = 1; i < argc; i++) {
        const char * arg = argv[i];
        if (std::strcmp(arg, "-h") == 0 || std::strcmp(arg, "--help") == 0) {
            return false;
        }
        if (std::strcmp(arg, "--dir") == 0 && i + 1 < argc) {
            a.dir = argv[++i];
        } else if (std::strcmp(arg, "--repo") == 0 && i + 1 < argc) {
            a.repo = argv[++i];
        } else if (std::strcmp(arg, "--mirror") == 0 && i + 1 < argc) {
            a.mirror = argv[++i];
        } else if (std::strcmp(arg, "--variant") == 0 && i + 1 < argc) {
            a.variant = argv[++i];
        } else if (std::strcmp(arg, "--quant") == 0 && i + 1 < argc) {
            a.quant = argv[++i];
        } else if (std::strcmp(arg, "--file") == 0 && i + 1 < argc) {
            a.file = argv[++i];
        } else if (std::strcmp(arg, "--list") == 0) {
            a.list_only = true;
        } else if (std::strcmp(arg, "--force") == 0) {
            a.force = true;
        } else if (std::strcmp(arg, "--all") == 0) {
            a.download_all = true;
        } else if (std::strcmp(arg, "--no-tokenizer") == 0) {
            a.no_tokenizer = true;
        } else {
            fprintf(stderr, "[download] ERROR: unknown argument: %s\n", arg);
            return false;
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static const ModelVariant * find_variant(const char * id) {
    for (int i = 0; i < NUM_VARIANTS; i++) {
        if (std::strcmp(VARIANTS[i].id, id) == 0) {
            return &VARIANTS[i];
        }
    }
    return NULL;
}

static int find_mirror_index(const char * id) {
    for (int i = 0; i < NUM_MIRRORS; i++) {
        if (std::strcmp(MIRRORS[i].id, id) == 0) {
            return i;
        }
    }
    return -1;
}

static std::string make_filename(const ModelVariant & v, const char * quant) {
    char buf[256];
    snprintf(buf, sizeof(buf), "%s-%s.gguf", v.name_prefix, quant);
    return std::string(buf);
}

static std::string make_url(int mirror_idx, const char * repo, const char * filename) {
    char buf[512];
    snprintf(buf, sizeof(buf), MIRRORS[mirror_idx].url_tmpl, repo, filename);
    return std::string(buf);
}

static bool file_exists(const char * path) {
#ifdef _WIN32
    DWORD attrs = GetFileAttributesA(path);
    return (attrs != INVALID_FILE_ATTRIBUTES &&
            !(attrs & FILE_ATTRIBUTE_DIRECTORY));
#else
    struct stat st;
    return (stat(path, &st) == 0 && S_ISREG(st.st_mode));
#endif
}

static bool dir_exists(const char * path) {
#ifdef _WIN32
    DWORD attrs = GetFileAttributesA(path);
    return (attrs != INVALID_FILE_ATTRIBUTES &&
            (attrs & FILE_ATTRIBUTE_DIRECTORY));
#else
    struct stat st;
    return (stat(path, &st) == 0 && S_ISDIR(st.st_mode));
#endif
}

static bool ensure_dir(const char * path) {
    if (dir_exists(path)) {
        return true;
    }
    if (MKDIR(path) == 0) {
        return true;
    }
    // Try creating parent directories recursively
    std::string spath(path);
    std::vector<std::string> parts;
    // Split path and create each level
    std::string current;
    for (size_t i = 0; i < spath.size(); i++) {
        char c = spath[i];
        if (c == '/' || c == '\\') {
            if (!current.empty() && !dir_exists(current.c_str())) {
                if (MKDIR(current.c_str()) != 0) {
                    return false;
                }
            }
        }
        current += c;
    }
    // Create the final directory
    if (!current.empty() && !dir_exists(current.c_str())) {
        return MKDIR(current.c_str()) == 0;
    }
    return true;
}

// Check if curl is available on the system
static bool curl_available() {
    FILE * f = POPEN("curl --version 2>/dev/null", "r");
    if (!f) {
        return false;
    }
    PCLOSE(f);
    return true;
}

// Download a file using curl. Returns true on success.
static bool download_file(const std::string & url, const std::string & out_path, bool force) {
    if (!force && file_exists(out_path.c_str())) {
        fprintf(stderr, "[skip] %s (already exists)\n", out_path.c_str());
        return true;
    }

    // Build curl command:
    //   -L          follow redirects
    //   -#          progress bar
    //   -C -        resume interrupted downloads
    //   -f          fail on HTTP errors (4xx/5xx)
    //   -o <path>   output file
    std::string cmd = "curl -L -# -C - -f -o \"";
    cmd += out_path;
    cmd += "\" \"";
    cmd += url;
    cmd += "\"";

    fprintf(stderr, "[download] %s -> %s\n", url.c_str(), out_path.c_str());

    int rc = std::system(cmd.c_str());
    if (rc != 0) {
        fprintf(stderr, "[download] ERROR: curl failed (exit code %d) for %s\n", rc, url.c_str());
        // Remove partially downloaded file on failure
        std::remove(out_path.c_str());
        return false;
    }

    // Verify the file was actually created
    if (!file_exists(out_path.c_str())) {
        fprintf(stderr, "[download] ERROR: output file not created: %s\n", out_path.c_str());
        return false;
    }

    return true;
}

// Format file size in human-readable form
static std::string format_size(int64_t bytes) {
    char buf[64];
    if (bytes >= 1073741824LL) {
        snprintf(buf, sizeof(buf), "%.1f GB", (double) bytes / 1073741824.0);
    } else if (bytes >= 1048576LL) {
        snprintf(buf, sizeof(buf), "%.1f MB", (double) bytes / 1048576.0);
    } else if (bytes >= 1024LL) {
        snprintf(buf, sizeof(buf), "%.1f KB", (double) bytes / 1024.0);
    } else {
        snprintf(buf, sizeof(buf), "%lld B", (long long) bytes);
    }
    return std::string(buf);
}

static int64_t get_file_size(const char * path) {
#ifdef _WIN32
    WIN32_FILE_ATTRIBUTE_DATA attrs;
    if (!GetFileAttributesExA(path, GetFileExInfoStandard, &attrs)) {
        return -1;
    }
    return ((int64_t) attrs.nFileSizeHigh << 32) | (int64_t) attrs.nFileSizeLow;
#else
    struct stat st;
    if (stat(path, &st) != 0) {
        return -1;
    }
    return (int64_t) st.st_size;
#endif
}

// ---------------------------------------------------------------------------
// List models
// ---------------------------------------------------------------------------

static int list_models() {
    fprintf(stderr, "\nAvailable model variants:\n\n");
    fprintf(stderr, "  %-20s  %-45s  %s\n", "ID", "Filename (Q8_0)", "Description");
    fprintf(stderr, "  %s\n", std::string(90, '-').c_str());

    for (int i = 0; i < NUM_VARIANTS; i++) {
        std::string fname = make_filename(VARIANTS[i], "Q8_0");
        fprintf(stderr, "  %-20s  %-45s  %s\n", VARIANTS[i].id, fname.c_str(), VARIANTS[i].desc);
    }

    fprintf(stderr, "\nQuantisation types: F32, BF16, Q8_0, Q4_K_M\n");
    fprintf(stderr, "\nDefault download: tokenizer + base-1.7b (Q8_0)\n");
    fprintf(stderr, "Mirrors: hf (HuggingFace), modelscope (ModelScope)\n\n");
    return 0;
}

// ---------------------------------------------------------------------------
// Main download logic
// ---------------------------------------------------------------------------

static int run(const Args & a) {
    // List mode
    if (a.list_only) {
        return list_models();
    }

    // Check curl availability
    if (!curl_available()) {
        fprintf(stderr, "[download] ERROR: curl is not available on this system.\n");
        fprintf(stderr, "  Please install curl: https://curl.se/download.html\n");
        return 1;
    }

    // Resolve mirror
    int mirror_idx = find_mirror_index(a.mirror);
    if (mirror_idx < 0) {
        fprintf(stderr, "[download] ERROR: unknown mirror '%s' (valid: hf, modelscope)\n", a.mirror);
        return 1;
    }

    // Resolve repo
    const char * repo = a.repo;
    if (!repo) {
        repo = DEFAULT_REPOS[mirror_idx];
    }

    // Ensure output directory
    if (!ensure_dir(a.dir)) {
        fprintf(stderr, "[download] ERROR: cannot create directory '%s'\n", a.dir);
        return 1;
    }

    // Whether this is a default-model download (not --file and not custom --repo).
    // Only default-model downloads auto-retry on the ModelScope mirror when
    // HuggingFace fails.  Custom --file / --repo downloads just report the
    // error and let the user switch manually.
    const bool is_default_download = (!a.file && !a.repo);

    int n_ok   = 0;
    int n_fail = 0;

    // Custom file mode — no mirror auto-fallback
    if (a.file) {
        std::string url = make_url(mirror_idx, repo, a.file);
        std::string out = std::string(a.dir) + "/" + a.file;
        if (download_file(url, out, a.force)) {
            n_ok++;
            int64_t sz = get_file_size(out.c_str());
            if (sz > 0) {
                fprintf(stderr, "[done] %s (%s)\n", a.file, format_size(sz).c_str());
            }
        } else {
            n_fail++;
        }
        if (n_fail > 0) {
            fprintf(stderr, "\n[download] %d file(s) failed. Use --mirror or --repo to switch source.\n", n_fail);
        }
        return n_fail > 0 ? 1 : 0;
    }

    // Variant mode: determine which files to download
    std::vector<const ModelVariant *> to_download;

    if (a.download_all) {
        // Download all variants
        for (int i = 0; i < NUM_VARIANTS; i++) {
            to_download.push_back(&VARIANTS[i]);
        }
    } else {
        // Default: tokenizer + selected variant
        const ModelVariant * tok = find_variant("tokenizer");
        if (!a.no_tokenizer && tok) {
            to_download.push_back(tok);
        }

        const ModelVariant * var = find_variant(a.variant);
        if (!var) {
            fprintf(stderr, "[download] ERROR: unknown variant '%s'\n", a.variant);
            fprintf(stderr, "  Use --list to see available variants.\n");
            return 1;
        }
        if (!var->is_tokenizer) {
            to_download.push_back(var);
        } else if (a.no_tokenizer) {
            // User explicitly selected tokenizer with --no-tokenizer, that's
            // contradictory; just skip
            fprintf(stderr, "[download] WARNING: --no-tokenizer with tokenizer variant has no effect\n");
        }
    }

    fprintf(stderr, "\n[download] Mirror:   %s\n", MIRRORS[mirror_idx].id);
    fprintf(stderr, "[download] Repo:     %s\n", repo);
    fprintf(stderr, "[download] Quant:    %s\n", a.quant);
    fprintf(stderr, "[download] Dir:      %s\n", a.dir);
    fprintf(stderr, "[download] Files:    %d\n\n", (int) to_download.size());

    for (size_t i = 0; i < to_download.size(); i++) {
        const ModelVariant & v     = *to_download[i];
        std::string          fname = make_filename(v, a.quant);
        std::string          url   = make_url(mirror_idx, repo, fname.c_str());
        std::string          out   = std::string(a.dir) + "/" + fname;

        fprintf(stderr, "[%zu/%zu] %s (%s)\n", i + 1, to_download.size(), fname.c_str(), v.desc);

        if (download_file(url, out, a.force)) {
            n_ok++;
            int64_t sz = get_file_size(out.c_str());
            if (sz > 0) {
                fprintf(stderr, "[done] %s (%s)\n\n", fname.c_str(), format_size(sz).c_str());
            }
        } else if (is_default_download && mirror_idx != find_mirror_index("modelscope")) {
            // Auto-retry on ModelScope mirror for default model downloads
            int ms_idx = find_mirror_index("modelscope");
            if (ms_idx >= 0) {
                const char * ms_repo = DEFAULT_REPOS[ms_idx];
                std::string ms_url = make_url(ms_idx, ms_repo, fname.c_str());
                fprintf(stderr, "[retry] HuggingFace failed, switching to ModelScope mirror...\n");
                if (download_file(ms_url, out, a.force)) {
                    n_ok++;
                    int64_t sz = get_file_size(out.c_str());
                    if (sz > 0) {
                        fprintf(stderr, "[done] %s (%s) [via ModelScope]\n\n", fname.c_str(), format_size(sz).c_str());
                    }
                    continue;
                }
            }
            n_fail++;
            // Try F32 fallback for tokenizer if the selected quant is not available
            if (v.is_tokenizer && std::strcmp(a.quant, "F32") != 0) {
                fprintf(stderr, "[retry] Trying F32 for tokenizer...\n");
                std::string fname_f32 = make_filename(v, "F32");
                bool f32_ok = false;
                // Try HF first
                std::string url_f32 = make_url(mirror_idx, repo, fname_f32.c_str());
                std::string out_f32 = std::string(a.dir) + "/" + fname_f32;
                if (download_file(url_f32, out_f32, a.force)) {
                    f32_ok = true;
                } else if (is_default_download) {
                    // Try ModelScope
                    int ms2 = find_mirror_index("modelscope");
                    if (ms2 >= 0) {
                        std::string ms_url_f32 = make_url(ms2, DEFAULT_REPOS[ms2], fname_f32.c_str());
                        fprintf(stderr, "[retry] F32 on ModelScope...\n");
                        if (download_file(ms_url_f32, out_f32, a.force)) {
                            f32_ok = true;
                        }
                    }
                }
                if (f32_ok) {
                    n_ok++;
                    n_fail--;
                    int64_t sz = get_file_size(out_f32.c_str());
                    if (sz > 0) {
                        fprintf(stderr, "[done] %s (%s) [fallback F32]\n\n", fname_f32.c_str(),
                                format_size(sz).c_str());
                    }
                    continue;
                }
            }
            fprintf(stderr, "[fail] %s\n\n", fname.c_str());
        } else {
            n_fail++;
            // Try F32 fallback for tokenizer if the selected quant is not available
            if (v.is_tokenizer && std::strcmp(a.quant, "F32") != 0) {
                fprintf(stderr, "[retry] Trying F32 for tokenizer...\n");
                std::string fname_f32 = make_filename(v, "F32");
                std::string url_f32   = make_url(mirror_idx, repo, fname_f32.c_str());
                std::string out_f32   = std::string(a.dir) + "/" + fname_f32;
                if (download_file(url_f32, out_f32, a.force)) {
                    n_ok++;
                    n_fail--;
                    int64_t sz = get_file_size(out_f32.c_str());
                    if (sz > 0) {
                        fprintf(stderr, "[done] %s (%s) [fallback F32]\n\n", fname_f32.c_str(),
                                format_size(sz).c_str());
                    }
                    continue;
                }
            }
            fprintf(stderr, "[fail] %s\n\n", fname.c_str());
        }
    }

    // Summary
    fprintf(stderr, "========================================\n");
    fprintf(stderr, "[download] Complete: %d OK, %d failed\n", n_ok, n_fail);

    if (n_ok > 0) {
        fprintf(stderr, "\nDownloaded files in %s/:\n", a.dir);
        for (size_t i = 0; i < to_download.size(); i++) {
            std::string fname = make_filename(*to_download[i], a.quant);
            std::string out   = std::string(a.dir) + "/" + fname;
            if (file_exists(out.c_str())) {
                int64_t sz = get_file_size(out.c_str());
                fprintf(stderr, "  %-50s  %s\n", fname.c_str(), sz > 0 ? format_size(sz).c_str() : "?");
            }
        }

        // Print example usage
        const ModelVariant * talker = NULL;
        for (size_t i = 0; i < to_download.size(); i++) {
            if (!to_download[i]->is_tokenizer) {
                talker = to_download[i];
                break;
            }
        }
        if (talker) {
            std::string tok_name  = make_filename(*find_variant("tokenizer"), a.quant);
            std::string talk_name = make_filename(*talker, a.quant);
            // Check if F32 fallback was used for tokenizer
            {
                std::string tok_f32 = make_filename(*find_variant("tokenizer"), "F32");
                std::string out_f32 = std::string(a.dir) + "/" + tok_f32;
                if (file_exists(out_f32.c_str()) && !file_exists((std::string(a.dir) + "/" + tok_name).c_str())) {
                    tok_name = tok_f32;
                }
            }
            fprintf(stderr, "\nExample usage:\n");
            fprintf(stderr, "  ./qwen-tts --model %s/%s --codec %s/%s -o out.wav < text.txt\n",
                    a.dir, talk_name.c_str(), a.dir, tok_name.c_str());
        }
    }

    if (n_fail > 0) {
        if (is_default_download) {
            fprintf(stderr, "\nAll mirrors tried. Try a different --quant (F32, BF16, Q8_0, Q4_K_M) or check network.\n");
        } else {
            fprintf(stderr, "\nTip: try --mirror modelscope if HuggingFace is slow or blocked.\n");
            fprintf(stderr, "     try a different --quant (F32, BF16, Q8_0, Q4_K_M) if a file is not found.\n");
        }
    }

    return n_fail > 0 ? 1 : 0;
}

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------

int main(int argc, char ** argv) {
    Args a;
    if (!parse_args(argc, argv, a)) {
        print_usage(argv[0]);
        return 1;
    }
    return run(a);
}