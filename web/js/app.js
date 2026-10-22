// =============================================================================
//  web/js/app.js
//  pp 应用入口：认证、会话、好友、群组、资料，全部在这里编排。
// =============================================================================
(function () {
    'use strict';

    const client = new WsClient(WS_URL);

    const EMOJIS = [
        '😀', '😄', '😁', '😆', '😅', '😂', '🙂', '😉', '😊', '😍',
        '😘', '😜', '🤪', '🤔', '🤨', '😐', '😴', '😭', '😡', '🥺',
        '😎', '🤓', '🥳', '😇', '🤗', '🙃', '😏', '🤝', '👍', '👎',
        '👏', '🙏', '💪', '👌', '✌️', '🤞', '❤️', '💔', '🔥', '✨',
        '🎉', '🎁', '☕', '🍺', '🍜', '🌙', '☀️', '🐶', '🐱', '🚀',
    ];

    const historyState = new Map();     // key -> { hasMore }
    const historyRequests = new Map();  // key -> beforeId

    // 长期会话令牌（与 oauth.html 共用同一个 key，实现「授权页免重复登录」）
    const TOKEN_KEY = 'pp_token';

    let pendingAuth = null;
    let silentRelogin = false;
    let sessionRestoring = false;       // 正在用 localStorage 令牌静默恢复登录
    let pendingView = '';               // 从外部页（如授权管理）回来时要恢复的 tab
    let userFeedReturnView = null;      // 进入「TA 的动态」前的视图，返回时原路恢复
    let authTimer = null;
    let typingStopTimer = null;
    let lastTypingSentAt = 0;
    let remoteTypingTimer = null;
    let pendingProfile = null;
    let searchDebounce = null;
    let uploadWaiter = null;            // { resolve, reject, timer }

    // ---- 动态 ----
    const feedState = { posts: [], hasMore: false };
    let feedPendingBefore = 0;
    const userFeedState = { uid: '', nickname: '', posts: [], hasMore: false };
    let userFeedPendingBefore = 0;
    let feedUnread = 0;
    let pendingAvatarUrl = '';
    let postImages = [];                // [{ url }]

    // -------------------------------------------------------------------------
    //  小工具
    // -------------------------------------------------------------------------
    function genClientMsgId() {
        if (window.crypto && typeof window.crypto.randomUUID === 'function') {
            return window.crypto.randomUUID();
        }
        return Date.now().toString(36) + '-' + Math.random().toString(36).slice(2, 10);
    }

    function nowSeconds() {
        return Math.floor(Date.now() / 1000);
    }

    function toNumber(id) {
        const n = Number(id);
        return Number.isFinite(n) ? n : 0;
    }

    function autoResizeInput() {
        const input = UI.el.input;
        if (!input) return;
        // 先量出内容的真实高度（此刻的 'auto' 是同步 reflow，不会画到屏幕上），
        // 再只在结果变化时回写，避免每个按键都改高度造成「弹一下」。
        const prev = input.style.height;
        input.style.height = 'auto';
        const target = Math.min(Math.max(input.scrollHeight, 42), 140);
        const prevPx = parseFloat(prev);
        if (!prev || !isFinite(prevPx) || Math.abs(prevPx - target) >= 1) {
            input.style.height = target + 'px';
        }
    }

    function button(text, className, onClick) {
        const btn = UI.make('button', className || 'btn btn-ghost', text);
        btn.type = 'button';
        if (onClick) btn.addEventListener('click', onClick);
        return btn;
    }

    function field(label, input) {
        const wrap = UI.make('label', 'modal-field');
        wrap.appendChild(UI.make('span', '', label));
        wrap.appendChild(input);
        return wrap;
    }

    function inputEl(value, placeholder) {
        const input = UI.make('input', 'modal-input');
        input.type = 'text';
        input.value = value || '';
        if (placeholder) input.placeholder = placeholder;
        return input;
    }

    function textareaEl(value, placeholder) {
        const area = UI.make('textarea', 'modal-textarea');
        area.value = value || '';
        if (placeholder) area.placeholder = placeholder;
        return area;
    }

    // -------------------------------------------------------------------------
    //  登录 / 注册
    // -------------------------------------------------------------------------
    function setAuthTab(name) {
        UI.setAuthTab(name);
        if (name === 'register') UI.el.registerUsername.focus();
        else UI.el.loginUsername.focus();
    }

    function submitLogin() {
        const username = UI.el.loginUsername.value.trim();
        const password = UI.el.loginPassword.value;

        if (!username || !password) {
            UI.showAuthError('login', '请输入用户名和密码');
            return;
        }

        pendingAuth = { kind: 'login', username, password };
        UI.showAuthError('login', '');
        UI.setAuthBusy('login', true);

        if (!client.send(MsgType.LOGIN_REQ, { username, password })) {
            UI.setAuthBusy('login', false);
            UI.showAuthError('login', '正在连接服务器，请稍后重试…');
            client.connect();
            return;
        }

        clearTimeout(authTimer);
        authTimer = setTimeout(() => {
            UI.setAuthBusy('login', false);
            UI.showAuthError('login', '服务器无响应，请检查服务端是否已启动');
        }, 8000);
    }

    function submitRegister() {
        const username = UI.el.registerUsername.value.trim();
        const nickname = UI.el.registerNickname.value.trim();
        const password = UI.el.registerPassword.value;
        const password2 = UI.el.registerPassword2.value;

        if (!username || !nickname || !password) {
            UI.showAuthError('register', '请填写完整的注册信息');
            return;
        }
        if (password.length < 6) {
            UI.showAuthError('register', '密码至少 6 位');
            return;
        }
        if (password !== password2) {
            UI.showAuthError('register', '两次输入的密码不一致');
            return;
        }

        pendingAuth = { kind: 'register', username, password, nickname };
        UI.showAuthError('register', '');
        UI.setAuthBusy('register', true);

        if (!client.send(MsgType.REGISTER_REQ, { username, password, nickname })) {
            UI.setAuthBusy('register', false);
            UI.showAuthError('register', '正在连接服务器，请稍后重试…');
            client.connect();
        }
    }

    function onLoginSuccess(data) {
        const nickname = data.nickname || (pendingAuth && pendingAuth.nickname) ||
            (pendingAuth && pendingAuth.username) || '我';
        const wasLoggedIn = !!Store.me;
        const username = (pendingAuth && pendingAuth.username) ||
            (Store.credentials && Store.credentials.username) || '';

        Store.me = {
            uid: String(data.uid),
            nickname,
            pp_id: data.ppId || '',
            signature: data.signature || '',
            avatarUrl: data.avatarUrl || '',
            username,
        };

        // 长期会话令牌：存下来，刷新页面就不用重新输密码
        if (data.token) {
            try { localStorage.setItem(TOKEN_KEY, data.token); } catch (err) { /* 隐私模式忽略 */ }
        }

        if (pendingAuth && pendingAuth.password) {
            Store.credentials = { username, password: pendingAuth.password };
        }

        pendingAuth = null;
        silentRelogin = false;
        clearTimeout(authTimer);

        UI.setAuthBusy('login', false);
        UI.setAuthBusy('register', false);
        UI.showAuthError('login', '');
        UI.showAuthError('register', '');
        UI.showApp();

        // 从外部页（/grants.html 等）回来时按 ?view= 恢复原来的 tab；
        // 否则沿用登录前的视图（默认聊天）。
        const view = pendingView || Store.view || 'chats';
        pendingView = '';
        UI.setView(view);
        UI.setConnected(true);
        if (view === 'feed') loadFeed();
        tryDeepLink();

        if (wasLoggedIn) {
            refreshAfterReconnect();
        } else if (!Store.activeKey) {
            UI.setActiveKey(null);
        }
    }

    function forceLogout(message) {
        client.close();
        clearTimeout(authTimer);
        pendingAuth = null;
        silentRelogin = false;
        sessionRestoring = false;

        // 顺手把服务端令牌也撤销掉，别让登出后的令牌还能用 7 天
        try {
            const saved = localStorage.getItem(TOKEN_KEY) || '';
            if (saved) {
                fetch('/api/logout', {
                    method: 'POST',
                    headers: { Authorization: 'Bearer ' + saved },
                    keepalive: true,
                }).catch(() => {});
            }
        } catch (err) { /* 隐私模式忽略 */ }
        try { localStorage.removeItem(TOKEN_KEY); } catch (err) { /* 忽略 */ }
        Store.reset();
        historyState.clear();
        historyRequests.clear();
        feedUnread = 0;
        feedState.posts = [];
        feedState.hasMore = false;
        userFeedState.posts = [];
        userFeedState.uid = '';
        UI.setFeedBadge(0);

        UI.closeMobileChat();
        UI.setActiveKey(null);
        UI.showAuth();
        UI.setConnected(false);
        UI.el.connBanner.hidden = true;

        if (message) UI.toast(message, 3000);
    }

    function refreshAfterReconnect() {
        const active = Store.activeKey;
        Store.messages.clear();
        Store.historyLoaded.clear();
        historyState.clear();

        UI.renderSidebar();
        if (active) {
            requestHistory(active, 0);
        }
    }

    // -------------------------------------------------------------------------
    //  会话 / 消息
    // -------------------------------------------------------------------------
    function openKey(key) {
        if (!key) return;
        const info = Store.parseKey(key);
        const conv = info.kind === 'group'
            ? Store.ensureGroupConv(info.id)
            : Store.ensurePrivateConv(info.id);

        conv.unread = 0;
        UI.setActiveKey(key);
        UI.showTyping(false);

        const state = historyState.get(key);
        UI.setLoadMoreVisible(!!(state && state.hasMore));

        if (!Store.historyLoaded.has(key)) {
            requestHistory(key, 0);
        }

        if (client.isOpen() && Store.me) {
            if (info.kind === 'group') {
                client.send(MsgType.GROUP_READ_REQ, { gid: toNumber(info.id) });
            } else {
                client.send(MsgType.READ_REQ, { peerUid: toNumber(info.id) });
            }
        }
        UI.el.input.focus();
    }

    function requestHistory(key, beforeId) {
        if (!client.isOpen()) return;
        const info = Store.parseKey(key);
        const before = beforeId || 0;
        historyRequests.set(key, before);

        if (info.kind === 'group') {
            client.send(MsgType.GROUP_HISTORY_REQ, {
                gid: toNumber(info.id),
                beforeId: before,
                limit: 30,
            });
        } else {
            client.send(MsgType.HISTORY_REQ, {
                peerUid: toNumber(info.id),
                beforeId: before,
                limit: 30,
            });
        }
    }

    // -------------------------------------------------------------------------
    //  引用回复 / 长按菜单 / 收藏 / 自定义表情
    // -------------------------------------------------------------------------
    let pendingQuote = null;        // { id, text, nickname }

    function quotePreviewOf(msg) {
        if (msg.msgKind === 2) return '[图片]';
        if (msg.msgKind === 3) {
            if (/^voice-\d+s\./.test(msg.fileName || '')) return '[语音]';
            return '[文件] ' + (msg.fileName || '');
        }
        return String(msg.content || '').slice(0, 60);
    }

    function setQuote(msg) {
        if (!msg.id) {
            UI.toast('消息还在发送中，暂时无法引用');
            return;
        }
        const nickname = msg.fromNickname || Store.displayNameOfUser(msg.fromUid);
        pendingQuote = { id: String(msg.id), text: quotePreviewOf(msg), nickname: nickname };
        UI.el.quoteBarName.textContent = nickname;
        UI.el.quoteBarText.textContent = pendingQuote.text;
        UI.el.quoteBar.hidden = false;
        UI.el.input.focus();
    }

    function clearQuote() {
        pendingQuote = null;
        if (UI.el.quoteBar) UI.el.quoteBar.hidden = true;
    }

    /** 取出待引用消息（发送时调用，取完即清空）。 */
    function takeQuote() {
        const quote = pendingQuote;
        pendingQuote = null;
        if (UI.el.quoteBar) UI.el.quoteBar.hidden = true;
        return quote;
    }

    function copyTextToClipboard(text) {
        if (navigator.clipboard && navigator.clipboard.writeText) {
            navigator.clipboard.writeText(text).then(
                () => UI.toast('已复制'),
                () => UI.toast('复制失败，请长按文本手动复制')
            );
            return;
        }
        const ta = document.createElement('textarea');
        ta.value = text;
        ta.style.position = 'fixed';
        ta.style.opacity = '0';
        document.body.appendChild(ta);
        ta.select();
        try {
            document.execCommand('copy');
            UI.toast('已复制');
        } catch (err) {
            UI.toast('复制失败，请长按文本手动复制');
        }
        document.body.removeChild(ta);
    }

    function closeMessageMenu() {
        const menu = document.getElementById('msg-menu');
        if (menu) menu.remove();
    }

    /** 长按/右键菜单：复制、引用、收藏、添加到表情（与微信一致）。 */
    function showMessageMenu(msg, mine, x, y) {
        closeMessageMenu();

        const menu = document.createElement('div');
        menu.className = 'msg-menu';
        menu.id = 'msg-menu';

        const addItem = (label, dangerOrNull, handler) => {
            const btn = document.createElement('button');
            btn.type = 'button';
            btn.textContent = label;
            if (dangerOrNull) btn.className = 'danger';
            btn.addEventListener('click', (event) => {
                event.stopPropagation();
                closeMessageMenu();
                handler();
            });
            menu.appendChild(btn);
        };

        if (msg.msgKind === 4) {
            // 拍一拍不是内容消息：只能撤回，不能复制/引用/收藏
            if (mine && msg.id && !msg.recalled) {
                addItem('撤回', true, () => sendRecall(msg));
            }
        } else {
            if (msg.msgKind === 1 && msg.content) {
                addItem('复制', false, () => copyTextToClipboard(msg.content));
            }
            addItem('引用', false, () => setQuote(msg));
            addItem('收藏', false, () => sendFavoriteAdd(msg));
            if (mine && msg.id && !msg.recalled) {
                addItem('撤回', true, () => sendRecall(msg));
            }
            if (msg.msgKind === 2 && msg.fileUrl) {
                addItem('添加到表情', false, () => addCustomEmoji(msg.fileUrl));
            }
            if (msg.msgKind === 3 && msg.fileUrl) {
                addItem('打开文件', false, () => window.open(msg.fileUrl, '_blank', 'noopener'));
            }
        }

        document.body.appendChild(menu);

        // 贴着长按位置弹出，超出视口就翻到内侧
        const rect = menu.getBoundingClientRect();
        const left = Math.max(8, Math.min(x, window.innerWidth - rect.width - 12));
        const top = Math.max(8, Math.min(y, window.innerHeight - rect.height - 12));
        menu.style.left = left + 'px';
        menu.style.top = top + 'px';

        setTimeout(() => {
            document.addEventListener('click', closeMessageMenu, { once: true });
        }, 0);
    }

    // ---- 收藏夹 ----
    function sendFavoriteAdd(msg) {
        if (!client.isOpen() || !Store.me) {
            UI.toast('连接已断开，无法收藏');
            return;
        }
        if (!msg.id) {
            UI.toast('消息还在发送中，请稍后再试');
            return;
        }
        client.send(MsgType.FAVORITE_ADD_REQ, {
            msgId: String(msg.id),
            msgKind: toNumber(msg.msgKind || 1),
            content: msg.content || '',
            fileUrl: msg.fileUrl || '',
            fileName: msg.fileName || '',
            fileSize: toNumber(msg.fileSize || 0),
            fromNickname: msg.fromNickname || Store.displayNameOfUser(msg.fromUid) || '',
            timestamp: toNumber(msg.timestamp || 0),
        });
    }

    function requestFavorites() {
        if (!client.isOpen()) {
            UI.toast('连接已断开');
            return;
        }
        client.send(MsgType.FAVORITE_LIST_REQ, {});
    }

    function removeFavorite(msgId) {
        if (!client.isOpen() || !msgId) return;
        client.send(MsgType.FAVORITE_REMOVE_REQ, { msgId: String(msgId) });
    }

    // ---- 撤回 ----
    /** 撤回自己发的消息（服务端限制 2 分钟内）。 */
    function sendRecall(msg) {
        if (!client.isOpen() || !Store.me) {
            UI.toast('连接已断开，无法撤回');
            return;
        }
        if (!msg.id) {
            UI.toast('消息还在发送中，请稍后再试');
            return;
        }
        if (msg.recalled) return;
        client.send(MsgType.RECALL_REQ, { msgId: String(msg.id) });
    }

    /**
     * 把某条消息标记为已撤回。msg_id 是全局自增主键，
     * 所以在所有会话的消息列表里找一遍即可命中（自己撤回 / 对方撤回共用）。
     */
    function markMessageRecalled(msgId) {
        const id = String(msgId || '');
        if (!id) return;

        const touched = new Set();
        for (const [conversationKey, list] of Store.messages) {
            for (const msg of list) {
                if (!msg.id || String(msg.id) !== id) continue;
                msg.recalled = true;
                msg.content = '';
                msg.fileUrl = '';
                msg.quoteText = '';
                msg.quoteNickname = '';
                const conv = Store.conversations.get(conversationKey);
                if (conv && list[list.length - 1] === msg) {
                    conv.lastContent = '[消息已撤回]';
                }
                touched.add(conversationKey);
            }
        }
        if (!touched.size) return;

        UI.renderSidebar();
        if (Store.activeKey && touched.has(Store.activeKey)) {
            UI.renderMessages();
        }
    }

    function handleRecallRsp(data) {
        if (!data.success) {
            UI.toast(data.errMsg || '撤回失败');
            return;
        }
        markMessageRecalled(data.msgId);
        UI.toast('已撤回');
    }

    function handleRecallPush(data) {
        const mine = Store.me && String(data.fromUid) === String(Store.me.uid);
        markMessageRecalled(data.msgId);
        if (!mine) {
            const name = data.fromNickname || Store.displayNameOfUser(String(data.fromUid)) || '对方';
            UI.toast(name + '撤回了一条消息');
        }
    }

    // ---- 拍一拍 ----
    let lastPatAt = 0;

    /** 双击对方头像：拍一拍（服务端存成 msgKind=4 的消息，因此可以撤回）。 */
    function sendPat(msg) {
        if (!client.isOpen() || !Store.me) {
            UI.toast('连接已断开，无法拍一拍');
            return;
        }
        if (!msg || !msg.fromUid || !Store.activeKey) return;

        const now = Date.now();
        if (now - lastPatAt < 800) return;   // 冷却，防连点刷屏
        lastPatAt = now;

        const info = Store.parseKey(Store.activeKey);
        if (info.kind === 'group') {
            client.send(MsgType.PAT_REQ, {
                gid: toNumber(info.id),
                targetUid: toNumber(msg.fromUid),
            });
        } else {
            client.send(MsgType.PAT_REQ, { peerUid: toNumber(msg.fromUid) });
        }
    }

    /** 把一条拍一拍提示写进对应会话。 */
    function applyPat(key, data, fromUid) {
        if (!key || key === 'p:' || key === 'g:') return;
        const ts = Number(data.timestamp) || 0;
        Store.addMessage(key, {
            id: String(data.msgId || ''),
            fromUid: String(fromUid || ''),
            toUid: String(data.peerUid || data.targetUid || ''),
            gid: String(data.gid || '') || undefined,
            fromNickname: data.fromNickname || '',
            content: data.targetNickname || '',
            timestamp: ts,
            status: 'sent',
            kind: 1,
            msgKind: 4,
        });
        Store.touchConv(key, { lastContent: '[拍一拍]', lastTime: ts || undefined });
        UI.renderSidebar();
        if (Store.activeKey === key) UI.renderMessages({ forceBottom: true });
    }

    function handlePatRsp(data) {
        if (!data.success) {
            UI.toast(data.errMsg || '拍一拍失败');
            return;
        }
        const gid = String(data.gid || '');
        const key = gid
            ? Store.keyOfGroup(gid)
            : Store.keyOfPrivate(String(data.peerUid || ''));
        applyPat(key, data, Store.me ? Store.me.uid : '');
    }

    function handlePatPush(data) {
        const gid = String(data.gid || '');
        const from = String(data.fromUid || '');
        const key = gid ? Store.keyOfGroup(gid) : Store.keyOfPrivate(from);

        if (!gid && from) {
            // 私聊：先把对方写成已知用户，侧栏才能显示昵称
            Store.upsertUser({ uid: from, nickname: data.fromNickname || '', online: true });
            Store.ensurePrivateConv(from, data.fromNickname || '');
        }
        applyPat(key, data, from);

        if (Store.me && from !== String(Store.me.uid)) {
            const name = data.fromNickname || Store.displayNameOfUser(from) || '对方';
            UI.toast(gid
                ? (name + ' 拍了拍 ' + (data.targetNickname || '对方'))
                : (name + ' 拍了拍你'));
        }
    }

    /** 收藏 → 转发：先选一个会话，再把收藏内容按原类型发出去。 */
    function forwardFavorite(item) {
        if (!item) return;
        const keys = [...Store.conversations.keys()];
        if (!keys.length) {
            UI.toast('还没有可转发的会话');
            return;
        }

        const picker = document.createElement('div');
        picker.className = 'picker';

        for (const key of keys) {
            const conv = Store.conversations.get(key);
            if (!conv) continue;
            const info = Store.parseKey(key);
            const name = info.kind === 'group'
                ? (conv.name || ('群聊 ' + info.id))
                : (conv.nickname || Store.displayNameOfUser(info.id));

            const row = document.createElement('button');
            row.type = 'button';
            row.className = 'picker-item';
            row.style.width = '100%';
            row.style.textAlign = 'left';
            row.textContent = name;
            row.addEventListener('click', () => {
                UI.closeModal();
                forwardFavoriteTo(item, key);
            });
            picker.appendChild(row);
        }

        UI.openModal('转发到', { body: picker, kind: 'forward' });
    }

    function forwardFavoriteTo(item, key) {
        const kind = Number(item.msgKind) || 1;
        if (kind === 1) {
            sendPlainText(key, item.content || '');
            return;
        }
        if (!item.fileUrl) {
            UI.toast('这条收藏缺少文件地址，无法转发');
            return;
        }
        sendMediaMessage({
            key: key,
            isImage: kind === 2,
            isAudio: /^voice-\d+s\./.test(item.fileName || ''),
            fileName: item.fileName || 'file',
            // 服务端要求 file_size 在 (0, 5MB]，收藏里可能没有记录
            fileSize: Number(item.fileSize) > 0 ? Number(item.fileSize) : 1,
        }, item.fileUrl);
    }

    /** 往指定会话发一条纯文本（不走输入框，供转发使用）。 */
    function sendPlainText(key, content) {
        const text = String(content || '').replace(/\s+$/, '');
        if (!key || !text) return;
        if (!client.isOpen() || !Store.me) {
            UI.toast('连接已断开，消息未发送');
            return;
        }

        const info = Store.parseKey(key);
        const clientMsgId = genClientMsgId();
        const timestamp = nowSeconds();
        const msg = {
            id: '',
            clientMsgId,
            fromUid: String(Store.me.uid),
            fromNickname: Store.me.nickname,
            content: text,
            timestamp,
            status: 'sending',
            kind: 1,
            msgKind: 1,
        };

        let sent = false;
        if (info.kind === 'group') {
            msg.gid = info.id;
            sent = client.send(MsgType.CHAT_GROUP, {
                gid: toNumber(info.id),
                content: text,
                clientMsgId,
            });
        } else {
            msg.toUid = info.id;
            sent = client.send(MsgType.CHAT_PRIVATE, {
                toUid: toNumber(info.id),
                content: text,
                clientMsgId,
            });
        }

        Store.addMessage(key, msg);
        Store.touchConv(key, {
            lastContent: text,
            lastTime: timestamp,
            lastSender: info.kind === 'group' ? Store.me.nickname : '',
            unread: 0,
        });
        if (Store.activeKey === key) UI.renderMessages({ forceBottom: true });
        UI.renderSidebar();
        if (sent) UI.toast('已转发');
    }

    // ---- 自定义表情（本地保存，跨设备不同步）----
    const CUSTOM_EMOJI_KEY = 'pp_custom_emoji';
    const MAX_CUSTOM_EMOJI = 24;

    function loadCustomEmoji() {
        try {
            const raw = localStorage.getItem(CUSTOM_EMOJI_KEY);
            const list = raw ? JSON.parse(raw) : [];
            return Array.isArray(list) ? list.filter((u) => typeof u === 'string' && u) : [];
        } catch (err) {
            return [];
        }
    }

    function saveCustomEmoji(list) {
        try {
            localStorage.setItem(CUSTOM_EMOJI_KEY, JSON.stringify(list.slice(0, MAX_CUSTOM_EMOJI)));
        } catch (err) { /* 隐私模式忽略 */ }
    }

    function addCustomEmoji(url) {
        if (!url) return;
        const list = loadCustomEmoji();
        if (list.includes(url)) {
            UI.toast('这个表情已经添加过了');
            return;
        }
        if (list.length >= MAX_CUSTOM_EMOJI) {
            UI.toast('自定义表情最多 ' + MAX_CUSTOM_EMOJI + ' 个');
            return;
        }
        list.unshift(url);
        saveCustomEmoji(list);
        renderEmojiPanel();
        UI.toast('已添加到表情');
    }

    function sendEmojiImage(url) {
        const key = Store.activeKey;
        if (!key) {
            UI.toast('请先选择一个会话');
            return;
        }
        // 表情是已经存在的 COS 图片，直接复用媒体消息通道（size 仅用于服务端范围校验）
        sendMediaMessage({
            key: key,
            isImage: true,
            fileName: 'emoji.png',
            fileSize: 1,
        }, url);
    }

    /** 表情面板当前 tab：'emoji' 内置表情 / 'custom' 自己收藏的表情。 */
    let emojiTab = 'emoji';

    function setEmojiTab(tab) {
        emojiTab = tab === 'custom' ? 'custom' : 'emoji';
        for (const btn of document.querySelectorAll('.emoji-tab')) {
            btn.classList.toggle('active', btn.dataset.tab === emojiTab);
        }
        renderEmojiPanel();
    }

    /** 重建表情格：按 tab 显示内置 emoji 或自己收藏的表情（与微信一致）。 */
    function renderEmojiPanel() {
        const grid = UI.el.emojiGrid;
        if (!grid) return;
        grid.textContent = '';

        if (emojiTab === 'custom') {
            const list = loadCustomEmoji();
            if (!list.length) {
                grid.appendChild(UI.make('div', 'emoji-empty',
                    '还没有收藏的表情。长按一张图片消息，选「添加到表情」即可。'));
                return;
            }
            for (const url of list) {
                const btn = document.createElement('button');
                btn.type = 'button';
                btn.className = 'custom-emoji';
                btn.title = '发送这个表情';
                const img = document.createElement('img');
                img.src = url;
                img.alt = '表情';
                img.loading = 'lazy';
                btn.appendChild(img);
                btn.addEventListener('click', () => {
                    UI.el.emojiPanel.hidden = true;
                    sendEmojiImage(url);
                });
                grid.appendChild(btn);
            }
            return;
        }

        for (const emoji of EMOJIS) {
            const btn = document.createElement('button');
            btn.type = 'button';
            btn.textContent = emoji;
            btn.addEventListener('click', () => {
                const input = UI.el.input;
                const start = input.selectionStart || 0;
                const end = input.selectionEnd || 0;
                input.value = input.value.slice(0, start) + emoji + input.value.slice(end);
                input.selectionStart = input.selectionEnd = start + emoji.length;
                input.focus();
                autoResizeInput();
            });
            grid.appendChild(btn);
        }
    }

    function sendMessage() {
        const key = Store.activeKey;
        const content = UI.el.input.value.replace(/\s+$/, '');

        if (!key || content.length === 0) return;
        if (content.length > 2000) {
            UI.toast('消息太长了，最多 2000 个字符');
            return;
        }
        if (!client.isOpen() || !Store.me) {
            UI.toast('连接已断开，消息未发送');
            return;
        }

        const info = Store.parseKey(key);
        const clientMsgId = genClientMsgId();
        const timestamp = nowSeconds();
        const quote = takeQuote();
        const msg = {
            id: '',
            clientMsgId,
            fromUid: String(Store.me.uid),
            fromNickname: Store.me.nickname,
            content,
            timestamp,
            status: 'sending',
            kind: 1,
            msgKind: 1,
            quoteId: quote ? quote.id : '',
            quoteText: quote ? quote.text : '',
            quoteNickname: quote ? quote.nickname : '',
        };

        let sent = false;
        if (info.kind === 'group') {
            msg.gid = info.id;
            sent = client.send(MsgType.CHAT_GROUP, {
                gid: toNumber(info.id),
                content,
                clientMsgId,
                quoteId: msg.quoteId,
                quoteText: msg.quoteText,
                quoteNickname: msg.quoteNickname,
            });
        } else {
            msg.toUid = info.id;
            sent = client.send(MsgType.CHAT_PRIVATE, {
                toUid: toNumber(info.id),
                content,
                clientMsgId,
                quoteId: msg.quoteId,
                quoteText: msg.quoteText,
                quoteNickname: msg.quoteNickname,
            });
        }

        Store.addMessage(key, msg);
        Store.touchConv(key, {
            lastContent: content,
            lastTime: timestamp,
            lastSender: info.kind === 'group' ? Store.me.nickname : '',
            unread: 0,
        });

        UI.clearInput();
        UI.renderMessages({ forceBottom: true });
        UI.renderSidebar();

        if (!sent) {
            msg.status = 'failed';
            UI.renderMessages();
            return;
        }

        setTimeout(() => {
            if (msg.status === 'sending') {
                msg.status = 'failed';
                if (Store.activeKey === key) UI.renderMessages();
            }
        }, 8000);

        if (info.kind === 'private') {
            if (typingStopTimer) clearTimeout(typingStopTimer);
            typingStopTimer = null;
            client.send(MsgType.TYPING, { toUid: toNumber(info.id), typing: false });
            lastTypingSentAt = 0;
        }
    }

    // -------------------------------------------------------------------------
    //  图片 / 文件（COS 直传：服务器只签名，文件不经过本服务器）
    // -------------------------------------------------------------------------
    const MAX_FILE_BYTES = 5 * 1024 * 1024;
    const IMAGE_EXTS = ['png', 'jpg', 'jpeg', 'gif', 'webp'];
    // 语音消息（MediaRecorder 产物）：webm/ogg = Chrome/Edge，m4a = Safari
    const AUDIO_EXTS = ['webm', 'ogg', 'm4a', 'mp3', 'wav', 'amr', 'aac'];
    const ALLOWED_EXTS = IMAGE_EXTS.concat(AUDIO_EXTS).concat([
        'pdf', 'txt', 'md', 'zip', 'doc', 'docx', 'xls', 'xlsx', 'ppt', 'pptx',
    ]);

    function fileExt(name) {
        const parts = String(name || '').toLowerCase().split('.');
        return parts.length > 1 ? parts.pop() : '';
    }

    function guessContentType(name) {
        const map = {
            png: 'image/png', jpg: 'image/jpeg', jpeg: 'image/jpeg',
            gif: 'image/gif', webp: 'image/webp',
            webm: 'audio/webm', ogg: 'audio/ogg', m4a: 'audio/mp4',
            mp3: 'audio/mpeg', wav: 'audio/wav', amr: 'audio/amr', aac: 'audio/aac',
            pdf: 'application/pdf', txt: 'text/plain', md: 'text/markdown',
            zip: 'application/zip',
            doc: 'application/msword',
            docx: 'application/vnd.openxmlformats-officedocument.wordprocessingml.document',
            xls: 'application/vnd.ms-excel',
            xlsx: 'application/vnd.openxmlformats-officedocument.spreadsheetml.sheet',
            ppt: 'application/vnd.ms-powerpoint',
            pptx: 'application/vnd.openxmlformats-officedocument.presentationml.presentation',
        };
        return map[fileExt(name)] || 'application/octet-stream';
    }

    function readableSize(bytes) {
        const size = Number(bytes) || 0;
        if (size < 1024) return size + ' B';
        if (size < 1024 * 1024) return (size / 1024).toFixed(1) + ' KB';
        return (size / (1024 * 1024)).toFixed(1) + ' MB';
    }

    // 浏览器端压缩：最长边 1600（可指定）、JPEG 0.82；GIF 与小图保持原样
    async function compressImage(file, maxSize) {
        const ext = fileExt(file.name);
        if (ext === 'gif' || file.type === 'image/gif') return file;
        if (file.size <= 300 * 1024) return file;
        if (typeof window.createImageBitmap !== 'function') return file;

        try {
            const bitmap = await window.createImageBitmap(file);
            const max = maxSize || 1600;
            const scale = Math.min(1, max / Math.max(bitmap.width, bitmap.height));
            const width = Math.max(1, Math.round(bitmap.width * scale));
            const height = Math.max(1, Math.round(bitmap.height * scale));

            const canvas = document.createElement('canvas');
            canvas.width = width;
            canvas.height = height;
            const ctx = canvas.getContext('2d');
            ctx.drawImage(bitmap, 0, 0, width, height);
            if (typeof bitmap.close === 'function') bitmap.close();

            const blob = await new Promise((resolve) => {
                canvas.toBlob(resolve, 'image/jpeg', 0.82);
            });
            if (!blob || blob.size >= file.size) return file;

            const name = file.name.replace(/\.[^.]+$/, '') + '.jpg';
            return new File([blob], name, { type: 'image/jpeg' });
        } catch (err) {
            return file;
        }
    }

    function pickFile(kind) {
        if (!Store.activeKey) {
            UI.toast('请先选择一个会话');
            return;
        }
        if (!client.isOpen() || !Store.me) {
            UI.toast('连接已断开，无法上传');
            return;
        }
        if (uploadWaiter) {
            UI.toast('上一份文件还在上传中');
            return;
        }
        const input = UI.el.fileInput;
        // 微信式：相册只选图片，文件选任意类型
        input.accept = kind === 'image'
            ? 'image/png,image/jpeg,image/gif,image/webp'
            : 'image/png,image/jpeg,image/gif,image/webp,application/pdf,text/plain,text/markdown,application/zip,.doc,.docx,.xls,.xlsx,.ppt,.pptx';
        input.click();
    }

    function onFileChosen() {
        const input = UI.el.fileInput;
        const file = input.files && input.files[0];
        input.value = '';
        if (file) startUpload(file);
    }

    // ---- COS 直传：取票据 -> PUT -> 拿到公网地址 ----
    function requestUploadTicket(file) {
        return new Promise((resolve, reject) => {
            if (uploadWaiter) {
                reject(new Error('上一份文件还在上传中'));
                return;
            }
            const filename = file.name || 'file';
            const contentType = file.type || guessContentType(filename);
            const timer = setTimeout(() => {
                if (uploadWaiter) {
                    uploadWaiter = null;
                    reject(new Error('上传超时，请重试'));
                }
            }, 15000);

            uploadWaiter = { resolve, reject, timer };

            const sent = client.send(MsgType.UPLOAD_TICKET_REQ, {
                filename,
                size: file.size,
                contentType,
            });
            if (!sent) {
                clearTimeout(timer);
                uploadWaiter = null;
                reject(new Error('连接已断开'));
            }
        });
    }

    function handleUploadTicket(data) {
        const waiter = uploadWaiter;
        if (!waiter) return;

        uploadWaiter = null;
        clearTimeout(waiter.timer);

        if (!data.success) {
            waiter.reject(new Error(data.errMsg || '无法上传'));
            return;
        }
        waiter.resolve(data);
    }

    // ---- 收藏夹 ----
    function handleFavoriteAddRsp(data) {
        if (data && data.success) UI.toast('已收藏');
        else UI.toast((data && data.errMsg) || '收藏失败');
    }

    function handleFavoriteListRsp(data) {
        Store.favorites = (data && data.items) || [];
        if (Store.view === 'favorites') UI.renderFavorites();
    }

    function handleFavoriteRemoveRsp(data) {
        if (data && data.success) {
            UI.toast('已取消收藏');
            requestFavorites();
        } else {
            UI.toast((data && data.errMsg) || '取消收藏失败');
        }
    }

    async function uploadToCos(file) {
        const ticket = await requestUploadTicket(file);
        const res = await fetch(ticket.uploadUrl, {
            method: 'PUT',
            body: file,
            headers: { 'Content-Type': file.type || guessContentType(file.name) },
        });
        if (!res.ok) throw new Error('HTTP ' + res.status);
        return ticket.fileUrl;
    }

    async function startUpload(rawFile) {
        const key = Store.activeKey;
        if (!key) return;

        const ext = fileExt(rawFile.name);
        if (!ALLOWED_EXTS.includes(ext)) {
            UI.toast('不支持的文件类型');
            return;
        }
        if (rawFile.size > MAX_FILE_BYTES) {
            UI.toast('文件大小需在 5MB 以内');
            return;
        }

        const isImage = IMAGE_EXTS.includes(ext);
        const isAudio = AUDIO_EXTS.includes(ext);
        let file = rawFile;
        if (isImage) {
            UI.toast('正在压缩图片…', 1200);
            file = await compressImage(rawFile);
        }
        if (file.size > MAX_FILE_BYTES) {
            UI.toast('压缩后仍超过 5MB，请换一张');
            return;
        }

        UI.toast('正在上传 ' + readableSize(file.size) + '…', 1200);
        try {
            const fileUrl = await uploadToCos(file);
            sendMediaMessage({
                key,
                isImage,
                isAudio,
                fileName: file.name || rawFile.name,
                fileSize: file.size,
            }, fileUrl);
        } catch (err) {
            UI.toast('上传失败：' + (err && err.message ? err.message : '网络错误'), 3200);
        }
    }

    function sendMediaMessage(pending, fileUrl) {
        const info = Store.parseKey(pending.key);
        const msgKind = pending.isImage ? 2 : 3;
        const placeholder = pending.isImage
            ? '[图片]'
            : (pending.isAudio ? '[语音]' : '[文件] ' + pending.fileName);
        const clientMsgId = genClientMsgId();
        const timestamp = nowSeconds();

        const msg = {
            id: '',
            clientMsgId,
            fromUid: String(Store.me.uid),
            fromNickname: Store.me.nickname,
            content: placeholder,
            timestamp,
            status: 'sending',
            kind: 1,
            msgKind,
            fileUrl,
            fileName: pending.fileName,
            fileSize: pending.fileSize,
        };

        let sent = false;
        if (info.kind === 'group') {
            msg.gid = info.id;
            sent = client.send(MsgType.CHAT_GROUP, {
                gid: toNumber(info.id),
                content: placeholder,
                clientMsgId,
                msgKind,
                fileUrl,
                fileName: pending.fileName,
                fileSize: pending.fileSize,
            });
        } else {
            msg.toUid = info.id;
            sent = client.send(MsgType.CHAT_PRIVATE, {
                toUid: toNumber(info.id),
                content: placeholder,
                clientMsgId,
                msgKind,
                fileUrl,
                fileName: pending.fileName,
                fileSize: pending.fileSize,
            });
        }

        Store.addMessage(pending.key, msg);
        Store.touchConv(pending.key, {
            lastContent: placeholder,
            lastTime: timestamp,
            lastSender: info.kind === 'group' ? Store.me.nickname : '',
            unread: 0,
        });

        if (Store.activeKey === pending.key) UI.renderMessages({ forceBottom: true });
        UI.renderSidebar();

        if (!sent) {
            msg.status = 'failed';
            if (Store.activeKey === pending.key) UI.renderMessages();
            return;
        }

        setTimeout(() => {
            if (msg.status === 'sending') {
                msg.status = 'failed';
                if (Store.activeKey === pending.key) UI.renderMessages();
            }
        }, 8000);
    }

    // -------------------------------------------------------------------------
    //  动态（朋友圈 / 微博）
    // -------------------------------------------------------------------------
    function mapComment(raw) {
        return {
            id: String(raw.id || ''),
            uid: String(raw.uid || '0'),
            nickname: raw.nickname || '',
            content: raw.content || '',
            createdAt: Number(raw.createdAt) || 0,
        };
    }

    function mapPost(raw) {
        return {
            id: String(raw.id || ''),
            uid: String(raw.uid || '0'),
            nickname: raw.nickname || '',
            signature: raw.signature || '',
            avatarUrl: raw.avatarUrl || '',
            content: raw.content || '',
            images: raw.images || [],
            createdAt: Number(raw.createdAt) || 0,
            likeCount: Number(raw.likeCount) || 0,
            liked: !!raw.liked,
            commentCount: Number(raw.commentCount) || 0,
            comments: (raw.comments || []).map(mapComment),
            mine: !!raw.mine,
        };
    }

    function findPostIn(list, postId) {
        const key = String(postId);
        if (!list) return null;
        return list.find((p) => String(p.id) === key) || null;
    }

    function findPost(postId) {
        return findPostIn(feedState.posts, postId) || findPostIn(userFeedState.posts, postId);
    }

    function renderFeedQuiet() {
        UI.renderFeed(feedState.posts, feedState.hasMore);
    }

    function renderUserFeedQuiet() {
        UI.renderUserFeed(userFeedState.posts, userFeedState.hasMore);
    }

    function renderAllFeeds() {
        renderFeedQuiet();
        if (!UI.el.userFeedView.hidden) renderUserFeedQuiet();
    }

    function openUserFeed(uid, nickname) {
        const id = String(uid || '0');
        if (!id || id === '0') return;

        // 记住「进入 TA 的动态」之前所在的视图（动态 / 联系人 / 聊天），
        // 返回时原路回去。注意必须在 setView('feed') 之前读 Store.view。
        if (UI.el.userFeedView.hidden) {
            userFeedReturnView = Store.view;
        }
        UI.setView('feed');

        userFeedState.uid = id;
        userFeedState.nickname = nickname || '';
        userFeedState.posts = [];
        userFeedState.hasMore = false;
        userFeedPendingBefore = 0;

        UI.el.userFeedTitle.textContent = (nickname || 'TA') + ' 的动态';
        UI.el.userFeedList.textContent = '';
        UI.el.userFeedEmpty.hidden = true;
        UI.el.userFeedMore.hidden = true;
        UI.el.userFeedView.hidden = false;
        UI.el.feedView.hidden = true;

        if (client.isOpen()) {
            client.send(MsgType.FEED_REQ, {
                beforeId: 0,
                limit: 20,
                authorUid: Number(id),
            });
        }
    }

    function closeUserFeed() {
        UI.el.userFeedView.hidden = true;

        const back = userFeedReturnView;
        userFeedReturnView = null;

        // 从联系人 / 聊天进来的：回到原视图，而不是丢到一个空白的动态页
        if (back && back !== 'feed') {
            UI.setView(back);
            return;
        }

        UI.setView('feed');
        // 如果动态列表还没加载过（例如从资料卡直达），补一次加载
        if (feedState.posts.length === 0) {
            loadFeed();
        } else {
            renderFeedQuiet();
        }
    }

    function loadMoreUserFeed() {
        const last = userFeedState.posts[userFeedState.posts.length - 1];
        if (!client.isOpen() || !last) return;

        userFeedPendingBefore = Number(last.id) || 0;
        client.send(MsgType.FEED_REQ, {
            beforeId: userFeedPendingBefore,
            limit: 20,
            authorUid: Number(userFeedState.uid),
        });
    }

    function loadFeed() {
        if (!client.isOpen() || !Store.me) {
            UI.toast('连接已断开，稍后再试');
            return;
        }
        feedPendingBefore = 0;
        client.send(MsgType.FEED_REQ, { beforeId: 0, limit: 20 });
    }

    function loadMoreFeed() {
        if (!client.isOpen() || feedState.posts.length === 0) return;
        const last = feedState.posts[feedState.posts.length - 1];
        feedPendingBefore = Number(last.id) || 0;
        client.send(MsgType.FEED_REQ, { beforeId: feedPendingBefore, limit: 20 });
    }

    function handleFeedRsp(data) {
        if (String(data.authorUid || '0') !== '0') {
            handleUserFeedRsp(data);
            return;
        }

        const incoming = (data.posts || []).map(mapPost);
        if (feedPendingBefore === 0) {
            feedState.posts = incoming;
        } else {
            const known = new Set(feedState.posts.map((p) => String(p.id)));
            for (const post of incoming) {
                if (!known.has(String(post.id))) feedState.posts.push(post);
            }
        }
        feedState.hasMore = !!data.hasMore;
        feedPendingBefore = 0;
        renderFeedQuiet();
    }

    function handleUserFeedRsp(data) {
        const incoming = (data.posts || []).map(mapPost);
        if (userFeedPendingBefore === 0) {
            userFeedState.posts = incoming;
        } else {
            const known = new Set(userFeedState.posts.map((p) => String(p.id)));
            for (const post of incoming) {
                if (!known.has(String(post.id))) userFeedState.posts.push(post);
            }
        }
        userFeedState.hasMore = !!data.hasMore;
        userFeedPendingBefore = 0;
        UI.el.userFeedEmpty.hidden = userFeedState.posts.length > 0;
        renderUserFeedQuiet();
    }

    function openCompose() {
        if (!Store.me) return;
        if (!client.isOpen()) {
            UI.toast('连接已断开，稍后再试');
            return;
        }
        UI.el.feedView.hidden = true;
        UI.el.userFeedView.hidden = true;
        UI.el.postComposeView.hidden = false;
        UI.setFeedBadge(feedUnread);
        updateComposeState();
        setTimeout(() => UI.el.postInput.focus(), 60);
    }

    function closeCompose() {
        UI.el.postComposeView.hidden = true;
        if (Store.view === 'feed') {
            UI.el.feedView.hidden = false;
            renderFeedQuiet();
        }
    }

    function handlePostCreated(data) {
        if (!data.success) {
            UI.toast(data.errMsg || '发布失败');
            updateComposeState();
            return;
        }
        const post = mapPost(data.post || {});
        feedState.posts.unshift(post);
        renderFeedQuiet();

        UI.el.postInput.value = '';
        postImages = [];
        renderPostThumbs();
        updateComposeState();
        closeCompose();
        UI.toast('已发布');
    }

    function handlePostLikeRsp(data) {
        if (data.errMsg) {
            UI.toast(data.errMsg);
            return;
        }
        const post = findPost(data.postId);
        if (!post) return;

        const patch = { liked: !!data.liked, likeCount: Number(data.likeCount) || 0 };
        for (const list of [feedState.posts, userFeedState.posts]) {
            const item = findPostIn(list, data.postId);
            if (item) Object.assign(item, patch);
        }
        renderAllFeeds();
    }

    function handleCommentRsp(data) {
        if (!data.success) {
            UI.toast(data.errMsg || '评论失败');
            return;
        }
        const comment = data.comment || {};
        const mapped = mapComment(comment);

        for (const list of [feedState.posts, userFeedState.posts]) {
            const post = findPostIn(list, comment.postId);
            if (!post) continue;
            post.comments = post.comments || [];
            if (!post.comments.some((c) => String(c.id) === String(mapped.id))) {
                post.comments.push(mapped);
            }
            post.commentCount = Number(data.commentCount) || post.comments.length;
        }
        renderAllFeeds();
    }

    function handlePostDeleted(data) {
        if (data.errMsg) {
            UI.toast(data.errMsg);
            return;
        }
        const key = String(data.postId);
        feedState.posts = feedState.posts.filter((p) => String(p.id) !== key);
        userFeedState.posts = userFeedState.posts.filter((p) => String(p.id) !== key);
        renderAllFeeds();
        UI.toast('已删除');
    }

    function handlePostPush(data) {
        const post = mapPost(data || {});
        if (!post.id) return;

        if (!findPost(post.id)) feedState.posts.unshift(post);

        if (userFeedState.uid && userFeedState.uid === String(post.uid) &&
            !findPostIn(userFeedState.posts, post.id)) {
            userFeedState.posts.unshift(post);
            if (!UI.el.userFeedView.hidden) renderUserFeedQuiet();
        }

        if (Store.view === 'feed' && UI.el.postComposeView.hidden) {
            renderFeedQuiet();
        } else {
            feedUnread += 1;
            UI.setFeedBadge(feedUnread);
        }

        UI.toast((post.nickname || '好友') + ' 发布了新动态', 2200);
    }

    function handlePostNotice(data) {
        const name = data.fromNickname || '有人';
        const kind = Number(data.kind) || 0;

        if (kind === 1) {
            UI.toast(name + ' 赞了你的动态', 2400);
        } else if (kind === 2) {
            UI.toast(name + ' 评论了你的动态：' + (data.content || ''), 3200);
        }

        const post = findPost(data.postId);
        if (!post) return;

        if (kind === 1) post.likeCount = Number(data.likeCount) || post.likeCount;
        if (kind === 2) post.commentCount = Number(data.commentCount) || post.commentCount;
        renderAllFeeds();
    }

    // ---- 发布框 ----
    function renderPostThumbs() {
        const box = UI.el.composeThumbs;
        box.textContent = '';
        box.hidden = postImages.length === 0;

        postImages.forEach((item, index) => {
            const wrap = UI.make('div', 'compose-thumb');
            const img = document.createElement('img');
            img.src = item.url;
            img.alt = '待发布图片';

            const del = UI.make('button', '', '×');
            del.type = 'button';
            del.addEventListener('click', () => {
                postImages.splice(index, 1);
                renderPostThumbs();
                updateComposeState();
            });

            wrap.append(img, del);
            box.appendChild(wrap);
        });
    }

    function updateComposeState() {
        const text = UI.el.postInput.value;
        UI.el.postCount.textContent = text.length + '/2000';
        UI.el.btnPostSend.disabled = !(text.trim().length > 0 || postImages.length > 0);
    }

    async function onPostImagesChosen() {
        const input = UI.el.postFileInput;
        const files = Array.from(input.files || []);
        input.value = '';
        if (files.length === 0) return;

        if (postImages.length + files.length > 9) {
            UI.toast('一条动态最多 9 张图片');
            return;
        }

        for (const raw of files) {
            if (!/^image\//.test(raw.type) && !IMAGE_EXTS.includes(fileExt(raw.name))) {
                UI.toast(raw.name + ' 不是图片');
                continue;
            }
            if (raw.size > MAX_FILE_BYTES) {
                UI.toast(raw.name + ' 超过 5MB');
                continue;
            }
            try {
                const file = await compressImage(raw);
                UI.toast('正在上传图片…', 1000);
                const url = await uploadToCos(file);
                postImages.push({ url });
                renderPostThumbs();
                updateComposeState();
            } catch (err) {
                UI.toast('图片上传失败：' + (err && err.message ? err.message : '网络错误'), 3200);
            }
        }
    }

    function publishPost() {
        if (!client.isOpen() || !Store.me) {
            UI.toast('连接已断开，发布失败');
            return;
        }
        const content = UI.el.postInput.value.trim();
        if (!content && postImages.length === 0) return;

        UI.el.btnPostSend.disabled = true;
        client.send(MsgType.POST_CREATE_REQ, {
            content,
            images: postImages.map((item) => item.url),
        });
    }

    function bindPostList(listEl) {
        if (!listEl) return;

        listEl.addEventListener('click', (event) => {
            const card = event.target.closest('.post');
            if (!card) return;
            const postId = card.dataset.postId;

            const userNode = event.target.closest('[data-post-user]');
            if (userNode) {
                const post = findPost(postId);
                openUserFeed(userNode.dataset.postUser, post ? post.nickname : '');
                return;
            }

            if (event.target.closest('[data-post-like]')) {
                const post = findPost(postId);
                if (post) {
                    client.send(MsgType.POST_LIKE_REQ, {
                        postId: Number(post.id),
                        like: !post.liked,
                    });
                }
                return;
            }

            if (event.target.closest('[data-post-comment]')) {
                const box = card.querySelector('.post-comment-box');
                if (!box) return;
                box.hidden = !box.hidden;
                if (!box.hidden) box.querySelector('input').focus();
                return;
            }

            if (event.target.closest('[data-post-comment-send]')) {
                const input = card.querySelector('[data-post-comment-input]');
                const text = (input && input.value || '').trim();
                if (!text) return;
                client.send(MsgType.COMMENT_CREATE_REQ, {
                    postId: Number(postId),
                    content: text,
                });
                input.value = '';
                return;
            }

            if (event.target.closest('[data-post-delete]')) {
                if (!window.confirm('删除这条动态？')) return;
                client.send(MsgType.POST_DELETE_REQ, { postId: Number(postId) });
            }
        });

        listEl.addEventListener('keydown', (event) => {
            if (event.key !== 'Enter') return;
            const input = event.target.closest('[data-post-comment-input]');
            if (!input) return;
            event.preventDefault();

            const card = input.closest('.post');
            const text = input.value.trim();
            if (!card || !text) return;

            client.send(MsgType.COMMENT_CREATE_REQ, {
                postId: Number(card.dataset.postId),
                content: text,
            });
            input.value = '';
        });
    }

    function bindFeed() {
        if (!UI.el.feedView) return;

        UI.el.btnFeedRefresh.addEventListener('click', loadFeed);
        UI.el.btnFeedMore.addEventListener('click', loadMoreFeed);
        UI.el.btnNewPost.addEventListener('click', openCompose);
        UI.el.btnComposeBack.addEventListener('click', closeCompose);
        UI.el.btnPickImages.addEventListener('click', () => UI.el.postFileInput.click());
        UI.el.postFileInput.addEventListener('change', onPostImagesChosen);
        UI.el.postInput.addEventListener('input', updateComposeState);
        UI.el.btnPostSend.addEventListener('click', publishPost);

        bindPostList(UI.el.feedList);
        bindPostList(UI.el.userFeedList);

        UI.el.btnUserFeedBack.addEventListener('click', closeUserFeed);
        UI.el.btnUserFeedMore.addEventListener('click', loadMoreUserFeed);
    }

    // -------------------------------------------------------------------------
    //  编辑资料页
    // -------------------------------------------------------------------------
    function updateEditCounters() {
        UI.el.editNicknameCount.textContent = UI.el.editNickname.value.length + '/20';
        UI.el.editPpidCount.textContent = UI.el.editPpid.value.length + '/20';
        UI.el.editSignatureCount.textContent = UI.el.editSignature.value.length + '/80';
    }

    function openEditProfile(focus) {
        if (!Store.me) return;

        pendingAvatarUrl = '';
        UI.el.editNickname.value = Store.me.nickname || '';
        UI.el.editPpid.value = Store.me.pp_id || '';
        UI.el.editSignature.value = Store.me.signature || '';
        updateEditCounters();

        UI.showMePage('edit');

        const targets = {
            nickname: UI.el.editNickname,
            ppId: UI.el.editPpid,
            signature: UI.el.editSignature,
        };
        const target = targets[focus];
        if (target) {
            setTimeout(() => {
                target.focus();
                if (target.select) target.select();
            }, 80);
        }
    }

    function saveProfile() {
        const nickname = UI.el.editNickname.value.trim();
        const ppId = UI.el.editPpid.value.trim();
        const signature = UI.el.editSignature.value.trim();

        if (!nickname) {
            UI.toast('名字不能为空');
            return;
        }
        if (ppId && !/^[A-Za-z0-9_]{4,20}$/.test(ppId)) {
            UI.toast('pp 号需 4~20 位字母、数字或下划线');
            return;
        }

        if (!client.isOpen()) {
            UI.toast('连接已断开');
            return;
        }

        pendingProfile = { nickname, ppId, signature, avatarUrl: pendingAvatarUrl };
        client.send(MsgType.PROFILE_UPDATE_REQ, pendingProfile);
    }

    async function onAvatarChosen() {
        const input = UI.el.avatarFileInput;
        const raw = input.files && input.files[0];
        input.value = '';
        if (!raw) return;

        if (!/^image\//.test(raw.type) && !IMAGE_EXTS.includes(fileExt(raw.name))) {
            UI.toast('请选择图片文件');
            return;
        }
        if (raw.size > MAX_FILE_BYTES) {
            UI.toast('图片不能超过 5MB');
            return;
        }

        try {
            UI.toast('正在上传头像…', 1200);
            const file = await compressImage(raw, 400);
            const url = await uploadToCos(file);
            pendingAvatarUrl = url;
            UI.applyAvatar(UI.el.meEditAvatar, Store.me.uid, Store.me.nickname, url);
            UI.toast('头像已上传，点右上角「保存」生效');
        } catch (err) {
            UI.toast('头像上传失败：' + (err && err.message ? err.message : '网络错误'), 3200);
        }
    }

    function bindEditProfile() {
        if (!UI.el.meEdit) return;

        UI.el.editNickname.addEventListener('input', updateEditCounters);
        UI.el.editPpid.addEventListener('input', updateEditCounters);
        UI.el.editSignature.addEventListener('input', updateEditCounters);
        UI.el.btnSaveProfile.addEventListener('click', saveProfile);

        UI.el.btnChangeAvatar.addEventListener('click', () => UI.el.avatarFileInput.click());
        UI.el.avatarFileInput.addEventListener('change', onAvatarChosen);
    }

    // -------------------------------------------------------------------------
    //  服务端推送
    // -------------------------------------------------------------------------
    function handleOnlineUsers(data) {
        for (const user of data.users || []) {
            Store.upsertUser({
                uid: user.uid,
                nickname: user.nickname,
                pp_id: user.ppId,
                signature: user.signature,
                avatarUrl: user.avatarUrl,
                online: user.online,
            });
        }
        UI.renderSidebar();
        if (Store.activeKey) UI.renderChatHeader();
    }

    function handleConversationList(data) {
        Store.applyConversations(data.conversations || []);

        if (Store.activeKey) {
            const info = Store.parseKey(Store.activeKey);
            const conv = Store.conversations.get(Store.activeKey);
            if (conv) {
                conv.unread = 0;
                if (client.isOpen()) {
                    if (info.kind === 'group') client.send(MsgType.GROUP_READ_REQ, { gid: toNumber(info.id) });
                    else client.send(MsgType.READ_REQ, { peerUid: toNumber(info.id) });
                }
            }
        }

        UI.renderSidebar();
        if (Store.activeKey) UI.renderChatHeader();
    }

    function handleIncomingPrivate(data) {
        const from = String(data.fromUid);
        const nickname = data.fromNickname || Store.displayNameOfUser(from);

        Store.upsertUser({ uid: from, nickname, online: true });

        const key = Store.keyOfPrivate(from);
        const conv = Store.ensurePrivateConv(from, nickname);

        Store.addMessage(key, {
            id: String(data.msgId || ''),
            clientMsgId: data.clientMsgId || '',
            fromUid: from,
            toUid: String(data.toUid || ''),
            content: data.content || '',
            timestamp: Number(data.timestamp) || 0,
            status: 'sent',
            kind: 1,
            msgKind: Number(data.msgKind) || 1,
            fileUrl: data.fileUrl || '',
            fileName: data.fileName || '',
            fileSize: Number(data.fileSize) || 0,
            quoteId: data.quoteId || '',
            quoteText: data.quoteText || '',
            quoteNickname: data.quoteNickname || '',
        });

        conv.lastContent = data.content || '';
        conv.lastTime = Number(data.timestamp) || 0;
        conv.lastSender = '';

        const active = Store.activeKey === key;
        if (active) {
            conv.unread = 0;
            client.send(MsgType.READ_REQ, { peerUid: toNumber(from) });
            UI.showTyping(false);
            UI.renderMessages({ forceBottom: true });
        } else {
            conv.unread = (conv.unread || 0) + 1;
        }
        UI.renderSidebar();
    }

    function handleIncomingGroup(data) {
        const gid = String(data.gid);
        const key = Store.keyOfGroup(gid);
        const kind = Number(data.kind) || 1;

        if (!Store.groups.has(gid)) {
            Store.upsertGroup({ gid, name: '群聊 ' + gid, member_count: 0 });
        }
        const conv = Store.ensureGroupConv(gid);
        const from = String(data.fromUid || '0');
        const nickname = data.fromNickname || '';

        Store.addMessage(key, {
            id: String(data.msgId || ''),
            clientMsgId: data.clientMsgId || '',
            gid,
            fromUid: from,
            fromNickname: nickname,
            content: data.content || '',
            timestamp: Number(data.timestamp) || 0,
            status: 'sent',
            kind,
            msgKind: Number(data.msgKind) || 1,
            fileUrl: data.fileUrl || '',
            fileName: data.fileName || '',
            fileSize: Number(data.fileSize) || 0,
            quoteId: data.quoteId || '',
            quoteText: data.quoteText || '',
            quoteNickname: data.quoteNickname || '',
        });

        conv.lastContent = data.content || '';
        conv.lastTime = Number(data.timestamp) || 0;
        conv.lastSender = kind === 2 ? '' : nickname;

        const active = Store.activeKey === key;
        if (active) {
            conv.unread = 0;
            client.send(MsgType.GROUP_READ_REQ, { gid: toNumber(gid) });
            UI.renderMessages({ forceBottom: true });
        } else if (kind !== 2) {
            conv.unread = (conv.unread || 0) + 1;
        }
        UI.renderSidebar();
    }

    function handlePrivateAck(data) {
        const key = Store.keyOfPrivate(data.toUid);
        const list = Store.messages.get(key) || [];
        const msg = list.find((m) => m.clientMsgId && m.clientMsgId === data.clientMsgId);
        if (msg) {
            msg.id = String(data.msgId || '');
            msg.timestamp = Number(data.timestamp) || msg.timestamp;
            msg.status = 'sent';
        }
        if (Store.activeKey === key) UI.renderMessages();
    }

    function handleGroupAck(data) {
        const key = Store.keyOfGroup(data.gid);
        const list = Store.messages.get(key) || [];
        const msg = list.find((m) => m.clientMsgId && m.clientMsgId === data.clientMsgId);
        if (msg) {
            msg.id = String(data.msgId || '');
            msg.timestamp = Number(data.timestamp) || msg.timestamp;
            msg.status = 'sent';
        }
        if (Store.activeKey === key) UI.renderMessages();
    }

    function handlePrivateHistory(data) {
        const peer = String(data.peerUid);
        const key = Store.keyOfPrivate(peer);
        const before = historyRequests.get(key) || 0;

        for (const m of data.messages || []) {
            Store.addMessage(key, {
                id: String(m.msgId || ''),
                clientMsgId: m.clientMsgId || '',
                fromUid: String(m.fromUid),
                toUid: String(m.toUid),
                content: m.content || '',
                timestamp: Number(m.timestamp) || 0,
                status: 'sent',
                kind: 1,
                msgKind: Number(m.msgKind) || 1,
                fileUrl: m.fileUrl || '',
                fileName: m.fileName || '',
                fileSize: Number(m.fileSize) || 0,
                quoteId: m.quoteId || '',
                quoteText: m.quoteText || '',
                quoteNickname: m.quoteNickname || '',
                recalled: !!m.recalled,
            });
        }

        Store.historyLoaded.add(key);
        historyState.set(key, { hasMore: !!data.hasMore });

        if (Store.activeKey === key) {
            UI.renderMessages({ keepPosition: before > 0, forceBottom: before === 0 });
            UI.setLoadMoreVisible(!!data.hasMore);
        }
    }

    function handleGroupHistory(data) {
        const gid = String(data.gid);
        const key = Store.keyOfGroup(gid);
        const before = historyRequests.get(key) || 0;

        for (const m of data.messages || []) {
            Store.addMessage(key, {
                id: String(m.msgId || ''),
                clientMsgId: m.clientMsgId || '',
                gid,
                fromUid: String(m.fromUid || '0'),
                fromNickname: m.fromNickname || '',
                content: m.content || '',
                timestamp: Number(m.timestamp) || 0,
                status: 'sent',
                kind: Number(m.kind) || 1,
                msgKind: Number(m.msgKind) || 1,
                fileUrl: m.fileUrl || '',
                fileName: m.fileName || '',
                fileSize: Number(m.fileSize) || 0,
                quoteId: m.quoteId || '',
                quoteText: m.quoteText || '',
                quoteNickname: m.quoteNickname || '',
                recalled: !!m.recalled,
            });
        }

        Store.historyLoaded.add(key);
        historyState.set(key, { hasMore: !!data.hasMore });

        if (Store.activeKey === key) {
            UI.renderMessages({ keepPosition: before > 0, forceBottom: before === 0 });
            UI.setLoadMoreVisible(!!data.hasMore);
        }
    }

    function handleTyping(data) {
        const from = String(data.toUid);
        if (Store.activeKey !== Store.keyOfPrivate(from)) return;

        UI.showTyping(!!data.typing, Store.displayNameOfUser(from));
        clearTimeout(remoteTypingTimer);
        if (data.typing) {
            remoteTypingTimer = setTimeout(() => UI.showTyping(false), 4000);
        }
    }

    function handleFriendList(data) {
        Store.applyFriends(data.friends || []);
        UI.renderSidebar();
        if (Store.activeKey) UI.renderChatHeader();
    }

    function handleGroupList(data) {
        const previous = Store.activeKey;
        Store.applyGroups(data.groups || []);

        if (previous && !Store.conversations.has(previous)) {
            UI.setActiveKey(null);
        } else {
            UI.renderSidebar();
            if (Store.activeKey) UI.renderChatHeader();
        }
    }

    function handleRequests(data) {
        Store.requests = data.requests || [];
        UI.renderSidebar();
        UI.refreshModal();
    }

    function handleRequestPush(data) {
        const request = data.request || {};
        const status = Number(request.status) || 0;

        if (status === 0) {
            if (request.fromUid) {
                Store.upsertUser({
                    uid: request.fromUid,
                    nickname: request.fromNickname,
                    pp_id: request.fromPpId,
                });
            }
            UI.toast((request.fromNickname || '有人') + ' 请求加你为好友');
            client.send(MsgType.FRIEND_REQUEST_LIST_REQ, {});
            UI.renderSidebar();
            UI.refreshModal();
            return;
        }

        if (String(request.fromUid) === String(Store.me && Store.me.uid)) {
            UI.toast(status === 1 ? '对方通过了你的好友申请' : '对方拒绝了你的好友申请');
            client.send(MsgType.FRIEND_REQUEST_LIST_REQ, {});
        }
    }

    function handleGroupCreateRsp(data) {
        if (!data.success) {
            UI.toast(data.errMsg || '创建群聊失败');
            return;
        }
        const group = data.group || {};
        const gid = String(group.gid);
        Store.upsertGroup(group);
        Store.ensureGroupConv(gid, group.name);
        UI.closeModal();
        UI.toast('群聊已创建');
        openKey(Store.keyOfGroup(gid));
    }

    function handleMemberList(data) {
        Store.groupMembers.set(String(data.gid), data.members || []);
        if (UI.modalKind() === 'members' || UI.modalKind() === 'invite') {
            UI.refreshModal();
        }
        if (Store.view === 'contacts') UI.renderContactsPage();
    }

    function handleProfileRsp(data) {
        if (data.success && pendingProfile) {
            Store.me.nickname = pendingProfile.nickname || Store.me.nickname;
            Store.me.signature = pendingProfile.signature;
            if (pendingProfile.ppId) Store.me.pp_id = pendingProfile.ppId;
            if (pendingProfile.avatarUrl) Store.me.avatarUrl = pendingProfile.avatarUrl;
            pendingProfile = null;
            pendingAvatarUrl = '';
            UI.showApp();
            UI.renderMe();
            UI.renderSidebar();
            UI.closeModal();
            if (Store.view === 'me') UI.showMePage('detail');
        } else if (!data.success) {
            pendingProfile = null;
        }
        UI.toast(data.errMsg || (data.success ? '资料已更新' : '更新失败'));
    }

    function handleError(data) {
        UI.toast(data.message || '发生错误');
        if (Number(data.code) === 1001) forceLogout('账号已在其他窗口登录');
    }

    function handleServerMessage(event) {
        const type = event.type;
        const data = event.body || {};

        switch (type) {
            case MsgType.LOGIN_RSP:
                if (!data.success) {
                    clearTimeout(authTimer);
                    UI.setAuthBusy('login', false);
                    if (Store.me) forceLogout('登录已失效，请重新登录');
                    else UI.showAuthError('login', data.errMsg || '登录失败');
                    pendingAuth = null;
                    return;
                }
                onLoginSuccess(data);
                break;

            case MsgType.SESSION_LOGIN_RSP:
                sessionRestoring = false;
                if (!data.success) {
                    // 令牌过期/被撤销：清掉本地令牌，回到登录页（不弹刺眼错误）
                    try { localStorage.removeItem(TOKEN_KEY); } catch (err) { /* 忽略 */ }
                    if (Store.me) {
                        forceLogout('登录状态已失效，请重新登录');
                        return;
                    }
                    UI.setAuthBusy('login', false);
                    UI.showAuth();
                    UI.el.loginUsername.focus();
                    return;
                }
                onLoginSuccess(data);
                break;

            case MsgType.REGISTER_RSP:
                UI.setAuthBusy('register', false);
                if (!data.success) {
                    UI.showAuthError('register', data.errMsg || '注册失败');
                    return;
                }
                if (pendingAuth) {
                    const username = pendingAuth.username;
                    const password = pendingAuth.password;
                    UI.setAuthTab('login');
                    UI.el.loginUsername.value = username;
                    UI.el.loginPassword.value = password;
                    pendingAuth = { kind: 'login', username, password };
                    UI.setAuthBusy('login', true);
                    UI.toast('注册成功，你的 pp 号是 ' + (data.ppId || '') + '，正在登录…', 3200);
                    client.send(MsgType.LOGIN_REQ, { username, password });
                }
                break;

            case MsgType.ONLINE_USERS_PUSH:          handleOnlineUsers(data); break;
            case MsgType.CONVERSATION_LIST_PUSH:     handleConversationList(data); break;
            case MsgType.CHAT_PRIVATE:               handleIncomingPrivate(data); break;
            case MsgType.CHAT_GROUP:                 handleIncomingGroup(data); break;
            case MsgType.CHAT_ACK:                   handlePrivateAck(data); break;
            case MsgType.CHAT_GROUP_ACK:             handleGroupAck(data); break;
            case MsgType.HISTORY_RSP:                handlePrivateHistory(data); break;
            case MsgType.GROUP_HISTORY_RSP:          handleGroupHistory(data); break;
            case MsgType.UPLOAD_TICKET_RSP:          handleUploadTicket(data); break;
            case MsgType.FAVORITE_ADD_RSP:           handleFavoriteAddRsp(data); break;
            case MsgType.FAVORITE_LIST_RSP:          handleFavoriteListRsp(data); break;
            case MsgType.FAVORITE_REMOVE_RSP:        handleFavoriteRemoveRsp(data); break;
            case MsgType.RECALL_RSP:                 handleRecallRsp(data); break;
            case MsgType.RECALL_PUSH:                handleRecallPush(data); break;
            case MsgType.PAT_RSP:                    handlePatRsp(data); break;
            case MsgType.PAT_PUSH:                   handlePatPush(data); break;
            case MsgType.FEED_RSP:                   handleFeedRsp(data); break;
            case MsgType.POST_CREATE_RSP:            handlePostCreated(data); break;
            case MsgType.POST_LIKE_RSP:              handlePostLikeRsp(data); break;
            case MsgType.COMMENT_CREATE_RSP:         handleCommentRsp(data); break;
            case MsgType.POST_DELETE_RSP:            handlePostDeleted(data); break;
            case MsgType.POST_PUSH:                  handlePostPush(data); break;
            case MsgType.POST_NOTICE:                handlePostNotice(data); break;
            case MsgType.TYPING:                     handleTyping(data); break;
            case MsgType.FRIEND_LIST_PUSH:           handleFriendList(data); break;
            case MsgType.GROUP_LIST_PUSH:            handleGroupList(data); break;
            case MsgType.FRIEND_REQUEST_LIST_RSP:    handleRequests(data); break;
            case MsgType.FRIEND_REQUEST_PUSH:        handleRequestPush(data); break;
            case MsgType.GROUP_CREATE_RSP:           handleGroupCreateRsp(data); break;
            case MsgType.GROUP_MEMBER_LIST_RSP:      handleMemberList(data); break;
            case MsgType.PROFILE_UPDATE_RSP:         handleProfileRsp(data); break;
            case MsgType.FRIEND_SEARCH_RSP:
                if (pendingDeepLink) handleDeepLinkResult(data);
                else renderSearchResults(data.users || []);
                break;

            case MsgType.FRIEND_REQUEST_RSP:
            case MsgType.FRIEND_REQUEST_HANDLE_RSP:
            case MsgType.GROUP_INVITE_RSP:
            case MsgType.GROUP_QUIT_RSP:
                UI.toast(data.errMsg || (data.success ? '操作成功' : '操作失败'));
                if (type === MsgType.GROUP_QUIT_RSP && data.success) {
                    setTimeout(() => {
                        if (Store.activeKey && Store.parseKey(Store.activeKey).kind === 'group'
                            && !Store.conversations.has(Store.activeKey)) {
                            UI.setActiveKey(null);
                        }
                    }, 400);
                }
                break;

            case MsgType.ERROR_PUSH:                 handleError(data); break;
            case MsgType.HEARTBEAT:                  break;
            default:
                console.debug('[app] 未处理的消息类型:', type, data);
                break;
        }
    }

    // -------------------------------------------------------------------------
    //  弹窗：加好友 / 好友申请 / 群组
    // -------------------------------------------------------------------------

    // -------------------------------------------------------------------------
    //  我（个人中心）
    // -------------------------------------------------------------------------
    function handleMeAction(action) {
        switch (action) {
            case 'detail':      UI.showMePage('detail'); break;
            case 'back':        UI.showMePage('me'); break;
            case 'qr':          UI.showMePage('qr'); renderMyQr(); break;
            case 'close-edit':  UI.showMePage('detail'); break;
            case 'edit-avatar':
                // 详情页点头像：直接进编辑页并弹出选图（点击本身是用户手势，不会被拦）
                openEditProfile();
                UI.el.avatarFileInput.click();
                break;
            case 'edit-nickname':  openEditProfile('nickname'); break;
            case 'edit-ppid':      openEditProfile('ppId'); break;
            case 'edit-signature': openEditProfile('signature'); break;
            case 'security':    openSecurityModal(); break;
            case 'about':       openAboutModal(); break;
            case 'grants':      window.location.href = (window.BASE_PATH || '') + '/grants.html?from=' + encodeURIComponent(Store.view || 'me'); break;
            case 'logout':
                if (window.confirm('确定退出登录吗？')) forceLogout('');
                break;
            default: break;
        }
    }

    // -------------------------------------------------------------------------
    //  我的二维码（浏览器本地生成，服务端零开销）
    // -------------------------------------------------------------------------
    function qrLinkFor(ppId) {
        const base = window.BASE_PATH || '';
        // 用查询参数而不是 hash：部分 App 内置浏览器会丢掉 # 后面的内容
        return window.location.origin + base + '/?u=' + encodeURIComponent(ppId);
    }

    async function renderMyQr() {
        const me = Store.me;
        if (!me || !me.pp_id) return;
        if (!window.PPQR) return;

        const canvas = UI.el.meQrCanvas;
        if (!canvas) return;

        UI.el.qrLoading.hidden = false;
        UI.el.qrLoading.textContent = '二维码生成中…';

        try {
            await window.PPQR.render(canvas, qrLinkFor(me.pp_id), { size: 216 });
            UI.el.qrLoading.hidden = true;
        } catch (err) {
            UI.el.qrLoading.textContent = '二维码生成失败，可复制 pp 号';
        }
    }

    function saveQrImage() {
        const canvas = UI.el.meQrCanvas;
        if (!canvas || !canvas.width) {
            UI.toast('二维码还没生成好');
            return;
        }

        const ppId = (Store.me && Store.me.pp_id) || 'qrcode';

        try {
            // 用 dataURL 同步触发下载：保持在用户手势内，兼容性最好
            const link = document.createElement('a');
            link.href = canvas.toDataURL('image/png');
            link.download = 'pp-' + ppId + '.png';
            document.body.appendChild(link);
            link.click();
            link.remove();
            UI.toast('二维码图片已保存');
        } catch (err) {
            UI.toast('保存失败，请长按二维码保存');
        }
    }

    async function copyText(text, tip) {
        try {
            if (navigator.clipboard && window.isSecureContext) {
                await navigator.clipboard.writeText(text);
            } else {
                const box = document.createElement('textarea');
                box.value = text;
                box.style.position = 'fixed';
                box.style.opacity = '0';
                document.body.appendChild(box);
                box.select();
                document.execCommand('copy');
                box.remove();
            }
            UI.toast(tip || '已复制');
        } catch (err) {
            UI.toast('复制失败：' + text, 4000);
        }
    }

    // -------------------------------------------------------------------------
    //  扫码深链：#u=pp100009
    // -------------------------------------------------------------------------
    let pendingDeepLink = '';

    function parseDeepLink() {
        const search = window.location.search || '';
        const hash = (window.location.hash || '').replace(/^#/, '');

        let target = '';

        if (search) {
            target = new URLSearchParams(search).get('u') || '';
        }
        if (!target && hash) {
            target = new URLSearchParams(hash).get('u') || '';
        }

        if (target && /^[A-Za-z0-9_]{2,32}$/.test(target)) {
            // 先不清 URL：用户可能还没登录，登录后再处理
            pendingDeepLink = target;
        }
    }

    function clearDeepLink() {
        pendingDeepLink = '';
        if (window.history && window.history.replaceState) {
            window.history.replaceState(null, '', window.location.pathname);
        }
    }

    function tryDeepLink() {
        if (!pendingDeepLink || !Store.me) return;

        if (Store.me.pp_id &&
            pendingDeepLink.toLowerCase() === String(Store.me.pp_id).toLowerCase()) {
            clearDeepLink();
            UI.toast('这是你自己的二维码');
            return;
        }
        if (!client.isOpen()) return;
        client.send(MsgType.FRIEND_SEARCH_REQ, { keyword: pendingDeepLink });
    }

    function handleDeepLinkResult(data) {
        const target = pendingDeepLink;
        clearDeepLink();

        const users = data.users || [];
        const user = users.find((u) =>
            String(u.ppId || '').toLowerCase() === String(target).toLowerCase()) || users[0];

        if (!user) {
            UI.toast('没有找到 pp 号 ' + target, 3000);
            return;
        }
        openAddFriendCard(user);
    }

    function openAddFriendCard(user) {
        const body = UI.make('div');

        const card = UI.make('div', 'me-profile');
        card.style.marginBottom = '12px';

        const avatar = UI.makeAvatar(user.uid, user.nickname, false);
        avatar.style.width = '56px';
        avatar.style.height = '56px';
        avatar.style.fontSize = '22px';

        const main = UI.make('div', 'me-profile-main');
        main.appendChild(UI.make('div', 'me-name', user.nickname || '用户'));
        main.appendChild(UI.make('div', 'me-meta', 'pp号：' + (user.ppId || '—')));
        card.append(avatar, main);
        body.appendChild(card);

        body.appendChild(UI.make('p', 'me-footnote',
            user.signature || '扫码添加对方为好友，对方同意后即可聊天。'));

        const footer = UI.make('div');
        footer.style.display = 'flex';
        footer.style.gap = '10px';

        if (user.isFriend) {
            footer.append(
                button('关闭', 'btn btn-ghost', () => UI.closeModal()),
                button('发消息', 'btn btn-primary', () => {
                    UI.closeModal();
                    UI.setView('chats');
                    openKey(Store.keyOfPrivate(String(user.uid)));
                })
            );
        } else {
            footer.append(
                button('取消', 'btn btn-ghost', () => UI.closeModal()),
                button('添加好友', 'btn btn-primary', (event) => {
                    const btn = event.currentTarget;
                    btn.disabled = true;
                    btn.textContent = '已发送';
                    client.send(MsgType.FRIEND_REQUEST_REQ, {
                        toUid: toNumber(user.uid),
                        message: '我是 ' + (Store.me ? Store.me.nickname : '') + '，扫码加你',
                    });
                })
            );
        }

        UI.openModal('扫码加好友', { body, footer, kind: 'add-by-qr' });
    }

    function openSecurityModal() {
        const me = Store.me;
        if (!me) return;

        const body = UI.make('div');
        const rows = [
            ['登录账号', me.username || '—'],
            ['pp 号', me.pp_id || '未设置'],
            ['内部 UID', String(me.uid)],
        ];

        for (const [label, value] of rows) {
            const row = UI.make('div', 'me-row me-row-static');
            row.appendChild(UI.make('span', 'me-row-label', label));
            row.appendChild(UI.make('span', 'me-row-value', value));
            body.appendChild(row);
        }
        body.appendChild(UI.make('p', 'me-footnote',
            '密码使用加盐 SHA-256 存储，服务端也无法查看；如需改密请用新账号重注册。'));

        const footer = UI.make('div');
        footer.appendChild(button('关闭', 'btn btn-ghost', () => UI.closeModal()));
        UI.openModal('账号与安全', { body, footer, kind: 'security' });
    }

    function openAboutModal() {
        const body = UI.make('div');
        body.appendChild(UI.make('p', 'me-footnote',
            'pp 是一个可读源码的即时通讯项目：C++17 + Asio + Protobuf + SQLite，' +
            '前端为原生 HTML/CSS/JS，图片与文件直传腾讯云 COS。'));

        const rows = [
            ['版本', 'v1.0.0'],
            ['网页域名', 'pp.xiaojietong.online'],
            ['传输协议', 'WebSocket + Protobuf'],
        ];
        for (const [label, value] of rows) {
            const row = UI.make('div', 'me-row me-row-static');
            row.appendChild(UI.make('span', 'me-row-label', label));
            row.appendChild(UI.make('span', 'me-row-value', value));
            body.appendChild(row);
        }

        const footer = UI.make('div');
        footer.appendChild(button('好的', 'btn btn-primary', () => UI.closeModal()));
        UI.openModal('关于 pp', { body, footer, kind: 'about' });
    }

    function bindMe() {
        if (!UI.el.meView) return;

        UI.el.meView.addEventListener('click', (event) => {
            const node = event.target.closest('[data-me]');
            if (!node) return;
            handleMeAction(node.dataset.me);
        });

        UI.el.meProfile.addEventListener('keydown', (event) => {
            if (event.key === 'Enter' || event.key === ' ') {
                event.preventDefault();
                UI.showMePage('detail');
            }
        });

        UI.el.btnCopyPpid.addEventListener('click', () => {
            const ppId = Store.me && Store.me.pp_id;
            if (!ppId) {
                UI.toast('还没有设置 pp 号');
                return;
            }
            copyText(ppId, '已复制 pp 号：' + ppId);
        });
        UI.el.btnCopyQrLink.addEventListener('click', () => {
            if (!Store.me || !Store.me.pp_id) return;
            copyText(qrLinkFor(Store.me.pp_id), '已复制加好友链接');
        });
        UI.el.btnSaveQr.addEventListener('click', saveQrImage);
    }

    function renderSearchResults(users) {
        const box = document.getElementById('friend-search-results');
        if (!box) return;

        box.textContent = '';
        if (!users.length) {
            box.appendChild(UI.make('div', 'picker-empty', '没有找到用户'));
            return;
        }

        for (const user of users) {
            const row = UI.make('div', 'list-row');
            row.appendChild(UI.makeAvatar(user.uid, user.nickname, true));

            const main = UI.make('div', 'row-main');
            main.appendChild(UI.make('div', 'row-title', user.nickname));
            const sub = 'pp号：' + (user.ppId || '—') +
                (user.signature ? ' · ' + user.signature : '');
            main.appendChild(UI.make('div', 'row-sub', sub));
            row.appendChild(main);

            if (user.isFriend) {
                const tag = button('已是好友', 'btn btn-ghost btn-small');
                tag.disabled = true;
                row.appendChild(tag);
            } else {
                const add = button('添加', 'btn btn-primary btn-small', () => {
                    add.disabled = true;
                    add.textContent = '已申请';
                    client.send(MsgType.FRIEND_REQUEST_REQ, {
                        toUid: toNumber(user.uid),
                        message: '我是 ' + (Store.me ? Store.me.nickname : ''),
                    });
                });
                row.appendChild(add);
            }
            box.appendChild(row);
        }
    }

    function openAddFriendModal() {
        const body = UI.make('div');
        const search = inputEl('', '输入 pp 号 / 用户名 / 昵称');
        const results = UI.make('div', '');
        results.id = 'friend-search-results';

        body.append(field('搜索用户', search), results);

        search.addEventListener('input', () => {
            clearTimeout(searchDebounce);
            const keyword = search.value.trim();
            if (!keyword) {
                results.textContent = '';
                return;
            }
            searchDebounce = setTimeout(() => {
                client.send(MsgType.FRIEND_SEARCH_REQ, { keyword });
            }, 250);
        });

        UI.openModal('添加朋友', { body, kind: 'add-friend' });
        search.focus();
    }

    function openRequestsModal() {
        const body = UI.make('div');
        const render = () => {
            body.textContent = '';
            if (!Store.requests.length) {
                body.appendChild(UI.make('div', 'picker-empty', '暂无新的好友申请'));
                return;
            }

            for (const req of Store.requests) {
                const row = UI.make('div', 'list-row');
                row.appendChild(UI.makeAvatar(req.fromUid, req.fromNickname, true));

                const main = UI.make('div', 'row-main');
                main.appendChild(UI.make('div', 'row-title', req.fromNickname || '用户'));
                main.appendChild(UI.make('div', 'row-sub',
                    'pp号：' + (req.fromPpId || '—') + ' · ' + (req.message || '')));
                row.appendChild(main);

                const accept = button('接受', 'btn btn-primary btn-small', () => {
                    client.send(MsgType.FRIEND_REQUEST_HANDLE_REQ, {
                        requestId: toNumber(req.requestId),
                        accept: true,
                    });
                });
                const reject = button('拒绝', 'btn btn-ghost btn-small', () => {
                    client.send(MsgType.FRIEND_REQUEST_HANDLE_REQ, {
                        requestId: toNumber(req.requestId),
                        accept: false,
                    });
                });

                const actions = UI.make('div');
                actions.style.display = 'flex';
                actions.style.gap = '6px';
                actions.append(reject, accept);
                row.appendChild(actions);
                body.appendChild(row);
            }
        };

        UI.openModal('新的朋友', { body, kind: 'requests', refresh: render });
        render();
        client.send(MsgType.FRIEND_REQUEST_LIST_REQ, {});
    }

    function openCreateGroupModal() {
        const body = UI.make('div');
        const name = inputEl('', '群聊名称');
        const picker = UI.make('div', 'picker');
        const checks = [];

        const friends = Store.friendList();
        if (!friends.length) {
            picker.appendChild(UI.make('div', 'picker-empty', '还没有好友，先去添加好友吧'));
        }
        for (const user of friends) {
            const label = UI.make('label', 'picker-item');
            const check = document.createElement('input');
            check.type = 'checkbox';
            check.value = user.uid;
            label.append(UI.makeAvatar(user.uid, user.nickname, true), check,
                UI.make('span', '', user.nickname));
            picker.appendChild(label);
            checks.push(check);
        }

        body.append(field('群名称', name), field('选择成员', picker));

        const footer = UI.make('div');
        footer.style.display = 'flex';
        footer.style.gap = '10px';

        const create = button('创建', 'btn btn-primary', () => {
            const groupName = name.value.trim();
            if (!groupName) {
                UI.toast('请输入群名称');
                return;
            }
            const members = checks.filter((c) => c.checked).map((c) => toNumber(c.value));
            client.send(MsgType.GROUP_CREATE_REQ, {
                name: groupName,
                memberUids: members,
            });
        });

        footer.append(button('取消', 'btn btn-ghost', () => UI.closeModal()), create);
        UI.openModal('创建群聊', { body, footer, kind: 'create-group' });
        name.focus();
    }

    function openGroupMembersModal(gid) {
        const gidStr = String(gid);
        const body = UI.make('div');
        body.id = 'group-members-body';

        const render = () => {
            body.textContent = '';
            const members = Store.groupMembers.get(gidStr);
            if (!members) {
                body.appendChild(UI.make('div', 'picker-empty', '加载中…'));
                return;
            }

            for (const member of members) {
                const row = UI.make('div', 'list-row');
                const avatar = UI.makeAvatar(member.uid, member.nickname, true);
                avatar.appendChild(UI.make('span', 'online-dot' + (member.online ? ' on' : '')));
                row.appendChild(avatar);

                const main = UI.make('div', 'row-main');
                let title = member.nickname;
                if (Number(member.role) === 1) title += ' · 群主';
                if (Store.me && String(member.uid) === String(Store.me.uid)) title += ' · 我';
                main.appendChild(UI.make('div', 'row-title', title));
                main.appendChild(UI.make('div', 'row-sub',
                    'pp号：' + (member.ppId || '—') + (member.online ? ' · 在线' : ' · 离线')));
                row.appendChild(main);
                body.appendChild(row);
            }
        };

        const footer = UI.make('div');
        footer.style.display = 'flex';
        footer.style.gap = '10px';

        const group = Store.groups.get(gidStr);
        const isOwner = group && Store.me && String(group.ownerUid) === String(Store.me.uid);

        footer.append(
            button('邀请好友', 'btn btn-ghost', () => openInviteModal(gidStr)),
            button(isOwner ? '解散群聊' : '退出群聊', 'btn btn-danger', () => {
                const tip = isOwner ? '确定解散该群聊吗？消息会一并删除。' : '确定退出该群聊吗？';
                if (!window.confirm(tip)) return;
                client.send(MsgType.GROUP_QUIT_REQ, { gid: toNumber(gidStr) });
                UI.closeModal();
            })
        );

        UI.openModal('群成员', { body, footer, kind: 'members', refresh: render });
        render();
        client.send(MsgType.GROUP_MEMBER_LIST_REQ, { gid: toNumber(gidStr) });
    }

    function openInviteModal(gid) {
        const gidStr = String(gid);
        const members = Store.groupMembers.get(gidStr) || [];
        const memberIds = new Set(members.map((m) => String(m.uid)));
        const candidates = Store.friendList().filter((u) => !memberIds.has(u.uid));

        const body = UI.make('div');
        const picker = UI.make('div', 'picker');
        const checks = [];

        if (!candidates.length) {
            picker.appendChild(UI.make('div', 'picker-empty', '所有好友都已经在群里了'));
        }
        for (const user of candidates) {
            const label = UI.make('label', 'picker-item');
            const check = document.createElement('input');
            check.type = 'checkbox';
            check.value = user.uid;
            label.append(UI.makeAvatar(user.uid, user.nickname, true), check,
                UI.make('span', '', user.nickname));
            picker.appendChild(label);
            checks.push(check);
        }
        body.appendChild(picker);

        const footer = UI.make('div');
        footer.style.display = 'flex';
        footer.style.gap = '10px';
        const invite = button('邀请', 'btn btn-primary', () => {
            const ids = checks.filter((c) => c.checked).map((c) => toNumber(c.value));
            if (!ids.length) {
                UI.toast('请选择要邀请的好友');
                return;
            }
            client.send(MsgType.GROUP_INVITE_REQ, { gid: toNumber(gidStr), memberUids: ids });
            UI.closeModal();
        });

        footer.append(button('取消', 'btn btn-ghost', () => UI.closeModal()), invite);
        UI.openModal('邀请好友', { body, footer, kind: 'invite' });
    }

    function openUserProfileModal(uid) {
        const user = Store.userOf(uid);
        if (!user) return;

        const body = UI.make('div');

        const card = UI.make('div', 'me-profile');
        card.style.marginBottom = '12px';

        const avatar = UI.makeAvatar(user.uid, user.nickname, false);
        avatar.style.width = '56px';
        avatar.style.height = '56px';
        avatar.style.fontSize = '22px';

        const main = UI.make('div', 'me-profile-main');
        main.appendChild(UI.make('div', 'me-name', user.nickname || '用户'));
        main.appendChild(UI.make('div', 'me-meta', 'pp号：' + (user.pp_id || '—')));
        card.append(avatar, main);
        body.appendChild(card);

        const group = UI.make('div', 'me-group');
        const rows = [
            ['个性签名', user.signature || '这个人很懒，什么都没写'],
            ['状态', user.online ? '在线' : '离线'],
        ];
        for (const [label, value] of rows) {
            const row = UI.make('div', 'me-row me-row-static');
            row.appendChild(UI.make('span', 'me-row-label', label));
            row.appendChild(UI.make('span', 'me-row-value', value));
            group.appendChild(row);
        }
        body.appendChild(group);

        const actionGroup = UI.make('div', 'me-group');
        const feedRow = UI.make('button', 'me-row');
        feedRow.type = 'button';
        feedRow.appendChild(UI.make('span', 'me-row-label', 'TA 的动态'));
        feedRow.appendChild(UI.make('span', 'chev', '›'));
        feedRow.addEventListener('click', () => {
            UI.closeModal();
            openUserFeed(user.uid, user.nickname);
        });
        actionGroup.appendChild(feedRow);
        body.appendChild(actionGroup);

        const footer = UI.make('div');
        footer.style.display = 'flex';
        footer.style.gap = '10px';
        footer.append(
            button('关闭', 'btn btn-ghost', () => UI.closeModal()),
            button('发消息', 'btn btn-primary', () => {
                UI.closeModal();
                openKey(Store.keyOfPrivate(String(user.uid)));
            })
        );
        UI.openModal('用户资料', { body, footer, kind: 'user-profile' });
    }

    function openChatInfo() {
        if (!Store.activeKey) return;
        const info = Store.parseKey(Store.activeKey);
        if (info.kind === 'group') openGroupMembersModal(info.id);
        else openUserProfileModal(info.id);
    }

    // -------------------------------------------------------------------------
    //  事件绑定
    // -------------------------------------------------------------------------
    function bindAuth() {
        document.querySelectorAll('[data-auth-tab]').forEach((node) => {
            node.addEventListener('click', (event) => {
                event.preventDefault();
                setAuthTab(node.dataset.authTab);
            });
        });

        UI.el.loginForm.addEventListener('submit', (event) => {
            event.preventDefault();
            submitLogin();
        });
        UI.el.registerForm.addEventListener('submit', (event) => {
            event.preventDefault();
            submitRegister();
        });
    }

    function bindSidebar() {
        UI.el.railButtons.forEach((btn) => {
            btn.addEventListener('click', () => {
                UI.setView(btn.dataset.view);
                if (btn.dataset.view === 'feed' && Store.me) {
                    feedUnread = 0;
                    UI.setFeedBadge(0);
                    loadFeed();
                }
                if (btn.dataset.view === 'favorites' && Store.me) {
                    requestFavorites();
                }
            });
        });

        // 长按消息 → 菜单；收藏夹里删除收藏 / 转发收藏
        UI.onMessageLongPress = showMessageMenu;
        UI.onAvatarPat = sendPat;
        UI.onFavoriteRemove = removeFavorite;
        UI.onFavoriteForward = forwardFavorite;

        UI.el.btnNewChat.addEventListener('click', () => UI.setView('contacts'));

        UI.el.searchInput.addEventListener('input', () => {
            Store.filter = UI.el.searchInput.value;
            UI.renderSidebar();
        });

        UI.el.convList.addEventListener('click', (event) => {
            const item = event.target.closest('.conv-item');
            if (!item) return;
            openKey(item.dataset.key);
        });
    }

    // -------------------------------------------------------------------------
    //  联系人页
    // -------------------------------------------------------------------------
    function bindContacts() {
        document.getElementById('btn-contact-add').addEventListener('click', openAddFriendModal);
        document.getElementById('btn-contact-group').addEventListener('click', openCreateGroupModal);

        document.querySelectorAll('.contacts-tab').forEach((btn) => {
            btn.addEventListener('click', () => UI.setContactsTab(btn.dataset.contactTab));
        });

        UI.el.contactsList.addEventListener('click', (event) => {
            const reqBtn = event.target.closest('[data-req-action]');
            if (reqBtn) {
                const requestId = Number(reqBtn.dataset.reqId) || 0;
                if (!requestId) return;
                client.send(MsgType.FRIEND_REQUEST_HANDLE_REQ, {
                    requestId,
                    accept: reqBtn.dataset.reqAction === 'accept',
                });
                reqBtn.disabled = true;
                return;
            }

            const row = event.target.closest('.contact-row');
            if (!row) return;
            UI.selectContact(row.dataset.contactKind, row.dataset.contactId);

            if (row.dataset.contactKind === 'group') {
                const gid = String(row.dataset.contactId);
                if (!Store.groupMembers.has(gid)) {
                    client.send(MsgType.GROUP_MEMBER_LIST_REQ, { gid: toNumber(gid) });
                }
            }
        });

        UI.el.contactsDetailPanel.addEventListener('click', (event) => {
            if (event.target.closest('#btn-contact-back')) {
                UI.closeContactDetail();
                return;
            }

            const node = event.target.closest('[data-detail-action]');
            if (!node) return;

            const action = node.dataset.detailAction;
            const id = node.dataset.detailId;

            if (action === 'message') {
                UI.setView('chats');
                openKey(Store.keyOfPrivate(id));
                return;
            }
            if (action === 'user-feed') {
                openUserFeed(id, node.dataset.detailName || '');
                return;
            }
            if (action === 'open-group') {
                UI.setView('chats');
                openKey(Store.keyOfGroup(id));
                return;
            }
            if (action === 'invite') {
                openInviteModal(id);
                return;
            }
            if (action === 'quit-group') {
                if (!window.confirm('确定退出这个群聊吗？')) return;
                client.send(MsgType.GROUP_QUIT_REQ, { gid: toNumber(id) });
            }
        });
    }

    // ---- 语音输入（微信式：语音 / 键盘切换 + 按住说话 +「说完转成文字」）----
    let voiceMode = false;
    let voicePressing = false;
    let voiceStarted = false;
    let voiceStartX = 0;
    let voiceStartY = 0;
    // 手势结果：send=松手直接发语音条；text=松手先转文字；cancel=松手丢弃
    let voiceGesture = 'send';
    let voiceTimer = null;
    let voiceBusy = false;

    // 录音中的手势提示文案
    const TIP_SEND = '松开发送';
    const TIP_TEXT = '松开转文字';
    const TIP_CANCEL = '松开取消';

    /** 高亮左（转文字）/ 右（取消）半区。 */
    function paintVoiceZones(mode) {
        if (UI.el.voiceZoneText) {
            UI.el.voiceZoneText.classList.toggle('active', mode === 'text');
        }
        if (UI.el.voiceZoneCancel) {
            UI.el.voiceZoneCancel.classList.toggle('active', mode === 'cancel');
        }
    }

    /** 语音模式下录音浮层上的计时器。 */
    function startVoiceTimer() {
        const tick = () => {
            const secs = Math.floor(Voice.elapsedSeconds());
            if (UI.el.voiceRecTimer) {
                UI.el.voiceRecTimer.textContent = secs + ' ″';
            }
        };
        tick();
        clearInterval(voiceTimer);
        voiceTimer = setInterval(tick, 200);
    }

    function stopVoiceTimer() {
        clearInterval(voiceTimer);
        voiceTimer = null;
    }

    /** 切换「键盘 / 语音」输入模式（微信左下角那个按钮）。 */
    function setVoiceMode(on) {
        voiceMode = on;
        UI.el.input.hidden = on;
        UI.el.btnVoiceHold.hidden = !on;
        UI.el.btnSend.hidden = on;
        UI.el.btnVoiceToggle.classList.toggle('is-on', on);
        UI.el.btnVoiceToggle.title = on ? '切换到键盘输入' : '切换语音输入';
        // 图标由 CSS 根据 .is-on 切换（ico-mic / ico-keyboard），不再写 emoji
        if (!on) {
            Voice.cancel();
            stopVoiceTimer();
            UI.el.voiceRecording.hidden = true;
            voicePressing = false;
        } else {
            UI.el.input.blur();
        }
    }

    /** 录制完成：要么作为语音条发出，要么先转文字再发文字。 */
    async function deliverVoiceClip(clip, wantText) {
        if (!clip || voiceBusy) return;

        // 空语音（全程静音 / 只有麦克风底噪）：不发语音条，也不浪费一次转写
        if (clip.silent) {
            UI.toast('没有检测到声音，请重新录制');
            return;
        }

        if (!wantText) {
            const name = 'voice-' + clip.seconds + 's.' + clip.ext;
            const file = new File([clip.blob], name, { type: clip.blob.type || 'audio/webm' });
            startUpload(file);
            return;
        }

        voiceBusy = true;
        UI.toast('正在转文字…', 4000);
        try {
            const result = await Voice.transcribe(clip.blob, clip.ext);
            if (result.speechDetected === false) {
                UI.toast('没有检测到声音，请重新录制');
                return;
            }
            if (!result.text) {
                UI.toast('没有听清，请再说一次');
                return;
            }
            // 转写成功后直接当文字消息发出（与微信「语音转文字」一致）
            UI.el.input.value = result.text;
            sendMessage();
        } catch (err) {
            UI.toast((err && err.message) || '语音转文字失败，请重试', 3600);
        } finally {
            voiceBusy = false;
        }
    }

    function bindVoiceInput() {
        if (!UI.el.btnVoiceToggle || !UI.el.btnVoiceHold) return;

        if (!Voice.isSupported()) {
            UI.el.btnVoiceToggle.classList.add('is-off');
            UI.el.btnVoiceToggle.title = '当前浏览器不支持录音';
            UI.el.btnVoiceToggle.addEventListener('click', () => {
                UI.toast('当前浏览器不支持录音，请使用新版 Chrome / Edge / Safari');
            });
            return;
        }

        UI.el.btnVoiceToggle.addEventListener('click', () => setVoiceMode(!voiceMode));

        const hold = UI.el.btnVoiceHold;

        const resetHoldUi = () => {
            stopVoiceTimer();
            UI.el.voiceRecording.hidden = true;
            paintVoiceZones('send');
            hold.textContent = '按住 说话';
            hold.classList.remove('is-recording', 'is-cancel', 'is-text');
            voicePressing = false;
            voiceStarted = false;
            voiceGesture = 'send';
        };

        hold.addEventListener('pointerdown', async (event) => {
            if (voicePressing || voiceBusy) return;
            event.preventDefault();
            voicePressing = true;
            voiceStarted = false;
            voiceStartX = event.clientX;
            voiceStartY = event.clientY;
            voiceGesture = 'send';
            hold.textContent = '正在准备麦克风…';
            hold.classList.add('is-recording');

            const ok = await Voice.start((clip) => {
                // 到达 60 秒上限自动结束，直接发语音条
                resetHoldUi();
                deliverVoiceClip(clip, false);
            });
            if (!ok) {
                resetHoldUi();
                UI.toast('无法访问麦克风，请在地址栏允许麦克风权限');
                return;
            }
            if (!voicePressing) {
                // 麦克风就绪前用户已经松手：丢掉这段录音，不报错
                Voice.cancel();
                resetHoldUi();
                return;
            }
            voiceStarted = true;
            hold.textContent = '松开 结束';
            UI.el.voiceRecording.hidden = false;
            UI.el.voiceRecTip.textContent = TIP_SEND;
            paintVoiceZones('send');
            startVoiceTimer();
            if (hold.setPointerCapture) {
                try { hold.setPointerCapture(event.pointerId); } catch (err) { /* 忽略 */ }
            }
        });

        hold.addEventListener('pointermove', (event) => {
            if (!voicePressing) return;

            // 先要往上滑过一小段，再看左右方向：左=转文字，右=取消
            const up = voiceStartY - event.clientY;
            const dx = event.clientX - voiceStartX;
            let next = 'send';
            if (up > 40) {
                if (dx < -40) next = 'text';
                else if (dx > 40) next = 'cancel';
            }

            if (next === voiceGesture) return;
            voiceGesture = next;
            hold.classList.toggle('is-cancel', next === 'cancel');
            hold.classList.toggle('is-text', next === 'text');
            paintVoiceZones(next);
            UI.el.voiceRecTip.textContent = next === 'cancel' ? TIP_CANCEL
                : next === 'text' ? TIP_TEXT : TIP_SEND;
        });

        const finishHold = async () => {
            if (!voicePressing) return;
            const gesture = voiceGesture;
            const wasStarted = voiceStarted;
            voicePressing = false;
            hold.classList.remove('is-cancel', 'is-text');
            hold.textContent = '按住 说话';
            paintVoiceZones('send');

            if (!wasStarted) {
                // 麦克风还没就绪就松手了：静默收尾，不报「说话太短」
                resetHoldUi();
                return;
            }

            if (gesture === 'cancel') {
                Voice.cancel();
                resetHoldUi();
                UI.toast('已取消');
                return;
            }

            const clip = await Voice.stop();
            resetHoldUi();
            if (!clip) {
                UI.toast('说话时间太短，请按住说完整一句');
                return;
            }
            deliverVoiceClip(clip, gesture === 'text');
        };

        hold.addEventListener('pointerup', finishHold);
        hold.addEventListener('pointercancel', () => {
            Voice.cancel();
            resetHoldUi();
        });
        // 防止长按弹出系统菜单 / 选中文本
        hold.addEventListener('contextmenu', (event) => event.preventDefault());
    }

    function bindChat() {
        UI.el.btnSend.addEventListener('click', sendMessage);
        UI.el.fileInput.addEventListener('change', onFileChosen);

        // 微信式「+」面板：展开时输入栏整体上移，下方露出功能格
        UI.el.btnMore.addEventListener('click', (event) => {
            event.stopPropagation();
            setMorePanel(!(UI.el.morePanel && !UI.el.morePanel.hidden));
        });
        UI.el.morePanel.addEventListener('click', (event) => {
            const btn = event.target.closest('[data-more]');
            if (!btn) return;
            const what = btn.dataset.more;
            setMorePanel(false);
            if (what === 'image') pickFile('image');
            else if (what === 'file') pickFile('file');
            else if (what === 'voice') setVoiceMode(true);
            else if (what === 'favorites' || what === 'favorite-list') UI.setView('favorites');
        });

        bindVoiceInput();

        UI.el.input.addEventListener('input', () => {
            autoResizeInput();

            if (!Store.activeKey) return;
            const info = Store.parseKey(Store.activeKey);
            if (info.kind !== 'private' || !client.isOpen()) return;

            const now = Date.now();
            if (now - lastTypingSentAt > 2000) {
                client.send(MsgType.TYPING, { toUid: toNumber(info.id), typing: true });
                lastTypingSentAt = now;
            }
            clearTimeout(typingStopTimer);
            typingStopTimer = setTimeout(() => {
                client.send(MsgType.TYPING, { toUid: toNumber(info.id), typing: false });
                lastTypingSentAt = 0;
            }, 2500);
        });

        UI.el.input.addEventListener('keydown', (event) => {
            if (event.key === 'Enter' && !event.shiftKey) {
                event.preventDefault();
                sendMessage();
            }
        });

        UI.el.btnBack.addEventListener('click', () => UI.closeMobileChat());
        UI.el.btnChatInfo.addEventListener('click', openChatInfo);

        UI.el.btnLoadMore.addEventListener('click', () => {
            const key = Store.activeKey;
            if (!key) return;
            const list = Store.messages.get(key) || [];
            const first = list.find((m) => m.id);
            if (!first) return;
            requestHistory(key, Number(first.id));
        });

        UI.el.btnLogout.addEventListener('click', () => {
            if (!window.confirm('确定退出登录吗？')) return;
            forceLogout('');
        });
        UI.el.modalClose.addEventListener('click', () => UI.closeModal());
        document.querySelectorAll('[data-modal-close]').forEach((node) => {
            node.addEventListener('click', () => UI.closeModal());
        });
        document.addEventListener('keydown', (event) => {
            if (event.key === 'Escape') UI.closeModal();
        });

        document.addEventListener('visibilitychange', () => {
            if (document.visibilityState !== 'visible') return;
            const key = Store.activeKey;
            if (!key || !Store.me) return;

            const info = Store.parseKey(key);
            const conv = Store.conversations.get(key);
            if (conv && conv.unread > 0) {
                conv.unread = 0;
                if (client.isOpen()) {
                    if (info.kind === 'group') client.send(MsgType.GROUP_READ_REQ, { gid: toNumber(info.id) });
                    else client.send(MsgType.READ_REQ, { peerUid: toNumber(info.id) });
                }
                UI.renderSidebar();
            }
            if (Store.activeKey === key) UI.renderMessages({ forceBottom: true });
        });

        renderEmojiPanel();

        for (const tab of document.querySelectorAll('.emoji-tab')) {
            tab.addEventListener('click', (event) => {
                event.stopPropagation();
                setEmojiTab(tab.dataset.tab);
            });
        }

        UI.el.quoteBarClose.addEventListener('click', () => clearQuote());

        UI.el.btnEmoji.addEventListener('click', (event) => {
            event.stopPropagation();
            const willOpen = UI.el.emojiPanel.hidden;
            setMorePanel(false);
            UI.el.emojiPanel.hidden = !willOpen;
            if (willOpen) renderEmojiPanel();
        });

        document.addEventListener('click', (event) => {
            if (!UI.el.emojiPanel.hidden &&
                !UI.el.emojiPanel.contains(event.target) &&
                event.target !== UI.el.btnEmoji &&
                !UI.el.btnEmoji.contains(event.target)) {
                UI.el.emojiPanel.hidden = true;
            }
            if (UI.el.morePanel && !UI.el.morePanel.hidden &&
                !UI.el.morePanel.contains(event.target) &&
                event.target !== UI.el.btnMore &&
                !UI.el.btnMore.contains(event.target)) {
                setMorePanel(false);
            }
        });
    }

    /** 展开/收起「+」更多面板（展开时联动收起表情面板）。 */
    function setMorePanel(open) {
        if (!UI.el.morePanel) return;
        UI.el.morePanel.hidden = !open;
        UI.el.btnMore.classList.toggle('is-on', !!open);
        if (open) UI.el.emojiPanel.hidden = true;
        // 展开时滚到底，避免新消息被面板遮住
        const box = UI.el.messages;
        if (open && box) box.scrollTop = box.scrollHeight;
    }

    function bindSocket() {
        client.on('open', () => {
            UI.setConnected(true);

            let saved = '';
            try { saved = localStorage.getItem(TOKEN_KEY) || ''; } catch (err) { saved = ''; }

            if (pendingAuth && pendingAuth.kind === 'login') {
                client.send(MsgType.LOGIN_REQ, {
                    username: pendingAuth.username,
                    password: pendingAuth.password,
                });
                return;
            }

            // 断线重连：优先用刚登录时记住的账号密码（不用碰令牌）
            if (Store.me && Store.credentials) {
                silentRelogin = true;
                client.send(MsgType.LOGIN_REQ, {
                    username: Store.credentials.username,
                    password: Store.credentials.password,
                });
                return;
            }

            // 冷启动 / 仅持有令牌：静默恢复，不要求重新输密码
            if (saved) {
                silentRelogin = true;
                sessionRestoring = true;
                client.send(MsgType.SESSION_LOGIN_REQ, { token: saved });
            }
        });

        client.on('close', (info) => {
            UI.setConnected(false);
            if (!info.manual && Store.me) {
                UI.toast('连接已断开，正在重连…');
            }
        });

        client.on('reconnecting', (info) => {
            UI.el.connBannerText.textContent =
                '连接已断开，' + Math.round(info.delay / 1000) + ' 秒后重试（第 ' + info.attempt + ' 次）';
        });

        client.on('message', (event) => {
            if (event.type === MsgType.LOGIN_RSP && !event.body.success && silentRelogin) {
                silentRelogin = false;
                forceLogout('登录已失效，请重新登录');
                return;
            }
            handleServerMessage(event);
        });
    }

    // -------------------------------------------------------------------------
    //  启动
    // -------------------------------------------------------------------------
    document.addEventListener('DOMContentLoaded', () => {
        UI.init();

        // 从外部页（授权管理等）回来时用 ?view=me 指定要回到的 tab
        const wantedView = new URLSearchParams(location.search).get('view');
        if (wantedView && ['chats', 'contacts', 'feed', 'me'].indexOf(wantedView) !== -1) {
            pendingView = wantedView;
            try { window.history.replaceState(null, '', location.pathname); } catch (err) { /* 忽略 */ }
        }

        parseDeepLink();
        window.addEventListener('hashchange', () => {
            parseDeepLink();
            tryDeepLink();
        });
        bindAuth();
        bindSidebar();
        bindChat();
        bindMe();
        bindContacts();
        bindFeed();
        bindEditProfile();
        bindSocket();

        UI.setConnected(false);
        UI.setAuthTab('login');
        UI.setView('chats');
        UI.el.chatPane.hidden = true;
        UI.el.chatEmpty.hidden = false;
        autoResizeInput();

        client.connect();
        UI.el.loginUsername.focus();
    });
})();
