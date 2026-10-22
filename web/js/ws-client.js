// =============================================================================
//  web/js/ws-client.js
//  WebSocket 传输层：发送信封、接收信封、心跳、断线指数退避重连。
// =============================================================================
(function (global) {
    'use strict';

    class WsClient {
        constructor(url) {
            this.url = url;
            this.socket = null;
            this.handlers = new Map();
            this.manualClose = false;
            this.attempt = 0;
            this.reconnectTimer = null;
            this.heartbeatTimer = null;
        }

        on(event, handler) {
            if (!this.handlers.has(event)) {
                this.handlers.set(event, []);
            }
            this.handlers.get(event).push(handler);
            return this;
        }

        emit(event, payload) {
            const list = this.handlers.get(event) || [];
            for (const fn of list) {
                try {
                    fn(payload);
                } catch (err) {
                    console.error('[ws] handler error:', err);
                }
            }
        }

        isOpen() {
            return this.socket !== null && this.socket.readyState === WebSocket.OPEN;
        }

        connect() {
            this.manualClose = false;
            clearTimeout(this.reconnectTimer);

            if (this.socket) {
                this.socket.onopen = null;
                this.socket.onmessage = null;
                this.socket.onclose = null;
                this.socket.onerror = null;
                try { this.socket.close(); } catch (e) { /* ignore */ }
                this.socket = null;
            }

            const socket = new WebSocket(this.url);
            this.socket = socket;

            socket.onopen = () => {
                this.attempt = 0;
                this.startHeartbeat();
                this.emit('open');
            };

            socket.onmessage = (event) => {
                let envelope;
                try {
                    envelope = JSON.parse(event.data);
                } catch (err) {
                    console.warn('[ws] 非法帧:', event.data);
                    return;
                }

                let body = {};
                if (typeof envelope.body === 'string' && envelope.body.length > 0) {
                    try {
                        body = JSON.parse(envelope.body);
                    } catch (err) {
                        console.warn('[ws] body 不是合法 JSON:', envelope.body);
                        return;
                    }
                }
                this.emit('message', { type: Number(envelope.type), body });
            };

            socket.onerror = () => {
                // 具体错误在 onclose 里统一处理
            };

            socket.onclose = () => {
                this.stopHeartbeat();
                this.emit('close', { manual: this.manualClose });
                if (!this.manualClose) {
                    this.scheduleReconnect();
                }
            };
        }

        scheduleReconnect() {
            const delay = Math.min(1000 * Math.pow(2, this.attempt), 8000);
            this.attempt += 1;
            this.emit('reconnecting', { delay, attempt: this.attempt });
            clearTimeout(this.reconnectTimer);
            this.reconnectTimer = setTimeout(() => this.connect(), delay);
        }

        startHeartbeat() {
            this.stopHeartbeat();
            this.heartbeatTimer = setInterval(() => {
                this.send(MsgType.HEARTBEAT, {});
            }, 25000);
            this.send(MsgType.HEARTBEAT, {});
        }

        stopHeartbeat() {
            if (this.heartbeatTimer !== null) {
                clearInterval(this.heartbeatTimer);
                this.heartbeatTimer = null;
            }
        }

        /**
         * 发送一条业务消息。
         * 服务端 WebEnvelope 的 body 是「字符串」，所以这里要二次 stringify。
         */
        send(type, data) {
            if (!this.isOpen()) {
                return false;
            }
            const envelope = {
                type: type,
                body: JSON.stringify(data || {}),
            };
            this.socket.send(JSON.stringify(envelope));
            return true;
        }

        close() {
            this.manualClose = true;
            clearTimeout(this.reconnectTimer);
            this.stopHeartbeat();
            if (this.socket) {
                try { this.socket.close(1000, 'bye'); } catch (e) { /* ignore */ }
            }
        }
    }

    global.WsClient = WsClient;
})(window);
