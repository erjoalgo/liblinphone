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

#include <sys/types.h>
#include <sys/stat.h>
#include "linphone/core.h"
#include "linphone/lpconfig.h"
#include "liblinphone_tester.h"
#include "tester_utils.h"
#include "mediastreamer2/msutils.h"
#include "belle-sip/sipstack.h"
#include <bctoolbox/defs.h>

#ifdef _WIN32
#define unlink _unlink
#ifndef F_OK
#define F_OK 00 /*visual studio does not define F_OK*/
#endif
#endif

static void srtp_call(void) {
	call_base(LinphoneMediaEncryptionSRTP,FALSE,FALSE,LinphonePolicyNoFirewall,FALSE);
}

static void srtp_call_non_zero_tag(void) {
	LinphoneCoreManager* marie = linphone_core_manager_new("marie_rc");
	linphone_core_set_media_encryption(marie->lc,LinphoneMediaEncryptionSRTP);
	linphone_core_set_media_encryption_mandatory(marie->lc, TRUE);

	LinphoneCoreManager* pauline = linphone_core_manager_new(transport_supported(LinphoneTransportTls) ? "pauline_rc" : "pauline_tcp_rc");
	linphone_core_set_media_encryption(pauline->lc,LinphoneMediaEncryptionSRTP);
	linphone_core_set_media_encryption_mandatory(pauline->lc, TRUE);
	linphone_config_set_int(linphone_core_get_config(pauline->lc), "sip", "crypto_suite_tag_starting_value", 264);

	linphone_core_invite_address(pauline->lc,marie->identity);
	BC_ASSERT_TRUE(wait_for(marie->lc, pauline->lc, &pauline->stat.number_of_LinphoneCallOutgoingInit, 1));
	BC_ASSERT_TRUE(wait_for(marie->lc, pauline->lc, &pauline->stat.number_of_LinphoneCallOutgoingProgress, 1));
	BC_ASSERT_TRUE(wait_for(marie->lc, pauline->lc, &marie->stat.number_of_LinphoneCallIncomingReceived,1));
	BC_ASSERT_TRUE(wait_for(marie->lc, pauline->lc, &pauline->stat.number_of_LinphoneCallOutgoingRinging,1));
	linphone_call_accept(linphone_core_get_current_call(marie->lc));
	liblinphone_tester_check_rtcp(marie, pauline);
	BC_ASSERT_TRUE(wait_for(marie->lc, pauline->lc, &marie->stat.number_of_LinphoneCallConnected,1));
	BC_ASSERT_TRUE(wait_for(marie->lc, pauline->lc, &marie->stat.number_of_LinphoneCallStreamsRunning,1));
	BC_ASSERT_TRUE(wait_for(marie->lc, pauline->lc, &pauline->stat.number_of_LinphoneCallConnected,1));
	BC_ASSERT_TRUE(wait_for(marie->lc, pauline->lc, &pauline->stat.number_of_LinphoneCallStreamsRunning,1));
	end_call(pauline, marie);

	linphone_core_manager_destroy(pauline);
	linphone_core_manager_destroy(marie);
}

/*
 *Purpose of this test is to check that even if caller and callee does not have exactly the same crypto suite configured, the matching crypto suite is used.
 */
static void srtp_call_with_different_crypto_suite(void) {
	call_base_with_configfile(LinphoneMediaEncryptionSRTP,FALSE,FALSE,LinphonePolicyNoFirewall,FALSE, "laure_tcp_rc", "marie_rc");
}

static void mgr_calling_each_other(LinphoneCoreManager * marie, LinphoneCoreManager * pauline) {

	// Reset stats
	reset_counters(&marie->stat);
	reset_counters(&pauline->stat);
	linphone_core_reset_tone_manager_stats(marie->lc);
	linphone_core_reset_tone_manager_stats(pauline->lc);

	BC_ASSERT_TRUE(call(pauline,marie));
	LinphoneCall * marie_call = linphone_core_get_current_call(marie->lc);
	BC_ASSERT_PTR_NOT_NULL(marie_call);
	LinphoneCall * pauline_call = linphone_core_get_current_call(marie->lc);
	BC_ASSERT_PTR_NOT_NULL(pauline_call);
	if (marie_call && pauline_call) {
		liblinphone_tester_check_rtcp(marie, pauline);

		BC_ASSERT_GREATER(linphone_core_manager_get_max_audio_down_bw(marie),70,int,"%i");
		LinphoneCallStats *pauline_stats = linphone_call_get_audio_stats(linphone_core_get_current_call(pauline->lc));
		BC_ASSERT_TRUE(linphone_call_stats_get_download_bandwidth(pauline_stats)>70);
		linphone_call_stats_unref(pauline_stats);
		pauline_stats = NULL;

		end_call(marie, pauline);
	}

	// Reset stats
	reset_counters(&marie->stat);
	reset_counters(&pauline->stat);
	linphone_core_reset_tone_manager_stats(marie->lc);
	linphone_core_reset_tone_manager_stats(pauline->lc);

	BC_ASSERT_TRUE(call(marie, pauline));

	marie_call = linphone_core_get_current_call(marie->lc);
	BC_ASSERT_PTR_NOT_NULL(marie_call);
	pauline_call = linphone_core_get_current_call(marie->lc);
	BC_ASSERT_PTR_NOT_NULL(pauline_call);
	if (marie_call && pauline_call) {
		liblinphone_tester_check_rtcp(pauline, marie);

		BC_ASSERT_GREATER(linphone_core_manager_get_max_audio_down_bw(pauline),70,int,"%i");
		LinphoneCallStats *marie_stats = linphone_call_get_audio_stats(linphone_core_get_current_call(marie->lc));
		BC_ASSERT_TRUE(linphone_call_stats_get_download_bandwidth(marie_stats)>70);
		linphone_call_stats_unref(marie_stats);
		marie_stats = NULL;

		end_call(pauline, marie);
	}
}

static void srtp_call_with_crypto_suite_parameters(void) {
	LinphoneCoreManager* marie = linphone_core_manager_new("marie_rc");
	linphone_core_set_media_encryption(marie->lc,LinphoneMediaEncryptionSRTP);
	linphone_core_set_srtp_crypto_suites(marie->lc, "AES_CM_128_HMAC_SHA1_80, AES_CM_128_HMAC_SHA1_80 UNENCRYPTED_SRTCP, AES_CM_128_HMAC_SHA1_80 UNENCRYPTED_SRTP, AES_CM_128_HMAC_SHA1_80 UNENCRYPTED_SRTP UNENCRYPTED_SRTCP");

	LinphoneCoreManager* pauline = linphone_core_manager_new(transport_supported(LinphoneTransportTls) ? "pauline_rc" : "pauline_tcp_rc");
	linphone_core_set_media_encryption(pauline->lc,LinphoneMediaEncryptionSRTP);
	linphone_core_set_srtp_crypto_suites(pauline->lc, "AES_CM_128_HMAC_SHA1_80 UNENCRYPTED_SRTCP, AES_CM_128_HMAC_SHA1_80 UNENCRYPTED_SRTP, AES_CM_128_HMAC_SHA1_80 UNENCRYPTED_SRTP UNENCRYPTED_SRTCP, AES_CM_128_HMAC_SHA1_80");

	// Marie prefers encrypted but allows unencrypted STRP streams
	// Pauline prefers unencrypted but allows encrypted STRP streams
	mgr_calling_each_other(marie, pauline);

	linphone_core_set_srtp_crypto_suites(pauline->lc, "AES_CM_128_HMAC_SHA1_80 UNENCRYPTED_SRTCP");
	// Marie prefers encrypted but allows unencrypted SRTP streams
	// Pauline supports unencrypted only
	mgr_calling_each_other(marie, pauline);

	linphone_core_set_srtp_crypto_suites(marie->lc, "AES_CM_128_HMAC_SHA1_80");
	linphone_core_set_srtp_crypto_suites(pauline->lc, "AES_CM_128_HMAC_SHA1_80 UNENCRYPTED_SRTCP, AES_CM_128_HMAC_SHA1_80 UNENCRYPTED_SRTP, AES_CM_128_HMAC_SHA1_80 UNENCRYPTED_SRTP UNENCRYPTED_SRTCP, AES_CM_128_HMAC_SHA1_80");
	// Marie supports encrypted only
	// Pauline prefers unencrypted but allows encrypted STRP streams
	mgr_calling_each_other(marie, pauline);

	linphone_core_manager_destroy(pauline);
	linphone_core_manager_destroy(marie);

}

