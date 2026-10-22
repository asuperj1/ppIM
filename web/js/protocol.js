// =============================================================================
//  web/js/protocol.js
//  与服务端 proto/im_msg.proto 中的 MsgType 数值严格一致。
// =============================================================================
(function (global) {
    'use strict';

    const MsgType = Object.freeze({
        LOGIN_REQ: 1,
        LOGIN_RSP: 2,
        CHAT_PRIVATE: 3,
        HEARTBEAT: 4,
        REGISTER_REQ: 5,
        REGISTER_RSP: 6,
        ONLINE_USERS_PUSH: 7,
        CONVERSATION_LIST_PUSH: 8,
        HISTORY_REQ: 9,
        HISTORY_RSP: 10,
        READ_REQ: 11,
        TYPING: 12,
        ERROR_PUSH: 13,
        CHAT_ACK: 14,
        PROFILE_UPDATE_REQ: 15,
        PROFILE_UPDATE_RSP: 16,
        FRIEND_SEARCH_REQ: 17,
        FRIEND_SEARCH_RSP: 18,
        FRIEND_REQUEST_REQ: 19,
        FRIEND_REQUEST_RSP: 20,
        FRIEND_REQUEST_PUSH: 21,
        FRIEND_REQUEST_LIST_REQ: 22,
        FRIEND_REQUEST_LIST_RSP: 23,
        FRIEND_REQUEST_HANDLE_REQ: 24,
        FRIEND_REQUEST_HANDLE_RSP: 25,
        FRIEND_LIST_PUSH: 26,
        GROUP_CREATE_REQ: 27,
        GROUP_CREATE_RSP: 28,
        GROUP_LIST_PUSH: 29,
        GROUP_INVITE_REQ: 30,
        GROUP_INVITE_RSP: 31,
        GROUP_QUIT_REQ: 32,
        GROUP_QUIT_RSP: 33,
        GROUP_MEMBER_LIST_REQ: 34,
        GROUP_MEMBER_LIST_RSP: 35,
        CHAT_GROUP: 36,
        CHAT_GROUP_ACK: 37,
        GROUP_HISTORY_REQ: 38,
        GROUP_HISTORY_RSP: 39,
        GROUP_READ_REQ: 40,
        UPLOAD_TICKET_REQ: 41,
        UPLOAD_TICKET_RSP: 42,
        POST_CREATE_REQ: 43,
        POST_CREATE_RSP: 44,
        FEED_REQ: 45,
        FEED_RSP: 46,
        POST_LIKE_REQ: 47,
        POST_LIKE_RSP: 48,
        COMMENT_CREATE_REQ: 49,
        COMMENT_CREATE_RSP: 50,
        POST_DELETE_REQ: 51,
        POST_DELETE_RSP: 52,
        POST_PUSH: 53,
        POST_NOTICE: 54,
        // 刷新页面后凭 localStorage 里的长期令牌静默恢复登录
        SESSION_LOGIN_REQ: 55,
        SESSION_LOGIN_RSP: 56,
        // 收藏夹（长按消息 → 收藏）
        FAVORITE_ADD_REQ: 57,
        FAVORITE_ADD_RSP: 58,
        FAVORITE_LIST_REQ: 59,
        FAVORITE_LIST_RSP: 60,
        FAVORITE_REMOVE_REQ: 61,
        FAVORITE_REMOVE_RSP: 62,
        // 撤回消息（自己发的，2 分钟内）
        RECALL_REQ: 63,
        RECALL_RSP: 64,
        RECALL_PUSH: 65,
        // 拍一拍（双击头像，可撤回）
        PAT_REQ: 66,
        PAT_RSP: 67,
        PAT_PUSH: 68,
    });

    // proto3 JSON 把 int64 输出成「能精确表示时是数字，否则是字符串」，
    // 前端统一转成字符串做 Map 的 key，避免 1 和 "1" 混用。
    function idOf(value) {
        if (value === null || value === undefined) return '';
        return String(value);
    }

    function numOf(value) {
        if (value === null || value === undefined) return 0;
        const n = Number(value);
        return Number.isFinite(n) ? n : 0;
    }

    const wsProtocol = global.location.protocol === 'https:' ? 'wss://' : 'ws://';
    const ppConfig = global.PP_CONFIG || {};

    // 页面可能部署在 /、/pp/ 或独立子域名下，WebSocket 路径要跟着页面走。
    function detectBasePath() {
        const configured = String(ppConfig.basePath || '').replace(/\/+$/, '');
        if (configured) return configured;

        const path = global.location.pathname || '/';
        if (path.endsWith('/')) {
            return path.slice(0, -1);           // /       -> ''
                                                // /pp/    -> '/pp'
        }
        const idx = path.lastIndexOf('/');
        return idx > 0 ? path.slice(0, idx) : '';  // /pp/index.html -> '/pp'
    }

    const BASE_PATH = detectBasePath();
    const WS_URL = ppConfig.wsUrl ||
        global.WS_URL_OVERRIDE ||
        (wsProtocol + global.location.host + BASE_PATH + '/ws');

    global.MsgType = MsgType;
    global.WS_URL = WS_URL;
    global.BASE_PATH = BASE_PATH;
    global.idOf = idOf;
    global.numOf = numOf;
})(window);
