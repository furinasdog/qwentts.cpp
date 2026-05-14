#pragma once
// prompt-builder.h: assemble the talker prefix input embedding from
// a tokenized text plus a language tag, mirroring the upstream
// generate() function of Qwen3-TTS.
//
// Output shape: [T_ctx, hidden_size] f32 row-major. Two pad-aligned
// streams (text and codec) are summed at the granularity of single
// vectors. The trailing text hidden buffer is also produced for the
// streaming-text overlay used during generation.
//
// Modes :
//   base          text only, no instruct, no speaker
//   voice_design  text + instruct (style description), no speaker
//   custom_voice  text + speaker, optional instruct
//
// Empty / NULL strings disable the corresponding stream. The builder runs
// CPU-side using the BF16 weight blocks mmapped from the talker GGUF, no
// backend allocation, no graph compute.

#include "bpe.h"
#include "pipeline-tts.h"

#include <cstdint>
#include <string>
#include <vector>

struct PromptBuilderOutput {
    // Final talker input embedding [T_ctx, hidden] f32 row-major
    std::vector<float> input_embed;
    int                T_ctx;
    int                hidden;

    // Trailing text overlay: added on top of the next-token-input during
    // the autoregressive loop, one vector per generated frame until
    // exhausted, then tts_pad_embed for every following frame.
    std::vector<float> trailing_text_hidden;
    int                T_trailing;

    // tts_pad_embed [hidden] f32, kept around so the generation loop
    // can fall back on it once the trailing text runs out.
    std::vector<float> tts_pad_embed;

    // Token ids fed through the tokenizer (kept for debug parity with
    // the Python prompt-ids.bin dump).
    std::vector<int32_t> prompt_ids;

    // Length of the text segment used as utterance text, ie input_id[3:-5].
    int N_text;
};

// Assemble the prefix. instruct_text is the raw user style instruction
// (empty for none). speaker_name is the lowercased speaker key looked up
// in pt->speakers (empty for none). ref_spk_emb is an optional pointer to
// an [hidden] f32 vector that takes the place of the speaker preset row
// for voice clone mode A: when non NULL it is inserted between think_eos
// and codec_pad in the codec stream, mutually exclusive with speaker_name.
// ref_text and ref_codes activate voice clone mode B (ICL): the prompt
// becomes [icl_text + tts_eos] aligned with [codec_bos + ref_codes_summed],
// where ref_codes is a flat [num_code_groups, T_codec] int32 buffer
// produced by pipeline_codec_encode on the resampled reference audio.
// Returns false if BPE encoding produces fewer than the expected
// role/footer tokens, the language is unknown, speaker_name is set but
// not found, or speaker_name and ref_spk_emb are both set.
bool prompt_builder_build(const PipelineTTS *   pt,
                          const BPETokenizer *  tok,
                          const std::string &   utterance_text,
                          const std::string &   language,
                          const std::string &   instruct_text,
                          const std::string &   speaker_name,
                          const float *         ref_spk_emb,
                          const std::string &   ref_text,
                          const int32_t *       ref_codes,
                          int                   ref_codes_T,
                          PromptBuilderOutput * out);
