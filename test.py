#!/usr/bin/env python3
"""
Test script for zero-shot voice cloning API.
Loads reference audio and text from asset directory.
"""

import requests
import json
import base64
import sys
import os
from pathlib import Path

# Configuration
API_BASE_URL = "http://127.0.0.1:8080"
ASSET_DIR = Path(__file__).parent / "asset"
EXAMPLES_DIR = Path(__file__).parent / "examples"
OUTPUT_DIR = Path(__file__).parent / "test_output"

# Create output directory
OUTPUT_DIR.mkdir(exist_ok=True)

# Test assets
REF_WAV = ASSET_DIR / "ref.wav"
REF_TXT = ASSET_DIR / "ref.txt"
FREEMAN_WAV = EXAMPLES_DIR / "freeman.wav"
FREEMAN_TXT = EXAMPLES_DIR / "freeman.txt"


def load_ref_text(txt_path):
    """Load reference text from file."""
    with open(txt_path, 'r', encoding='utf-8') as f:
        return f.read().strip()


def load_wav_b64(wav_path):
    """Load WAV file and encode as base64."""
    with open(wav_path, 'rb') as f:
        return base64.b64encode(f.read()).decode('utf-8')


def test_zero_shot_b64():
    """Test zero-shot synthesis with Base64 reference audio."""
    print("\n" + "="*60)
    print("Test 1: Zero-shot synthesis with Base64 reference audio")
    print("="*60)
    
    if not REF_WAV.exists() or not REF_TXT.exists():
        print(f"❌ Missing assets: {REF_WAV} or {REF_TXT}")
        return False
    
    try:
        ref_text = load_ref_text(REF_TXT)
        ref_wav_b64 = load_wav_b64(REF_WAV)
        
        print(f"✓ Loaded reference audio: {REF_WAV}")
        print(f"✓ Loaded reference text: {ref_text[:50]}...")
        print(f"✓ Base64 encoded (length: {len(ref_wav_b64)})")
        
        # 验证 Base64 的前 50 字符和后 50 字符
        print(f"  Base64 前 50 字符: {ref_wav_b64[:50]}")
        print(f"  Base64 后 50 字符: {ref_wav_b64[-50:]}")
        
        # 验证 Base64 可以解码
        import base64
        try:
            decoded = base64.b64decode(ref_wav_b64)
            print(f"  ✓ Base64 解码成功，大小: {len(decoded)} 字节")
            # 检查 WAV 头
            if decoded[:4] == b'RIFF' and decoded[8:12] == b'WAVE':
                print(f"  ✓ 解码后为有效的 WAV 文件")
            else:
                print(f"  ❌ 解码后不是有效的 WAV 文件")
                print(f"     头部: {decoded[:12]}")
        except Exception as e:
            print(f"  ❌ Base64 解码失败: {e}")
        
        payload = {
            "input": "我很高兴见到你。这是一个测试。",
            "ref_wav_b64": ref_wav_b64,
            "ref_text": ref_text,
            "response_format": "wav"
        }
        
        print(f"\n📤 Sending request to {API_BASE_URL}/v1/audio/speech...")
        print(f"   Input: {payload['input']}")
        print(f"   Response format: wav")
        
        response = requests.post(
            f"{API_BASE_URL}/v1/audio/speech",
            json=payload,
            timeout=60
        )
        
        if response.status_code != 200:
            print(f"❌ Error: HTTP {response.status_code}")
            print(f"   Response: {response.text}")
            return False
        
        output_file = OUTPUT_DIR / "test_zero_shot_b64.wav"
        with open(output_file, 'wb') as f:
            f.write(response.content)
        
        print(f"✓ Success! Generated audio saved to: {output_file}")
        print(f"   File size: {output_file.stat().st_size} bytes")
        return True
        
    except Exception as e:
        print(f"❌ Error: {e}")
        import traceback
        traceback.print_exc()
        return False


def test_zero_shot_path():
    """Test zero-shot synthesis with file path reference audio (freeman.wav)."""
    print("\n" + "="*60)
    print("Test 2: Zero-shot synthesis with file path (freeman.wav)")
    print("="*60)
    
    if not FREEMAN_WAV.exists() or not FREEMAN_TXT.exists():
        print(f"❌ Missing assets: {FREEMAN_WAV} or {FREEMAN_TXT}")
        return False
    
    try:
        ref_text = load_ref_text(FREEMAN_TXT)
        abs_path = str(FREEMAN_WAV.absolute())
        
        print(f"✓ Reference audio path: {abs_path}")
        print(f"✓ Loaded reference text: {ref_text[:50]}...")
        
        payload = {
            "input": "This is a test of zero-shot voice cloning.",
            "ref_wav_path": abs_path,
            "ref_text": ref_text,
            "response_format": "wav"
        }
        
        print(f"\n📤 Sending request to {API_BASE_URL}/v1/audio/speech...")
        print(f"   Input: {payload['input']}")
        print(f"   Response format: wav")
        
        response = requests.post(
            f"{API_BASE_URL}/v1/audio/speech",
            json=payload,
            timeout=60
        )
        
        if response.status_code != 200:
            print(f"❌ Error: HTTP {response.status_code}")
            print(f"   Response: {response.text}")
            return False
        
        output_file = OUTPUT_DIR / "test_zero_shot_path_freeman.wav"
        with open(output_file, 'wb') as f:
            f.write(response.content)
        
        print(f"✓ Success! Generated audio saved to: {output_file}")
        print(f"   File size: {output_file.stat().st_size} bytes")
        return True
        
    except Exception as e:
        print(f"❌ Error: {e}")
        import traceback
        traceback.print_exc()
        return False


