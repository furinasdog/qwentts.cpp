# HTTP Server API Reference

OpenAI-compatible TTS HTTP server provided by `tts-server`. The server
loads a Talker language model and a Codec model once (GPU resident), and
serves text-to-speech synthesis over a REST API.

## Quick start

### Start the server

```bash
./build/tts-server \
    --model models/qwen-talker-1.7b-base-Q8_0.gguf \
    --codec models/qwen-tokenizer-12hz-Q8_0.gguf \
    --host 127.0.0.1 --port 8080 --lang auto
```

### Command-line arguments

| Argument | Required | Default | Description |
|---|---|---|---|
| `--model <gguf>` | Yes | — | Talker LM GGUF file (`qwen-talker-*.gguf`) |
| `--codec <gguf>` | Yes | — | Codec GGUF file (`qwen-tokenizer-*.gguf`) |
| `--host <ip>` | No | `127.0.0.1` | Listen address |
| `--port <n>` | No | `8080` | Listen port |
| `--lang <name>` | No | `auto` | Language label |
| `--no-fa` | No | — | Disable flash attention |
| `--clamp-fp16` | No | — | Clamp hidden states to FP16 range |
| `--help`, `-h` | No | — | Print usage and exit |

### Server defaults

| Setting | Value |
|---|---|
| Read timeout | 60 s |
| Write timeout | 120 s |
| Max request body | 32 MB |
| TCP_NODELAY | Enabled |
| SO_REUSEADDR | Enabled |
| CORS | `*` (all origins) |

---

## Endpoints

### POST /v1/audio/speech

Synthesize speech from text. Compatible with the OpenAI audio speech
API.

#### Request

```
POST /v1/audio/speech
Content-Type: application/json
```

**Body (JSON)**

| Field | Type | Required | Default | Description |
|---|---|---|---|---|
| `input` | string | Yes | — | Text to speak. Must be non-empty. |
| `voice` | string | No | `""` | Speaker name. Maps to a loaded speaker when the model contains embedded speakers. Ignored when the model has none. |
| `instructions` | string | No | `""` | Voice style instructions (e.g. tone, emotion). Maps to the instruct field in the ABI. |
| `response_format` | string | No | `"pcm"` | Output format. `"pcm"` for streaming s16le audio, `"wav"` for a one-shot RIFF WAV file. |
| `speed` | number | No | `1.0` | Speed multiplier. Parsed for compatibility but currently ignored (no time-stretch in the ABI). |

#### Response

**When `response_format` = `"pcm"` (default)**

Streams audio in real time via HTTP chunked transfer encoding.

| Property | Value |
|---|---|
| Content-Type | `audio/pcm` |
| Transfer-Encoding | `chunked` |
| Cache-Control | `no-cache` |
| X-Accel-Buffering | `no` |
| Format | s16le (signed 16-bit little-endian) |
| Sample rate | 24000 Hz |
| Channels | mono |

The audio chunks are pushed as soon as the codec produces them, enabling
real-time playback before the full utterance is finished.

**When `response_format` = `"wav"`**

Returns the complete audio as a single RIFF WAV file after synthesis
finishes.

| Property | Value |
|---|---|
| Content-Type | `audio/wav` |
| Format | RIFF WAV, s16 PCM |
| Sample rate | 24000 Hz |
| Channels | mono |

#### Errors

Returns a JSON error envelope with the appropriate HTTP status code.

```json
{
  "error": {
    "message": "description of the error",
    "type": "error_type"
  }
}
```

| Status | Condition |
|---|---|
| 400 | `input` missing or empty |
| 400 | `response_format` is not `"pcm"` or `"wav"` |
| 400 | Invalid request JSON |
| 400 | Invalid parameters or mode (ABI status -1 or -2) |
| 502 | Synthesis failed (server-side / GPU error) |

#### Examples

**Stream PCM and play in real time:**

```bash
curl -s -X POST "http://127.0.0.1:8080/v1/audio/speech" \
    -H "Content-Type: application/json" \
    -d '{"input":"The quick brown fox jumps over the lazy dog."}' \
    | ffplay -f s16le -ar 24000 -ch_layout mono -nodisp -autoexit -i -
```

**Save as WAV file:**

```bash
curl -s -X POST "http://127.0.0.1:8080/v1/audio/speech" \
    -H "Content-Type: application/json" \
    -d '{"input":"This one is written to a file.","response_format":"wav"}' \
    --output out.wav
```

**With voice and instructions:**

```bash
curl -s -X POST "http://127.0.0.1:8080/v1/audio/speech" \
    -H "Content-Type: application/json" \
    -d '{
      "input": "Hello, how are you today?",
      "voice": "freeman",
      "instructions": "Speak in a warm and friendly tone.",
      "response_format": "wav"
    }' \
    --output out.wav
```

**Python (requests):**

```python
import requests

resp = requests.post(
    "http://127.0.0.1:8080/v1/audio/speech",
    json={"input": "Hello world.", "response_format": "wav"},
)
with open("out.wav", "wb") as f:
    f.write(resp.content)
```

**Python (OpenAI SDK):**

```python
from openai import OpenAI

client = OpenAI(base_url="http://127.0.0.1:8080/v1", api_key="unused")
response = client.audio.speech.create(
    model="qwen-tts",
    voice="freeman",
    input="Hello from the OpenAI SDK!",
    response_format="wav",
)
response.stream_to_file("out.wav")
```

---

### GET /v1/models

List the loaded model.

#### Request

```
GET /v1/models
```

No parameters.

#### Response

```json
{
  "object": "list",
  "data": [
    {
      "id": "qwen-talker-1.7b-base-Q8_0.gguf",
      "object": "model",
      "owned_by": "local"
    }
  ]
}
```

| Field | Type | Description |
|---|---|---|
| `object` | string | Always `"list"` |
| `data` | array | Array of model objects |
| `data[].id` | string | Model filename (basename of the `--model` path) |
| `data[].object` | string | Always `"model"` |
| `data[].owned_by` | string | Always `"local"` |

---

### GET /v1/voices

List available speaker voices. Returns an empty array when the model has
no embedded speakers.

#### Request

```
GET /v1/voices
```

No parameters.

#### Response

```json
{
  "voices": [
    {"name": "freeman"},
    {"name": "alice"}
  ]
}
```

| Field | Type | Description |
|---|---|---|
| `voices` | array | Array of voice objects |
| `voices[].name` | string | Speaker name, usable as the `voice` field in `/v1/audio/speech` |

---

### GET /health

Liveness probe. Returns immediately to confirm the server is running.

#### Request

```
GET /health
```

No parameters.

#### Response

```json
{"status": "ok"}
```

---

## Concurrency

All synthesis requests are serialized through a global mutex (FIFO
order). The server handles one GPU synthesis at a time. HTTP connections
are accepted concurrently, but only one synthesis runs on the GPU at any
given moment.

If a streaming client disconnects mid-synthesis, the generation is
aborted immediately to free the GPU for the next request.

## CORS

The server allows all origins (`Access-Control-Allow-Origin: *`). The
`OPTIONS` preflight handler permits `GET`, `POST`, and `OPTIONS` methods
with the `Content-Type` header.
