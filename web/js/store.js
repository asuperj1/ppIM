// =============================================================================
//  web/js/store.js
//  前端唯一数据源：用户 / 好友 / 群组 / 会话 / 消息 / 好友申请
//
//  会话 key 约定：
//      私聊  p:<uid>
//      群聊  g:<gid>
//  这样会话列表、消息缓存、已读状态可以用一套逻辑处理两种聊天。
// =============================================================================
(function (global) {
    'use strict';

    function keyOfPrivate(uid) {
        return 'p:' + String(uid);
    }

    function keyOfGroup(gid) {
        return 'g:' + String(gid);
    }

    function parseKey(key) {
        const text = String(key || '');
        const idx = text.indexOf(':');
        if (idx < 0) return { kind: 'private', id: '' };
        return {
            kind: text.slice(0, idx) === 'g' ? 'group' : 'private',
            id: text.slice(idx + 1),
        };
    }

    const Store = {
        me: null,                     // { uid, nickname, pp_id, signature, username }
        credentials: null,
        users: new Map(),             // uid -> { uid, nickname, pp_id, signature, online }
        friends: new Map(),           // uid -> true
        groups: new Map(),            // gid -> { gid, name, ownerUid, memberCount }
        groupMembers: new Map(),      // gid -> [member]
        conversations: new Map(),     // key -> conv
        messages: new Map(),          // key -> [msg]
        historyLoaded: new Set(),
        requests: [],                 // 待处理好友申请
        favorites: [],                // 收藏夹（服务端下发的副本）
        activeKey: null,
        view: 'chats',
        filter: '',
        connected: false,

        reset() {
            this.me = null;
            this.credentials = null;
            this.favorites = [];
            this.users.clear();
            this.friends.clear();
            this.groups.clear();
            this.groupMembers.clear();
            this.conversations.clear();
            this.messages.clear();
            this.historyLoaded.clear();
            this.requests = [];
            this.activeKey = null;
            this.view = 'chats';
            this.filter = '';
        },

        keyOfPrivate,
        keyOfGroup,
        parseKey,

        isMe(uid) {
            return !!this.me && String(uid) === String(this.me.uid);
        },

        userOf(uid) {
            return this.users.get(String(uid)) || null;
        },

        displayNameOfUser(uid) {
            const key = String(uid);
            if (this.me && key === String(this.me.uid)) {
                return this.me.nickname || this.me.pp_id || '我';
            }
            const user = this.users.get(key);
            if (user && user.nickname) return user.nickname;
            const conv = this.conversations.get(keyOfPrivate(key));
            if (conv && conv.name) return conv.name;
            return '用户 ' + key;
        },

        displayNameOf(key) {
            const conv = this.conversations.get(key);
            if (conv && conv.name) return conv.name;
            const info = parseKey(key);
            if (info.kind === 'group') {
                const group = this.groups.get(info.id);
                return group ? group.name : '群聊 ' + info.id;
            }
            return this.displayNameOfUser(info.id);
        },

        upsertUser(info) {
            if (!info || info.uid === undefined || info.uid === null) return null;
            const uid = String(info.uid);
            if (!uid || uid === 'undefined') return null;

            const prev = this.users.get(uid) || {};
            const user = {
                uid,
                nickname: info.nickname || prev.nickname || ('用户 ' + uid),
                pp_id: info.pp_id !== undefined ? info.pp_id : (prev.pp_id || ''),
                signature: info.signature !== undefined ? info.signature : (prev.signature || ''),
                avatarUrl: info.avatarUrl !== undefined ? info.avatarUrl
                    : (info.avatar_url !== undefined ? info.avatar_url : (prev.avatarUrl || '')),
                online: info.online === undefined ? !!prev.online : !!info.online,
            };
            this.users.set(uid, user);

            const conv = this.conversations.get(keyOfPrivate(uid));
            if (conv) {
                conv.name = user.nickname;
                conv.online = user.online;
            }
            return user;
        },

        upsertGroup(info) {
            const gid = String(info.gid);
            if (!gid || gid === 'undefined') return null;

            const prev = this.groups.get(gid) || {};
            const group = {
                gid,
                name: info.name || prev.name || ('群聊 ' + gid),
                ownerUid: info.owner_uid !== undefined ? String(info.owner_uid)
                    : (info.ownerUid !== undefined ? String(info.ownerUid) : prev.ownerUid || ''),
                memberCount: info.member_count !== undefined ? Number(info.member_count)
                    : (info.memberCount !== undefined ? Number(info.memberCount) : prev.memberCount || 0),
            };
            this.groups.set(gid, group);

            const conv = this.conversations.get(keyOfGroup(gid));
            if (conv) {
                conv.name = group.name;
            }
            return group;
        },

        applyFriends(list) {
            this.friends.clear();
            for (const user of list || []) {
                const saved = this.upsertUser(user);
                if (saved) this.friends.set(saved.uid, true);
            }
        },

        isFriend(uid) {
            return this.friends.has(String(uid));
        },

        applyGroups(list) {
            const keep = new Set();
            for (const group of list || []) {
                const saved = this.upsertGroup(group);
                if (saved) keep.add(saved.gid);
            }

            // 被解散 / 被移出的群：删掉本地群、会话和缓存
            for (const gid of Array.from(this.groups.keys())) {
                if (!keep.has(gid)) {
                    this.groups.delete(gid);
                    this.groupMembers.delete(gid);
                }
            }
            for (const key of Array.from(this.conversations.keys())) {
                if (key.startsWith('g:') && !this.groups.has(key.slice(2))) {
                    this.conversations.delete(key);
                    this.messages.delete(key);
                    this.historyLoaded.delete(key);
                    if (this.activeKey === key) this.activeKey = null;
                }
            }
        },

        ensurePrivateConv(uid, name) {
            const id = String(uid);
            const key = keyOfPrivate(id);
            let conv = this.conversations.get(key);
            if (!conv) {
                conv = {
                    key,
                    kind: 'private',
                    id,
                    name: name || this.displayNameOfUser(id),
                    online: !!(this.users.get(id) && this.users.get(id).online),
                    lastContent: '',
                    lastTime: 0,
                    lastSender: '',
                    unread: 0,
                };
                this.conversations.set(key, conv);
            } else if (name) {
                conv.name = name;
            }
            return conv;
        },

        ensureGroupConv(gid, name) {
            const id = String(gid);
            const key = keyOfGroup(id);
            let conv = this.conversations.get(key);
            if (!conv) {
                conv = {
                    key,
                    kind: 'group',
                    id,
                    name: name || (this.groups.get(id) ? this.groups.get(id).name : ('群聊 ' + id)),
                    online: false,
                    lastContent: '',
                    lastTime: 0,
                    lastSender: '',
                    unread: 0,
                };
                this.conversations.set(key, conv);
            } else if (name) {
                conv.name = name;
            }
            return conv;
        },

        applyConversations(list) {
            const oldActive = this.activeKey ? this.conversations.get(this.activeKey) : null;
            const next = new Map();
            for (const item of list || []) {
                const kind = Number(item.kind) === 2 ? 'group' : 'private';
                const id = kind === 'group' ? String(item.gid) : String(item.peerUid);
                if (!id || id === 'undefined' || id === '0') continue;

                const key = kind === 'group' ? keyOfGroup(id) : keyOfPrivate(id);
                if (kind === 'group') {
                    if (!this.groups.has(id)) {
                        this.upsertGroup({ gid: id, name: item.nickname, member_count: 0 });
                    }
                } else {
                    this.upsertUser({ uid: id, nickname: item.nickname, online: item.online });
                }

                next.set(key, {
                    key,
                    kind,
                    id,
                    name: item.nickname || (kind === 'group' ? '群聊 ' + id : '用户 ' + id),
                    online: !!item.online,
                    lastContent: item.lastContent || '',
                    lastTime: Number(item.lastTime) || 0,
                    lastSender: item.lastSender || '',
                    unread: Number(item.unread) || 0,
                });
            }

            // 当前打开的会话如果服务端列表里还没有（比如刚点开、还没发消息），保留它
            if (this.activeKey && !next.has(this.activeKey) && oldActive) {
                next.set(this.activeKey, oldActive);
            }

            this.conversations = next;
        },

        messageList(key) {
            let list = this.messages.get(key);
            if (!list) {
                list = [];
                this.messages.set(key, list);
            }
            return list;
        },

        addMessage(key, msg) {
            const list = this.messageList(key);
            const msgId = msg.id ? String(msg.id) : '';

            if (msgId) {
                const exist = list.find((m) => m.id && String(m.id) === msgId);
                if (exist) {
                    Object.assign(exist, msg);
                    return exist;
                }
            }
            if (msg.clientMsgId) {
                const exist = list.find((m) => m.clientMsgId && m.clientMsgId === msg.clientMsgId);
                if (exist) {
                    Object.assign(exist, msg);
                    return exist;
                }
            }

            list.push(msg);
            list.sort((a, b) => {
                if (a.id && b.id) return Number(a.id) - Number(b.id);
                return a.timestamp - b.timestamp;
            });
            return msg;
        },

        touchConv(key, patch) {
            let conv = this.conversations.get(key);
            if (!conv) {
                const info = parseKey(key);
                conv = info.kind === 'group'
                    ? this.ensureGroupConv(info.id)
                    : this.ensurePrivateConv(info.id);
            }
            Object.assign(conv, patch || {});
            return conv;
        },

        totalUnread() {
            let total = 0;
            for (const conv of this.conversations.values()) {
                total += conv.unread || 0;
            }
            return total;
        },

        pendingCount() {
            return this.requests.length;
        },

        sortedConversations() {
            return Array.from(this.conversations.values())
                .filter((c) => c.lastTime > 0 || (this.messages.get(c.key) || []).length > 0)
                .sort((a, b) => b.lastTime - a.lastTime);
        },

        friendList() {
            const list = [];
            for (const uid of this.friends.keys()) {
                const user = this.users.get(uid);
                if (user) list.push(user);
            }
            list.sort((a, b) => {
                if (a.online !== b.online) return a.online ? -1 : 1;
                return a.nickname.localeCompare(b.nickname, 'zh-CN');
            });
            return list;
        },

        groupList() {
            return Array.from(this.groups.values()).sort((a, b) => {
                return a.name.localeCompare(b.name, 'zh-CN');
            });
        },
    };

    global.Store = Store;
})(window);
