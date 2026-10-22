// =============================================================================
//  web/js/voice.js
//  语音输入模块：录音（MediaRecorder）+ 语音转文字（复用校捷通 ASR）
//
//  设计要点
//  --------
//  1. 录音用浏览器原生 MediaRecorder：Chrome/Edge 出 webm(opus)，Safari 出 mp4(aac)。
//  2. 语音转文字**不重复部署模型**：直接调用校捷通那一份 ASR。
//     校捷通的 /api/v1/voice/transcribe 需要它自己的 JWT，pp 用户没有，
//     因此走同源反代 /voice-api/*（Caddy 在服务器侧注入内部令牌，前端拿不到密钥）。
//     见 deploy/caddy-xiaojietong.Caddyfile。
//  3. 本模块只管「录出来」和「转成字」，怎么发消息由 app.js 决定。
// =============================================================================
(function (global) {
    'use strict';

    const TRANSCRIBE_URL = '/voice-api/api/v1/voice/transcribe';

    //: 微信语音条上限 60 秒，到达上限自动停止
    const MAX_VOICE_SECONDS = 60;

    //: 判定「有声」的 RMS 阈值（约 -42 dBFS）。安静房间麦克风底噪约 0.001~0.004，
    //: 正常说话约 0.02~0.10。低于它且有声帧占比极低 ⇒ 空语音。
    const SILENCE_RMS = 0.008;
    //: 有声帧至少占这么多比例才认为说过话（0.06 = 6%）
    const MIN_VOICED_RATIO = 0.06;
    //: 电平采样间隔（毫秒）
    const LEVEL_INTERVAL_MS = 60;

    const supported = !!(navigator.mediaDevices &&
        navigator.mediaDevices.getUserMedia &&
        global.MediaRecorder);

    let rec = null;
    let stream = null;
    let chunks = [];
    let startedAt = 0;
    let autoStop = null;
    let cancelled = false;

    // ---- 录音期间的电平监测（空语音检测）----
    let audioCtx = null;
    let analyser = null;
    let levelTimer = null;
    let levelBuf = null;
    let voicedFrames = 0;
    let totalFrames = 0;
    let peakRms = 0;

    function pickMimeType() {
        // 优先 mp4(aac)：AAC 是跨平台兼容性最好的格式——Safari 播不了 webm/opus，
        // Electron/部分内核也缺 opus 解码器。只有浏览器不支持 mp4 录制时才退到 webm。
        const types = [
            'audio/mp4;codecs=mp4a.40.2',
            'audio/mp4',
            'audio/webm;codecs=opus',
            'audio/webm',
            'audio/ogg;codecs=opus',
        ];
        for (let i = 0; i < types.length; i += 1) {
            const t = types[i];
            if (global.MediaRecorder.isTypeSupported && global.MediaRecorder.isTypeSupported(t)) {
                return t;
            }
        }
        return '';
    }

    /** 由 mime 推出扩展名（决定 COS 上的 Content-Type 与 <audio> 能否播放）。 */
    function extOf(mime) {
        if (mime.indexOf('mp4') >= 0) return 'm4a';
        if (mime.indexOf('ogg') >= 0) return 'ogg';
        if (mime.indexOf('mpeg') >= 0) return 'mp3';
        if (mime.indexOf('wav') >= 0) return 'wav';
        return 'webm';
    }

    /** 音频在本地能播吗？用于提前提示“对方可能播不出该格式”。 */
    function canPlayExt(ext) {
        const probe = document.createElement('audio');
        const map = {
            m4a: 'audio/mp4;codecs="mp4a.40.2"',
            webm: 'audio/webm;codecs="opus"',
            ogg: 'audio/ogg;codecs="opus"',
            mp3: 'audio/mpeg',
            wav: 'audio/wav',
            amr: 'audio/amr',
            aac: 'audio/aac',
        };
        return probe.canPlayType(map[ext] || '') !== '';
    }

    function cleanup() {
        if (autoStop) {
            clearTimeout(autoStop);
            autoStop = null;
        }
        if (stream) {
            stream.getTracks().forEach(function (t) { t.stop(); });
            stream = null;
        }
        stopLevelMonitor();
        rec = null;
    }

    /** 开始统计音量。拿不到 AudioContext 就静默跳过（不影响录音本身）。 */
    function startLevelMonitor(s) {
        stopLevelMonitor();
        const Ctx = global.AudioContext || global.webkitAudioContext;
        if (!Ctx) return;
        try {
            audioCtx = new Ctx();
            const source = audioCtx.createMediaStreamSource(s);
            analyser = audioCtx.createAnalyser();
            analyser.fftSize = 2048;
            levelBuf = new Float32Array(analyser.fftSize);
            // 只接 analyser，不接 destination：否则会把自己的声音回放到扬声器
            source.connect(analyser);
            voicedFrames = 0;
            totalFrames = 0;
            peakRms = 0;
            levelTimer = setInterval(function () {
                if (!analyser) return;
                analyser.getFloatTimeDomainData(levelBuf);
                let sum = 0;
                for (let i = 0; i < levelBuf.length; i += 1) sum += levelBuf[i] * levelBuf[i];
                const rms = Math.sqrt(sum / levelBuf.length);
                totalFrames += 1;
                if (rms >= SILENCE_RMS) voicedFrames += 1;
                if (rms > peakRms) peakRms = rms;
            }, LEVEL_INTERVAL_MS);
            if (audioCtx.state === 'suspended' && audioCtx.resume) {
                audioCtx.resume().catch(function () { /* 忽略 */ });
            }
        } catch (err) {
            stopLevelMonitor();
        }
    }

    function stopLevelMonitor() {
        if (levelTimer) {
            clearInterval(levelTimer);
            levelTimer = null;
        }
        if (audioCtx) {
            try { audioCtx.close(); } catch (err) { /* 忽略 */ }
            audioCtx = null;
        }
        analyser = null;
        levelBuf = null;
        voicedFrames = 0;
        totalFrames = 0;
        peakRms = 0;
    }

    /** 本次录音的语音报告。采样不足 0.4 秒时不判定「空」，交给时长校验。 */
    function levelReport() {
        const ratio = totalFrames > 0 ? voicedFrames / totalFrames : 0;
        return {
            peakRms: peakRms,
            voicedRatio: ratio,
            silent: totalFrames >= 6 && peakRms < SILENCE_RMS && ratio < MIN_VOICED_RATIO,
        };
    }

    function isRecording() {
        return !!(rec && rec.state === 'recording');
    }

    function elapsedSeconds() {
        return startedAt ? (Date.now() - startedAt) / 1000 : 0;
    }

    /**
     * 开始录音。
     * @param {Function} onAutoStop 到达 60 秒上限自动结束后回调，参数同 stop() 的返回值
     * @returns {Promise<boolean>} false = 不支持或麦克风被拒
     */
    function start(onAutoStop) {
        if (!supported) return Promise.resolve(false);
        if (isRecording()) return Promise.resolve(true);

        cancelled = false;
        return navigator.mediaDevices.getUserMedia({
            audio: { channelCount: 1, echoCancellation: true, noiseSuppression: true },
        }).then(function (s) {
            stream = s;
            const mime = pickMimeType();
            try {
                rec = mime ? new MediaRecorder(stream, { mimeType: mime }) : new MediaRecorder(stream);
            } catch (err) {
                cleanup();
                return false;
            }

            chunks = [];
            rec.ondataavailable = function (ev) {
                if (ev.data && ev.data.size) chunks.push(ev.data);
            };
            rec.start();
            startedAt = Date.now();
            startLevelMonitor(stream);

            autoStop = setTimeout(function () {
                if (!isRecording()) return;
                stop().then(function (done) {
                    if (done && typeof onAutoStop === 'function') onAutoStop(done);
                });
            }, MAX_VOICE_SECONDS * 1000);
            return true;
        }).catch(function () {
            cleanup();
            return false;
        });
    }

    /**
     * 结束录音。
     * MediaRecorder.stop() 之后最后一块数据是异步到达的，必须等 onstop 才能拼 Blob，
     * 否则语音条会缺尾巴（表现为播放突然截断）。
     * @returns {Promise<{blob: Blob, seconds: number, ext: string, mime: string}|null>}
     */
    function stop() {
        return new Promise(function (resolve) {
            const current = rec;
            if (!current || current.state !== 'recording') {
                chunks = [];
                cleanup();
                startedAt = 0;
                resolve(null);
                return;
            }

            const secondsRaw = Math.max(0, (Date.now() - startedAt) / 1000);
            const mime = current.mimeType || 'audio/webm';

            current.onstop = function () {
                const level = levelReport();
                const blob = new Blob(chunks, { type: mime.split(';')[0] || 'audio/webm' });
                chunks = [];
                cleanup();
                startedAt = 0;

                if (cancelled || !blob.size || secondsRaw < 0.6) {
                    resolve(null);
                    return;
                }
                resolve({
                    blob: blob,
                    seconds: Math.max(1, Math.round(secondsRaw)),
                    ext: extOf(mime),
                    mime: mime,
                    // 空语音（静音 / 只有底噪）：上层据此拒发、也不送去转写
                    silent: level.silent,
                    peakRms: level.peakRms,
                    voicedRatio: level.voicedRatio,
                });
            };

            try {
                current.stop();
            } catch (err) {
                chunks = [];
                cleanup();
                startedAt = 0;
                resolve(null);
            }
        });
    }

    /** 取消录音（上滑取消 / 切换会话）。 */
    function cancel() {
        cancelled = true;
        if (isRecording()) {
            try { rec.stop(); } catch (err) { /* 已经停了 */ }
        }
        chunks = [];
        cleanup();
        startedAt = 0;
    }

    /**
     * 语音转文字：复用校捷通 ASR（服务器上只有那一份）。
     * @returns {Promise<{text: string, durationMs: number}>}
     */
    function transcribe(blob, ext) {
        const form = new FormData();
        form.append('file', blob, 'voice.' + (ext || 'webm'));
        form.append('language', 'zh');
        form.append('prompt', 'pp 校捷通 图书馆 选课 报修 学生证');

        return fetch(TRANSCRIBE_URL, { method: 'POST', body: form }).then(function (res) {
            return res.json().catch(function () { return null; }).then(function (body) {
                if (!res.ok || !body || body.code !== 0) {
                    const message = (body && body.message) || ('语音转写失败（HTTP ' + res.status + '）');
                    const error = new Error(message);
                    error.code = body && body.code;
                    throw error;
                }
                const data = body.data || {};
                return {
                    text: String(data.text || '').trim(),
                    durationMs: Number(data.duration_ms) || 0,
                    // 服务端能量检测：false = 这段基本是静音，不是识别失败
                    speechDetected: data.speech_detected !== false,
                };
            });
        });
    }

    global.Voice = {
        MAX_VOICE_SECONDS: MAX_VOICE_SECONDS,
        isSupported: function () { return supported; },
        canPlayExt: canPlayExt,
        isRecording: isRecording,
        elapsedSeconds: elapsedSeconds,
        start: start,
        stop: stop,
        cancel: cancel,
        transcribe: transcribe,
    };
})(window);