// This test was added to ensure correct parsing of SDP with 2 crypto attributes
static void srtp_call_with_crypto_suite_parameters_2(void) {
	LinphoneCoreManager* marie = linphone_core_manager_new("marie_rc");
	linphone_core_set_media_encryption(marie->lc,LinphoneMediaEncryptionSRTP);
	linphone_core_set_media_encryption_mandatory(marie->lc, TRUE);
	linphone_core_set_srtp_crypto_suites(marie->lc, "AES_CM_128_HMAC_SHA1_80 UNENCRYPTED_SRTP UNENCRYPTED_SRTCP");

	LinphoneCoreManager* pauline = linphone_core_manager_new(transport_supported(LinphoneTransportTls) ? "pauline_rc" : "pauline_tcp_rc");
	linphone_core_set_media_encryption(pauline->lc,LinphoneMediaEncryptionSRTP);
	linphone_core_set_media_encryption_mandatory(pauline->lc, FALSE);
	linphone_core_set_srtp_crypto_suites(pauline->lc, "AES_CM_128_HMAC_SHA1_80 UNENCRYPTED_SRTP");

	LinphoneCall *call = linphone_core_invite_address(marie->lc,pauline->identity);
	linphone_call_ref(call);
	BC_ASSERT_TRUE(wait_for(marie->lc, pauline->lc, &marie->stat.number_of_LinphoneCallOutgoingInit, 1));
	BC_ASSERT_TRUE(wait_for(marie->lc, pauline->lc, &marie->stat.number_of_LinphoneCallOutgoingProgress, 1));
	BC_ASSERT_TRUE(wait_for_until(marie->lc, pauline->lc, &marie->stat.number_of_LinphoneCallError, 1, 6000));
	BC_ASSERT_EQUAL(linphone_call_get_reason(call), LinphoneReasonNotAcceptable, int, "%d");
	BC_ASSERT_EQUAL(pauline->stat.number_of_LinphoneCallIncomingReceived, 0, int, "%d");
	linphone_call_unref(call);

	linphone_core_manager_destroy(pauline);
	linphone_core_manager_destroy(marie);
}

static void srtp_call_with_crypto_suite_parameters_and_mandatory_encryption(void) {
	LinphoneCoreManager* marie = linphone_core_manager_new("marie_rc");
	linphone_core_set_media_encryption(marie->lc,LinphoneMediaEncryptionSRTP);
	linphone_core_set_media_encryption_mandatory(marie->lc, TRUE);
	linphone_core_set_srtp_crypto_suites(marie->lc, "AES_CM_128_HMAC_SHA1_80 UNENCRYPTED_SRTCP UNENCRYPTED_SRTP, AES_CM_128_HMAC_SHA1_80 UNENCRYPTED_SRTP, AES_CM_128_HMAC_SHA1_80 UNENCRYPTED_SRTCP");

	LinphoneCoreManager* pauline = linphone_core_manager_new(transport_supported(LinphoneTransportTls) ? "pauline_rc" : "pauline_tcp_rc");
	linphone_core_set_media_encryption(pauline->lc,LinphoneMediaEncryptionSRTP);
	linphone_core_set_media_encryption_mandatory(pauline->lc, TRUE);
	linphone_core_set_srtp_crypto_suites(pauline->lc, "AES_CM_128_HMAC_SHA1_80 UNENCRYPTED_SRTCP, AES_CM_128_HMAC_SHA1_80 UNENCRYPTED_SRTP, AES_CM_128_HMAC_SHA1_80 UNENCRYPTED_SRTP UNENCRYPTED_SRTCP,AES_CM_128_HMAC_SHA1_80");

	LinphoneCall *call = linphone_core_invite_address(marie->lc,pauline->identity);
	linphone_call_ref(call);
	BC_ASSERT_TRUE(wait_for(marie->lc, pauline->lc, &marie->stat.number_of_LinphoneCallOutgoingInit, 1));
	BC_ASSERT_TRUE(wait_for(marie->lc, pauline->lc, &marie->stat.number_of_LinphoneCallOutgoingProgress, 1));
	BC_ASSERT_TRUE(wait_for_until(marie->lc, pauline->lc, &marie->stat.number_of_LinphoneCallError, 1, 6000));
	BC_ASSERT_EQUAL(linphone_call_get_reason(call), LinphoneReasonNotAcceptable, int, "%d");
	BC_ASSERT_EQUAL(pauline->stat.number_of_LinphoneCallIncomingReceived, 0, int, "%d");
	linphone_call_unref(call);

	// Marie answers with an inactive audio stream hence the call aborts
	reset_counters(&marie->stat);
	reset_counters(&pauline->stat);
	call = linphone_core_invite_address(pauline->lc,marie->identity);
	linphone_call_ref(call);
	BC_ASSERT_TRUE(wait_for(marie->lc, pauline->lc, &pauline->stat.number_of_LinphoneCallOutgoingInit, 1));
	BC_ASSERT_TRUE(wait_for(marie->lc, pauline->lc, &pauline->stat.number_of_LinphoneCallOutgoingProgress, 1));
	BC_ASSERT_TRUE(wait_for(marie->lc, pauline->lc, &marie->stat.number_of_LinphoneCallIncomingReceived,1));
	BC_ASSERT_TRUE(wait_for(marie->lc, pauline->lc, &pauline->stat.number_of_LinphoneCallOutgoingRinging,1));
	linphone_call_accept(linphone_core_get_current_call(marie->lc));
	BC_ASSERT_TRUE(wait_for(marie->lc, pauline->lc, &marie->stat.number_of_LinphoneCallConnected,1));
	BC_ASSERT_TRUE(wait_for(marie->lc, pauline->lc, &marie->stat.number_of_LinphoneCallStreamsRunning,1));
	BC_ASSERT_TRUE(wait_for(marie->lc, pauline->lc, &pauline->stat.number_of_LinphoneCallConnected,1));
	BC_ASSERT_TRUE(wait_for_until(marie->lc, pauline->lc, &pauline->stat.number_of_LinphoneCallError, 1, 6000));
	BC_ASSERT_EQUAL(linphone_call_get_reason(call), LinphoneReasonNone, int, "%d");
	BC_ASSERT_TRUE(wait_for(marie->lc, pauline->lc, &pauline->stat.number_of_LinphoneCallReleased,1));
	BC_ASSERT_TRUE(wait_for(marie->lc, pauline->lc, &marie->stat.number_of_LinphoneCallEnd,1));
	BC_ASSERT_TRUE(wait_for(marie->lc, pauline->lc, &marie->stat.number_of_LinphoneCallReleased,1));
	linphone_call_unref(call);

	linphone_core_manager_destroy(pauline);
	linphone_core_manager_destroy(marie);
}

static void srtp_call_with_crypto_suite_parameters_and_mandatory_encryption_2(void) {
	LinphoneCoreManager* marie = linphone_core_manager_new("marie_rc");
	linphone_core_set_media_encryption(marie->lc,LinphoneMediaEncryptionSRTP);
	linphone_core_set_media_encryption_mandatory(marie->lc, TRUE);
	linphone_core_set_srtp_crypto_suites(marie->lc, "AES_CM_128_HMAC_SHA1_80 UNENCRYPTED_SRTCP UNENCRYPTED_SRTP, AES_CM_128_HMAC_SHA1_80 UNENCRYPTED_SRTP, AES_CM_128_HMAC_SHA1_80 UNENCRYPTED_SRTCP");

	LinphoneCoreManager* pauline = linphone_core_manager_new(transport_supported(LinphoneTransportTls) ? "pauline_rc" : "pauline_tcp_rc");
	linphone_core_set_media_encryption(pauline->lc,LinphoneMediaEncryptionSRTP);
	linphone_core_set_media_encryption_mandatory(pauline->lc, TRUE);

	LinphoneCall *call = linphone_core_invite_address(marie->lc,pauline->identity);
	linphone_call_ref(call);
	BC_ASSERT_TRUE(wait_for(marie->lc, pauline->lc, &marie->stat.number_of_LinphoneCallOutgoingInit, 1));
	BC_ASSERT_TRUE(wait_for(marie->lc, pauline->lc, &marie->stat.number_of_LinphoneCallOutgoingProgress, 1));
	BC_ASSERT_TRUE(wait_for_until(marie->lc, pauline->lc, &marie->stat.number_of_LinphoneCallError, 1, 6000));
	BC_ASSERT_EQUAL(linphone_call_get_reason(call), LinphoneReasonNotAcceptable, int, "%d");
	BC_ASSERT_EQUAL(pauline->stat.number_of_LinphoneCallIncomingReceived, 0, int, "%d");
	linphone_call_unref(call);

	// Marie answers with an inactive audio stream hence the call aborts
	reset_counters(&marie->stat);
	reset_counters(&pauline->stat);
	call = linphone_core_invite_address(pauline->lc,marie->identity);
	linphone_call_ref(call);
	BC_ASSERT_TRUE(wait_for(marie->lc, pauline->lc, &pauline->stat.number_of_LinphoneCallOutgoingInit, 1));
	BC_ASSERT_TRUE(wait_for(marie->lc, pauline->lc, &pauline->stat.number_of_LinphoneCallOutgoingProgress, 1));
	BC_ASSERT_TRUE(wait_for(marie->lc, pauline->lc, &marie->stat.number_of_LinphoneCallIncomingReceived,1));
	BC_ASSERT_TRUE(wait_for(marie->lc, pauline->lc, &pauline->stat.number_of_LinphoneCallOutgoingRinging,1));
	linphone_call_accept(linphone_core_get_current_call(marie->lc));
	BC_ASSERT_TRUE(wait_for(marie->lc, pauline->lc, &marie->stat.number_of_LinphoneCallConnected,1));
	BC_ASSERT_TRUE(wait_for(marie->lc, pauline->lc, &marie->stat.number_of_LinphoneCallStreamsRunning,1));
	BC_ASSERT_TRUE(wait_for(marie->lc, pauline->lc, &pauline->stat.number_of_LinphoneCallConnected,1));
	BC_ASSERT_TRUE(wait_for_until(marie->lc, pauline->lc, &pauline->stat.number_of_LinphoneCallError, 1, 6000));
	BC_ASSERT_EQUAL(linphone_call_get_reason(call), LinphoneReasonNone, int, "%d");
	BC_ASSERT_TRUE(wait_for(marie->lc, pauline->lc, &pauline->stat.number_of_LinphoneCallReleased,1));
	BC_ASSERT_TRUE(wait_for(marie->lc, pauline->lc, &marie->stat.number_of_LinphoneCallEnd,1));
	BC_ASSERT_TRUE(wait_for(marie->lc, pauline->lc, &marie->stat.number_of_LinphoneCallReleased,1));
	linphone_call_unref(call);

	linphone_core_manager_destroy(pauline);
	linphone_core_manager_destroy(marie);

}

