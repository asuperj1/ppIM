# pp 语音输入（录音 + 语音转文字）

> 目标：pp 端提供微信式语音输入；**语音识别能力全站只有一份**（部署在校捷通侧），
> pp 不重复安装 whisper 模型、不重复跑推理服务。

## 一、能力边界

| 功能 | 实现 | 是否依赖 ASR |
|---|---|---|
| 语音消息（语音条，可播放） | 浏览器 `MediaRecorder` 录音 → COS 直传 → `msgKind=3` 文件消息 | 否 |
| 语音转文字 | 录音 → `POST /voice-api/api/v1/voice/transcribe` → 走校捷通 ASR | 是 |

两者通过输入栏的「🎤 / ⌨️」按钮切换（微信式）：语音模式下按住说话，
浮层上方的「说完转成文字」勾选后，松手先转写再以**文字消息**发出。

## 二、为什么这样复用

校捷通的转写接口需要它自己的 JWT，而 pp 用户与校捷通账号体系是两套。
因此不走「前端直连」，而是：

```
浏览器 pp.xiaojietong.online
   │  POST /voice-api/api/v1/voice/transcribe   （同源，无 CORS）
   ▼
Caddy（pp 站点）
   │  uri strip_prefix /voice-api
   │  header_up X-Access-Token {env.XJT_VOICE_TOKEN}     ← 密钥只在服务器
   ▼
校捷通 FastAPI  /api/v1/voice/transcribe
   │  Depends(get_internal_or_user)：内部令牌命中 → 以「系统用户」放行
   ▼
ASR 服务 127.0.0.1:9001（faster-whisper，全站唯一一份）
```

对应代码：
- 前端：`web/js/voice.js`（录音 + 转写）、`web/js/app.js`（语音模式交互）
- 后端放行：`校捷通 backend/app/core/deps.py::get_internal_or_user`
- 反代：`deploy/caddy-xiaojietong.Caddyfile`

## 三、部署清单

### 1. 校捷通侧（一次性）
```bash
# 生成服务间令牌（写入 server.env 与 caddy 的 systemd drop-in，不回显明文）
sudo bash deploy/voice-token-setup.sh
```

### 2. ASR 服务（全站唯一）
```bash
# 依赖
sudo -u xjt env HOME=/tmp /opt/xiaojietong/app/backend/.venv/bin/pip install faster-whisper

# 模型：服务器连不上 huggingface，从 ModelScope 拉 CTranslate2 版本
sudo bash deploy/fetch_whisper_model.sh          # → /opt/xiaojietong/asr-model

# 常驻服务
sudo cp deploy/xjt-asr.service /etc/systemd/system/
sudo mkdir -p /opt/xiaojietong/cache && sudo chown xjt:xjt /opt/xiaojietong/cache
sudo systemctl daemon-reload && sudo systemctl enable --now xjt-asr
curl -s http://127.0.0.1:9001/health
```

### 3. pp 侧
- 前端：`web/index.html`、`web/js/{app,ui,voice}.js`、`web/css/app.css` → `/opt/pp/web`
- 服务端：`chat_service.cpp` 的 `validFileExt / defaultContentType` 已加入
  `webm/ogg/m4a/mp3/wav/amr/aac`，改完需重新编译并重启 `pp`：
  ```bash
  cd /opt/pp && cmake --build build-linux -j2 && sudo systemctl restart pp
  ```

## 四、消息与协议

- 语音消息复用**文件消息**（`msgKind=3` + `fileUrl`），协议**零改动**。
- 时长编码在文件名里：`voice-<秒>s.<ext>`，前端据此渲染微信式宽度与秒数。
  例：`voice-7s.webm`。
- 音频格式由浏览器决定：Chrome/Edge → `webm(opus)`，Safari → `m4a(aac)`。

### 空语音检测（三层）

Whisper 在静音上会稳定幻听（实测输出「优优独播剧场——YoYo Television Series」）。
`vad_filter=True` 只挡得住**数字静音**，挡不住安静房间的麦克风底噪，因此再加两层：

| 层 | 位置 | 判定 | 结果 |
|---|---|---|---|
| 1. 录音电平 | `web/js/voice.js` | 每 60ms 采样 `AnalyserNode` 算 RMS；整段 `peakRms < 0.008` 且有声帧 < 6% | 前端直接提示「没有检测到声音，请重新录制」，不发消息、不发请求 |
| 2. 音频能量 | `xjt/.../asr/silence.py` | 按 40ms 分帧算 RMS（`XJT_ASR_SILENCE_RMS`，默认 0.0065 ≈ -44dBFS），有声帧 < 5% | 不跑模型，返回 `text=""` + `speech_detected=false` |
| 3. 幻听文案 | `silence.is_hallucination()` | 短文本（≤24 字符）命中「优优独播剧场 / 字幕由…提供 / 请不吝点赞」等 | 文本置空，前端提示「没有听清，请再说一次」 |

`speech_detected` 从 ASR 服务（9001）→ 校捷通 `routers/voice.py` → pp 前端逐层透传，
前端在 `app.js` 的 `deliverVoiceClip()` 里据此拒绝转写结果。

阈值调优：静音仍被识别成文字 → 调高 `XJT_ASR_SILENCE_RMS`；正常语音被拦 → 调低。

## 五、排障

| 现象 | 原因 | 处理 |
|---|---|---|
| 按住说话提示「无法访问麦克风」 | 浏览器未授权 / 非 HTTPS | 地址栏允许麦克风；必须 HTTPS 或 localhost |
| 语音条发出但对方播放失败 | 浏览器不支持该编码（Safari 播不了 webm/opus） | 换 Chrome 播放，或让对方用同内核浏览器 |
| 转文字提示「语音转写服务不可用」 | `xjt-asr` 未运行 / 9001 不通 | `systemctl status xjt-asr`、`curl 127.0.0.1:9001/health` |
| 转文字提示「没有检测到声音」 | 录音全程静音（三层检测判定） | 正常现象；确认麦克风没被静音，再按住说话 |
| 输入框出现「优优独播剧场」等字幕组文案 | ASR 未启用静音检测（老版本） | 升级 `app/services/asr/{silence,whisper_local}.py` 并重启 `xjt-asr` |
| 转文字 401 | 内部令牌不匹配 | 重跑 `deploy/voice-token-setup.sh` 并重启 caddy 与 xiaojietong-api |
| 传音频报「不支持的文件类型」 | pp 服务端未更新 | 重新编译 `im_server` 并重启 `pp` |