def test_zero_shot_path_ref():
    """Test zero-shot synthesis with file path reference audio (asset/ref.wav)."""
    print("\n" + "="*60)
    print("Test 2b: Zero-shot synthesis with file path (asset/ref.wav)")
    print("="*60)
    
    if not REF_WAV.exists() or not REF_TXT.exists():
        print(f"❌ Missing assets: {REF_WAV} or {REF_TXT}")
        return False
    
    try:
        ref_text = load_ref_text(REF_TXT)
        abs_path = str(REF_WAV.absolute())
        
        print(f"✓ Reference audio path: {abs_path}")
        print(f"✓ Loaded reference text: {ref_text[:50]}...")
        
        payload = {
            "input": "我很高兴见到你。这是一个测试。",
            "ref_wav_path": abs_path,
            "ref_text": ref_text,
            "response_format": "wav"
        }
        
        print(f"\n📤 Sending request to {API_BASE_URL}/v1/audio/speech...")
        print(f"   Input: {payload['input']}")
        print(f"   Response format: wav")
        
        response = requests.post(
            f"{API_BASE_URL}/v1/audio/speech",
            json=payload,
            timeout=60
        )
        
        if response.status_code != 200:
            print(f"❌ Error: HTTP {response.status_code}")
            print(f"   Response: {response.text}")
            return False
        
        output_file = OUTPUT_DIR / "test_zero_shot_path_ref.wav"
        with open(output_file, 'wb') as f:
            f.write(response.content)
        
        print(f"✓ Success! Generated audio saved to: {output_file}")
        print(f"   File size: {output_file.stat().st_size} bytes")
        return True
        
    except Exception as e:
        print(f"❌ Error: {e}")
        import traceback
        traceback.print_exc()
        return False


def test_encode_endpoint():
    """Test /v1/audio/encode endpoint."""
    print("\n" + "="*60)
    print("Test 3: /v1/audio/encode pre-encoding endpoint")
    print("="*60)
    
    if not REF_WAV.exists() or not REF_TXT.exists():
        print(f"❌ Missing assets: {REF_WAV} or {REF_TXT}")
        return False
    
    try:
        ref_text = load_ref_text(REF_TXT)
        ref_wav_b64 = load_wav_b64(REF_WAV)
        
        print(f"✓ Loaded reference audio: {REF_WAV}")
        print(f"✓ Loaded reference text: {ref_text[:50]}...")
        print(f"✓ Base64 encoded (length: {len(ref_wav_b64)})")
        
        payload = {
            "ref_wav_b64": ref_wav_b64,
            "ref_text": ref_text
        }
        
        print(f"\n📤 Sending request to {API_BASE_URL}/v1/audio/encode...")
        
        response = requests.post(
            f"{API_BASE_URL}/v1/audio/encode",
            json=payload,
            timeout=60
        )
        
        if response.status_code != 200:
            print(f"❌ Error: HTTP {response.status_code}")
            print(f"   Response: {response.text}")
            return False
        
        result = response.json()
        print(f"✓ Success! Pre-encoding result:")
        print(f"   - Hash: {result.get('hash', 'N/A')}")
        print(f"   - Cached: {result.get('cached', False)}")
        print(f"   - Speaker embedding (.spk): {len(result.get('spk', ''))} chars (base64)")
        print(f"   - RVQ codebook (.rvq): {len(result.get('rvq', ''))} chars (base64)")
        
        # Save the result
        output_file = OUTPUT_DIR / "test_encode_result.json"
        with open(output_file, 'w') as f:
            json.dump(result, f, indent=2)
        
        print(f"✓ Result saved to: {output_file}")
        return True
        
    except Exception as e:
        print(f"❌ Error: {e}")
        import traceback
        traceback.print_exc()
        return False