static void srtp_call_with_crypto_suite_parameters_and_mandatory_encryption_3(void) {
	LinphoneCoreManager* marie = linphone_core_manager_new("marie_rc");
	linphone_core_set_media_encryption(marie->lc,LinphoneMediaEncryptionSRTP);
	linphone_core_set_media_encryption_mandatory(marie->lc, TRUE);
	linphone_core_set_srtp_crypto_suites(marie->lc, "AES_CM_128_HMAC_SHA1_80");

	LinphoneCoreManager* pauline = linphone_core_manager_new(transport_supported(LinphoneTransportTls) ? "pauline_rc" : "pauline_tcp_rc");
	linphone_core_set_media_encryption(pauline->lc,LinphoneMediaEncryptionSRTP);
	linphone_core_set_media_encryption_mandatory(pauline->lc, FALSE);
	linphone_core_set_srtp_crypto_suites(pauline->lc, "AES_CM_128_HMAC_SHA1_80 UNENCRYPTED_SRTCP, AES_CM_128_HMAC_SHA1_80 UNENCRYPTED_SRTP, AES_CM_128_HMAC_SHA1_80 UNENCRYPTED_SRTP UNENCRYPTED_SRTCP");

	LinphoneCall *call = linphone_core_invite_address(marie->lc,pauline->identity);
	linphone_call_ref(call);

	BC_ASSERT_TRUE(wait_for(marie->lc, pauline->lc, &marie->stat.number_of_LinphoneCallOutgoingInit, 1));
	BC_ASSERT_TRUE(wait_for(marie->lc, pauline->lc, &marie->stat.number_of_LinphoneCallOutgoingProgress, 1));
	BC_ASSERT_TRUE(wait_for(marie->lc, pauline->lc, &pauline->stat.number_of_LinphoneCallIncomingReceived,1));
	BC_ASSERT_TRUE(wait_for(marie->lc, pauline->lc, &marie->stat.number_of_LinphoneCallOutgoingRinging,1));
	linphone_call_accept(linphone_core_get_current_call(pauline->lc));
	BC_ASSERT_TRUE(wait_for(marie->lc, pauline->lc, &pauline->stat.number_of_LinphoneCallConnected,1));
	BC_ASSERT_TRUE(wait_for(marie->lc, pauline->lc, &pauline->stat.number_of_LinphoneCallStreamsRunning,1));
	BC_ASSERT_TRUE(wait_for(marie->lc, pauline->lc, &marie->stat.number_of_LinphoneCallConnected,1));
	BC_ASSERT_TRUE(wait_for_until(marie->lc, pauline->lc, &marie->stat.number_of_LinphoneCallError, 1, 6000));
	BC_ASSERT_EQUAL(linphone_call_get_reason(call), LinphoneReasonNone, int, "%d");
	BC_ASSERT_TRUE(wait_for(marie->lc, pauline->lc, &marie->stat.number_of_LinphoneCallReleased,1));
	BC_ASSERT_TRUE(wait_for(marie->lc, pauline->lc, &pauline->stat.number_of_LinphoneCallEnd,1));
	BC_ASSERT_TRUE(wait_for(marie->lc, pauline->lc, &pauline->stat.number_of_LinphoneCallReleased,1));
	linphone_call_unref(call);

	// Marie answers with an inactive audio stream hence the call aborts
	reset_counters(&marie->stat);
	reset_counters(&pauline->stat);
	call = linphone_core_invite_address(pauline->lc,marie->identity);
	linphone_call_ref(call);
	BC_ASSERT_TRUE(wait_for(marie->lc, pauline->lc, &pauline->stat.number_of_LinphoneCallOutgoingInit, 1));
	BC_ASSERT_TRUE(wait_for(marie->lc, pauline->lc, &pauline->stat.number_of_LinphoneCallOutgoingProgress, 1));
	BC_ASSERT_TRUE(wait_for(marie->lc, pauline->lc, &marie->stat.number_of_LinphoneCallIncomingReceived,1));
	BC_ASSERT_TRUE(wait_for(marie->lc, pauline->lc, &pauline->stat.number_of_LinphoneCallOutgoingRinging,1));
	linphone_call_accept(linphone_core_get_current_call(marie->lc));
	BC_ASSERT_TRUE(wait_for(marie->lc, pauline->lc, &marie->stat.number_of_LinphoneCallConnected,1));
	BC_ASSERT_TRUE(wait_for(marie->lc, pauline->lc, &marie->stat.number_of_LinphoneCallStreamsRunning,1));
	BC_ASSERT_TRUE(wait_for(marie->lc, pauline->lc, &pauline->stat.number_of_LinphoneCallConnected,1));
	BC_ASSERT_TRUE(wait_for_until(marie->lc, pauline->lc, &pauline->stat.number_of_LinphoneCallError, 1, 6000));
	BC_ASSERT_EQUAL(linphone_call_get_reason(call), LinphoneReasonNone, int, "%d");
	BC_ASSERT_TRUE(wait_for(marie->lc, pauline->lc, &pauline->stat.number_of_LinphoneCallReleased,1));
	BC_ASSERT_TRUE(wait_for(marie->lc, pauline->lc, &marie->stat.number_of_LinphoneCallEnd,1));
	BC_ASSERT_TRUE(wait_for(marie->lc, pauline->lc, &marie->stat.number_of_LinphoneCallReleased,1));
	linphone_call_unref(call);

	linphone_core_manager_destroy(pauline);
	linphone_core_manager_destroy(marie);

}
static void srtp_call_with_crypto_suite_parameters_and_mandatory_encryption_4(void) {
	LinphoneCoreManager* marie = linphone_core_manager_new("marie_rc");
	linphone_core_set_media_encryption(marie->lc,LinphoneMediaEncryptionSRTP);
	linphone_core_set_srtp_crypto_suites(marie->lc, "AES_CM_128_HMAC_SHA1_80, AES_CM_128_HMAC_SHA1_80 UNENCRYPTED_SRTCP, AES_CM_128_HMAC_SHA1_80 UNENCRYPTED_SRTP, AES_CM_128_HMAC_SHA1_80 UNENCRYPTED_SRTP UNENCRYPTED_SRTCP");
	linphone_core_set_media_encryption_mandatory(marie->lc, TRUE);

	LinphoneCoreManager* pauline = linphone_core_manager_new(transport_supported(LinphoneTransportTls) ? "pauline_rc" : "pauline_tcp_rc");
	linphone_core_set_media_encryption(pauline->lc,LinphoneMediaEncryptionSRTP);
	linphone_core_set_srtp_crypto_suites(pauline->lc, "AES_CM_128_HMAC_SHA1_80 UNENCRYPTED_SRTCP, AES_CM_128_HMAC_SHA1_80 UNENCRYPTED_SRTP, AES_CM_128_HMAC_SHA1_80 UNENCRYPTED_SRTP UNENCRYPTED_SRTCP, AES_CM_128_HMAC_SHA1_80");
	linphone_core_set_media_encryption_mandatory(pauline->lc, TRUE);

	mgr_calling_each_other(marie, pauline);

	linphone_core_manager_destroy(pauline);
	linphone_core_manager_destroy(marie);

}
static void zrtp_call(void) {
	call_base(LinphoneMediaEncryptionZRTP,FALSE,FALSE,LinphonePolicyNoFirewall,FALSE);
}

