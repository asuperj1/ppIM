// =============================================================================
//  web/js/qr.js
//  二维码渲染：本地库（web/vendor/qrcode.min.js）按需加载，浏览器端生成。
//  服务端零参与：不算码、不存图、不发请求。
// =============================================================================
(function (global) {
    'use strict';

    let loading = null;

    function vendorUrl() {
        return (global.BASE_PATH || '') + '/vendor/qrcode.min.js';
    }

    function loadLib() {
        if (global.qrcode) return Promise.resolve(global.qrcode);
        if (loading) return loading;

        loading = new Promise((resolve, reject) => {
            const script = document.createElement('script');
            script.src = vendorUrl();
            script.async = true;
            script.onload = () => {
                if (global.qrcode) resolve(global.qrcode);
                else reject(new Error('二维码库加载失败'));
            };
            script.onerror = () => reject(new Error('二维码库加载失败'));
            document.head.appendChild(script);
        });

        return loading;
    }

    function draw(canvas, text, options) {
        const opts = options || {};
        const size = opts.size || 216;
        const margin = opts.margin === undefined ? 4 : opts.margin;

        // 纠错级别 M（约 15% 容错）：扫码成功率和码点密度的平衡点
        const qr = global.qrcode(0, 'M');
        qr.addData(text);
        qr.make();

        const count = qr.getModuleCount();
        const total = count + margin * 2;
        const dpr = Math.min(global.devicePixelRatio || 1, 3);
        const pixels = Math.round(size * dpr);
        const cell = pixels / total;

        canvas.width = pixels;
        canvas.height = pixels;
        canvas.style.width = size + 'px';
        canvas.style.height = size + 'px';

        const ctx = canvas.getContext('2d');
        ctx.fillStyle = opts.light || '#ffffff';
        ctx.fillRect(0, 0, pixels, pixels);
        ctx.fillStyle = opts.dark || '#111111';

        for (let row = 0; row < count; row += 1) {
            for (let col = 0; col < count; col += 1) {
                if (!qr.isDark(row, col)) continue;
                const x = Math.round((col + margin) * cell);
                const y = Math.round((row + margin) * cell);
                const w = Math.round((col + margin + 1) * cell) - x;
                const h = Math.round((row + margin + 1) * cell) - y;
                ctx.fillRect(x, y, w, h);
            }
        }

        return true;
    }

    async function render(canvas, text, options) {
        if (!canvas || !text) return false;
        await loadLib();
        return draw(canvas, text, options);
    }

    global.PPQR = {
        render: render,
        preload: loadLib,
    };
})(window);
