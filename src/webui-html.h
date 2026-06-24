#pragma once
// webui-html.h: embedded HTML/CSS/JS for the TTS WebUI single-page application.

static const char WEBUI_HTML[] = R"rawhtml(
<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>QwenTTS WebUI</title>
<style>
  *, *::before, *::after { box-sizing: border-box; margin: 0; padding: 0; }
  :root {
    --bg: #f7f7f8;
    --surface: #ffffff;
    --border: #e0e0e6;
    --accent: #ff7c00;
    --accent-hover: #e86e00;
    --secondary: #6c6c80;
    --secondary-hover: #5a5a6e;
    --text: #1a1a2e;
    --text-muted: #6c6c80;
    --error: #d32f2f;
    --success: #388e3c;
    --radius: 8px;
    --shadow: 0 1px 4px rgba(0,0,0,0.08);
  }
  body {
    font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, "Helvetica Neue", sans-serif;
    background: var(--bg);
    color: var(--text);
    min-height: 100vh;
    font-size: 14px;
  }
  .app-header {
    background: var(--surface);
    border-bottom: 1px solid var(--border);
    padding: 18px 32px;
    box-shadow: var(--shadow);
  }
  .app-header h1 { font-size: 22px; font-weight: 700; color: var(--text); }
  .app-header p { color: var(--text-muted); margin-top: 2px; font-size: 13px; }
  .app-body { max-width: 960px; margin: 0 auto; padding: 24px 16px 48px; }
  .tabs-nav {
    display: flex; gap: 4px;
    background: var(--surface);
    border: 1px solid var(--border);
    border-radius: var(--radius);
    padding: 4px;
    margin-bottom: 20px;
    box-shadow: var(--shadow);
  }
  .tab-btn {
    flex: 1; padding: 9px 8px; border: none; background: transparent;
    border-radius: 6px; cursor: pointer; font-size: 13px; font-weight: 500;
    color: var(--text-muted); transition: all .18s;
  }
  .tab-btn:hover { background: var(--bg); color: var(--text); }
  .tab-btn.active { background: var(--accent); color: #fff; }
  .tab-panel { display: none; }
  .tab-panel.active { display: block; }
  .card {
    background: var(--surface);
    border: 1px solid var(--border);
    border-radius: var(--radius);
    padding: 20px;
    margin-bottom: 16px;
    box-shadow: var(--shadow);
  }
  .card-title {
    font-size: 13px; font-weight: 600; color: var(--text-muted);
    text-transform: uppercase; letter-spacing: .04em;
    margin-bottom: 14px; padding-bottom: 10px;
    border-bottom: 1px solid var(--border);
  }
  .row { display: flex; gap: 16px; margin-bottom: 14px; }
  .row > * { flex: 1; min-width: 0; }
  .col { display: flex; flex-direction: column; gap: 14px; }
  label { display: block; font-size: 12px; font-weight: 600; color: var(--text-muted); margin-bottom: 5px; }
  input[type=text], input[type=number], textarea, select {
    width: 100%; padding: 8px 10px;
    border: 1px solid var(--border); border-radius: 6px;
    background: var(--bg); color: var(--text);
    font-size: 13px; font-family: inherit; outline: none;
    transition: border-color .15s;
  }
  input[type=text]:focus, input[type=number]:focus, textarea:focus, select:focus {
    border-color: var(--accent);
  }
  textarea { resize: vertical; min-height: 72px; }
  .file-upload-wrap {
    border: 2px dashed var(--border); border-radius: 6px;
    padding: 14px; text-align: center; cursor: pointer;
    background: var(--bg); transition: border-color .15s; position: relative;
  }
  .file-upload-wrap:hover { border-color: var(--accent); }
  .file-upload-wrap input[type=file] {
    position: absolute; inset: 0; opacity: 0; cursor: pointer; width: 100%; height: 100%;
  }
  .file-upload-wrap .upload-icon { font-size: 22px; margin-bottom: 4px; }
  .file-upload-wrap .upload-hint { font-size: 12px; color: var(--text-muted); }
  .file-name { font-size: 12px; color: var(--accent); margin-top: 4px; font-weight: 500; }
  .btn {
    display: inline-flex; align-items: center; justify-content: center;
    padding: 10px 24px; border: none; border-radius: 6px;
    font-size: 14px; font-weight: 600; cursor: pointer;
    transition: background .15s, opacity .15s; gap: 6px;
  }
  .btn-primary { background: var(--accent); color: #fff; }
  .btn-primary:hover { background: var(--accent-hover); }
  .btn-secondary { background: var(--secondary); color: #fff; }
  .btn-secondary:hover { background: var(--secondary-hover); }
  .btn:disabled { opacity: .5; cursor: not-allowed; }
  .btn-row { display: flex; gap: 10px; align-items: center; flex-wrap: wrap; margin-top: 6px; }
  .spinner {
    width: 16px; height: 16px; border: 2px solid rgba(255,255,255,.4);
    border-top-color: #fff; border-radius: 50%;
    animation: spin .7s linear infinite; display: none;
  }
  @keyframes spin { to { transform: rotate(360deg); } }
  .status-box {
    padding: 10px 14px; border-radius: 6px; font-size: 13px;
    margin-top: 12px; display: none;
  }
  .status-box.error { background: #fdecea; color: var(--error); border: 1px solid #f5c6cb; display: block; }
  .status-box.success { background: #e8f5e9; color: var(--success); border: 1px solid #c8e6c9; display: block; }
  .status-box.loading { background: #fff8e1; color: #795548; border: 1px solid #ffe082; display: block; }
  .audio-result { margin-top: 14px; display: none; }
  .audio-result audio { width: 100%; border-radius: 6px; outline: none; }
  .download-links { display: flex; gap: 10px; margin-top: 10px; flex-wrap: wrap; }
  .download-link {
    display: inline-flex; align-items: center; gap: 5px;
    padding: 6px 14px; background: var(--bg); border: 1px solid var(--border);
    border-radius: 6px; text-decoration: none; color: var(--text); font-size: 12px; font-weight: 600;
    transition: border-color .15s;
  }
  .download-link:hover { border-color: var(--accent); color: var(--accent); }
  details.advanced-panel {
    border: 1px solid var(--border); border-radius: 6px;
    background: var(--bg); margin-bottom: 14px; overflow: hidden;
  }
  details.advanced-panel summary {
    padding: 10px 14px; cursor: pointer; font-size: 13px; font-weight: 600;
    color: var(--text-muted); user-select: none; list-style: none;
    display: flex; align-items: center; gap: 6px;
  }
  details.advanced-panel summary::before { content: "▶"; font-size: 10px; transition: transform .2s; }
  details.advanced-panel[open] summary::before { transform: rotate(90deg); }
  details.advanced-panel .adv-body { padding: 14px; border-top: 1px solid var(--border); }
  .param-grid { display: grid; grid-template-columns: repeat(auto-fill, minmax(200px, 1fr)); gap: 12px; }
  .range-wrap { display: flex; align-items: center; gap: 8px; }
  .range-wrap input[type=range] { flex: 1; accent-color: var(--accent); cursor: pointer; }
  .range-wrap .range-val { min-width: 42px; text-align: right; font-size: 12px; color: var(--text-muted); font-weight: 600; }
  .checkbox-wrap { display: flex; align-items: center; gap: 8px; }
  .checkbox-wrap input[type=checkbox] { width: 16px; height: 16px; accent-color: var(--accent); cursor: pointer; }
  .info-note {
    background: #fff8e1; border: 1px solid #ffe082;
    border-radius: 6px; padding: 12px 14px;
    font-size: 13px; color: #6d4c00; margin-bottom: 14px; line-height: 1.7;
  }
  .info-note table { border-collapse: collapse; width: 100%; margin-top: 8px; }
  .info-note th, .info-note td { border: 1px solid #ffe082; padding: 5px 10px; font-size: 12px; }
  .info-note th { background: #fff3cd; font-weight: 600; }
  .speaker-info-box {
    padding: 8px 12px; background: var(--bg); border: 1px solid var(--border);
    border-radius: 6px; font-size: 13px; color: var(--text-muted); min-height: 36px;
  }
  @media (max-width: 600px) {
    .row { flex-direction: column; }
    .tabs-nav { flex-wrap: wrap; }
    .param-grid { grid-template-columns: 1fr; }
  }
</style>
</head>
)rawhtml"
    R"rawhtml(
<body>
<header class="app-header">
  <h1>QwenTTS 语音合成 WebUI</h1>
  <p>基于 qwentts.cpp 的本地化语音克隆与合成界面</p>
  <p>此页面仅提供语音合成测试，使用AI编写，但其他内容均为古法编程</p>
</header>
<div class="app-body">
  <nav class="tabs-nav">
    <button class="tab-btn active" onclick="switchTab(0)">语音克隆</button>
    <button class="tab-btn" onclick="switchTab(1)">语音编码</button>
    <button class="tab-btn" onclick="switchTab(2)">编码文件克隆</button>
    <button class="tab-btn" onclick="switchTab(3)">VoiceDesign / CustomVoice</button>
  </nav>

  <!-- ============================================================ TAB 1: 语音克隆 -->
  <div class="tab-panel active" id="tab-0">
    <div class="card">
      <div class="card-title">参考音频与文本</div>
      <div class="row">
        <div>
          <label>参考音频 (.wav)</label>
          <div class="file-upload-wrap" id="clone-audio-wrap">
            <div class="upload-icon">🎵</div>
            <div class="upload-hint">点击或拖入 .wav 文件</div>
            <div class="file-name" id="clone-audio-name"></div>
            <input type="file" id="clone-ref-audio" accept=".wav" onchange="showFileName(this,'clone-audio-name')">
          </div>
        </div>
        <div class="col">
          <div>
            <label>参考文本（直接输入）</label>
            <textarea id="clone-ref-text" placeholder="请输入参考音频对应的文本内容..." rows="3"></textarea>
          </div>
          <div>
            <label>或上传参考文本文件 (.txt)</label>
            <div class="file-upload-wrap" style="padding:8px 14px;">
              <div class="upload-hint">点击上传 .txt 文件</div>
              <div class="file-name" id="clone-txt-name"></div>
              <input type="file" id="clone-ref-txt-file" accept=".txt" onchange="showFileName(this,'clone-txt-name')">
            </div>
          </div>
        </div>
      </div>
    </div>
    <div class="card">
      <div class="card-title">合成参数</div>
      <div>
        <label>待合成文本</label>
        <textarea id="clone-target-text" placeholder="请输入要合成的内容..." rows="4"></textarea>
      </div>
      <div class="row" style="margin-top:14px;">
        <div>
          <label>语言</label>
          <select id="clone-lang">
            <option value="Chinese">Chinese（中文）</option>
            <option value="English">English（英文）</option>
          </select>
        </div>
        <div>
          <label>输出格式</label>
          <select id="clone-fmt">
            <option value="wav16">wav16（16-bit PCM）</option>
            <option value="wav24">wav24（24-bit PCM）</option>
            <option value="wav32">wav32（32-bit float）</option>
          </select>
        </div>
      </div>
      <div id="adv-clone"></div>
    </div>
    <div class="btn-row">
      <button class="btn btn-primary" id="clone-submit" onclick="doClone()">
        <span class="spinner" id="clone-spin"></span>开始语音克隆
      </button>
    </div>
    <div class="status-box" id="clone-status"></div>
    <div class="audio-result" id="clone-audio-result">
      <label>合成音频</label>
      <audio controls id="clone-audio-player"></audio>
      <div class="download-links">
        <a class="download-link" id="clone-download" download="output.wav">⬇ 下载 WAV</a>
      </div>
    </div>
  </div>

  <!-- ============================================================ TAB 2: 语音编码 -->
  <div class="tab-panel" id="tab-1">
    <div class="card">
      <div class="card-title">参考音频</div>
      <div>
        <label>参考音频 (.wav)</label>
        <div class="file-upload-wrap" id="enc-audio-wrap">
          <div class="upload-icon">🎵</div>
          <div class="upload-hint">点击或拖入 .wav 文件</div>
          <div class="file-name" id="enc-audio-name"></div>
          <input type="file" id="enc-ref-audio" accept=".wav" onchange="showFileName(this,'enc-audio-name')">
        </div>
      </div>
    </div>
    <div class="card">
      <div class="card-title">说明</div>
      <div class="info-note">
        提取参考音频的说话人嵌入（<b>.spk</b>）与 RVQ 编码（<b>.rvq</b>），可重复用于"编码文件克隆"标签页，无需重复上传原始音频。<br>
        <b>注意</b>：此功能仅支持 base 模型（含说话人编码器）。
      </div>
    </div>
    <div class="btn-row">
      <button class="btn btn-primary" id="enc-submit" onclick="doEncode()">
        <span class="spinner" id="enc-spin"></span>开始编码
      </button>
    </div>
    <div class="status-box" id="enc-status"></div>
    <div class="audio-result" id="enc-result">
      <div class="download-links">
        <a class="download-link" id="enc-dl-spk" download="extracted.spk">⬇ 下载 .spk</a>
        <a class="download-link" id="enc-dl-rvq" download="extracted.rvq">⬇ 下载 .rvq</a>
      </div>
    </div>
  </div>
)rawhtml"
    R"rawhtml(

  <!-- ============================================================ TAB 3: 编码文件克隆 -->
  <div class="tab-panel" id="tab-2">
    <div class="card">
      <div class="card-title">编码文件</div>
      <div class="row">
        <div>
          <label>说话人嵌入 (.spk) <span style="color:var(--error)">*</span></label>
          <div class="file-upload-wrap">
            <div class="upload-icon">📁</div>
            <div class="upload-hint">点击上传 .spk 文件</div>
            <div class="file-name" id="fc-spk-name"></div>
            <input type="file" id="fc-spk-file" accept=".spk" onchange="showFileName(this,'fc-spk-name')">
          </div>
        </div>
        <div>
          <label>参考 RVQ 编码 (.rvq，可选）</label>
          <div class="file-upload-wrap">
            <div class="upload-icon">📁</div>
            <div class="upload-hint">点击上传 .rvq 文件（可选）</div>
            <div class="file-name" id="fc-rvq-name"></div>
            <input type="file" id="fc-rvq-file" accept=".rvq" onchange="showFileName(this,'fc-rvq-name')">
          </div>
        </div>
      </div>
      <div class="row" style="margin-top:4px;">
        <div>
          <label>参考文本（使用 .rvq 时建议填写）</label>
          <textarea id="fc-ref-text" placeholder="请输入参考音频的文本内容..." rows="3"></textarea>
        </div>
        <div>
          <label>或上传参考文本文件 (.txt)</label>
          <div class="file-upload-wrap" style="padding:8px 14px;">
            <div class="upload-hint">点击上传 .txt 文件</div>
            <div class="file-name" id="fc-txt-name"></div>
            <input type="file" id="fc-ref-txt-file" accept=".txt" onchange="showFileName(this,'fc-txt-name')">
          </div>
        </div>
      </div>
    </div>
    <div class="card">
      <div class="card-title">合成参数</div>
      <div>
        <label>待合成文本</label>
        <textarea id="fc-target-text" placeholder="请输入要合成的内容..." rows="4"></textarea>
      </div>
      <div class="row" style="margin-top:14px;">
        <div>
          <label>语言</label>
          <select id="fc-lang">
            <option value="Chinese">Chinese（中文）</option>
            <option value="English">English（英文）</option>
          </select>
        </div>
        <div>
          <label>输出格式</label>
          <select id="fc-fmt">
            <option value="wav16">wav16（16-bit PCM）</option>
            <option value="wav24">wav24（24-bit PCM）</option>
            <option value="wav32">wav32（32-bit float）</option>
          </select>
        </div>
      </div>
      <div id="adv-fc"></div>
    </div>
    <div class="btn-row">
      <button class="btn btn-primary" id="fc-submit" onclick="doFileClone()">
        <span class="spinner" id="fc-spin"></span>开始编码文件克隆
      </button>
    </div>
    <div class="status-box" id="fc-status"></div>
    <div class="audio-result" id="fc-audio-result">
      <label>合成音频</label>
      <audio controls id="fc-audio-player"></audio>
      <div class="download-links">
        <a class="download-link" id="fc-download" download="output.wav">⬇ 下载 WAV</a>
      </div>
    </div>
  </div>

  <!-- ============================================================ TAB 4: VoiceDesign / CustomVoice -->
  <div class="tab-panel" id="tab-3">
    <div class="card">
      <div class="card-title">模型说明</div>
      <div class="info-note">
        <b>VoiceDesign / CustomVoice 合成</b>：无需参考音频，通过以下方式控制合成风格：<br>
        <table>
          <tr><th>模型类型</th><th>必填参数</th><th>可选参数</th><th>说明</th></tr>
          <tr><td><b>VoiceDesign</b></td><td>风格指令 (instruct)</td><td>—</td><td>用文本描述目标音色风格</td></tr>
          <tr><td><b>CustomVoice</b></td><td>说话人 (speaker)</td><td>风格指令 (instruct)</td><td>选择内置说话人</td></tr>
        </table>
        <div style="margin-top:8px;"><b>注意</b>：基础（base）模型不支持此功能，请确保加载的是 VoiceDesign 或 CustomVoice 模型。</div>
      </div>
    </div>
    <div class="card">
      <div class="card-title">说话人查询</div>
      <div class="row" style="align-items:flex-end;">
        <div style="flex:0 0 auto;">
          <button class="btn btn-secondary" id="info-btn" onclick="doQueryInfo()">
            <span class="spinner" id="info-spin"></span>查询内置说话人
          </button>
        </div>
        <div style="flex:1;">
          <label>说话人列表（CustomVoice 模型）</label>
          <div class="speaker-info-box" id="gen-speaker-info">点击「查询内置说话人」按钮加载模型并列出可用说话人</div>
        </div>
      </div>
    </div>
    <div class="card">
      <div class="card-title">合成参数</div>
      <div class="row">
        <div>
          <label>风格指令 (instruct) <span style="color:var(--text-muted);font-weight:400;">— VoiceDesign 必填</span></label>
          <textarea id="gen-instruct" placeholder="例如：A calm, slow, and gentle female narrator voice. 温柔、缓慢、成熟女性播音员..." rows="2"></textarea>
        </div>
        <div>
          <label>指定说话人 (speaker) <span style="color:var(--text-muted);font-weight:400;">— CustomVoice 必填</span></label>
          <input type="text" id="gen-speaker" placeholder="例如：cherry, vivian...">
        </div>
      </div>
      <div style="margin-top:4px;">
        <label>待合成文本</label>
        <textarea id="gen-target-text" placeholder="请输入要合成的内容..." rows="4"></textarea>
      </div>
      <div class="row" style="margin-top:14px;">
        <div>
          <label>语言</label>
          <select id="gen-lang">
            <option value="Chinese">Chinese（中文）</option>
            <option value="English">English（英文）</option>
          </select>
        </div>
        <div>
          <label>输出格式</label>
          <select id="gen-fmt">
            <option value="wav16">wav16（16-bit PCM）</option>
            <option value="wav24">wav24（24-bit PCM）</option>
            <option value="wav32">wav32（32-bit float）</option>
          </select>
        </div>
      </div>
      <div id="adv-gen"></div>
    </div>
    <div class="btn-row">
      <button class="btn btn-primary" id="gen-submit" onclick="doGenerate()">
        <span class="spinner" id="gen-spin"></span>开始合成
      </button>
    </div>
    <div class="status-box" id="gen-status"></div>
    <div class="audio-result" id="gen-audio-result">
      <label>合成音频</label>
      <audio controls id="gen-audio-player"></audio>
      <div class="download-links">
        <a class="download-link" id="gen-download" download="output.wav">⬇ 下载 WAV</a>
      </div>
    </div>
  </div>
</div>
)rawhtml"
    R"rawhtml(

<script>
// ─── Tab 切换 ─────────────────────────────────────────────────────────────────
function switchTab(idx) {
  document.querySelectorAll('.tab-btn').forEach((b, i) => b.classList.toggle('active', i === idx));
  document.querySelectorAll('.tab-panel').forEach((p, i) => p.classList.toggle('active', i === idx));
}

// ─── 文件名显示 ────────────────────────────────────────────────────────────────
function showFileName(input, nameId) {
  var el = document.getElementById(nameId);
  if (input.files && input.files[0]) {
    el.textContent = '已选：' + input.files[0].name;
  } else {
    el.textContent = '';
  }
}

// ─── 采样参数面板构建 ──────────────────────────────────────────────────────────
function buildAdvancedPanel(containerId, prefix) {
  var html = '<details class="advanced-panel"><summary>高级采样参数</summary><div class="adv-body"><div class="param-grid">';
  html += '<div><label>随机种子（-1 表示随机）</label><input type="number" id="' + prefix + '-seed" value="-1"></div>';
  html += '<div><label>最大音频帧数 <span class="range-val" id="' + prefix + '-tokens-val">2048</span></label><div class="range-wrap"><input type="range" id="' + prefix + '-tokens" min="64" max="4096" step="64" value="2048" oninput="document.getElementById(\'' + prefix + '-tokens-val\').textContent=this.value"></div></div>';
  html += '<div><label>&nbsp;</label><div class="checkbox-wrap"><input type="checkbox" id="' + prefix + '-do-sample" checked><label style="margin:0;font-size:13px;font-weight:500;color:var(--text)">启用采样 (do_sample)</label></div></div>';
  html += '<div><label>温度 <span class="range-val" id="' + prefix + '-temp-val">0.9</span></label><div class="range-wrap"><input type="range" id="' + prefix + '-temp" min="0.1" max="1.5" step="0.05" value="0.9" oninput="document.getElementById(\'' + prefix + '-temp-val\').textContent=parseFloat(this.value).toFixed(2)"></div></div>';
  html += '<div><label>Top-K <span class="range-val" id="' + prefix + '-topk-val">50</span></label><div class="range-wrap"><input type="range" id="' + prefix + '-topk" min="0" max="200" step="1" value="50" oninput="document.getElementById(\'' + prefix + '-topk-val\').textContent=this.value"></div></div>';
  html += '<div><label>Top-P <span class="range-val" id="' + prefix + '-topp-val">1.00</span></label><div class="range-wrap"><input type="range" id="' + prefix + '-topp" min="0" max="1.0" step="0.05" value="1.0" oninput="document.getElementById(\'' + prefix + '-topp-val\').textContent=parseFloat(this.value).toFixed(2)"></div></div>';
  html += '<div><label>重复惩罚 <span class="range-val" id="' + prefix + '-rep-val">1.05</span></label><div class="range-wrap"><input type="range" id="' + prefix + '-rep" min="1.0" max="2.0" step="0.05" value="1.05" oninput="document.getElementById(\'' + prefix + '-rep-val\').textContent=parseFloat(this.value).toFixed(2)"></div></div>';
  html += '</div></div></details>';
  document.getElementById(containerId).innerHTML = html;
}

function getSamplingParams(prefix) {
  return {
    seed: parseInt(document.getElementById(prefix + '-seed').value) || -1,
    max_new_tokens: parseInt(document.getElementById(prefix + '-tokens').value),
    do_sample: document.getElementById(prefix + '-do-sample').checked,
    temperature: parseFloat(document.getElementById(prefix + '-temp').value),
    top_k: parseInt(document.getElementById(prefix + '-topk').value),
    top_p: parseFloat(document.getElementById(prefix + '-topp').value),
    repetition_penalty: parseFloat(document.getElementById(prefix + '-rep').value)
  };
}

// ─── 初始化高级参数面板 ────────────────────────────────────────────────────────
buildAdvancedPanel('adv-clone', 'clone');
buildAdvancedPanel('adv-fc', 'fc');
buildAdvancedPanel('adv-gen', 'gen');

// ─── UI 辅助函数 ───────────────────────────────────────────────────────────────
function setStatus(id, msg, type) {
  var el = document.getElementById(id);
  el.className = 'status-box ' + type;
  el.textContent = msg;
}
function clearStatus(id) {
  var el = document.getElementById(id);
  el.className = 'status-box';
  el.textContent = '';
}
function setLoading(btnId, spinId, loading) {
  var btn = document.getElementById(btnId);
  var spin = document.getElementById(spinId);
  btn.disabled = loading;
  spin.style.display = loading ? 'inline-block' : 'none';
}
function showAudioResult(resultId, playerId, dlId, blob, filename) {
  var url = URL.createObjectURL(blob);
  var player = document.getElementById(playerId);
  player.src = url;
  var dl = document.getElementById(dlId);
  dl.href = url;
  dl.download = filename || 'output.wav';
  document.getElementById(resultId).style.display = 'block';
  player.scrollIntoView({ behavior: 'smooth', block: 'nearest' });
}
function readFileAsText(file) {
  return new Promise(function(resolve) {
    if (!file) { resolve(''); return; }
    var r = new FileReader();
    r.onload = function(e) { resolve(e.target.result); };
    r.readAsText(file);
  });
}

// ─── Tab 1: 语音克隆 ──────────────────────────────────────────────────────────
async function doClone() {
  var audioFile = document.getElementById('clone-ref-audio').files[0];
  var targetText = document.getElementById('clone-target-text').value.trim();
  if (!audioFile) { setStatus('clone-status', '错误：请先上传参考音频。', 'error'); return; }
  if (!targetText) { setStatus('clone-status', '错误：请输入待合成文本。', 'error'); return; }

  var refText = document.getElementById('clone-ref-text').value.trim();
  var txtFile = document.getElementById('clone-ref-txt-file').files[0];
  if (txtFile) { refText = await readFileAsText(txtFile); }

  setLoading('clone-submit', 'clone-spin', true);
  setStatus('clone-status', '合成中，请稍候...', 'loading');
  document.getElementById('clone-audio-result').style.display = 'none';

  var fd = new FormData();
  fd.append('ref_wav', audioFile);
  fd.append('ref_text', refText);
  fd.append('text', targetText);
  fd.append('lang', document.getElementById('clone-lang').value);
  fd.append('format', document.getElementById('clone-fmt').value);
  var sp = getSamplingParams('clone');
  for (var k in sp) fd.append(k, sp[k]);

  try {
    var resp = await fetch('/api/clone', { method: 'POST', body: fd });
    if (!resp.ok) {
      var errJson = await resp.json().catch(function() { return { error: 'HTTP ' + resp.status }; });
      setStatus('clone-status', '错误：' + (errJson.error || resp.statusText), 'error');
    } else {
      var blob = await resp.blob();
      showAudioResult('clone-audio-result', 'clone-audio-player', 'clone-download', blob, 'clone_output.wav');
      setStatus('clone-status', '合成完成！', 'success');
    }
  } catch(e) {
    setStatus('clone-status', '错误：' + e.message, 'error');
  } finally {
    setLoading('clone-submit', 'clone-spin', false);
  }
}

// ─── Tab 2: 语音编码 ──────────────────────────────────────────────────────────
async function doEncode() {
  var audioFile = document.getElementById('enc-ref-audio').files[0];
  if (!audioFile) { setStatus('enc-status', '错误：请先上传参考音频。', 'error'); return; }

  setLoading('enc-submit', 'enc-spin', true);
  setStatus('enc-status', '编码中，请稍候...', 'loading');
  document.getElementById('enc-result').style.display = 'none';

  var fd = new FormData();
  fd.append('ref_wav', audioFile);

  try {
    var resp = await fetch('/api/encode', { method: 'POST', body: fd });
    var data = await resp.json();
    if (!resp.ok || data.error) {
      setStatus('enc-status', '错误：' + (data.error || resp.statusText), 'error');
    } else {
      // data: { spk: "base64...", rvq: "base64..." }
      var spkBlob = b64ToBlob(data.spk, 'application/octet-stream');
      var rvqBlob = b64ToBlob(data.rvq, 'application/octet-stream');
      var spkUrl = URL.createObjectURL(spkBlob);
      var rvqUrl = URL.createObjectURL(rvqBlob);
      document.getElementById('enc-dl-spk').href = spkUrl;
      document.getElementById('enc-dl-spk').download = 'extracted.spk';
      document.getElementById('enc-dl-rvq').href = rvqUrl;
      document.getElementById('enc-dl-rvq').download = 'extracted.rvq';
      document.getElementById('enc-result').style.display = 'block';
      setStatus('enc-status', '编码完成！已生成 .spk 和 .rvq 文件，点击下载。', 'success');
    }
  } catch(e) {
    setStatus('enc-status', '错误：' + e.message, 'error');
  } finally {
    setLoading('enc-submit', 'enc-spin', false);
  }
}

function b64ToBlob(b64, mime) {
  var byteStr = atob(b64);
  var ab = new ArrayBuffer(byteStr.length);
  var ia = new Uint8Array(ab);
  for (var i = 0; i < byteStr.length; i++) ia[i] = byteStr.charCodeAt(i);
  return new Blob([ab], { type: mime });
}
)rawhtml"
    R"rawhtml(

// ─── Tab 3: 编码文件克隆 ──────────────────────────────────────────────────────
async function doFileClone() {
  var spkFile = document.getElementById('fc-spk-file').files[0];
  var targetText = document.getElementById('fc-target-text').value.trim();
  if (!spkFile) { setStatus('fc-status', '错误：请上传 .spk 文件。', 'error'); return; }
  if (!targetText) { setStatus('fc-status', '错误：请输入待合成文本。', 'error'); return; }

  var refText = document.getElementById('fc-ref-text').value.trim();
  var txtFile = document.getElementById('fc-ref-txt-file').files[0];
  if (txtFile) { refText = await readFileAsText(txtFile); }

  setLoading('fc-submit', 'fc-spin', true);
  setStatus('fc-status', '合成中，请稍候...', 'loading');
  document.getElementById('fc-audio-result').style.display = 'none';

  var fd = new FormData();
  fd.append('spk', spkFile);
  var rvqFile = document.getElementById('fc-rvq-file').files[0];
  if (rvqFile) fd.append('rvq', rvqFile);
  fd.append('ref_text', refText);
  fd.append('text', targetText);
  fd.append('lang', document.getElementById('fc-lang').value);
  fd.append('format', document.getElementById('fc-fmt').value);
  var sp = getSamplingParams('fc');
  for (var k in sp) fd.append(k, sp[k]);

  try {
    var resp = await fetch('/api/clone_from_spk', { method: 'POST', body: fd });
    if (!resp.ok) {
      var errJson = await resp.json().catch(function() { return { error: 'HTTP ' + resp.status }; });
      setStatus('fc-status', '错误：' + (errJson.error || resp.statusText), 'error');
    } else {
      var blob = await resp.blob();
      showAudioResult('fc-audio-result', 'fc-audio-player', 'fc-download', blob, 'file_clone_output.wav');
      setStatus('fc-status', '合成完成！', 'success');
    }
  } catch(e) {
    setStatus('fc-status', '错误：' + e.message, 'error');
  } finally {
    setLoading('fc-submit', 'fc-spin', false);
  }
}

// ─── Tab 4: VoiceDesign / CustomVoice ─────────────────────────────────────────
async function doQueryInfo() {
  document.getElementById('info-btn').disabled = true;
  document.getElementById('info-spin').style.display = 'inline-block';
  document.getElementById('gen-speaker-info').textContent = '查询中...';
  try {
    var resp = await fetch('/api/info');
    var data = await resp.json();
    if (data.error) {
      document.getElementById('gen-speaker-info').textContent = '查询失败：' + data.error;
    } else {
      var modelType = data.model_type || '未知';
      var n = data.n_speakers || 0;
      var speakers = data.speakers || [];
      if (modelType === 'custom_voice' && n > 0) {
        document.getElementById('gen-speaker-info').textContent =
          '模型类型：' + modelType + '　共 ' + n + ' 位说话人：' + speakers.join(', ');
      } else if (modelType === 'voice_design') {
        document.getElementById('gen-speaker-info').textContent =
          '模型类型：voice_design，请通过"风格指令"描述音色，无内置说话人。';
      } else {
        document.getElementById('gen-speaker-info').textContent =
          '模型类型：' + modelType + (n > 0 ? '　说话人：' + speakers.join(', ') : '　无内置说话人');
      }
    }
  } catch(e) {
    document.getElementById('gen-speaker-info').textContent = '查询失败：' + e.message;
  } finally {
    document.getElementById('info-btn').disabled = false;
    document.getElementById('info-spin').style.display = 'none';
  }
}

async function doGenerate() {
  var targetText = document.getElementById('gen-target-text').value.trim();
  if (!targetText) { setStatus('gen-status', '错误：请输入待合成文本。', 'error'); return; }

  setLoading('gen-submit', 'gen-spin', true);
  setStatus('gen-status', '合成中，请稍候...', 'loading');
  document.getElementById('gen-audio-result').style.display = 'none';

  var fd = new FormData();
  fd.append('text', targetText);
  fd.append('lang', document.getElementById('gen-lang').value);
  fd.append('format', document.getElementById('gen-fmt').value);
  fd.append('instruct', document.getElementById('gen-instruct').value.trim());
  fd.append('speaker', document.getElementById('gen-speaker').value.trim());
  var sp = getSamplingParams('gen');
  for (var k in sp) fd.append(k, sp[k]);

  try {
    var resp = await fetch('/api/generate', { method: 'POST', body: fd });
    if (!resp.ok) {
      var errJson = await resp.json().catch(function() { return { error: 'HTTP ' + resp.status }; });
      setStatus('gen-status', '错误：' + (errJson.error || resp.statusText), 'error');
    } else {
      var blob = await resp.blob();
      showAudioResult('gen-audio-result', 'gen-audio-player', 'gen-download', blob, 'generate_output.wav');
      setStatus('gen-status', '合成完成！', 'success');
    }
  } catch(e) {
    setStatus('gen-status', '错误：' + e.message, 'error');
  } finally {
    setLoading('gen-submit', 'gen-spin', false);
  }
}

// ─── 拖拽上传支持 ─────────────────────────────────────────────────────────────
function setupDrop(wrapId, inputId, nameId, accept) {
  var wrap = document.getElementById(wrapId);
  if (!wrap) return;
  wrap.addEventListener('dragover', function(e) { e.preventDefault(); wrap.style.borderColor = 'var(--accent)'; });
  wrap.addEventListener('dragleave', function() { wrap.style.borderColor = ''; });
  wrap.addEventListener('drop', function(e) {
    e.preventDefault(); wrap.style.borderColor = '';
    var files = e.dataTransfer.files;
    if (!files.length) return;
    var inp = document.getElementById(inputId);
    // Check extension
    var ext = files[0].name.split('.').pop().toLowerCase();
    if (accept && !accept.includes('.' + ext)) {
      alert('不支持该文件类型，请上传 ' + accept + ' 格式文件。');
      return;
    }
    // DataTransfer workaround for setting files
    var dt = new DataTransfer();
    dt.items.add(files[0]);
    inp.files = dt.files;
    if (nameId) document.getElementById(nameId).textContent = '已选：' + files[0].name;
  });
}

setupDrop('clone-audio-wrap', 'clone-ref-audio', 'clone-audio-name', '.wav');
setupDrop('enc-audio-wrap', 'enc-ref-audio', 'enc-audio-name', '.wav');
</script>
</body>
</html>
)rawhtml";