static void zrtp_sas_call(void) {
	call_base_with_configfile(LinphoneMediaEncryptionZRTP,FALSE,FALSE,LinphonePolicyNoFirewall,FALSE, "marie_zrtp_b256_rc", "pauline_zrtp_b256_rc");
	call_base_with_configfile(LinphoneMediaEncryptionZRTP,FALSE,FALSE,LinphonePolicyNoFirewall,FALSE, "marie_zrtp_b256_rc", "pauline_tcp_rc");
}

static void zrtp_cipher_call(void) {
	call_base_with_configfile(LinphoneMediaEncryptionZRTP,FALSE,FALSE,LinphonePolicyNoFirewall,FALSE, "marie_zrtp_srtpsuite_aes256_rc", "pauline_zrtp_srtpsuite_aes256_rc");
	call_base_with_configfile(LinphoneMediaEncryptionZRTP,FALSE,FALSE,LinphonePolicyNoFirewall,FALSE, "marie_zrtp_aes256_rc", "pauline_zrtp_aes256_rc");
	call_base_with_configfile(LinphoneMediaEncryptionZRTP,FALSE,FALSE,LinphonePolicyNoFirewall,FALSE, "marie_zrtp_aes256_rc", "pauline_tcp_rc");
}

static void zrtp_key_agreement_call(void) {
	call_base_with_configfile(LinphoneMediaEncryptionZRTP,FALSE,FALSE,LinphonePolicyNoFirewall,FALSE, "marie_zrtp_ecdh255_rc", "pauline_zrtp_ecdh255_rc");
	call_base_with_configfile(LinphoneMediaEncryptionZRTP,FALSE,FALSE,LinphonePolicyNoFirewall,FALSE, "marie_zrtp_ecdh448_rc", "pauline_zrtp_ecdh448_rc");
}

static void dtls_srtp_call(void) {
	call_base(LinphoneMediaEncryptionDTLS,FALSE,FALSE,LinphonePolicyNoFirewall,FALSE);
}

static void dtls_srtp_call_with_ice(void) {
	call_base(LinphoneMediaEncryptionDTLS,FALSE,FALSE,LinphonePolicyUseIce,FALSE);
}

static void dtls_srtp_call_with_ice_and_dtls_start_immediate(void) {
	call_base_with_configfile(LinphoneMediaEncryptionDTLS, FALSE, FALSE, LinphonePolicyUseIce, FALSE, "marie_dtls_srtp_immediate_rc", "pauline_dtls_srtp_immediate_rc");
}

static void dtls_srtp_call_with_media_realy(void) {
	call_base(LinphoneMediaEncryptionDTLS,FALSE,TRUE,LinphonePolicyNoFirewall,FALSE);
}

static void zrtp_silent_call(void) {
	call_base_with_configfile_play_nothing(LinphoneMediaEncryptionZRTP,FALSE,TRUE,LinphonePolicyNoFirewall,FALSE,  "marie_rc", "pauline_tcp_rc");
}

static void call_with_declined_srtp(void) {
	LinphoneCoreManager* marie = linphone_core_manager_new("marie_rc");
	LinphoneCoreManager* pauline = linphone_core_manager_new(transport_supported(LinphoneTransportTls) ? "pauline_rc" : "pauline_tcp_rc");
	if (linphone_core_media_encryption_supported(marie->lc,LinphoneMediaEncryptionSRTP)) {
		linphone_core_set_media_encryption(pauline->lc,LinphoneMediaEncryptionSRTP);

		BC_ASSERT_TRUE(call(pauline,marie));

		end_call(marie, pauline);
	} else {
		ms_warning ("not tested because srtp not available");
	}
	linphone_core_manager_destroy(marie);
	linphone_core_manager_destroy(pauline);
}

static void call_srtp_paused_and_resumed(void) {
	/*
	 * This test was made to evidence a bug due to internal usage of current_params while not yet filled by linphone_call_get_current_params().
	 * As a result it must not use the call() function because it calls linphone_call_get_current_params().
	 */
	LinphoneCoreManager* marie = linphone_core_manager_new("marie_rc");
	LinphoneCoreManager* pauline = linphone_core_manager_new(transport_supported(LinphoneTransportTls) ? "pauline_rc" : "pauline_tcp_rc");
	const LinphoneCallParams *params;
	LinphoneCall *pauline_call;

	if (!linphone_core_media_encryption_supported(marie->lc,LinphoneMediaEncryptionSRTP)) goto end;
	linphone_core_set_media_encryption(pauline->lc,LinphoneMediaEncryptionSRTP);

	linphone_core_invite_address(pauline->lc, marie->identity);

	if (!BC_ASSERT_TRUE(wait_for(pauline->lc,marie->lc,&marie->stat.number_of_LinphoneCallIncomingReceived,1))) goto end;
	pauline_call = linphone_core_get_current_call(pauline->lc);
	linphone_call_accept(linphone_core_get_current_call(marie->lc));

	if (!BC_ASSERT_TRUE(wait_for(pauline->lc,marie->lc,&marie->stat.number_of_LinphoneCallStreamsRunning,1))) goto end;
	if (!BC_ASSERT_TRUE(wait_for(pauline->lc,marie->lc,&pauline->stat.number_of_LinphoneCallStreamsRunning,1))) goto end;

	linphone_call_pause(pauline_call);

	BC_ASSERT_TRUE(wait_for(pauline->lc,marie->lc,&pauline->stat.number_of_LinphoneCallPaused,1));
	BC_ASSERT_TRUE(wait_for(pauline->lc,marie->lc,&marie->stat.number_of_LinphoneCallPausedByRemote,1));

	linphone_call_resume(pauline_call);
	if (!BC_ASSERT_TRUE(wait_for(pauline->lc,marie->lc,&marie->stat.number_of_LinphoneCallStreamsRunning,2))) goto end;
	if (!BC_ASSERT_TRUE(wait_for(pauline->lc,marie->lc,&pauline->stat.number_of_LinphoneCallStreamsRunning,2))) goto end;

	/*assert that after pause and resume, SRTP is still being used*/
	params = linphone_call_get_current_params(linphone_core_get_current_call(pauline->lc));
	BC_ASSERT_EQUAL(linphone_call_params_get_media_encryption(params) , LinphoneMediaEncryptionSRTP, int, "%d");
	params = linphone_call_get_current_params(linphone_core_get_current_call(marie->lc));
	BC_ASSERT_EQUAL(linphone_call_params_get_media_encryption(params) , LinphoneMediaEncryptionSRTP, int, "%d");

	end_call(pauline, marie);
end:
	linphone_core_manager_destroy(marie);
	linphone_core_manager_destroy(pauline);
}

static void call_with_zrtp_configured_calling_base(LinphoneCoreManager *marie, LinphoneCoreManager *pauline) {
	if (ms_zrtp_available()) {

		linphone_core_set_media_encryption(pauline->lc, LinphoneMediaEncryptionZRTP);
		if (BC_ASSERT_TRUE(call(pauline,marie))){

			liblinphone_tester_check_rtcp(marie,pauline);

			LinphoneCall *call = linphone_core_get_current_call(marie->lc);
			if (!BC_ASSERT_PTR_NOT_NULL(call)) return;
			BC_ASSERT_EQUAL(linphone_call_params_get_media_encryption(linphone_call_get_current_params(call))
						, LinphoneMediaEncryptionZRTP, int, "%i");

			call = linphone_core_get_current_call(pauline->lc);
			if (!BC_ASSERT_PTR_NOT_NULL(call)) return;
			BC_ASSERT_EQUAL(linphone_call_params_get_media_encryption(linphone_call_get_current_params(call))
						, LinphoneMediaEncryptionZRTP, int, "%i");
			end_call(pauline, marie);
		}
	} else {
		ms_warning("Test skipped, ZRTP not available");
	}
}

static void call_with_zrtp_configured_calling_side(void) {
	LinphoneCoreManager* marie = linphone_core_manager_new("marie_rc");
	LinphoneCoreManager* pauline = linphone_core_manager_new(transport_supported(LinphoneTransportTls) ? "pauline_rc" : "pauline_tcp_rc");

	call_with_zrtp_configured_calling_base(marie,pauline);

	/* now set other encryptions mode for receiver(marie), we shall always fall back to caller preference: ZRTP */
	linphone_core_set_media_encryption(marie->lc, LinphoneMediaEncryptionDTLS);
	call_with_zrtp_configured_calling_base(marie,pauline);

	linphone_core_set_media_encryption(marie->lc, LinphoneMediaEncryptionSRTP);
	call_with_zrtp_configured_calling_base(marie,pauline);


	linphone_core_set_media_encryption(marie->lc, LinphoneMediaEncryptionNone);

	linphone_core_set_user_agent(pauline->lc, "Natted Linphone", NULL);
	linphone_core_set_user_agent(marie->lc, "Natted Linphone", NULL);
	call_with_zrtp_configured_calling_base(marie,pauline);

	linphone_core_set_firewall_policy(marie->lc,LinphonePolicyUseIce);
	linphone_core_set_firewall_policy(pauline->lc,LinphonePolicyUseIce);
	call_with_zrtp_configured_calling_base(marie,pauline);

	linphone_core_manager_destroy(marie);
	linphone_core_manager_destroy(pauline);
}

