// =============================================================================
//  im_server/src/core/json_codec.cpp
// =============================================================================
#include "core/json_codec.hpp"

#include <google/protobuf/util/json_util.h>

#include <spdlog/spdlog.h>

#include "im/common/msg_type.hpp"
#include "im_msg.pb.h"

namespace im::jsonc {
namespace {

const google::protobuf::util::JsonPrintOptions& printOptions() {
    static const google::protobuf::util::JsonPrintOptions options = [] {
        google::protobuf::util::JsonPrintOptions opts;
        opts.add_whitespace = false;
        opts.preserve_proto_field_names = false;   // 输出 lowerCamelCase
        // 注意：不设置 always_print_fields_with_no_presence / unquote_int64_if_possible，
        // 这两个选项在 Ubuntu 的 protobuf 3.21 里还不存在。
        // 前端已经统一用 String()/Number() 处理 int64 与缺省字段。
        return opts;
    }();
    return options;
}

const google::protobuf::util::JsonParseOptions& parseOptions() {
    static const google::protobuf::util::JsonParseOptions options = [] {
        google::protobuf::util::JsonParseOptions opts;
        opts.ignore_unknown_fields = true;
        return opts;
    }();
    return options;
}

}  // namespace

std::string toJson(const google::protobuf::Message& msg) {
    std::string out;
    const auto status = google::protobuf::util::MessageToJsonString(msg, &out, printOptions());
    if (!status.ok()) {
        spdlog::error("[json] serialize {} failed: {}", msg.GetTypeName(), status.ToString());
        return "{}";
    }
    return out;
}

bool fromJson(std::string_view json, google::protobuf::Message* msg) {
    if (msg == nullptr) {
        return false;
    }
    const auto status = google::protobuf::util::JsonStringToMessage(json, msg, parseOptions());
    if (!status.ok()) {
        spdlog::debug("[json] parse {} failed: {}", msg->GetTypeName(), status.ToString());
        return false;
    }
    return true;
}

bool toBinary(std::uint32_t type, std::string_view json, std::string* out) {
    if (out == nullptr) {
        return false;
    }

    switch (static_cast<im::MsgType>(type)) {
        case im::MsgType::kRegisterReq: {
            im::RegisterReq msg;
            if (!fromJson(json, &msg)) return false;
            return msg.SerializeToString(out);
        }
        case im::MsgType::kLoginReq: {
            im::LoginReq msg;
            if (!fromJson(json, &msg)) return false;
            return msg.SerializeToString(out);
        }
        case im::MsgType::kChatPrivate: {
            im::ChatPrivate msg;
            if (!fromJson(json, &msg)) return false;
            return msg.SerializeToString(out);
        }
        case im::MsgType::kHeartbeat: {
            im::HeartBeat msg;
            if (!fromJson(json, &msg)) return false;
            return msg.SerializeToString(out);
        }
        case im::MsgType::kHistoryReq: {
            im::HistoryReq msg;
            if (!fromJson(json, &msg)) return false;
            return msg.SerializeToString(out);
        }
        case im::MsgType::kReadReq: {
            im::ReadReq msg;
            if (!fromJson(json, &msg)) return false;
            return msg.SerializeToString(out);
        }
        case im::MsgType::kTyping: {
            im::Typing msg;
            if (!fromJson(json, &msg)) return false;
            return msg.SerializeToString(out);
        }
        case im::MsgType::kProfileUpdateReq: {
            im::ProfileUpdateReq msg;
            if (!fromJson(json, &msg)) return false;
            return msg.SerializeToString(out);
        }
        case im::MsgType::kFriendSearchReq: {
            im::FriendSearchReq msg;
            if (!fromJson(json, &msg)) return false;
            return msg.SerializeToString(out);
        }
        case im::MsgType::kFriendRequestReq: {
            im::FriendRequestReq msg;
            if (!fromJson(json, &msg)) return false;
            return msg.SerializeToString(out);
        }
        case im::MsgType::kFriendRequestListReq: {
            im::FriendRequestListReq msg;
            if (!fromJson(json, &msg)) return false;
            return msg.SerializeToString(out);
        }
        case im::MsgType::kFriendRequestHandleReq: {
            im::FriendRequestHandleReq msg;
            if (!fromJson(json, &msg)) return false;
            return msg.SerializeToString(out);
        }
        case im::MsgType::kGroupCreateReq: {
            im::GroupCreateReq msg;
            if (!fromJson(json, &msg)) return false;
            return msg.SerializeToString(out);
        }
        case im::MsgType::kGroupInviteReq: {
            im::GroupInviteReq msg;
            if (!fromJson(json, &msg)) return false;
            return msg.SerializeToString(out);
        }
        case im::MsgType::kGroupQuitReq: {
            im::GroupQuitReq msg;
            if (!fromJson(json, &msg)) return false;
            return msg.SerializeToString(out);
        }
        case im::MsgType::kGroupMemberListReq: {
            im::GroupMemberListReq msg;
            if (!fromJson(json, &msg)) return false;
            return msg.SerializeToString(out);
        }
        case im::MsgType::kChatGroup: {
            im::ChatGroup msg;
            if (!fromJson(json, &msg)) return false;
            return msg.SerializeToString(out);
        }
        case im::MsgType::kGroupHistoryReq: {
            im::GroupHistoryReq msg;
            if (!fromJson(json, &msg)) return false;
            return msg.SerializeToString(out);
        }
        case im::MsgType::kGroupReadReq: {
            im::GroupReadReq msg;
            if (!fromJson(json, &msg)) return false;
            return msg.SerializeToString(out);
        }
        case im::MsgType::kUploadTicketReq: {
            im::UploadTicketReq msg;
            if (!fromJson(json, &msg)) return false;
            return msg.SerializeToString(out);
        }
        case im::MsgType::kPostCreateReq: {
            im::PostCreateReq msg;
            if (!fromJson(json, &msg)) return false;
            return msg.SerializeToString(out);
        }
        case im::MsgType::kFeedReq: {
            im::FeedReq msg;
            if (!fromJson(json, &msg)) return false;
            return msg.SerializeToString(out);
        }
        case im::MsgType::kPostLikeReq: {
            im::PostLikeReq msg;
            if (!fromJson(json, &msg)) return false;
            return msg.SerializeToString(out);
        }
        case im::MsgType::kCommentCreateReq: {
            im::CommentCreateReq msg;
            if (!fromJson(json, &msg)) return false;
            return msg.SerializeToString(out);
        }
        case im::MsgType::kPostDeleteReq: {
            im::PostDeleteReq msg;
            if (!fromJson(json, &msg)) return false;
            return msg.SerializeToString(out);
        }
        case im::MsgType::kSessionLoginReq: {
            im::SessionLoginReq msg;
            if (!fromJson(json, &msg)) return false;
            return msg.SerializeToString(out);
        }
        // 收藏夹：客户端 → 服务端的三种请求
        case im::MsgType::kFavoriteAddReq: {
            im::FavoriteAddReq msg;
            if (!fromJson(json, &msg)) return false;
            return msg.SerializeToString(out);
        }
        case im::MsgType::kFavoriteListReq: {
            im::FavoriteListReq msg;
            if (!fromJson(json, &msg)) return false;
            return msg.SerializeToString(out);
        }
        case im::MsgType::kFavoriteRemoveReq: {
            im::FavoriteRemoveReq msg;
            if (!fromJson(json, &msg)) return false;
            return msg.SerializeToString(out);
        }
        // 撤回：客户端 → 服务端
        case im::MsgType::kRecallReq: {
            im::RecallReq msg;
            if (!fromJson(json, &msg)) return false;
            return msg.SerializeToString(out);
        }
        // 拍一拍：客户端 → 服务端
        case im::MsgType::kPatReq: {
            im::PatReq msg;
            if (!fromJson(json, &msg)) return false;
            return msg.SerializeToString(out);
        }
        default:
            // 其余类型只能由服务端下发
            return false;
    }
}

bool parseEnvelope(std::string_view text, std::uint32_t* type, std::string* body_json) {
    im::WebEnvelope envelope;
    if (!fromJson(text, &envelope)) {
        return false;
    }
    if (type != nullptr) {
        *type = envelope.type();
    }
    if (body_json != nullptr) {
        *body_json = envelope.body();
    }
    return true;
}

std::string makeEnvelope(std::uint32_t type, const google::protobuf::Message& inner) {
    im::WebEnvelope envelope;
    envelope.set_type(type);
    envelope.set_body(toJson(inner));
    return toJson(envelope);
}

std::string makeErrorEnvelope(std::int32_t code, std::string_view message) {
    im::ErrorPush err;
    err.set_code(code);
    err.set_message(std::string(message));
    return makeEnvelope(static_cast<std::uint32_t>(im::MsgType::kErrorPush), err);
}

}  // namespace im::jsonc
