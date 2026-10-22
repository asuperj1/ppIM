// =============================================================================
//  web/js/ui.js
//  所有 DOM 渲染集中在这里；业务逻辑只调用 UI.xxx()。
// =============================================================================
(function (global) {
    'use strict';

    const COLORS = [
        '#3a7afe', '#7b5cff', '#f5a623', '#26c6a5', '#ef5b5b',
        '#5b8def', '#e26aa5', '#3fb27f', '#8d6e63', '#00a8cc',
    ];

    function colorOf(uid) {
        const key = String(uid || '');
        let hash = 0;
        for (let i = 0; i < key.length; i += 1) {
            hash = (hash * 31 + key.charCodeAt(i)) >>> 0;
        }
        return COLORS[hash % COLORS.length];
    }

    function initialOf(name) {
        const text = String(name || '?').trim();
        return text.length > 0 ? text.charAt(0).toUpperCase() : '?';
    }

    function startOfDay(ts) {
        const d = new Date(ts * 1000);
        d.setHours(0, 0, 0, 0);
        return d.getTime();
    }

    function pad2(n) {
        return n < 10 ? '0' + n : String(n);
    }

    function formatClock(ts) {
        const d = new Date(ts * 1000);
        return pad2(d.getHours()) + ':' + pad2(d.getMinutes());
    }

    function formatListTime(ts) {
        if (!ts) return '';
        const now = Date.now();
        const dayMs = 86400000;
        const today = startOfDay(now / 1000);
        const target = startOfDay(ts);

        if (target === today) return formatClock(ts);
        if (target === today - dayMs) return '昨天';
        if (target > today - 7 * dayMs) {
            return ['周日', '周一', '周二', '周三', '周四', '周五', '周六'][new Date(ts * 1000).getDay()];
        }
        const d = new Date(ts * 1000);
        return (d.getMonth() + 1) + '/' + d.getDate();
    }

    function formatDivider(ts) {
        const now = new Date();
        const dayMs = 86400000;
        const today = startOfDay(now.getTime() / 1000);
        const target = startOfDay(ts);
        const d = new Date(ts * 1000);

        if (target === today) return '今天';
        if (target === today - dayMs) return '昨天';
        if (d.getFullYear() === now.getFullYear()) {
            return (d.getMonth() + 1) + '月' + d.getDate() + '日';
        }
        return d.getFullYear() + '年' + (d.getMonth() + 1) + '月' + d.getDate() + '日';
    }

    function el(tag, className, text) {
        const node = document.createElement(tag);
        if (className) node.className = className;
        if (text !== undefined && text !== null) node.textContent = text;
        return node;
    }

    /** 内联 SVG 小图标（语音条等处用，避免和整套线条图标风格不一致的 emoji）。 */
    function svgIcon(inner, className) {
        const svg = document.createElementNS('http://www.w3.org/2000/svg', 'svg');
        svg.setAttribute('viewBox', '0 0 24 24');
        svg.setAttribute('aria-hidden', 'true');
        if (className) svg.setAttribute('class', className);
        svg.innerHTML = inner;
        return svg;
    }

    const MIC_SVG = '<rect x="9" y="2.6" width="6" height="11" rx="3"/>'
        + '<path d="M5 11.4a7 7 0 0 0 14 0"/>'
        + '<line x1="12" y1="18.4" x2="12" y2="21.6"/>'
        + '<line x1="8.6" y1="21.6" x2="15.4" y2="21.6"/>';

    function formatFileSize(bytes) {
        const size = Number(bytes) || 0;
        if (size < 1024) return size + ' B';
        if (size < 1024 * 1024) return (size / 1024).toFixed(1) + ' KB';
        return (size / (1024 * 1024)).toFixed(1) + ' MB';
    }

    function fileIconOf(name) {
        const ext = String(name || '').split('.').pop().toLowerCase();
        if (['zip', 'rar', '7z'].includes(ext)) return '🗜️';
        if (['doc', 'docx'].includes(ext)) return '📝';
        if (['xls', 'xlsx'].includes(ext)) return '📊';
        if (['ppt', 'pptx'].includes(ext)) return '📽️';
        if (ext === 'pdf') return '📕';
        return '📄';
    }

    function avatarUrlOf(uid) {
        const key = String(uid || '');
        if (Store.me && key === String(Store.me.uid)) return Store.me.avatarUrl || '';
        const user = Store.users.get(key);
        return (user && user.avatarUrl) || '';
    }

    function paintAvatar(node, uid, name, urlOverride) {
        if (!node) return;
        const url = urlOverride || avatarUrlOf(uid);
        node.textContent = '';

        if (url) {
            node.style.background = '#eeeeee';
            const img = document.createElement('img');
            img.src = url;
            img.alt = name || '';
            img.loading = 'lazy';
            node.appendChild(img);
            return;
        }

        node.style.background = 'linear-gradient(135deg, ' + colorOf(uid) + ', ' +
            colorOf(String(uid) + 'x') + ')';
        node.textContent = initialOf(name);
    }

    // ---------------------------- 语音消息 ----------------------------
    // 文件名约定 voice-<秒>s.<ext>：时长写在文件名里，不改协议就能渲染微信式语音条。
    const VOICE_NAME_RE = /^voice-(\d+)s\./i;

    function voiceSecondsOf(fileName) {
        const m = VOICE_NAME_RE.exec(String(fileName || ''));
        return m ? Math.max(1, Number(m[1])) : 0;
    }

    let voiceAudio = null;
    let voicePlayingBtn = null;

    function stopVoicePlayback() {
        if (voiceAudio) {
            try { voiceAudio.pause(); } catch (err) { /* 忽略 */ }
            voiceAudio.src = '';
            voiceAudio = null;
        }
        if (voicePlayingBtn) {
            voicePlayingBtn.classList.remove('playing');
            voicePlayingBtn = null;
        }
    }

    function toggleVoicePlayback(btn, url) {
        if (voicePlayingBtn === btn && voiceAudio && !voiceAudio.paused) {
            stopVoicePlayback();
            return;
        }
        stopVoicePlayback();

        const audio = new Audio(url);
        voiceAudio = audio;
        voicePlayingBtn = btn;
        btn.classList.add('playing');
        audio.addEventListener('ended', stopVoicePlayback);
        audio.addEventListener('error', function () {
            stopVoicePlayback();
            if (global.UI && global.UI.toast) {
                global.UI.toast('语音播放失败：当前浏览器可能不支持该音频格式');
            }
        });
        const p = audio.play();
        if (p && p.catch) {
            p.catch(function () { stopVoicePlayback(); });
        }
    }

    function buildVoiceBubble(msg) {
        const secs = voiceSecondsOf(msg.fileName) || 1;
        const bubble = el('div', 'bubble voice');
        const btn = el('div', 'voice-bubble');
        // 微信式：气泡宽度随语音时长增长
        btn.style.width = Math.min(230, Math.max(78, 66 + secs * 7)) + 'px';
        btn.appendChild(svgIcon(MIC_SVG, 'voice-ico-svg'));
        const wave = el('span', 'voice-wave');
        wave.appendChild(el('i'));
        wave.appendChild(el('i'));
        wave.appendChild(el('i'));
        btn.appendChild(wave);
        btn.appendChild(el('span', 'voice-sec', secs + '″'));
        btn.addEventListener('click', function () {
            toggleVoicePlayback(btn, msg.fileUrl);
        });
        bubble.appendChild(btn);
        return bubble;
    }

    function buildBubble(msg) {
        const quote = msg.quoteText
            ? buildQuoteBlock(msg.quoteNickname || '', msg.quoteText)
            : null;

        if (msg.msgKind === 2 && msg.fileUrl) {
            const bubble = el('div', 'bubble image');
            if (quote) bubble.appendChild(quote);
            const img = document.createElement('img');
            img.className = 'msg-image';
            img.src = msg.fileUrl;
            img.alt = msg.fileName || '图片';
            img.loading = 'lazy';
            img.addEventListener('click', function () {
                window.open(msg.fileUrl, '_blank', 'noopener');
            });
            bubble.appendChild(img);
            return bubble;
        }

        if (msg.msgKind === 3 && msg.fileUrl) {
            if (voiceSecondsOf(msg.fileName)) {
                const voiceBubble = buildVoiceBubble(msg);
                if (quote) voiceBubble.insertBefore(quote, voiceBubble.firstChild);
                return voiceBubble;
            }

            const bubble = el('div', 'bubble');
            if (quote) bubble.appendChild(quote);
            const card = el('a', 'msg-file');
            card.href = msg.fileUrl;
            card.target = '_blank';
            card.rel = 'noopener';
            card.appendChild(el('div', 'file-icon', fileIconOf(msg.fileName)));
            const info = el('div', 'file-info');
            info.appendChild(el('div', 'file-name', msg.fileName || '文件'));
            info.appendChild(el('div', 'file-size', formatFileSize(msg.fileSize)));
            card.appendChild(info);
            bubble.appendChild(card);
            return bubble;
        }

        const textBubble = el('div', 'bubble');
        if (quote) textBubble.appendChild(quote);
        textBubble.appendChild(document.createTextNode(msg.content));
        return textBubble;
    }

    /** 引用块：显示在气泡顶部（微信式）。 */
    function buildQuoteBlock(nickname, text) {
        const block = el('div', 'quote-block');
        if (nickname) block.appendChild(el('span', 'quote-name', nickname + '：'));
        block.appendChild(el('span', 'quote-text', text));
        return block;
    }

    // 双击头像拍一拍（微信式）。移动端没有 dblclick，用两次点击的间隔判断。
    let lastAvatarTap = { uid: '', at: 0 };

    function attachAvatarPat(avatarEl, msg) {
        avatarEl.addEventListener('click', function (event) {
            event.stopPropagation();
            const now = Date.now();
            const uid = String(msg.fromUid || '');
            if (lastAvatarTap.uid === uid && now - lastAvatarTap.at < 320) {
                lastAvatarTap = { uid: '', at: 0 };
                if (global.UI.onAvatarPat) global.UI.onAvatarPat(msg);
                return;
            }
            lastAvatarTap = { uid: uid, at: now };
        });
    }

    // 长按（移动端 480ms）或右键（桌面）打开消息菜单，具体菜单项由 app.js 构建
    let longPressTimer = null;
    let longPressFired = false;

    function attachMessageMenu(rowEl, msg, mine) {
        rowEl.addEventListener('contextmenu', function (event) {
            event.preventDefault();
            longPressFired = true;
            if (global.UI.onMessageLongPress) {
                global.UI.onMessageLongPress(msg, mine, event.clientX, event.clientY);
            }
        });

        rowEl.addEventListener('touchstart', function (event) {
            const touch = event.touches[0];
            if (!touch) return;
            longPressFired = false;
            clearTimeout(longPressTimer);
            longPressTimer = setTimeout(function () {
                longPressFired = true;
                if (global.UI.onMessageLongPress) {
                    global.UI.onMessageLongPress(msg, mine, touch.clientX, touch.clientY);
                }
            }, 480);
        }, { passive: true });

        const cancel = function () {
            clearTimeout(longPressTimer);
            longPressTimer = null;
        };
        rowEl.addEventListener('touchend', cancel);
        rowEl.addEventListener('touchmove', cancel);
        rowEl.addEventListener('touchcancel', cancel);
        // 长按已弹出菜单时，抑制随后的 click（避免误触图片/语音播放）
        rowEl.addEventListener('click', function (event) {
            if (longPressFired) {
                event.stopPropagation();
                longPressFired = false;
            }
        }, true);
    }

    // ---------------------------- 动态（朋友圈）----------------------------
    function formatRelative(ts) {
        const now = Date.now() / 1000;
        const diff = Math.max(0, now - (Number(ts) || 0));
        if (diff < 60) return '刚刚';
        if (diff < 3600) return Math.floor(diff / 60) + ' 分钟前';
        if (diff < 86400) return Math.floor(diff / 3600) + ' 小时前';
        if (diff < 86400 * 2) return '昨天';
        if (diff < 86400 * 7) return Math.floor(diff / 86400) + ' 天前';
        const d = new Date(Number(ts) * 1000);
        return (d.getMonth() + 1) + '月' + d.getDate() + '日';
    }

    function imageGridClass(count) {
        if (count <= 1) return 'n1';
        if (count === 2) return 'n2';
        if (count === 4) return 'n4';
        return 'n3';
    }

    function buildPostCard(post) {
        const card = el('article', 'post');
        card.dataset.postId = String(post.id);

        const avatar = el('div', 'post-avatar');
        avatar.dataset.postUser = String(post.uid);
        paintAvatar(avatar, post.uid, post.nickname || '?', post.avatarUrl || '');
        card.appendChild(avatar);

        const body = el('div', 'post-body');

        const head = el('div', 'post-head');
        const name = el('span', 'post-name', post.nickname || ('用户 ' + post.uid));
        name.dataset.postUser = String(post.uid);
        head.appendChild(name);
        head.appendChild(el('span', 'post-time', formatRelative(post.createdAt)));
        if (post.mine) {
            const del = el('button', 'post-del', '删除');
            del.type = 'button';
            del.dataset.postDelete = '1';
            head.appendChild(del);
        }
        body.appendChild(head);

        if (post.content) {
            body.appendChild(el('p', 'post-text', post.content));
        }

        const images = post.images || [];
        if (images.length > 0) {
            const grid = el('div', 'post-images ' + imageGridClass(images.length));
            for (const url of images) {
                const img = document.createElement('img');
                img.src = url;
                img.alt = '图片';
                img.loading = 'lazy';
                img.addEventListener('click', function () {
                    window.open(url, '_blank', 'noopener');
                });
                grid.appendChild(img);
            }
            body.appendChild(grid);
        }

        const actions = el('div', 'post-actions');

        const like = el('button', 'post-action' + (post.liked ? ' liked' : ''),
            (post.liked ? '❤️ ' : '🤍 ') + (post.likeCount || 0));
        like.type = 'button';
        like.dataset.postLike = '1';

        const comment = el('button', 'post-action', '💬 ' + (post.commentCount || 0));
        comment.type = 'button';
        comment.dataset.postComment = '1';

        actions.append(like, comment);
        body.appendChild(actions);

        const comments = post.comments || [];
        if (comments.length > 0) {
            const box = el('div', 'post-comments');
            for (const item of comments) {
                const line = el('div', 'post-comment');
                line.appendChild(el('b', '', (item.nickname || '用户') + '：'));
                line.appendChild(document.createTextNode(item.content || ''));
                box.appendChild(line);
            }
            body.appendChild(box);
        }

        const box = el('div', 'post-comment-box');
        box.hidden = true;
        const input = document.createElement('input');
        input.type = 'text';
        input.maxLength = 500;
        input.placeholder = '说点什么…';
        input.dataset.postCommentInput = '1';
        const send = el('button', 'btn btn-primary btn-small', '发送');
        send.type = 'button';
        send.dataset.postCommentSend = '1';
        box.append(input, send);
        body.appendChild(box);

        card.appendChild(body);
        return card;
    }

    const UI = {
        el: {},
        _modalRefresh: null,
        contactsTab: 'friends',
        contactsSelection: null,

        init() {
            const $ = (id) => document.getElementById(id);
            this.el = {
                authView: $('auth-view'),
                appView: $('app-view'),

                loginForm: $('login-form'),
                registerForm: $('register-form'),
                loginUsername: $('login-username'),
                loginPassword: $('login-password'),
                loginError: $('login-error'),
                loginSubmit: $('login-submit'),
                registerUsername: $('register-username'),
                registerNickname: $('register-nickname'),
                registerPassword: $('register-password'),
                registerPassword2: $('register-password2'),
                registerError: $('register-error'),
                registerSubmit: $('register-submit'),

                railUnread: $('rail-unread'),
                railFeedBadge: $('rail-feed-badge'),
                railButtons: Array.from(document.querySelectorAll('.rail-btn[data-view]')),
                btnLogout: $('btn-logout'),

                sidebarTitle: $('sidebar-title'),
                searchInput: $('search-input'),
                convList: $('conv-list'),
                listEmpty: $('list-empty'),
                listEmptyText: $('list-empty-text'),

                contactsView: $('contacts-view'),
                contactsList: $('contacts-list'),
                contactsEmpty: $('contacts-empty'),
                contactsEmptyText: $('contacts-empty-text'),
                contactsDetailEmpty: $('contacts-detail-empty'),
                contactsDetailPanel: $('contacts-detail-panel'),
                contactRequestBadge: $('contact-request-badge'),
                btnNewChat: $('btn-new-chat'),

                connDot: $('conn-dot'),
                connText: $('conn-text'),
                connBanner: $('conn-banner'),
                connBannerText: $('conn-banner-text'),

                chat: $('chat'),
                chatEmpty: $('chat-empty'),
                chatPane: $('chat-pane'),
                chatName: $('chat-name'),
                chatPresence: $('chat-presence'),
                chatSub: $('chat-sub'),
                chatUid: $('chat-uid'),
                btnBack: $('btn-back'),
                btnChatInfo: $('btn-chat-info'),

                messages: $('messages'),
                messageList: $('message-list'),
                loadMore: $('load-more'),
                btnLoadMore: $('btn-load-more'),
                typing: $('typing-indicator'),
                typingText: $('typing-text'),

                input: $('message-input'),
                btnSend: $('btn-send'),
                btnEmoji: $('btn-emoji'),
                btnMore: $('btn-more'),
                btnAttach: $('btn-attach'),
                fileInput: $('file-input'),
                emojiPanel: $('emoji-panel'),
                emojiGrid: $('emoji-grid'),
                morePanel: $('more-panel'),
                composer: $('composer'),
                quoteBar: $('quote-bar'),
                quoteBarName: $('quote-bar-name'),
                quoteBarText: $('quote-bar-text'),
                quoteBarClose: $('quote-bar-close'),
                btnVoiceToggle: $('btn-voice-toggle'),
                btnVoiceHold: $('btn-voice-hold'),
                voiceRecording: $('voice-recording'),
                voiceRecTimer: $('voice-rec-timer'),
                voiceRecTip: $('voice-rec-tip'),
                voiceZoneText: $('vr-text'),
                voiceZoneCancel: $('vr-cancel'),

                meView: $('me-view'),
                favView: $('fav-view'),
                favList: $('fav-list'),
                favCount: $('fav-count'),
                mePage: $('me-page'),
                meDetail: $('me-detail'),
                meQr: $('me-qr'),
                meProfile: $('me-profile'),
                meAvatar: $('me-avatar'),
                meName: $('me-name'),
                meMeta: $('me-meta'),
                meSign: $('me-sign'),
                meDetailAvatar: $('me-detail-avatar'),
                meDetailName: $('me-detail-name'),
                meDetailPpid: $('me-detail-ppid'),
                meDetailSign: $('me-detail-sign'),
                meQrAvatar: $('me-qr-avatar'),
                meQrName: $('me-qr-name'),
                meQrId: $('me-qr-id'),
                meQrCanvas: $('me-qrcode'),
                qrLoading: $('qr-loading'),
                btnSaveQr: $('btn-save-qr'),
                btnCopyQrLink: $('btn-copy-qrlink'),
                btnCopyPpid: $('btn-copy-ppid'),

                meEdit: $('me-edit'),
                meEditAvatar: $('me-edit-avatar'),
                btnChangeAvatar: $('btn-change-avatar'),
                avatarFileInput: $('avatar-file-input'),
                editNickname: $('edit-nickname'),
                editPpid: $('edit-ppid'),
                editSignature: $('edit-signature'),
                editNicknameCount: $('edit-nickname-count'),
                editPpidCount: $('edit-ppid-count'),
                editSignatureCount: $('edit-signature-count'),
                btnSaveProfile: $('btn-save-profile'),

                feedView: $('feed-view'),
                feedScroll: $('feed-scroll'),
                feedList: $('feed-list'),
                feedEmpty: $('feed-empty'),
                feedMore: $('feed-more'),
                btnFeedMore: $('btn-feed-more'),
                btnFeedRefresh: $('btn-feed-refresh'),

                userFeedView: $('user-feed-view'),
                userFeedScroll: $('user-feed-scroll'),
                userFeedList: $('user-feed-list'),
                userFeedEmpty: $('user-feed-empty'),
                userFeedMore: $('user-feed-more'),
                userFeedTitle: $('user-feed-title'),
                btnUserFeedBack: $('btn-user-feed-back'),
                btnUserFeedMore: $('btn-user-feed-more'),

                postComposeView: $('post-compose-view'),
                btnNewPost: $('btn-new-post'),
                btnComposeBack: $('btn-compose-back'),
                composeAvatar: $('compose-avatar'),
                postInput: $('post-input'),
                composeThumbs: $('compose-thumbs'),
                postCount: $('post-count'),
                btnPickImages: $('btn-pick-images'),
                btnPostSend: $('btn-post-send'),
                postFileInput: $('post-file-input'),

                toast: $('toast'),

                modalRoot: $('modal-root'),
                modalTitle: $('modal-title'),
                modalBody: $('modal-body'),
                modalFoot: $('modal-foot'),
                modalClose: $('modal-close'),
            };
        },

        // ------------------------------------------------------------------
        //  基础组件
        // ------------------------------------------------------------------
        make(tag, className, text) {
            return el(tag, className, text);
        },

        makeAvatar(uid, name, small, isGroup) {
            const node = el('div', 'avatar' + (small ? ' small' : ''));
            if (isGroup) {
                node.classList.add('group');
                node.style.background = 'linear-gradient(135deg, ' + colorOf(uid) + ', ' +
                    colorOf(String(uid) + 'x') + ')';
                node.textContent = initialOf(name);
                return node;
            }
            paintAvatar(node, uid, name);
            return node;
        },

        applyAvatar(node, uid, name, urlOverride) {
            paintAvatar(node, uid, name, urlOverride);
        },

        // ------------------------------------------------------------------
        //  连接状态 / toast
        // ------------------------------------------------------------------
        setConnected(connected) {
            const e = this.el;
            e.connDot.classList.toggle('on', connected);
            e.connDot.classList.toggle('off', !connected);
            e.connText.textContent = connected ? '已连接' : '连接中…';
            e.connBanner.hidden = connected;
            if (!connected) {
                e.connBannerText.textContent = '连接已断开，正在重连…';
            }
        },

        toast(message, duration) {
            const node = this.el.toast;
            node.textContent = message;
            node.hidden = false;
            requestAnimationFrame(() => node.classList.add('show'));
            clearTimeout(this._toastTimer);
            this._toastTimer = setTimeout(() => {
                node.classList.remove('show');
                setTimeout(() => { node.hidden = true; }, 220);
            }, duration || 2400);
        },

        // ------------------------------------------------------------------
        //  登录页
        // ------------------------------------------------------------------
        setAuthTab(name) {
            document.querySelectorAll('.auth-tab').forEach((btn) => {
                btn.classList.toggle('active', btn.dataset.authTab === name);
            });
            this.el.loginForm.hidden = name !== 'login';
            this.el.registerForm.hidden = name !== 'register';
            this.showAuthError('', '');
        },

        showAuthError(kind, message) {
            const target = kind === 'register' ? this.el.registerError : this.el.loginError;
            if (!message) {
                target.hidden = true;
                target.textContent = '';
                return;
            }
            target.hidden = false;
            target.textContent = message;
        },

        setAuthBusy(kind, busy) {
            const btn = kind === 'register' ? this.el.registerSubmit : this.el.loginSubmit;
            btn.disabled = busy;
            btn.textContent = busy
                ? (kind === 'register' ? '创建中…' : '登录中…')
                : (kind === 'register' ? '创建账号' : '登 录');
        },

        showApp() {
            this.el.authView.hidden = true;
            this.el.appView.hidden = false;
            this.renderMe();
        },

        showAuth() {
            this.el.appView.hidden = true;
            this.el.authView.hidden = false;
            this.el.chatPane.hidden = true;
            this.el.chatEmpty.hidden = false;
            this.closeModal();
        },

        // ------------------------------------------------------------------
        //  侧栏
        // ------------------------------------------------------------------
        setView(view) {
            Store.view = view;
            this.el.railButtons.forEach((btn) => {
                btn.classList.toggle('active', btn.dataset.view === view);
            });

            const meMode = view === 'me';
            const feedMode = view === 'feed';
            const contactsMode = view === 'contacts';
            const favMode = view === 'favorites';
            this.el.appView.classList.toggle('me-mode', meMode);
            this.el.appView.classList.toggle('feed-mode', feedMode);
            this.el.appView.classList.toggle('contacts-mode', contactsMode);
            this.el.appView.classList.toggle('fav-mode', favMode);
            this.el.meView.hidden = !meMode;
            this.el.feedView.hidden = !feedMode;
            this.el.contactsView.hidden = !contactsMode;
            if (this.el.favView) this.el.favView.hidden = !favMode;
            if (this.el.userFeedView) this.el.userFeedView.hidden = true;
            if (this.el.postComposeView) this.el.postComposeView.hidden = true;

            if (meMode) {
                this.showMePage('me');
                return;
            }
            if (feedMode) {
                return;
            }
            if (favMode) {
                this.renderFavorites();
                return;
            }
            if (contactsMode) {
                this.renderContactsPage();
                return;
            }

            this.el.sidebarTitle.textContent = '聊天';
            this.renderSidebar();
        },

        // ------------------------------------------------------------------
        //  收藏夹
        // ------------------------------------------------------------------
        renderFavorites() {
            if (!this.el.favList) return;
            const items = Store.favorites || [];
            const list = this.el.favList;
            list.textContent = '';

            if (this.el.favCount) {
                this.el.favCount.textContent = items.length ? (items.length + ' 条收藏') : '';
            }

            if (!items.length) {
                list.appendChild(el('div', 'fav-empty', '还没有收藏。长按一条消息，选择「收藏」即可保存到这里。'));
                return;
            }

            for (const item of items) {
                list.appendChild(this.buildFavoriteCard(item));
            }
        },

        buildFavoriteCard(item) {
            const card = el('div', 'fav-card');
            const kind = Number(item.msgKind) || 1;
            const isVoice = kind === 3 && /^voice-\d+s\./.test(item.fileName || '');

            if (kind === 2 && item.fileUrl) {
                const img = document.createElement('img');
                img.className = 'fav-thumb';
                img.src = item.fileUrl;
                img.alt = '图片';
                img.loading = 'lazy';
                img.addEventListener('click', () => window.open(item.fileUrl, '_blank', 'noopener'));
                card.appendChild(img);
            } else if (isVoice) {
                const thumb = el('div', 'fav-thumb');
                thumb.style.display = 'grid';
                thumb.style.placeItems = 'center';
                thumb.appendChild(svgIcon(MIC_SVG, 'fav-thumb-ico'));
                card.appendChild(thumb);
            }

            const main = el('div', 'fav-main');
            let text = item.content || '';
            if (kind === 3 && !isVoice) text = '[文件] ' + (item.fileName || '');
            if (isVoice) text = '[语音] ' + voiceSecondsOf(item.fileName) + '″';
            main.appendChild(el('div', 'fav-text', text));

            const meta = el('div', 'fav-meta');
            if (item.fromNickname) meta.appendChild(el('span', '', '来自 ' + item.fromNickname));
            const when = item.createdAt || item.timestamp;
            if (when) meta.appendChild(el('span', '', formatRelative(when)));
            main.appendChild(meta);
            card.appendChild(main);

            const actions = el('div', 'fav-actions');
            const forwardBtn = el('button', '', '转发');
            forwardBtn.type = 'button';
            forwardBtn.addEventListener('click', () => {
                if (global.UI.onFavoriteForward) global.UI.onFavoriteForward(item);
            });
            actions.appendChild(forwardBtn);
            if (item.fileUrl && kind !== 2) {
                const openBtn = el('button', '', '打开');
                openBtn.type = 'button';
                openBtn.addEventListener('click', () => window.open(item.fileUrl, '_blank', 'noopener'));
                actions.appendChild(openBtn);
            }
            const delBtn = el('button', '', '删除');
            delBtn.type = 'button';
            delBtn.addEventListener('click', () => {
                if (global.UI.onFavoriteRemove) global.UI.onFavoriteRemove(item.msgId);
            });
            actions.appendChild(delBtn);
            card.appendChild(actions);

            return card;
        },

        // ------------------------------------------------------------------
        //  动态
        // ------------------------------------------------------------------
        renderFeed(posts, hasMore) {
            if (!this.el.feedList) return;

            const list = this.el.feedList;
            list.textContent = '';

            const items = posts || [];
            this.el.feedEmpty.hidden = items.length > 0;
            for (const post of items) {
                list.appendChild(buildPostCard(post));
            }
            this.el.feedMore.hidden = !hasMore;
        },

        renderUserFeed(posts, hasMore) {
            if (!this.el.userFeedList) return;

            const list = this.el.userFeedList;
            list.textContent = '';

            const items = posts || [];
            this.el.userFeedEmpty.hidden = items.length > 0;
            for (const post of items) {
                list.appendChild(buildPostCard(post));
            }
            this.el.userFeedMore.hidden = !hasMore;
        },

        // ------------------------------------------------------------------
        //  我（个人中心）
        // ------------------------------------------------------------------
        showMePage(page) {
            const name = page || 'me';
            if (this.el.mePage) this.el.mePage.hidden = name !== 'me';
            if (this.el.meDetail) this.el.meDetail.hidden = name !== 'detail';
            if (this.el.meQr) this.el.meQr.hidden = name !== 'qr';
            if (this.el.meEdit) this.el.meEdit.hidden = name !== 'edit';
            this.renderMe();
        },

        renderMe() {
            const me = Store.me;
            if (!me || !this.el.meView) return;

            const name = me.nickname || me.pp_id || me.username || '我';
            const initial = initialOf(name);
            const gradient = 'linear-gradient(135deg, ' +
                colorOf(me.uid) + ', ' + colorOf(me.uid + 'x') + ')';
            const ppText = me.pp_id ? 'pp号：' + me.pp_id : '未设置 pp 号';
            const signText = me.signature || '';

            [this.el.meAvatar, this.el.meDetailAvatar, this.el.meQrAvatar, this.el.meEditAvatar].forEach((node) => {
                if (!node) return;
                paintAvatar(node, me.uid, name);
            });

            if (this.el.composeAvatar) {
                paintAvatar(this.el.composeAvatar, me.uid, name);
            }

            this.el.meName.textContent = name;
            this.el.meMeta.textContent = ppText;
            this.el.meSign.textContent = signText;
            this.el.meSign.hidden = signText.length === 0;

            this.el.meDetailName.textContent = name;
            this.el.meDetailPpid.textContent = me.pp_id || '未设置';
            this.el.meDetailSign.textContent = signText || '未填写';

            this.el.meQrName.textContent = name;
            this.el.meQrId.textContent = ppText;
        },

        renderSidebar() {
            if (Store.view === 'contacts') {
                this.renderContactsPage();
                return;
            }
            this.renderConversations();
            this.renderUnreadBadge();
        },

        renderUnreadBadge() {
            const total = Store.totalUnread();
            this.el.railUnread.hidden = total === 0;
            this.el.railUnread.textContent = total > 99 ? '99+' : String(total);
        },

        setFeedBadge(count) {
            if (!this.el.railFeedBadge) return;
            const n = Number(count) || 0;
            this.el.railFeedBadge.hidden = n === 0;
            this.el.railFeedBadge.textContent = n > 99 ? '99+' : String(n);
        },

        renderConversations() {
            const list = this.el.convList;
            list.textContent = '';

            const keyword = Store.filter.trim().toLowerCase();
            let items = Store.sortedConversations();
            if (keyword) {
                items = items.filter((c) => String(c.name).toLowerCase().includes(keyword));
            }

            if (items.length === 0) {
                this.el.listEmpty.hidden = false;
                this.el.listEmptyText.textContent = keyword
                    ? '没有匹配的聊天'
                    : '还没有聊天，去通讯录找好友或建个群吧';
                return;
            }
            this.el.listEmpty.hidden = true;

            for (const conv of items) {
                const item = el('div', 'conv-item' + (Store.activeKey === conv.key ? ' active' : ''));
                item.dataset.key = conv.key;

                const avatar = this.makeAvatar(
                    conv.kind === 'group' ? 'g' + conv.id : conv.id,
                    conv.name,
                    false,
                    conv.kind === 'group'
                );
                if (conv.kind === 'private') {
                    const dot = el('span', 'online-dot' + (conv.online ? ' on' : ''));
                    avatar.appendChild(dot);
                } else {
                    avatar.appendChild(el('span', 'group-badge', '群'));
                }

                const main = el('div', 'conv-main');
                const top = el('div', 'conv-top');
                top.append(
                    el('span', 'conv-name', conv.name),
                    el('span', 'conv-time', formatListTime(conv.lastTime))
                );

                const previewText = conv.kind === 'group' && conv.lastSender
                    ? conv.lastSender + '：' + (conv.lastContent || '')
                    : (conv.lastContent || '开始聊天吧');
                const preview = el('div', 'conv-preview', previewText);

                main.append(top, preview);
                item.append(avatar, main);

                if (conv.unread > 0) {
                    item.appendChild(el('span', 'unread-badge', conv.unread > 99 ? '99+' : String(conv.unread)));
                }
                list.appendChild(item);
            }
        },

        // ------------------------------------------------------------------
        //  联系人页（好友 / 群聊 / 新的朋友）
        // ------------------------------------------------------------------
        setContactsTab(tab) {
            this.contactsTab = tab;
            this.contactsSelection = null;
            this.el.contactsView.classList.remove('show-detail');
            this.renderContactsPage();
        },

        selectContact(kind, id) {
            this.contactsSelection = { kind, id: String(id) };
            this.renderContactsPage();
            this.el.contactsView.classList.add('show-detail');
        },

        closeContactDetail() {
            this.contactsSelection = null;
            this.el.contactsView.classList.remove('show-detail');
            this.renderContactsPage();
        },

        buildContactRow(cfg) {
            const sel = this.contactsSelection;
            const active = sel && sel.kind === cfg.kind && String(sel.id) === String(cfg.id);

            const row = el('div', 'contact-row' + (active ? ' active' : ''));
            row.dataset.contactKind = cfg.kind;
            row.dataset.contactId = String(cfg.id);

            const avatarWrap = el('div', 'row-avatar');
            const avatarNode = cfg.avatar || this.makeAvatar(cfg.id, cfg.title);
            if (cfg.online === true) avatarNode.appendChild(el('span', 'online-dot on'));
            avatarWrap.appendChild(avatarNode);
            row.appendChild(avatarWrap);

            const main = el('div', 'row-main');
            const title = el('div', 'row-title');
            title.appendChild(document.createTextNode(cfg.title || ''));
            if (cfg.badge) title.appendChild(el('span', 'row-tag on', cfg.badge));
            main.appendChild(title);
            main.appendChild(el('div', 'row-sub', cfg.subtitle || ''));
            row.appendChild(main);

            return row;
        },

        buildRequestRow(req) {
            const row = el('div', 'contact-row request-row');

            const avatarWrap = el('div', 'row-avatar');
            avatarWrap.appendChild(this.makeAvatar(req.fromUid, req.fromNickname || '?'));
            row.appendChild(avatarWrap);

            const main = el('div', 'row-main');
            main.appendChild(el('div', 'row-title', req.fromNickname || ('用户 ' + req.fromUid)));
            main.appendChild(el('div', 'row-sub', req.message || '请求添加你为好友'));
            row.appendChild(main);

            const actions = el('div', 'row-actions');
            const requestId = String(req.requestId || req.id || '');

            const accept = el('button', 'mini-btn primary', '同意');
            accept.type = 'button';
            accept.dataset.reqAction = 'accept';
            accept.dataset.reqId = requestId;

            const reject = el('button', 'mini-btn', '拒绝');
            reject.type = 'button';
            reject.dataset.reqAction = 'reject';
            reject.dataset.reqId = requestId;

            actions.append(accept, reject);
            row.appendChild(actions);
            return row;
        },

        renderContactsPage() {
            if (!this.el.contactsList) return;

            const tab = this.contactsTab || 'friends';
            const pending = Store.pendingCount();

            this.el.contactRequestBadge.hidden = pending === 0;
            this.el.contactRequestBadge.textContent = String(pending);

            document.querySelectorAll('.contacts-tab').forEach((btn) => {
                btn.classList.toggle('active', btn.dataset.contactTab === tab);
            });

            const list = this.el.contactsList;
            list.textContent = '';

            let count = 0;

            if (tab === 'friends') {
                const friends = Store.friendList();
                count = friends.length;
                for (const user of friends) {
                    list.appendChild(this.buildContactRow({
                        kind: 'friend',
                        id: user.uid,
                        title: user.nickname,
                        subtitle: 'pp号：' + (user.pp_id || '—'),
                        avatar: this.makeAvatar(user.uid, user.nickname),
                        online: user.online,
                        badge: user.online ? '在线' : '',
                    }));
                }
                this.el.contactsEmptyText.textContent = '还没有好友，点右上角添加';
            } else if (tab === 'groups') {
                const groups = Store.groupList();
                count = groups.length;
                for (const group of groups) {
                    const avatar = this.makeAvatar('g' + group.gid, group.name, false, true);
                    avatar.appendChild(el('span', 'group-badge', '群'));
                    list.appendChild(this.buildContactRow({
                        kind: 'group',
                        id: group.gid,
                        title: group.name,
                        subtitle: (group.memberCount || 0) + ' 位成员',
                        avatar,
                    }));
                }
                this.el.contactsEmptyText.textContent = '还没有群聊，点右上角创建';
            } else {
                const requests = Store.requests || [];
                count = requests.length;
                for (const req of requests) {
                    list.appendChild(this.buildRequestRow(req));
                }
                this.el.contactsEmptyText.textContent = '没有新的朋友申请';
            }

            this.el.contactsEmpty.hidden = count > 0;

            const sel = this.contactsSelection;
            const valid = sel && (
                (tab === 'friends' && sel.kind === 'friend') ||
                (tab === 'groups' && sel.kind === 'group')
            );
            if (!valid && tab !== 'requests') this.contactsSelection = null;

            if (tab === 'requests' || !this.contactsSelection) {
                this.el.contactsDetailEmpty.hidden = false;
                this.el.contactsDetailPanel.hidden = true;
                this.el.contactsDetailPanel.textContent = '';
                this.el.contactsView.classList.remove('show-detail');
                return;
            }

            this.el.contactsDetailEmpty.hidden = true;
            this.el.contactsDetailPanel.hidden = false;
            this.renderContactDetail(this.contactsSelection);
        },

        buildInfoSection(title, rows) {
            const section = el('div', 'detail-section');
            section.appendChild(el('h3', '', title));

            for (const [label, value] of rows) {
                const row = el('div', 'detail-row');
                row.appendChild(el('span', 'detail-label', label));
                row.appendChild(el('span', 'detail-value', String(value)));
                section.appendChild(row);
            }
            return section;
        },

        renderContactDetail(sel) {
            const panel = this.el.contactsDetailPanel;
            panel.textContent = '';

            const back = el('button', 'icon-btn detail-back', '‹');
            back.type = 'button';
            back.id = 'btn-contact-back';
            back.title = '返回';
            panel.appendChild(back);

            if (sel.kind === 'friend') {
                const user = Store.userOf(sel.id) ||
                    { uid: sel.id, nickname: '用户 ' + sel.id, online: false };

                const card = el('div', 'detail-card');
                const avatar = this.makeAvatar(user.uid, user.nickname);
                avatar.classList.add('detail-avatar');
                card.appendChild(avatar);

                const info = el('div', 'detail-info');
                const name = el('div', 'detail-name', user.nickname || '');
                name.appendChild(el('span', 'detail-state' + (user.online ? ' on' : ''),
                    user.online ? '在线' : '离线'));
                info.appendChild(name);
                info.appendChild(el('div', 'detail-id', 'pp号：' + (user.pp_id || '—')));
                info.appendChild(el('div', 'detail-sign', user.signature || '这个人很懒，什么都没写'));
                card.appendChild(info);
                panel.appendChild(card);

                const actions = el('div', 'detail-actions');

                const msgBtn = el('button', 'btn btn-primary', '发消息');
                msgBtn.type = 'button';
                msgBtn.dataset.detailAction = 'message';
                msgBtn.dataset.detailId = String(user.uid);

                const feedBtn = el('button', 'btn btn-ghost', 'TA 的动态');
                feedBtn.type = 'button';
                feedBtn.dataset.detailAction = 'user-feed';
                feedBtn.dataset.detailId = String(user.uid);
                feedBtn.dataset.detailName = user.nickname || '';

                actions.append(msgBtn, feedBtn);
                panel.appendChild(actions);

                panel.appendChild(this.buildInfoSection('资料', [
                    ['pp 号', user.pp_id || '—'],
                    ['状态', user.online ? '在线' : '离线'],
                    ['个性签名', user.signature || '未填写'],
                ]));
                return;
            }

            const gid = String(sel.id);
            const group = Store.groups.get(gid) || { gid, name: '群聊', memberCount: 0 };
            const members = Store.groupMembers.get(gid) || [];

            const card = el('div', 'detail-card');
            const avatar = this.makeAvatar('g' + group.gid, group.name, false, true);
            avatar.classList.add('detail-avatar');
            avatar.appendChild(el('span', 'group-badge', '群'));
            card.appendChild(avatar);

            const info = el('div', 'detail-info');
            info.appendChild(el('div', 'detail-name', group.name));
            info.appendChild(el('div', 'detail-id',
                (members.length || group.memberCount || 0) + ' 位成员'));
            card.appendChild(info);
            panel.appendChild(card);

            const actions = el('div', 'detail-actions');

            const openBtn = el('button', 'btn btn-primary', '进入群聊');
            openBtn.type = 'button';
            openBtn.dataset.detailAction = 'open-group';
            openBtn.dataset.detailId = gid;

            const inviteBtn = el('button', 'btn btn-ghost', '邀请好友');
            inviteBtn.type = 'button';
            inviteBtn.dataset.detailAction = 'invite';
            inviteBtn.dataset.detailId = gid;

            const quitBtn = el('button', 'btn btn-ghost danger', '退出群聊');
            quitBtn.type = 'button';
            quitBtn.dataset.detailAction = 'quit-group';
            quitBtn.dataset.detailId = gid;

            actions.append(openBtn, inviteBtn, quitBtn);
            panel.appendChild(actions);

            const section = el('div', 'detail-section');
            section.appendChild(el('h3', '', '群成员 ' + members.length));

            const grid = el('div', 'member-list');
            for (const member of members) {
                const row = el('div', 'member-row');

                const av = this.makeAvatar(member.uid, member.nickname, true);
                if (member.online) av.appendChild(el('span', 'online-dot on'));
                row.appendChild(av);

                row.appendChild(el('span', 'member-name', member.nickname || ('用户 ' + member.uid)));
                if (Number(member.role) === 1) row.appendChild(el('span', 'member-tag', '群主'));
                if (member.online) row.appendChild(el('span', 'member-state', '在线'));
                grid.appendChild(row);
            }
            if (members.length === 0) {
                grid.appendChild(el('p', 'member-empty', '正在加载成员…'));
            }

            section.appendChild(grid);
            panel.appendChild(section);
        },


        // ------------------------------------------------------------------
        //  聊天区
        // ------------------------------------------------------------------
        setActiveKey(key) {
            Store.activeKey = key || null;
            const has = !!Store.activeKey;

            this.el.chatEmpty.hidden = has;
            this.el.chatPane.hidden = !has;

            if (!has) {
                this.el.chat.classList.remove('mobile-open');
                this.renderSidebar();
                return;
            }

            this.renderChatHeader();
            this.renderMessages({ forceBottom: true });
            this.renderSidebar();
            this.el.chat.classList.add('mobile-open');

            // 移动端 .chat 刚才还是 display:none，renderMessages 里的
            // `scrollTop = scrollHeight` 写在不可见元素上（scrollHeight 为 0）不生效。
            // 必须等元素真正可见、布局完成后再贴一次底。
            this.scrollToBottom();
        },

        /**
         * 把消息区滚到底部。
         * 连续两帧各滚一次：第一帧元素刚获得布局，第二帧等图片/字体重排完成。
         */
        scrollToBottom() {
            const box = this.el.messages;
            if (!box) return;
            box.scrollTop = box.scrollHeight;
            const again = function () { box.scrollTop = box.scrollHeight; };
            if (typeof requestAnimationFrame === 'function') {
                requestAnimationFrame(function () {
                    again();
                    requestAnimationFrame(again);
                });
            }
        },

        renderChatHeader() {
            const key = Store.activeKey;
            if (!key) return;

            const info = Store.parseKey(key);
            const conv = Store.conversations.get(key);
            const name = conv ? conv.name : Store.displayNameOf(key);

            this.el.chatName.textContent = name;
            this.el.btnChatInfo.hidden = false;

            if (info.kind === 'group') {
                const group = Store.groups.get(info.id);
                this.el.chatPresence.classList.add('offline');
                this.el.chatPresence.classList.remove('online');
                const count = group ? group.memberCount : 0;
                this.el.chatSub.textContent = count > 0 ? (count + ' 位成员') : '群聊';
                this.el.chatUid.textContent = '群号：' + info.id;
                return;
            }

            const user = Store.users.get(info.id);
            const online = user ? !!user.online : !!(conv && conv.online);
            this.el.chatPresence.classList.toggle('online', online);
            this.el.chatPresence.classList.toggle('offline', !online);
            this.el.chatSub.textContent = online ? '在线' : '离线';
            this.el.chatUid.textContent = 'pp号：' + (user && user.pp_id ? user.pp_id : '—');
        },

        renderMessages(options) {
            const opts = options || {};
            const key = Store.activeKey;
            if (!key) return;

            const box = this.el.messages;
            const prevHeight = box.scrollHeight;
            const prevTop = box.scrollTop;
            const nearBottom = prevHeight - prevTop - box.clientHeight < 100;

            const container = this.el.messageList;
            container.textContent = '';

            const info = Store.parseKey(key);
            const list = Store.messages.get(key) || [];
            let lastDay = '';
            let lastFrom = '';
            let lastTs = 0;

            for (const msg of list) {
                const day = formatDivider(msg.timestamp);
                // 微信规则：跨天一定显示时间；同一天内只有间隔≥2 分钟才插一条时间，
                // 连续聊天时不再逐条单开时间行。
                const needTime = day !== lastDay || (lastTs > 0 && msg.timestamp - lastTs >= 120);
                if (needTime) {
                    const label = (day === lastDay && lastTs > 0) ? formatClock(msg.timestamp) : day;
                    const divider = el('div', 'day-divider');
                    divider.appendChild(el('span', '', label));
                    container.appendChild(divider);
                    lastDay = day;
                    lastFrom = '';
                }
                lastTs = msg.timestamp;

                if (msg.kind === 2) {
                    const system = el('div', 'system-msg');
                    system.appendChild(el('span', '', msg.content));
                    container.appendChild(system);
                    lastFrom = '';
                    continue;
                }

                // 已撤回：不再渲染气泡，改成一条居中的系统提示（微信式）
                if (msg.recalled) {
                    const mineRecalled = Store.me && String(msg.fromUid) === String(Store.me.uid);
                    const who = mineRecalled
                        ? '你'
                        : (msg.fromNickname || Store.displayNameOfUser(msg.fromUid) || '对方');
                    const system = el('div', 'system-msg recall');
                    system.appendChild(el('span', '', who + '撤回了一条消息'));
                    container.appendChild(system);
                    lastFrom = '';
                    continue;
                }

                // 拍一拍（msgKind = 4）：居中提示，同样可以被撤回
                if (msg.msgKind === 4) {
                    const minePat = Store.me && String(msg.fromUid) === String(Store.me.uid);
                    const fromName = minePat
                        ? '你'
                        : (msg.fromNickname || Store.displayNameOfUser(msg.fromUid) || '对方');
                    const targetName = msg.content || '对方';
                    const text = info.kind === 'group'
                        ? (fromName + ' 拍了拍 ' + targetName)
                        : (minePat ? ('你拍了拍' + targetName) : (fromName + '拍了拍你'));
                    const system = el('div', 'system-msg pat');
                    system.appendChild(el('span', '', text));
                    // 拍一拍也是一条可撤回的消息：长按/右键只给「撤回」
                    attachMessageMenu(system, msg, minePat);
                    container.appendChild(system);
                    lastFrom = '';
                    continue;
                }

                const mine = Store.me && String(msg.fromUid) === String(Store.me.uid);
                const row = el('div', 'msg-row' + (mine ? ' mine' : ''));

                if (lastFrom !== String(msg.fromUid)) {
                    if (mine) {
                        const spacer = el('div', 'msg-avatar-spacer');
                        row.appendChild(spacer);
                    } else {
                        const avatarEl = el('div', 'msg-avatar');
                        paintAvatar(avatarEl, msg.fromUid,
                            msg.fromNickname || Store.displayNameOfUser(msg.fromUid));
                        attachAvatarPat(avatarEl, msg);
                        row.appendChild(avatarEl);
                    }
                } else {
                    row.appendChild(el('div', 'msg-avatar-spacer'));
                }

                const bodyEl = el('div', 'msg-body');

                if (info.kind === 'group' && !mine) {
                    bodyEl.appendChild(el('div', 'msg-name',
                        msg.fromNickname || Store.displayNameOfUser(msg.fromUid)));
                }

                // 每条消息上方不再重复显示时间：时间只在 day-divider 行出现一次
                const meta = el('div', 'msg-meta');
                let hasMeta = false;
                if (mine && msg.status === 'sending') {
                    meta.appendChild(el('span', 'msg-status', '发送中…'));
                    hasMeta = true;
                } else if (mine && msg.status === 'failed') {
                    meta.appendChild(el('span', 'msg-status failed', '发送失败'));
                    hasMeta = true;
                }

                const bubble = buildBubble(msg);
                if (hasMeta) bodyEl.appendChild(meta);
                bodyEl.appendChild(bubble);
                row.appendChild(bodyEl);
                attachMessageMenu(row, msg, mine);
                container.appendChild(row);

                lastFrom = String(msg.fromUid);
            }

            if (opts.keepPosition) {
                box.scrollTop = prevTop + (box.scrollHeight - prevHeight);
            } else if (opts.forceBottom || nearBottom) {
                // 统一走 scrollToBottom：刚显示的元素要等一帧才有正确的 scrollHeight
                this.scrollToBottom();
            }
        },

        setLoadMoreVisible(visible) {
            this.el.loadMore.hidden = !visible;
        },

        showTyping(show, name) {
            this.el.typing.hidden = !show;
            if (show) {
                this.el.typingText.textContent = (name || '对方') + '正在输入…';
            }
        },

        clearInput() {
            this.el.input.value = '';
            this.el.input.style.height = 'auto';
        },

        closeMobileChat() {
            this.el.chat.classList.remove('mobile-open');
        },

        // ------------------------------------------------------------------
        //  弹窗
        // ------------------------------------------------------------------
        openModal(title, options) {
            const opts = options || {};
            this.el.modalTitle.textContent = title;
            this.el.modalBody.textContent = '';
            this.el.modalFoot.textContent = '';

            if (opts.body) this.el.modalBody.appendChild(opts.body);
            if (opts.footer) {
                this.el.modalFoot.hidden = false;
                this.el.modalFoot.appendChild(opts.footer);
            } else {
                this.el.modalFoot.hidden = true;
            }

            this._modalRefresh = opts.refresh || null;
            this._modalKind = opts.kind || null;
            this.el.modalRoot.hidden = false;
        },

        closeModal() {
            this.el.modalRoot.hidden = true;
            this.el.modalBody.textContent = '';
            this.el.modalFoot.textContent = '';
            this._modalRefresh = null;
            this._modalKind = null;
        },

        refreshModal() {
            if (this._modalRefresh) this._modalRefresh();
        },

        modalKind() {
            return this._modalKind;
        },
    };

    UI.make = el;
    global.UI = UI;
    global.UI.formatListTime = formatListTime;
})(window);