static void call_with_zrtp_configured_callee_base(LinphoneCoreManager *marie, LinphoneCoreManager *pauline) {
	if (ms_zrtp_available()) {

		linphone_core_set_media_encryption(marie->lc, LinphoneMediaEncryptionZRTP);
		if (BC_ASSERT_TRUE(call(pauline,marie))){

			liblinphone_tester_check_rtcp(marie,pauline);

			LinphoneCall *call = linphone_core_get_current_call(marie->lc);
			if (!BC_ASSERT_PTR_NOT_NULL(call)) return;
			BC_ASSERT_EQUAL(linphone_call_params_get_media_encryption(linphone_call_get_current_params(call))
						, LinphoneMediaEncryptionZRTP, int, "%i");

			call = linphone_core_get_current_call(pauline->lc);
			if (!BC_ASSERT_PTR_NOT_NULL(call)) return;
			BC_ASSERT_EQUAL(linphone_call_params_get_media_encryption(linphone_call_get_current_params(call))
						, LinphoneMediaEncryptionZRTP, int, "%i");
			end_call(pauline, marie);
		}
	} else {
		ms_warning("Test skipped, ZRTP not available");
	}
}

static void call_with_zrtp_configured_callee_side(void) {
	LinphoneCoreManager* marie = linphone_core_manager_new("marie_rc");
	LinphoneCoreManager* pauline = linphone_core_manager_new(transport_supported(LinphoneTransportTls) ? "pauline_rc" : "pauline_tcp_rc");

	call_with_zrtp_configured_callee_base(marie,pauline);

	linphone_core_set_user_agent(pauline->lc, "Natted Linphone", NULL);
	linphone_core_set_user_agent(marie->lc, "Natted Linphone", NULL);
	call_with_zrtp_configured_callee_base(marie,pauline);

	linphone_core_set_firewall_policy(marie->lc,LinphonePolicyUseIce);
	linphone_core_set_firewall_policy(pauline->lc,LinphonePolicyUseIce);
	call_with_zrtp_configured_callee_base(marie,pauline);

	linphone_core_manager_destroy(marie);
	linphone_core_manager_destroy(pauline);
}

static bool_t quick_call(LinphoneCoreManager *m1, LinphoneCoreManager *m2){
	linphone_core_invite_address(m1->lc, m2->identity);
	if (!BC_ASSERT_TRUE(wait_for(m1->lc, m2->lc, &m2->stat.number_of_LinphoneCallIncomingReceived, 1)))
		return FALSE;
	linphone_call_accept(linphone_core_get_current_call(m2->lc));
	if (!BC_ASSERT_TRUE(wait_for(m1->lc, m2->lc, &m2->stat.number_of_LinphoneCallStreamsRunning, 1)))
		return FALSE;
	if (!BC_ASSERT_TRUE(wait_for(m1->lc, m2->lc, &m1->stat.number_of_LinphoneCallStreamsRunning, 1)))
		return FALSE;
	return TRUE;
}

static void call_with_encryption_mandatory(bool_t caller_has_encryption_mandatory){
	LinphoneCoreManager* marie = linphone_core_manager_new("marie_rc");
	LinphoneCoreManager* pauline = linphone_core_manager_new(transport_supported(LinphoneTransportTls) ? "pauline_rc" : "pauline_tcp_rc");
	LinphoneCallStats *marie_stats, *pauline_stats;
	/*marie doesn't support ZRTP at all*/
	// marie->lc->zrtp_not_available_simulation=1;
	linphone_core_set_zrtp_not_available_simulation(marie->lc, TRUE);

	/*pauline requests encryption to be mandatory*/
	linphone_core_set_media_encryption(pauline->lc, LinphoneMediaEncryptionZRTP);
	linphone_core_set_media_encryption_mandatory(pauline->lc, TRUE);

	if (!caller_has_encryption_mandatory){
		if (!BC_ASSERT_TRUE(quick_call(marie, pauline))) goto end;
	}else{
		if (!BC_ASSERT_TRUE(quick_call(pauline, marie))) goto end;
	}
	wait_for_until(pauline->lc, marie->lc, NULL, 0, 2000);

	/*assert that no RTP packets have been sent or received by Pauline*/
	/*testing packet_sent doesn't work, because packets dropped by the transport layer are counted as if they were sent.*/
#if 0
	BC_ASSERT_EQUAL(linphone_call_get_audio_stats(linphone_core_get_current_call(pauline->lc))->rtp_stats.packet_sent, 0, int, "%i");
#endif
	/*however we can trust packet_recv from the other party instead */
	marie_stats = linphone_call_get_audio_stats(linphone_core_get_current_call(marie->lc));
	pauline_stats = linphone_call_get_audio_stats(linphone_core_get_current_call(pauline->lc));
	BC_ASSERT_EQUAL((int)linphone_call_stats_get_rtp_stats(marie_stats)->packet_recv, 0, int, "%i");
	BC_ASSERT_EQUAL((int)linphone_call_stats_get_rtp_stats(pauline_stats)->packet_recv, 0, int, "%i");
	linphone_call_stats_unref(marie_stats);
	linphone_call_stats_unref(pauline_stats);
	end_call(marie, pauline);

	end:
	linphone_core_manager_destroy(marie);
	linphone_core_manager_destroy(pauline);
}

static void call_from_plain_rtp_to_zrtp(void){
	call_with_encryption_mandatory(FALSE);
}

static void call_from_zrtp_to_plain_rtp(void){
	call_with_encryption_mandatory(TRUE);
}

static void recreate_zrtpdb_when_corrupted(void) {
	LinphoneCoreManager* marie = linphone_core_manager_new("marie_rc");
	LinphoneCoreManager* pauline = linphone_core_manager_new("pauline_tcp_rc");

	if (BC_ASSERT_TRUE(linphone_core_media_encryption_supported(marie->lc,LinphoneMediaEncryptionZRTP))) {
		void *db;
		const char* db_file;
		const char *corrupt = "corrupt mwahahahaha";
		FILE *f;

		char *filepath = bc_tester_file("tmpZIDCacheMarie.sqlite");
		remove(filepath);
		char *filepath2 = bc_tester_file("tmpZIDCachePauline.sqlite");
		remove(filepath2);
		linphone_core_set_media_encryption(marie->lc,LinphoneMediaEncryptionZRTP);
		linphone_core_set_media_encryption(pauline->lc,LinphoneMediaEncryptionZRTP);
		linphone_core_set_zrtp_secrets_file(marie->lc, filepath);
		linphone_core_set_zrtp_secrets_file(pauline->lc, filepath2);

		BC_ASSERT_TRUE(call(pauline,marie));
		linphone_call_set_authentication_token_verified(linphone_core_get_current_call(marie->lc), TRUE);
		linphone_call_set_authentication_token_verified(linphone_core_get_current_call(pauline->lc), TRUE);
		BC_ASSERT_TRUE(linphone_call_get_authentication_token_verified(linphone_core_get_current_call(marie->lc)));
		BC_ASSERT_TRUE(linphone_call_get_authentication_token_verified(linphone_core_get_current_call(pauline->lc)));
		end_call(marie, pauline);

		db = linphone_core_get_zrtp_cache_db(marie->lc);
		BC_ASSERT_PTR_NOT_NULL(db);

		BC_ASSERT_TRUE(call(pauline,marie));
		BC_ASSERT_TRUE(linphone_call_get_authentication_token_verified(linphone_core_get_current_call(marie->lc)));
		BC_ASSERT_TRUE(linphone_call_get_authentication_token_verified(linphone_core_get_current_call(pauline->lc)));
		end_call(marie, pauline);

		//Corrupt db file
		db_file = linphone_core_get_zrtp_secrets_file(marie->lc);
		BC_ASSERT_PTR_NOT_NULL(db_file);

		f = fopen(db_file, "wb");
		fwrite(corrupt, 1, sizeof(corrupt), f);
		fclose(f);

		//Simulate relaunch of linphone core marie
		linphone_core_set_zrtp_secrets_file(marie->lc, filepath);
		db = linphone_core_get_zrtp_cache_db(marie->lc);
		BC_ASSERT_PTR_NULL(db);

		BC_ASSERT_TRUE(call(pauline,marie));
		linphone_call_set_authentication_token_verified(linphone_core_get_current_call(marie->lc), TRUE);
		linphone_call_set_authentication_token_verified(linphone_core_get_current_call(pauline->lc), TRUE);
		BC_ASSERT_TRUE(linphone_call_get_authentication_token_verified(linphone_core_get_current_call(marie->lc)));
		BC_ASSERT_TRUE(linphone_call_get_authentication_token_verified(linphone_core_get_current_call(pauline->lc)));
		end_call(marie, pauline);

		BC_ASSERT_TRUE(call(pauline,marie));
		BC_ASSERT_FALSE(linphone_call_get_authentication_token_verified(linphone_core_get_current_call(marie->lc)));
		BC_ASSERT_FALSE(linphone_call_get_authentication_token_verified(linphone_core_get_current_call(pauline->lc)));
		end_call(marie, pauline);

		//Db file should be recreated after corruption
		//Simulate relaunch of linphone core marie
		linphone_core_set_zrtp_secrets_file(marie->lc, filepath);

		BC_ASSERT_TRUE(call(pauline,marie));
		linphone_call_set_authentication_token_verified(linphone_core_get_current_call(marie->lc), TRUE);
		linphone_call_set_authentication_token_verified(linphone_core_get_current_call(pauline->lc), TRUE);
		BC_ASSERT_TRUE(linphone_call_get_authentication_token_verified(linphone_core_get_current_call(marie->lc)));
		BC_ASSERT_TRUE(linphone_call_get_authentication_token_verified(linphone_core_get_current_call(pauline->lc)));
		end_call(marie, pauline);

		db = linphone_core_get_zrtp_cache_db(marie->lc);
		BC_ASSERT_PTR_NOT_NULL(db);
		db_file = linphone_core_get_zrtp_secrets_file(marie->lc);
		BC_ASSERT_PTR_NOT_NULL(db_file);

		BC_ASSERT_TRUE(call(pauline,marie));
		BC_ASSERT_TRUE(linphone_call_get_authentication_token_verified(linphone_core_get_current_call(marie->lc)));
		BC_ASSERT_TRUE(linphone_call_get_authentication_token_verified(linphone_core_get_current_call(pauline->lc)));
		end_call(marie, pauline);

		ms_free(filepath);
		ms_free(filepath2);

	}

	linphone_core_manager_destroy(marie);
	linphone_core_manager_destroy(pauline);
}

