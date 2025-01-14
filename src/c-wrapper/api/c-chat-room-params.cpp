/*
 * Copyright (c) 2010-2019 Belledonne Communications SARL.
 *
 * This file is part of Liblinphone.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "linphone/api/c-chat-room-params.h"
#include "chat/chat-room/chat-room-params.h"
#include "c-wrapper/c-wrapper.h"
#include "linphone/wrapper_utils.h"
#include "core/core-p.h"

// =============================================================================

using namespace LinphonePrivate;

// =============================================================================

LinphoneChatRoomParams *linphone_chat_room_params_new(void) {
	return ChatRoomParams::createCObject();
}

LinphoneChatRoomParams *linphone_chat_room_params_new_and_init(LinphoneCore *core) {
	return ChatRoomParams::getDefaults(L_GET_CPP_PTR_FROM_C_OBJECT(core))->toC();
}

LinphoneChatRoomParams *linphone_chat_room_params_clone(const LinphoneChatRoomParams *params) {
	return LinphonePrivate::ChatRoomParams::toCpp(params)->clone()->toC();
}

LinphoneChatRoomParams *linphone_chat_room_params_ref(LinphoneChatRoomParams *params) {
	ChatRoomParams::toCpp(params)->ref();
	return params;
}

void linphone_chat_room_params_unref(LinphoneChatRoomParams *params) {
	ChatRoomParams::toCpp(params)->unref();
}

bool_t linphone_chat_room_params_is_valid(const LinphoneChatRoomParams *params) {
	return ChatRoomParams::toCpp(params)->isValid();
}

LinphoneChatRoomBackend linphone_chat_room_params_get_backend(const LinphoneChatRoomParams *params) {
	return static_cast<LinphoneChatRoomBackend>(ChatRoomParams::toCpp(params)->getChatRoomBackend());
}

LinphoneChatRoomEncryptionBackend linphone_chat_room_params_get_encryption_backend(const LinphoneChatRoomParams *params) {
	return static_cast<LinphoneChatRoomEncryptionBackend>(ChatRoomParams::toCpp(params)->getChatRoomEncryptionBackend());
}

bool_t linphone_chat_room_params_group_enabled(const LinphoneChatRoomParams *params) {
	return ChatRoomParams::toCpp(params)->isGroup();
}

LinphoneChatRoomEphemeralMode linphone_chat_room_params_get_ephemeral_mode(const LinphoneChatRoomParams *params) {
	return static_cast<LinphoneChatRoomEphemeralMode>(ChatRoomParams::toCpp(params)->getEphemeralMode());
}

long linphone_chat_room_params_get_ephemeral_lifetime(const LinphoneChatRoomParams *params) {
	return ChatRoomParams::toCpp(params)->getEphemeralLifetime();
}

bool_t linphone_chat_room_params_encryption_enabled(const LinphoneChatRoomParams *params) {
	return ChatRoomParams::toCpp(params)->isEncrypted();
}

bool_t linphone_chat_room_params_rtt_enabled(const LinphoneChatRoomParams *params) {
	return ChatRoomParams::toCpp(params)->isRealTimeText();
}

void linphone_chat_room_params_set_backend(LinphoneChatRoomParams *params, LinphoneChatRoomBackend backend) {
	ChatRoomParams::toCpp(params)->setChatRoomBackend(static_cast<ChatRoomParams::ChatRoomBackend>(backend));
}

void linphone_chat_room_params_set_encryption_backend(LinphoneChatRoomParams *params, LinphoneChatRoomEncryptionBackend backend) {
	ChatRoomParams::toCpp(params)->setChatRoomEncryptionBackend(static_cast<ChatRoomParams::ChatRoomEncryptionBackend>(backend));
}

void linphone_chat_room_params_enable_group(LinphoneChatRoomParams *params, bool_t group) {
	ChatRoomParams::toCpp(params)->setGroup(!!group);
}

void linphone_chat_room_params_set_ephemeral_mode(LinphoneChatRoomParams *params, LinphoneChatRoomEphemeralMode mode) {
	ChatRoomParams::toCpp(params)->setEphemeralMode(static_cast<AbstractChatRoom::EphemeralMode>(mode));
}

void linphone_chat_room_params_set_ephemeral_lifetime(LinphoneChatRoomParams *params, long lifetime) {
	ChatRoomParams::toCpp(params)->setEphemeralLifetime(lifetime);
}

void linphone_chat_room_params_enable_encryption(LinphoneChatRoomParams *params, bool_t encrypted) {
	ChatRoomParams::toCpp(params)->setEncrypted(!!encrypted);
}

void linphone_chat_room_params_enable_rtt(LinphoneChatRoomParams *params, bool_t rtt) {
	ChatRoomParams::toCpp(params)->setRealTimeText(!!rtt);
}

const char * linphone_chat_room_params_get_subject(const LinphoneChatRoomParams *params) {
	return L_STRING_TO_C(ChatRoomParams::toCpp(params)->getSubject());
}

void linphone_chat_room_params_set_subject(LinphoneChatRoomParams *params, const char *subject) {
	ChatRoomParams::toCpp(params)->setSubject(L_C_TO_STRING(subject));
}