def test_cache_hit():
    """Test that repeated requests use cache (faster)."""
    print("\n" + "="*60)
    print("Test 4: Cache hit (repeated request should be faster)")
    print("="*60)
    
    if not REF_WAV.exists() or not REF_TXT.exists():
        print(f"❌ Missing assets: {REF_WAV} or {REF_TXT}")
        return False
    
    try:
        ref_text = load_ref_text(REF_TXT)
        ref_wav_b64 = load_wav_b64(REF_WAV)
        
        payload = {
            "input": "缓存测试",
            "ref_wav_b64": ref_wav_b64,
            "ref_text": ref_text,
            "response_format": "wav"
        }
        
        import time
        
        # First request (cache miss)
        print("\n📤 First request (cache miss)...")
        start_time = time.time()
        response1 = requests.post(
            f"{API_BASE_URL}/v1/audio/speech",
            json=payload,
            timeout=60
        )
        time1 = time.time() - start_time
        
        if response1.status_code != 200:
            print(f"❌ First request failed: HTTP {response1.status_code}")
            return False
        
        print(f"✓ First request completed in {time1:.2f}s")
        
        # Second request (cache hit)
        print("\n📤 Second request (cache hit)...")
        start_time = time.time()
        response2 = requests.post(
            f"{API_BASE_URL}/v1/audio/speech",
            json=payload,
            timeout=60
        )
        time2 = time.time() - start_time
        
        if response2.status_code != 200:
            print(f"❌ Second request failed: HTTP {response2.status_code}")
            return False
        
        print(f"✓ Second request completed in {time2:.2f}s")
        
        if time2 < time1:
            print(f"✅ Cache working! Second request was {time1/time2:.1f}x faster")
        else:
            print(f"⚠️  Second request was not faster (cache may not be enabled)")
        
        return True
        
    except Exception as e:
        print(f"❌ Error: {e}")
        import traceback
        traceback.print_exc()
        return False


def test_regular_synthesis():
    """Test regular TTS without zero-shot (for comparison)."""
    print("\n" + "="*60)
    print("Test 5: Regular TTS synthesis (non-zero-shot)")
    print("="*60)
    
    try:
        payload = {
            "input": "这是一个常规合成测试。",
            "voice": "default",
            "response_format": "wav"
        }
        
        print(f"📤 Sending regular TTS request...")
        print(f"   Input: {payload['input']}")
        
        response = requests.post(
            f"{API_BASE_URL}/v1/audio/speech",
            json=payload,
            timeout=60
        )
        
        if response.status_code != 200:
            print(f"⚠️  Regular synthesis not available: HTTP {response.status_code}")
            return False
        
        output_file = OUTPUT_DIR / "test_regular_synthesis.wav"
        with open(output_file, 'wb') as f:
            f.write(response.content)
        
        print(f"✓ Success! Generated audio saved to: {output_file}")
        print(f"   File size: {output_file.stat().st_size} bytes")
        return True
        
    except Exception as e:
        print(f"⚠️  Regular synthesis test failed: {e}")
        return False


def test_health():
    """Test server health endpoint."""
    print("\n" + "="*60)
    print("Test 0: Server health check")
    print("="*60)
    
    try:
        response = requests.get(f"{API_BASE_URL}/health", timeout=5)
        if response.status_code == 200:
            print(f"✓ Server is healthy")
            return True
        else:
            print(f"❌ Server returned status: {response.status_code}")
            return False
    except Exception as e:
        print(f"❌ Cannot connect to server: {e}")
        print(f"\nPlease ensure the TTS server is running:")
        print(f"  ./build/tts-server --model models/qwen-talker-1.7b-base-Q8_0.gguf \\")
        print(f"                     --codec models/qwen-tokenizer-12hz-Q8_0.gguf")
        return False


def main():
    """Run all tests."""
    print("\n" + "="*60)
    print("QwenTTS Zero-shot Voice Cloning Test Suite")
    print("="*60)
    
    # Check if server is running
    if not test_health():
        sys.exit(1)
    
    # Run tests
    results = {}
    results["health"] = test_health()
    results["zero_shot_b64"] = test_zero_shot_b64()
    results["zero_shot_path_freeman"] = test_zero_shot_path()
    results["zero_shot_path_ref"] = test_zero_shot_path_ref()
    results["encode"] = test_encode_endpoint()
    results["cache_hit"] = test_cache_hit()
    results["regular_synthesis"] = test_regular_synthesis()
    
    # Summary
    print("\n" + "="*60)
    print("Test Summary")
    print("="*60)
    
    passed = sum(1 for v in results.values() if v)
    total = len(results)
    
    for name, result in results.items():
        status = "✅ PASS" if result else "❌ FAIL"
        print(f"{status}: {name}")
    
    print(f"\nTotal: {passed}/{total} tests passed")
    print(f"Output directory: {OUTPUT_DIR.absolute()}")
    
    return 0 if passed == total else 1


if __name__ == "__main__":
    sys.exit(main())