/*
 * This test verifies that when a user with a specific media encryption (mandatory or not) calls another
 * with a different mandatory media encryption, the call should be in error and the reason should be
 * 488 Not Acceptable.
 */
static void call_declined_encryption_mandatory(LinphoneMediaEncryption enc1, LinphoneMediaEncryption enc2, bool_t mandatory) {
	LinphoneCoreManager* marie = linphone_core_manager_new("marie_rc");
	LinphoneCoreManager* pauline = linphone_core_manager_new("pauline_rc");

	if (!linphone_core_media_encryption_supported(marie->lc, enc1)) goto end;
	linphone_core_set_media_encryption(marie->lc, enc1);
	linphone_core_set_media_encryption_mandatory(marie->lc, TRUE);

	if (!linphone_core_media_encryption_supported(pauline->lc, enc2)) goto end;
	linphone_core_set_media_encryption(pauline->lc, enc2);
	linphone_core_set_media_encryption_mandatory(pauline->lc, mandatory);

	LinphoneCall* out_call = linphone_core_invite_address(pauline->lc,marie->identity);
	linphone_call_ref(out_call);

	/* We expect a 488 Not Acceptable */
	BC_ASSERT_TRUE(wait_for(pauline->lc, marie->lc, &pauline->stat.number_of_LinphoneCallError, 1));
	BC_ASSERT_EQUAL(linphone_call_get_reason(out_call), LinphoneReasonNotAcceptable, int, "%d");
	
	linphone_call_unref(out_call);
	
end:
	linphone_core_manager_destroy(marie);
	linphone_core_manager_destroy(pauline);
}

static void call_declined_encryption_mandatory_both_sides(void) {
	/* If SRTP wasn't mandatory then the call would not error, so it's a good case to test both mandatory */
	call_declined_encryption_mandatory(LinphoneMediaEncryptionZRTP, LinphoneMediaEncryptionSRTP, TRUE);
}

static void zrtp_mandatory_called_by_non_zrtp(void) {
	/* We do not try with None or SRTP as it will accept the call and then set the media to ZRTP */
	call_declined_encryption_mandatory(LinphoneMediaEncryptionZRTP, LinphoneMediaEncryptionDTLS, FALSE);
}

static void srtp_mandatory_called_by_non_srtp(void) {
	call_declined_encryption_mandatory(LinphoneMediaEncryptionSRTP, LinphoneMediaEncryptionNone, FALSE);
	call_declined_encryption_mandatory(LinphoneMediaEncryptionSRTP, LinphoneMediaEncryptionZRTP, FALSE);
	call_declined_encryption_mandatory(LinphoneMediaEncryptionSRTP, LinphoneMediaEncryptionDTLS, FALSE);
}

static void srtp_dtls_mandatory_called_by_non_srtp_dtls(void) {
	/* We do not try with SRTP as it will accept the call and then set the media to DTLS */
	call_declined_encryption_mandatory(LinphoneMediaEncryptionDTLS, LinphoneMediaEncryptionNone, FALSE);
	call_declined_encryption_mandatory(LinphoneMediaEncryptionDTLS, LinphoneMediaEncryptionZRTP, FALSE);
}

static void zrtp_mandatory_called_by_srtp(void) {
	LinphoneCoreManager* marie = linphone_core_manager_new("marie_rc");
	LinphoneCoreManager* pauline = linphone_core_manager_new("pauline_rc");

	if (!linphone_core_media_encryption_supported(marie->lc, LinphoneMediaEncryptionZRTP)) goto end;
	linphone_core_set_media_encryption(marie->lc, LinphoneMediaEncryptionZRTP);
	linphone_core_set_media_encryption_mandatory(marie->lc, TRUE);

	if (!linphone_core_media_encryption_supported(pauline->lc, LinphoneMediaEncryptionSRTP)) goto end;
	linphone_core_set_media_encryption(pauline->lc, LinphoneMediaEncryptionSRTP);

	if (BC_ASSERT_TRUE(quick_call(pauline, marie))) {
		LinphoneCall *call = linphone_core_get_current_call(marie->lc);
		if (!BC_ASSERT_PTR_NOT_NULL(call)) goto end;

		BC_ASSERT_TRUE(wait_for(pauline->lc, marie->lc, &pauline->stat.number_of_LinphoneCallEncryptedOn, 1));

		wait_for_until(marie->lc, pauline->lc, NULL, 0, 1000);

		/*
		 * Marie is in ZRTP mandatory and Pauline in SRTP not mandatory.
		 * Declining SRTP with a 488 provokes a retry without SRTP, so the call should be in ZRTP.
		 */
		BC_ASSERT_EQUAL(linphone_call_params_get_media_encryption(linphone_call_get_current_params(call))
					, LinphoneMediaEncryptionZRTP, int, "%i");

		LinphoneCallParams *params = linphone_core_create_call_params(pauline->lc, linphone_core_get_current_call(pauline->lc));
		if (!BC_ASSERT_PTR_NOT_NULL(params)) goto end;

		/* We test that a reinvite with SRTP is still not acceptable and thus do not change the encryption. */
		linphone_call_params_set_media_encryption(params, LinphoneMediaEncryptionSRTP);
		linphone_call_update(linphone_core_get_current_call(pauline->lc), params);

		wait_for_until(marie->lc, pauline->lc, NULL, 0, 1000);
		BC_ASSERT_EQUAL(linphone_call_params_get_media_encryption(linphone_call_get_current_params(call))
					, LinphoneMediaEncryptionZRTP, int, "%i");

		end_call(pauline, marie);
		linphone_call_params_unref(params);
	}
	
end:
	linphone_core_manager_destroy(marie);
	linphone_core_manager_destroy(pauline);
}

