// =============================================================================
//  common/include/im/common/msg_type.hpp
//  消息类型常量。数值必须与 proto/im_msg.proto 里的注释表严格一致。
// =============================================================================
#pragma once

#include <cstdint>

namespace im {

enum class MsgType : std::uint32_t {
    kLoginReq               = 1,
    kLoginRsp               = 2,
    kChatPrivate            = 3,
    kHeartbeat              = 4,
    kRegisterReq            = 5,
    kRegisterRsp            = 6,
    kOnlineUsersPush        = 7,
    kConversationListPush   = 8,
    kHistoryReq             = 9,
    kHistoryRsp             = 10,
    kReadReq                = 11,
    kTyping                 = 12,
    kErrorPush              = 13,
    kChatAck                = 14,
    kProfileUpdateReq       = 15,
    kProfileUpdateRsp       = 16,
    kFriendSearchReq        = 17,
    kFriendSearchRsp        = 18,
    kFriendRequestReq       = 19,
    kFriendRequestRsp       = 20,
    kFriendRequestPush      = 21,
    kFriendRequestListReq   = 22,
    kFriendRequestListRsp   = 23,
    kFriendRequestHandleReq = 24,
    kFriendRequestHandleRsp = 25,
    kFriendListPush         = 26,
    kGroupCreateReq         = 27,
    kGroupCreateRsp         = 28,
    kGroupListPush          = 29,
    kGroupInviteReq         = 30,
    kGroupInviteRsp         = 31,
    kGroupQuitReq           = 32,
    kGroupQuitRsp           = 33,
    kGroupMemberListReq     = 34,
    kGroupMemberListRsp     = 35,
    kChatGroup              = 36,
    kChatGroupAck           = 37,
    kGroupHistoryReq        = 38,
    kGroupHistoryRsp        = 39,
    kGroupReadReq           = 40,
    kUploadTicketReq        = 41,
    kUploadTicketRsp        = 42,
    kPostCreateReq          = 43,
    kPostCreateRsp          = 44,
    kFeedReq                = 45,
    kFeedRsp                = 46,
    kPostLikeReq            = 47,
    kPostLikeRsp            = 48,
    kCommentCreateReq       = 49,
    kCommentCreateRsp       = 50,
    kPostDeleteReq          = 51,
    kPostDeleteRsp          = 52,
    kPostPush               = 53,
    kPostNotice             = 54,
    // 55/56：凭 localStorage 里的长期令牌静默恢复登录（刷新页面不掉线）
    kSessionLoginReq        = 55,
    kSessionLoginRsp        = 56,
    // 57~62：收藏夹（长按消息 → 收藏）
    kFavoriteAddReq         = 57,
    kFavoriteAddRsp         = 58,
    kFavoriteListReq        = 59,
    kFavoriteListRsp        = 60,
    kFavoriteRemoveReq      = 61,
    kFavoriteRemoveRsp      = 62,
    // 63~65：撤回消息（自己发的消息，2 分钟内）
    kRecallReq              = 63,
    kRecallRsp              = 64,
    kRecallPush             = 65,
    // 66~68：拍一拍（双击头像，可撤回）
    kPatReq                 = 66,
    kPatRsp                 = 67,
    kPatPush                = 68,
};

inline const char* msgTypeName(MsgType t) noexcept {
    switch (t) {
        case MsgType::kLoginReq:               return "LOGIN_REQ";
        case MsgType::kLoginRsp:               return "LOGIN_RSP";
        case MsgType::kChatPrivate:            return "CHAT_PRIVATE";
        case MsgType::kHeartbeat:              return "HEARTBEAT";
        case MsgType::kRegisterReq:            return "REGISTER_REQ";
        case MsgType::kRegisterRsp:            return "REGISTER_RSP";
        case MsgType::kOnlineUsersPush:        return "ONLINE_USERS_PUSH";
        case MsgType::kConversationListPush:   return "CONVERSATION_LIST_PUSH";
        case MsgType::kHistoryReq:             return "HISTORY_REQ";
        case MsgType::kHistoryRsp:             return "HISTORY_RSP";
        case MsgType::kReadReq:                return "READ_REQ";
        case MsgType::kTyping:                 return "TYPING";
        case MsgType::kErrorPush:              return "ERROR_PUSH";
        case MsgType::kChatAck:                return "CHAT_ACK";
        case MsgType::kProfileUpdateReq:       return "PROFILE_UPDATE_REQ";
        case MsgType::kProfileUpdateRsp:       return "PROFILE_UPDATE_RSP";
        case MsgType::kFriendSearchReq:        return "FRIEND_SEARCH_REQ";
        case MsgType::kFriendSearchRsp:        return "FRIEND_SEARCH_RSP";
        case MsgType::kFriendRequestReq:       return "FRIEND_REQUEST_REQ";
        case MsgType::kFriendRequestRsp:       return "FRIEND_REQUEST_RSP";
        case MsgType::kFriendRequestPush:      return "FRIEND_REQUEST_PUSH";
        case MsgType::kFriendRequestListReq:   return "FRIEND_REQUEST_LIST_REQ";
        case MsgType::kFriendRequestListRsp:   return "FRIEND_REQUEST_LIST_RSP";
        case MsgType::kFriendRequestHandleReq: return "FRIEND_REQUEST_HANDLE_REQ";
        case MsgType::kFriendRequestHandleRsp: return "FRIEND_REQUEST_HANDLE_RSP";
        case MsgType::kFriendListPush:         return "FRIEND_LIST_PUSH";
        case MsgType::kGroupCreateReq:         return "GROUP_CREATE_REQ";
        case MsgType::kGroupCreateRsp:         return "GROUP_CREATE_RSP";
        case MsgType::kGroupListPush:          return "GROUP_LIST_PUSH";
        case MsgType::kGroupInviteReq:         return "GROUP_INVITE_REQ";
        case MsgType::kGroupInviteRsp:         return "GROUP_INVITE_RSP";
        case MsgType::kGroupQuitReq:           return "GROUP_QUIT_REQ";
        case MsgType::kGroupQuitRsp:           return "GROUP_QUIT_RSP";
        case MsgType::kGroupMemberListReq:     return "GROUP_MEMBER_LIST_REQ";
        case MsgType::kGroupMemberListRsp:     return "GROUP_MEMBER_LIST_RSP";
        case MsgType::kChatGroup:              return "CHAT_GROUP";
        case MsgType::kChatGroupAck:           return "CHAT_GROUP_ACK";
        case MsgType::kGroupHistoryReq:        return "GROUP_HISTORY_REQ";
        case MsgType::kGroupHistoryRsp:        return "GROUP_HISTORY_RSP";
        case MsgType::kGroupReadReq:           return "GROUP_READ_REQ";
        case MsgType::kUploadTicketReq:        return "UPLOAD_TICKET_REQ";
        case MsgType::kUploadTicketRsp:        return "UPLOAD_TICKET_RSP";
        case MsgType::kPostCreateReq:          return "POST_CREATE_REQ";
        case MsgType::kPostCreateRsp:          return "POST_CREATE_RSP";
        case MsgType::kFeedReq:                return "FEED_REQ";
        case MsgType::kFeedRsp:                return "FEED_RSP";
        case MsgType::kPostLikeReq:            return "POST_LIKE_REQ";
        case MsgType::kPostLikeRsp:            return "POST_LIKE_RSP";
        case MsgType::kCommentCreateReq:       return "COMMENT_CREATE_REQ";
        case MsgType::kCommentCreateRsp:       return "COMMENT_CREATE_RSP";
        case MsgType::kPostDeleteReq:          return "POST_DELETE_REQ";
        case MsgType::kPostDeleteRsp:          return "POST_DELETE_RSP";
        case MsgType::kPostPush:               return "POST_PUSH";
        case MsgType::kPostNotice:             return "POST_NOTICE";
        case MsgType::kSessionLoginReq:        return "SESSION_LOGIN_REQ";
        case MsgType::kSessionLoginRsp:        return "SESSION_LOGIN_RSP";
        case MsgType::kFavoriteAddReq:         return "FAVORITE_ADD_REQ";
        case MsgType::kFavoriteAddRsp:         return "FAVORITE_ADD_RSP";
        case MsgType::kFavoriteListReq:        return "FAVORITE_LIST_REQ";
        case MsgType::kFavoriteListRsp:        return "FAVORITE_LIST_RSP";
        case MsgType::kFavoriteRemoveReq:      return "FAVORITE_REMOVE_REQ";
        case MsgType::kFavoriteRemoveRsp:      return "FAVORITE_REMOVE_RSP";
        case MsgType::kRecallReq:              return "RECALL_REQ";
        case MsgType::kRecallRsp:              return "RECALL_RSP";
        case MsgType::kRecallPush:             return "RECALL_PUSH";
        case MsgType::kPatReq:                 return "PAT_REQ";
        case MsgType::kPatRsp:                 return "PAT_RSP";
        case MsgType::kPatPush:                return "PAT_PUSH";
    }
    return "UNKNOWN";
}

inline bool isKnownMsgType(std::uint32_t v) noexcept {
    return v >= static_cast<std::uint32_t>(MsgType::kLoginReq) &&
           v <= static_cast<std::uint32_t>(MsgType::kPatPush);
}

}  // namespace im