static void video_srtp_call_without_audio(void) {
	/*
	 * The purpose of this test is to ensure SRTP is still present in the SDP event if the audio stream is disabled
	 */
	LinphoneCoreManager* marie = linphone_core_manager_new("marie_rc");
	LinphoneCoreManager* pauline = linphone_core_manager_new(transport_supported(LinphoneTransportTls) ? "pauline_rc" : "pauline_tcp_rc");
	LinphoneCallParams *pauline_params;
	const LinphoneCallParams *params;
	LinphoneVideoPolicy vpol;
	vpol.automatically_accept = TRUE;
	vpol.automatically_initiate = TRUE;

	if (!linphone_core_media_encryption_supported(marie->lc, LinphoneMediaEncryptionSRTP)) goto end;
	linphone_core_set_media_encryption(pauline->lc, LinphoneMediaEncryptionSRTP);

	linphone_core_set_video_policy(marie->lc, &vpol);
	linphone_core_enable_video_capture(marie->lc, TRUE);
	linphone_core_enable_video_display(marie->lc, TRUE);

	linphone_core_set_video_policy(pauline->lc, &vpol);
	linphone_core_enable_video_capture(pauline->lc, TRUE);
	linphone_core_enable_video_display(pauline->lc, TRUE);

	pauline_params = linphone_core_create_call_params(pauline->lc, NULL);
	linphone_call_params_enable_audio(pauline_params, FALSE);
	linphone_call_params_enable_video(pauline_params, TRUE);
	BC_ASSERT_EQUAL(linphone_call_params_get_media_encryption(pauline_params), LinphoneMediaEncryptionSRTP, int, "%i");
	linphone_core_invite_address_with_params(pauline->lc, marie->identity, pauline_params);
	linphone_call_params_unref(pauline_params);

	if (!BC_ASSERT_TRUE(wait_for(pauline->lc,marie->lc,&marie->stat.number_of_LinphoneCallIncomingReceived,1))) goto end;
	/*assert that SRTP is being used*/
	params = linphone_call_get_params(linphone_core_get_current_call(pauline->lc));
	BC_ASSERT_EQUAL(linphone_call_params_get_media_encryption(params) , LinphoneMediaEncryptionSRTP, int, "%d");
	params = linphone_call_get_remote_params(linphone_core_get_current_call(marie->lc));
	BC_ASSERT_EQUAL(linphone_call_params_get_media_encryption(params) , LinphoneMediaEncryptionSRTP, int, "%d");

	linphone_core_accept_call(marie->lc, linphone_core_get_current_call(marie->lc));
	wait_for_until(marie->lc, pauline->lc, NULL, 0, 1000);
	if (!BC_ASSERT_TRUE(wait_for(pauline->lc,marie->lc,&marie->stat.number_of_LinphoneCallStreamsRunning,1))) goto end;

	/*assert that SRTP is being used*/
	params = linphone_call_get_current_params(linphone_core_get_current_call(pauline->lc));
	BC_ASSERT_EQUAL(linphone_call_params_get_media_encryption(params) , LinphoneMediaEncryptionSRTP, int, "%d");
	params = linphone_call_get_current_params(linphone_core_get_current_call(marie->lc));
	BC_ASSERT_EQUAL(linphone_call_params_get_media_encryption(params) , LinphoneMediaEncryptionSRTP, int, "%d");

	end_call(pauline, marie);
end:
	linphone_core_manager_destroy(marie);
	linphone_core_manager_destroy(pauline);
}

static bool_t setup_dtls_srtp(LinphoneCoreManager *marie, LinphoneCoreManager *pauline){
	if (!linphone_core_media_encryption_supported(marie->lc,LinphoneMediaEncryptionDTLS)){
		BC_FAIL("SRTP-DTLS not supported.");
		return FALSE;
	}
	linphone_core_set_media_encryption(marie->lc, LinphoneMediaEncryptionDTLS);
	linphone_core_set_media_encryption(pauline->lc, LinphoneMediaEncryptionDTLS);
	char *path = bc_tester_file("certificates-marie");
	linphone_core_set_user_certificates_path(marie->lc, path);
	bc_free(path);
	path = bc_tester_file("certificates-pauline");
	linphone_core_set_user_certificates_path(pauline->lc, path);
	bc_free(path);
	bctbx_mkdir(linphone_core_get_user_certificates_path(marie->lc));
	bctbx_mkdir(linphone_core_get_user_certificates_path(pauline->lc));
	return TRUE;
}

static void _dtls_srtp_audio_call_with_rtcp_mux(bool_t rtcp_mux_not_accepted) {
	LinphoneCoreManager* marie;
	LinphoneCoreManager* pauline;
	LinphoneCall *pauline_call, *marie_call;
	
	marie = linphone_core_manager_new( "marie_rc");
	pauline = linphone_core_manager_new(transport_supported(LinphoneTransportTls) ? "pauline_rc" : "pauline_tcp_rc");
	
	linphone_config_set_int(linphone_core_get_config(marie->lc), "rtp", "rtcp_mux", 1);
	if (!rtcp_mux_not_accepted) linphone_config_set_int(linphone_core_get_config(pauline->lc), "rtp", "rtcp_mux", 1);
	
	setup_dtls_srtp(marie, pauline);
	{
		/*enable ICE on both ends*/
		LinphoneNatPolicy *pol;
		pol = linphone_core_get_nat_policy(marie->lc);
		linphone_nat_policy_enable_ice(pol, TRUE);
		linphone_nat_policy_enable_stun(pol, TRUE);
		linphone_core_set_nat_policy(marie->lc, pol);
		pol = linphone_core_get_nat_policy(pauline->lc);
		linphone_nat_policy_enable_ice(pol, TRUE);
		linphone_nat_policy_enable_stun(pol, TRUE);
		linphone_core_set_nat_policy(pauline->lc, pol);
	}
	
	BC_ASSERT_TRUE(call(marie,pauline));
	pauline_call = linphone_core_get_current_call(pauline->lc);
	marie_call = linphone_core_get_current_call(marie->lc);
	
	if (BC_ASSERT_PTR_NOT_NULL(pauline_call) && BC_ASSERT_PTR_NOT_NULL(marie_call)){
		BC_ASSERT_TRUE(linphone_call_params_get_media_encryption(linphone_call_get_current_params(pauline_call)) == LinphoneMediaEncryptionDTLS);
		BC_ASSERT_TRUE(linphone_call_params_get_media_encryption(linphone_call_get_current_params(marie_call)) == LinphoneMediaEncryptionDTLS);
		liblinphone_tester_check_rtcp(marie,pauline);
	}
	
	end_call(marie,pauline);
	linphone_core_manager_destroy(pauline);
	linphone_core_manager_destroy(marie);
}

static void dtls_srtp_audio_call_with_rtcp_mux(void){
	_dtls_srtp_audio_call_with_rtcp_mux(FALSE);
}

static void dtls_srtp_audio_call_with_rtcp_mux_not_accepted(void){
	_dtls_srtp_audio_call_with_rtcp_mux(TRUE);
}

#ifdef VIDEO_ENABLED
void call_with_several_video_switches_base(const LinphoneMediaEncryption caller_encryption, const LinphoneMediaEncryption callee_encryption) {
	int dummy = 0;
	LinphoneCoreManager* marie = linphone_core_manager_new("marie_rc");
	LinphoneCoreManager* pauline = linphone_core_manager_new(transport_supported(LinphoneTransportTls) ? "pauline_rc" : "pauline_tcp_rc");
	bool_t call_ok;

	if (linphone_core_media_encryption_supported(marie->lc, caller_encryption) && linphone_core_media_encryption_supported(marie->lc, callee_encryption)) {
		linphone_core_set_media_encryption(marie->lc, callee_encryption);
		linphone_core_set_media_encryption(pauline->lc, caller_encryption);

		BC_ASSERT_TRUE(call_ok=call(pauline,marie));
		if (!call_ok) goto end;

		liblinphone_tester_check_rtcp(marie, pauline);

		BC_ASSERT_TRUE(request_video(pauline,marie, TRUE));
		wait_for_until(pauline->lc,marie->lc,&dummy,1,1000); /* Wait for VFU request exchanges to be finished. */
		BC_ASSERT_TRUE(remove_video(pauline,marie));
		BC_ASSERT_TRUE(request_video(pauline,marie, TRUE));
		wait_for_until(pauline->lc,marie->lc,&dummy,1,1000); /* Wait for VFU request exchanges to be finished. */
		BC_ASSERT_TRUE(remove_video(pauline,marie));
		/**/
		end_call(pauline, marie);
	} else {
		ms_warning("Not tested because either callee doesn't support %s or caller doesn't support %s.", linphone_media_encryption_to_string(callee_encryption), linphone_media_encryption_to_string(caller_encryption));
	}
end:
	linphone_core_manager_destroy(marie);
	linphone_core_manager_destroy(pauline);
}

static void srtp_call_with_several_video_switches(void) {
	call_with_several_video_switches_base(LinphoneMediaEncryptionSRTP, LinphoneMediaEncryptionSRTP);
}

static void none_to_srtp_call_with_several_video_switches(void) {
	call_with_several_video_switches_base(LinphoneMediaEncryptionNone, LinphoneMediaEncryptionSRTP);
}

static void srtp_to_none_call_with_several_video_switches(void) {
	call_with_several_video_switches_base(LinphoneMediaEncryptionSRTP, LinphoneMediaEncryptionNone);
}

static void zrtp_call_with_several_video_switches(void) {
	call_with_several_video_switches_base(LinphoneMediaEncryptionZRTP, LinphoneMediaEncryptionZRTP);
}

static void none_to_zrtp_call_with_several_video_switches(void) {
	call_with_several_video_switches_base(LinphoneMediaEncryptionNone, LinphoneMediaEncryptionZRTP);
}

static void zrtp_to_none_call_with_several_video_switches(void) {
	call_with_several_video_switches_base(LinphoneMediaEncryptionZRTP, LinphoneMediaEncryptionNone);
}

static void dtls_srtp_call_with_several_video_switches(void) {
	call_with_several_video_switches_base(LinphoneMediaEncryptionDTLS, LinphoneMediaEncryptionDTLS);
}

static void none_to_dtls_srtp_call_with_several_video_switches(void) {
	call_with_several_video_switches_base(LinphoneMediaEncryptionNone, LinphoneMediaEncryptionDTLS);
}

static void dtls_srtp_to_none_call_with_several_video_switches(void) {
	call_with_several_video_switches_base(LinphoneMediaEncryptionDTLS, LinphoneMediaEncryptionNone);
}
#endif // VIDEO_ENABLED

static void call_accepting_all_encryptions(void) {
	LinphoneCoreManager* marie = linphone_core_manager_new("marie_rc");
	linphone_core_set_media_encryption(marie->lc,LinphoneMediaEncryptionSRTP);
	linphone_core_set_media_encryption_mandatory(marie->lc, TRUE);
	linphone_config_set_int(linphone_core_get_config(marie->lc), "rtp", "accept_any_encryption", 1);
	LinphoneCoreManager* pauline = linphone_core_manager_new(transport_supported(LinphoneTransportTls) ? "pauline_rc" : "pauline_tcp_rc");
	linphone_core_set_media_encryption(pauline->lc,LinphoneMediaEncryptionSRTP);
	linphone_core_set_media_encryption_mandatory(pauline->lc, TRUE);
	linphone_config_set_int(linphone_core_get_config(pauline->lc), "rtp", "accept_any_encryption", 1);

	LinphoneCallParams *marie_params = linphone_core_create_call_params(marie->lc, NULL);
	linphone_call_params_set_media_encryption(marie_params, LinphoneMediaEncryptionZRTP);

	LinphoneCallParams *pauline_params = linphone_core_create_call_params(marie->lc, NULL);
	linphone_call_params_set_media_encryption(pauline_params, LinphoneMediaEncryptionZRTP);
	BC_ASSERT_TRUE((call_with_params(marie,pauline,marie_params,pauline_params)));
	linphone_call_params_unref(marie_params);
	linphone_call_params_unref(pauline_params);

	const LinphoneCallParams *params = NULL;
	params = linphone_call_get_current_params(linphone_core_get_current_call(pauline->lc));
	BC_ASSERT_EQUAL(linphone_call_params_get_media_encryption(params) , LinphoneMediaEncryptionZRTP, int, "%d");
	params = linphone_call_get_current_params(linphone_core_get_current_call(marie->lc));
	BC_ASSERT_EQUAL(linphone_call_params_get_media_encryption(params) , LinphoneMediaEncryptionZRTP, int, "%d");

	end_call(pauline, marie);
	linphone_core_manager_destroy(marie);
	linphone_core_manager_destroy(pauline);
}

test_t call_secure_tests[] = {
	TEST_NO_TAG("SRTP call", srtp_call),
	TEST_NO_TAG("SRTP call with non zero crypto suite tag", srtp_call_non_zero_tag),
#ifdef VIDEO_ENABLED
	TEST_NO_TAG("SRTP call with several video switches", srtp_call_with_several_video_switches),
	TEST_NO_TAG("SRTP to none call with several video switches", srtp_to_none_call_with_several_video_switches),
	TEST_NO_TAG("None to SRTP call with several video switches", none_to_srtp_call_with_several_video_switches),
#endif // VIDEO_ENABLED
	TEST_NO_TAG("SRTP call with different crypto suite", srtp_call_with_different_crypto_suite),
	TEST_NO_TAG("SRTP call with crypto suite parameters", srtp_call_with_crypto_suite_parameters),
	TEST_NO_TAG("SRTP call with crypto suite parameters 2", srtp_call_with_crypto_suite_parameters_2),
	TEST_NO_TAG("SRTP call with crypto suite parameters and mandatory encryption", srtp_call_with_crypto_suite_parameters_and_mandatory_encryption),
	TEST_NO_TAG("SRTP call with crypto suite parameters and mandatory encryption 2", srtp_call_with_crypto_suite_parameters_and_mandatory_encryption_2),
	TEST_NO_TAG("SRTP call with crypto suite parameters and mandatory encryption 3", srtp_call_with_crypto_suite_parameters_and_mandatory_encryption_3),
	TEST_NO_TAG("SRTP call with crypto suite parameters and mandatory encryption 4", srtp_call_with_crypto_suite_parameters_and_mandatory_encryption_4),
	TEST_NO_TAG("ZRTP call", zrtp_call),
#ifdef VIDEO_ENABLED
	TEST_NO_TAG("ZRTP call with several video switches", zrtp_call_with_several_video_switches),
	TEST_NO_TAG("ZRTP to none call with several video switches", zrtp_to_none_call_with_several_video_switches),
	TEST_NO_TAG("None to ZRTP call with several video switches", none_to_zrtp_call_with_several_video_switches),
#endif // VIDEO_ENABLED
	TEST_NO_TAG("ZRTP silent call", zrtp_silent_call),
	TEST_NO_TAG("ZRTP SAS call", zrtp_sas_call),
	TEST_NO_TAG("ZRTP Cipher call", zrtp_cipher_call),
	TEST_NO_TAG("ZRTP Key Agreement call", zrtp_key_agreement_call),
	TEST_ONE_TAG("DTLS SRTP call", dtls_srtp_call, "DTLS"),
#ifdef VIDEO_ENABLED
	TEST_ONE_TAG("DTLS SRTP call with several video switches", dtls_srtp_call_with_several_video_switches, "DTLS"),
	TEST_ONE_TAG("DTLS SRTP to none call with several video switches", dtls_srtp_to_none_call_with_several_video_switches, "DTLS"),
	TEST_ONE_TAG("None to DTLS SRTP call with several video switches", none_to_dtls_srtp_call_with_several_video_switches, "DTLS"),
#endif // VIDEO_ENABLED
	TEST_ONE_TAG("DTLS SRTP call with ICE", dtls_srtp_call_with_ice, "DTLS"),
	TEST_ONE_TAG("DTLS SRTP call with ICE and dtls start immediatly", dtls_srtp_call_with_ice_and_dtls_start_immediate, "DTLS"),
	TEST_ONE_TAG("DTLS SRTP call with media relay", dtls_srtp_call_with_media_realy, "DTLS"),
	TEST_NO_TAG("SRTP call with declined srtp", call_with_declined_srtp),
	TEST_NO_TAG("SRTP call paused and resumed", call_srtp_paused_and_resumed),
	TEST_NO_TAG("Call with ZRTP configured calling side only", call_with_zrtp_configured_calling_side),
	TEST_NO_TAG("Call with ZRTP configured receiver side only", call_with_zrtp_configured_callee_side),
	TEST_NO_TAG("Call from plain RTP to ZRTP mandatory should be silent", call_from_plain_rtp_to_zrtp),
	TEST_NO_TAG("Call ZRTP mandatory to plain RTP should be silent", call_from_zrtp_to_plain_rtp),
	TEST_NO_TAG("Recreate ZRTP db file when corrupted", recreate_zrtpdb_when_corrupted),
	TEST_NO_TAG("Call declined with mandatory encryption on both sides", call_declined_encryption_mandatory_both_sides),
	TEST_NO_TAG("ZRTP mandatory called by non ZRTP", zrtp_mandatory_called_by_non_zrtp),
	TEST_NO_TAG("SRTP mandatory called by non SRTP", srtp_mandatory_called_by_non_srtp),
	TEST_NO_TAG("SRTP DTLS mandatory called by non SRTP DTLS", srtp_dtls_mandatory_called_by_non_srtp_dtls),
	TEST_NO_TAG("ZRTP mandatory called by SRTP", zrtp_mandatory_called_by_srtp),
	TEST_NO_TAG("Video SRTP call without audio", video_srtp_call_without_audio),
	TEST_NO_TAG("DTLS-SRTP call with rtcp-mux", dtls_srtp_audio_call_with_rtcp_mux),
	TEST_NO_TAG("DTLS-SRTP call with rtcp-mux not accepted", dtls_srtp_audio_call_with_rtcp_mux_not_accepted),
	TEST_NO_TAG("Call accepting all encryptions", call_accepting_all_encryptions)
};

test_suite_t call_secure_test_suite = {"Secure Call", NULL, NULL, liblinphone_tester_before_each, liblinphone_tester_after_each,
								sizeof(call_secure_tests) / sizeof(call_secure_tests[0]), call_secure_tests};
