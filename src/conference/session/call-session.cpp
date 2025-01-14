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

#include <bctoolbox/defs.h>

#include "linphone/api/c-content.h"
#include "linphone/core.h"

#include "account/account.h"
#include "address/address.h"
#include "c-wrapper/c-wrapper.h"
#include "call/call.h"
#include "conference/params/call-session-params-p.h"
#include "conference/session/call-session-p.h"
#include "conference/session/call-session.h"
#include "conference/conference-scheduler.h"
#include "core/core-p.h"
#include "logger/logger.h"

#include "conference_private.h"
#include "private.h"

using namespace std;

LINPHONE_BEGIN_NAMESPACE

// =============================================================================

int CallSessionPrivate::computeDuration () const {
	if (log->getConnectedTime() == 0) {
		if (log->getStartTime() == 0) return 0; 
		return (int)(ms_time(nullptr) - log->getStartTime());
	}
	return (int)(ms_time(nullptr) - log->getConnectedTime());
}

/*
 * Initialize call parameters according to incoming call parameters. This is to avoid to ask later (during reINVITEs) for features that the remote
 * end apparently does not support. This features are: privacy, video...
 */
void CallSessionPrivate::initializeParamsAccordingToIncomingCallParams () {
	currentParams->setPrivacy((LinphonePrivacyMask)op->getPrivacy());
}

void CallSessionPrivate::notifyReferState () {
	SalCallOp *refererOp = referer->getPrivate()->getOp();
	if (refererOp)
		refererOp->notifyReferState(op);
}

void CallSessionPrivate::restorePreviousState(){
	setState(prevState, prevMessageState);
}

void CallSessionPrivate::setState (CallSession::State newState, const string &message) {
	L_Q();

	// Keep a ref on the CallSession, otherwise it might get destroyed before the end of the method
	shared_ptr<CallSession> ref = q->getSharedFromThis();
	if (state != newState){
		prevState = state;
		prevMessageState = messageState;

		// Make sanity checks with call state changes. Any bad transition can result in unpredictable results
		// or irrecoverable errors in the application.
		if ((state == CallSession::State::End) || (state == CallSession::State::Error)) {
			if (newState != CallSession::State::Released) {
				lFatal() << "Abnormal call resurection from " << Utils::toString(state) <<
					" to " << Utils::toString(newState) << " , aborting";
				return;
			}
		} else if ((newState == CallSession::State::Released) && (prevState != CallSession::State::Error) && (prevState != CallSession::State::End)) {
			lFatal() << "Attempt to move CallSession [" << q << "] to Released state while it was not previously in Error or End state, aborting";
			return;
		}
		lInfo() << "CallSession [" << q << "] moving from state " << Utils::toString(state) << " to " << Utils::toString(newState);

		if (newState != CallSession::State::Referred) {
			// CallSession::State::Referred is rather an event, not a state.
			// Indeed it does not change the state of the call (still paused or running).
			state = newState;
			messageState = message;
		}

		switch (newState) {
			case CallSession::State::End:
			case CallSession::State::Error:
				switch (linphone_error_info_get_reason(q->getErrorInfo())) {
					case LinphoneReasonDeclined:
						if (log->getStatus() != LinphoneCallMissed) // Do not re-change the status of a call if it's already set
							log->setStatus(LinphoneCallDeclined);
						break;
					case LinphoneReasonNotAnswered:
						if (log->getDirection() == LinphoneCallIncoming)
							log->setStatus(LinphoneCallMissed);
						break;
					case LinphoneReasonNone:
						if (log->getDirection() == LinphoneCallIncoming) {
							if (ei) {
								int code = linphone_error_info_get_protocol_code(ei);
								if ((code >= 200) && (code < 300))
									log->setStatus(LinphoneCallAcceptedElsewhere);
								else if (code == 487)
									log->setStatus(LinphoneCallMissed);
							}
						}
						break;
					case LinphoneReasonDoNotDisturb:
						if (log->getDirection() == LinphoneCallIncoming) {
							if (ei) {
								int code = linphone_error_info_get_protocol_code(ei);
								if ((code >= 600) && (code < 700))
									log->setStatus(LinphoneCallDeclinedElsewhere);
							}
						}
						break;
					default:
						break;
				}
				setTerminated();
				break;
			case CallSession::State::Connected:
				log->setStatus(LinphoneCallSuccess);
				log->setConnectedTime(ms_time(nullptr));
				break;
			default:
				break;
		}

		if (message.empty()) {
			lError() << "You must fill a reason when changing call state (from " <<
				Utils::toString(prevState) << " to " << Utils::toString(state) << ")";
		}
		if (listener)
			listener->onCallSessionStateChanged(q->getSharedFromThis(), newState, message);

		if (newState == CallSession::State::Released) {
			setReleased(); /* Shall be performed after app notification */
		}
	}
}

void CallSessionPrivate::onCallStateChanged (LinphoneCall *call, LinphoneCallState state, const std::string &message) {
	this->executePendingActions();
}

void CallSessionPrivate::executePendingActions() {
	if ((state != CallSession::State::End) && (state != CallSession::State::Released) && (state != CallSession::State::Error)) {
		std::queue<std::function<LinphoneStatus()>> unsuccessfulActions;
		while (pendingActions.empty() == false) {
			// Store std::function in a temporary variable in order to take it out of the queue before executing it
			const auto f = pendingActions.front();
			pendingActions.pop();
			// Execute method
			const auto result = f();
			if (result != 0) {
				unsuccessfulActions.push(f);
			}
		}
		pendingActions = unsuccessfulActions;
	}
}

void CallSessionPrivate::setTransferState (CallSession::State newState) {
	L_Q();
	if (newState == transferState) {
		lError() << "Unable to change transfer state for CallSession [" << q << "] from ["
			<< Utils::toString(transferState) << "] to [" << Utils::toString(newState) << "]";
		return;
	}
	lInfo() << "Transfer state for CallSession [" << q << "] changed from ["
		<< Utils::toString(transferState) << "] to [" << Utils::toString(newState) << "]";

	transferState = newState;
	if (listener)
		listener->onCallSessionTransferStateChanged(q->getSharedFromThis(), newState);
}

void CallSessionPrivate::handleIncoming (bool tryStartRingtone) {
	L_Q();

	if (tryStartRingtone) { // The state is still in IncomingReceived state. Start ringing if it was needed
		listener->onStartRingtone(q->getSharedFromThis());
	}

	handleIncomingReceivedStateInIncomingNotification();
}

void CallSessionPrivate::startIncomingNotification () {
	L_Q();
	bool_t tryStartRingtone = TRUE;// Try to start a tone if this notification is not a PushIncomingReceived and have listener
	if (listener && state != CallSession::State::PushIncomingReceived)
		listener->onIncomingCallSessionStarted(q->getSharedFromThis());// Can set current call to this sessions
	else
		tryStartRingtone = FALSE;

	setState(CallSession::State::IncomingReceived, "Incoming call received"); // Change state and notify listeners

	// From now on, the application is aware of the call and supposed to take background task or already submitted
	// notification to the user. We can then drop our background task.
	if (listener)
		listener->onBackgroundTaskToBeStopped(q->getSharedFromThis());

	if ((state == CallSession::State::IncomingReceived && linphone_core_auto_send_ringing_enabled(q->getCore()->getCCore()))
		|| state == CallSession::State::IncomingEarlyMedia) { // If early media was accepted during setState callback above
		handleIncoming(tryStartRingtone);
	}

	if (q->mIsAccepting && listener) {
		lInfo() << "CallSession [" << q << "] is accepted early.";
		listener->onCallSessionAccepting(q->getSharedFromThis());
	}
}

bool CallSessionPrivate::startPing () {
	L_Q();
	if (q->getCore()->getCCore()->sip_conf.ping_with_options) {
		/* Defer the start of the call after the OPTIONS ping for outgoing call or
		 * send an option request back to the caller so that we get a chance to discover our nat'd address
		 * before answering for incoming call */
		pingReplied = false;
		pingOp = new SalOp(q->getCore()->getCCore()->sal.get());
		if (direction == LinphoneCallIncoming) {
			string from = pingOp->getFrom();
			string to = pingOp->getTo();
			linphone_configure_op(q->getCore()->getCCore(), pingOp, log->getFromAddress(), nullptr, false);
			pingOp->setRoute(op->getNetworkOrigin());
			pingOp->ping(from.c_str(), to.c_str());
		} else if (direction == LinphoneCallOutgoing) {
			char *from = linphone_address_as_string(log->getFromAddress());
			char *to = linphone_address_as_string(log->getToAddress());
			pingOp->ping(from, to);
			ms_free(from);
			ms_free(to);
		}
		pingOp->setUserPointer(this);
		return true;
	}
	return false;
}

// -----------------------------------------------------------------------------

void CallSessionPrivate::setParams (CallSessionParams *csp) {
	if (params)
		delete params;
	params = csp;
}

void CallSessionPrivate::createOp () {
	createOpTo(log->getToAddress());
}

bool CallSessionPrivate::isInConference () const {
	return params->getPrivate()->getInConference();
}

const std::string CallSessionPrivate::getConferenceId () const {
	return params->getPrivate()->getConferenceId();
}

void CallSessionPrivate::setConferenceId (const std::string id) {
	params->getPrivate()->setConferenceId(id);
}

// -----------------------------------------------------------------------------

void CallSessionPrivate::abort (const string &errorMsg) {
	op->terminate();
	setState(CallSession::State::Error, errorMsg);
}

void CallSessionPrivate::accepted () {
	/* Immediately notify the connected state, even if errors occur after */
	switch (state) {
		case CallSession::State::OutgoingProgress:
		case CallSession::State::OutgoingRinging:
		case CallSession::State::OutgoingEarlyMedia:
			/* Immediately notify the connected state */
			setState(CallSession::State::Connected, "Connected");
			break;
		default:
			break;
	}
	currentParams->setPrivacy((LinphonePrivacyMask)op->getPrivacy());
}

void CallSessionPrivate::ackBeingSent (LinphoneHeaders *headers) {
	L_Q();
	if (listener)
		listener->onAckBeingSent(q->getSharedFromThis(), headers);
}

void CallSessionPrivate::ackReceived (LinphoneHeaders *headers) {
	L_Q();
	if (listener)
		listener->onAckReceived(q->getSharedFromThis(), headers);
}

void CallSessionPrivate::cancelDone () {
	if (reinviteOnCancelResponseRequested) {
		reinviteOnCancelResponseRequested = false;
		reinviteToRecoverFromConnectionLoss();
	}
}

bool CallSessionPrivate::failure () {
	L_Q();
	const SalErrorInfo *ei = op->getErrorInfo();
	switch (ei->reason) {
		case SalReasonRedirect:
			if ((state == CallSession::State::OutgoingInit) || (state == CallSession::State::OutgoingProgress)
				|| (state == CallSession::State::OutgoingRinging) /* Push notification case */ || (state == CallSession::State::OutgoingEarlyMedia)) {
				const SalAddress *redirectionTo = op->getRemoteContactAddress();
				if (redirectionTo) {
					char *url = sal_address_as_string(redirectionTo);
					lWarning() << "Redirecting CallSession [" << q << "] to " << url;
					log->setToAddress(linphone_address_new(url));
					ms_free(url);
					restartInvite();
					return true;
				}
			}
			break;
		default:
			break;
	}

	/* Some call errors are not fatal */
	switch (state) {
		case CallSession::State::Updating:
		case CallSession::State::Pausing:
		case CallSession::State::Resuming:
		case CallSession::State::StreamsRunning:
			if (ei->reason == SalReasonRequestPending){
				/* there will be a retry. Keep this state. */
				lInfo() << "Call error on state [" << Utils::toString(state) << "], keeping this state until scheduled retry.";
				
				return true;;
			}
			if (ei->reason != SalReasonNoMatch ) {
				lInfo() << "Call error on state [" << Utils::toString(state) << "], restoring previous state [" << Utils::toString(prevState) << "]";
				setState(prevState, ei->full_string);
				return true;
			}
		default:
			break;
	}

	if ((state != CallSession::State::End) && (state != CallSession::State::Error)) {
		if (ei->reason == SalReasonDeclined)
			setState(CallSession::State::End, "Call declined");
		else {
			if (CallSession::isEarlyState(state))
				setState(CallSession::State::Error, ei->full_string ? ei->full_string : "");
			else
				setState(CallSession::State::End, ei->full_string ? ei->full_string : "");
		}
	}
	if (referer) {
		// Notify referer of the failure
		notifyReferState();
	}
	return false;
}

void CallSessionPrivate::infoReceived (SalBodyHandler *bodyHandler) {
	L_Q();
	LinphoneInfoMessage *info = linphone_core_create_info_message(q->getCore()->getCCore());
	linphone_info_message_set_headers(info, op->getRecvCustomHeaders());
	if (bodyHandler) {
		LinphoneContent *content = linphone_content_from_sal_body_handler(bodyHandler);
		linphone_info_message_set_content(info, content);
		linphone_content_unref(content);
	}
	if (listener)
		listener->onInfoReceived(q->getSharedFromThis(), info);
	linphone_info_message_unref(info);
}

void CallSessionPrivate::pingReply () {
	L_Q();
	if (state == CallSession::State::OutgoingInit) {
		pingReplied = true;
		if (isReadyForInvite())
			q->startInvite(nullptr, "");
	}
}

void CallSessionPrivate::referred (const Address &referToAddr) {
	L_Q();
	referToAddress = referToAddr;
	referTo = referToAddr.asString();
	referPending = true;
	setState(CallSession::State::Referred, "Referred");
	if (referPending && listener)
		listener->onCallSessionStartReferred(q->getSharedFromThis());
}

void CallSessionPrivate::remoteRinging () {
	/* Set privacy */
	currentParams->setPrivacy((LinphonePrivacyMask)op->getPrivacy());
	setState(CallSession::State::OutgoingRinging, "Remote ringing");
}

void CallSessionPrivate::replaceOp (SalCallOp *newOp) {
	L_Q();
	SalCallOp *oldOp = op;
	CallSession::State oldState = state;
	op = newOp;
	op->setUserPointer(q);
	op->setLocalMediaDescription(oldOp->getLocalMediaDescription());
	switch (state) {
		case CallSession::State::IncomingEarlyMedia:
		case CallSession::State::IncomingReceived:
			op->notifyRinging((state == CallSession::State::IncomingEarlyMedia) ? true : false, linphone_core_get_tag_100rel_support_level(q->getCore()->getCCore()));
			break;
		case CallSession::State::Connected:
		case CallSession::State::StreamsRunning:
			op->accept();
			break;
		case CallSession::State::PushIncomingReceived:
			break;
		default:
			lWarning() << "CallSessionPrivate::replaceOp(): don't know what to do in state [" << Utils::toString(state) << "]";
			break;
	}
	switch (oldState) {
		case CallSession::State::IncomingEarlyMedia:
		case CallSession::State::IncomingReceived:
			oldOp->setUserPointer(nullptr); // In order for the call session to not get terminated by terminating this op
			// Do not terminate a forked INVITE
			lInfo() << "CallSessionPrivate::replaceOp(): terminating old session in early state.";
			if (op->getReplaces()){
				oldOp->terminate();
			}else{
				oldOp->killDialog();
			}
			break;
		case CallSession::State::Connected:
		case CallSession::State::StreamsRunning:
			lInfo() << "CallSessionPrivate::replaceOp(): terminating old session in running state.";
			oldOp->terminate();
			oldOp->killDialog();
			break;
		default:
			break;
	}
	oldOp->release();
}

void CallSessionPrivate::terminated () {
	L_Q();
	switch (state) {
		case CallSession::State::End:
		case CallSession::State::Error:
			lWarning() << "terminated: already terminated, ignoring";
			return;
		case CallSession::State::IncomingReceived:
		case CallSession::State::IncomingEarlyMedia:
			if (!op->getReasonErrorInfo()->protocol || strcmp(op->getReasonErrorInfo()->protocol, "") == 0) {
				linphone_error_info_set(ei, nullptr, LinphoneReasonNotAnswered, 0, "Incoming call cancelled", nullptr);
				nonOpError = true;
			}
			break;
		default:
			break;
	}
	if (referPending && listener)
		listener->onCallSessionStartReferred(q->getSharedFromThis());

	setState(CallSession::State::End, "Call ended");
}

void CallSessionPrivate::updated (bool isUpdate) {
	L_Q();
	deferUpdate = !!linphone_config_get_int(linphone_core_get_config(q->getCore()->getCCore()), "sip", "defer_update_default", FALSE);
	SalErrorInfo sei;
	memset(&sei, 0, sizeof(sei));
	CallSession::State localState = state; //Member variable "state" may be changed within this function
	
	switch (localState) {
		case CallSession::State::PausedByRemote:
			updatedByRemote();
			break;
		/* SIP UPDATE CASE */
		case CallSession::State::OutgoingRinging:
		case CallSession::State::OutgoingEarlyMedia:
		case CallSession::State::IncomingEarlyMedia:
			if (isUpdate) {
				setState(CallSession::State::EarlyUpdatedByRemote, "EarlyUpdatedByRemote");
				acceptUpdate(nullptr, prevState, Utils::toString(prevState));
			}
			break;
		case CallSession::State::StreamsRunning:
		case CallSession::State::Connected:
		case CallSession::State::UpdatedByRemote: /* Can happen on UAC connectivity loss */
			updatedByRemote();
			break;
		case CallSession::State::Paused:
			/* We'll remain in pause state but accept the offer anyway according to default parameters */
			setState(CallSession::State::UpdatedByRemote, "Call updated by remote (while in Paused)");
			acceptUpdate(nullptr, CallSession::State::Paused, "Paused");
			break;
		case CallSession::State::Pausing:
		case CallSession::State::Updating:
		case CallSession::State::Resuming:
			/* Notify UpdatedByRemote state, then return to the original state, so that retryable transaction can complete.*/
			setState(CallSession::State::UpdatedByRemote, "Call updated by remote while in transcient state (Pausing/Updating/Resuming)");
			acceptUpdate(nullptr, localState, Utils::toString(localState));
			break;
		case CallSession::State::Idle:
		case CallSession::State::OutgoingInit:
		case CallSession::State::End:
		case CallSession::State::IncomingReceived:
		case CallSession::State::PushIncomingReceived:
		case CallSession::State::OutgoingProgress:
		case CallSession::State::Referred:
		case CallSession::State::Error:
		case CallSession::State::Released:
		case CallSession::State::EarlyUpdatedByRemote:
		case CallSession::State::EarlyUpdating:
			lWarning() << "Receiving reINVITE or UPDATE while in state [" << Utils::toString(state) << "], should not happen";
		break;
	}
}

void CallSessionPrivate::refreshed() {
	/* Briefly notifies the application that we received an UPDATE thanks to UpdatedByRemote state .*/
	setState(CallSession::State::UpdatedByRemote, "Session refresh");
	/* And immediately get back to previous state, since the actual call state doesn't change.*/
	restorePreviousState();
}

void CallSessionPrivate::updatedByRemote () {
	L_Q();

	setState(CallSession::State::UpdatedByRemote,"Call updated by remote");
	if (deferUpdate || deferUpdateInternal) {
		if (state == CallSession::State::UpdatedByRemote && !deferUpdateInternal){
			lInfo() << "CallSession [" << q << "]: UpdatedByRemoted was signaled but defered. LinphoneCore expects the application to call linphone_call_accept_update() later";
		}
	} else {
		if (state == CallSession::State::UpdatedByRemote)
			q->acceptUpdate(nullptr);
		else {
			// Otherwise it means that the app responded by CallSession::acceptUpdate() within the callback,
			// so job is already done
		}
	}
}

void CallSessionPrivate::updating (bool isUpdate) {
	updated(isUpdate);
}

// -----------------------------------------------------------------------------

void CallSessionPrivate::init () {
	currentParams = new CallSessionParams();
	ei = linphone_error_info_new();
}

// -----------------------------------------------------------------------------

void CallSessionPrivate::accept (const CallSessionParams *csp) {
	L_Q();
	/* Try to be best-effort in giving real local or routable contact address */
	setContactOp();
	if (csp) 
		setParams(new CallSessionParams(*csp));
	if (params) {
		op->enableCapabilityNegotiation (q->isCapabilityNegotiationEnabled());
		op->setSentCustomHeaders(params->getPrivate()->getCustomHeaders());
	}

	op->accept();
	setState(CallSession::State::Connected, "Connected");
}

void CallSessionPrivate::acceptOrTerminateReplacedSessionInIncomingNotification () {
	L_Q();
	CallSession *replacedSession = nullptr;
	if (linphone_config_get_int(linphone_core_get_config(q->getCore()->getCCore()), "sip", "auto_answer_replacing_calls", 1)) {
		if (op->getReplaces())
			replacedSession = static_cast<CallSession *>(op->getReplaces()->getUserPointer());
		if (replacedSession) {
			switch (replacedSession->getState()){
				/* If the replaced call is already accepted, then accept automatic replacement. */
				case CallSession::State::StreamsRunning:
				case CallSession::State::Connected:
				case CallSession::State::Paused:
				case CallSession::State::PausedByRemote:
				case CallSession::State::Pausing:
					lInfo() << " auto_answer_replacing_calls is true, replacing call is going to be accepted and replaced call terminated.";
					q->acceptDefault();
				break;
				default:
				break;
			}
		}
	}
}

LinphoneStatus CallSessionPrivate::acceptUpdate (const CallSessionParams *csp, CallSession::State nextState, const string &stateInfo) {
	return startAcceptUpdate(nextState, stateInfo);
}

LinphoneStatus CallSessionPrivate::checkForAcceptation () {
	L_Q();
	switch (state) {
		case CallSession::State::IncomingReceived:
		case CallSession::State::IncomingEarlyMedia:
		case CallSession::State::PushIncomingReceived:
			break;
		default:
			lError() << "checkForAcceptation() CallSession [" << q << "] is in state [" << Utils::toString(state) << "], operation not permitted";
			return -1;
	}
	if (listener)
		listener->onCheckForAcceptation(q->getSharedFromThis());

	/* Check if this call is supposed to replace an already running one */
	SalOp *replaced = op->getReplaces();
	if (replaced) {
		CallSession *session = static_cast<CallSession *>(replaced->getUserPointer());
		if (session) {
			lInfo() << "CallSession " << q << " replaces CallSession " << session << ". This last one is going to be terminated automatically";
			session->terminate();
		}
	}
	return 0;
}

void CallSessionPrivate::handleIncomingReceivedStateInIncomingNotification () {
	L_Q();
	/* Try to be best-effort in giving real local or routable contact address for 100Rel case */
	setContactOp();
	if (notifyRinging && state != CallSession::State::IncomingEarlyMedia)
		op->notifyRinging(false, linphone_core_get_tag_100rel_support_level(q->getCore()->getCCore()));
	acceptOrTerminateReplacedSessionInIncomingNotification();
}

bool CallSessionPrivate::isReadyForInvite () const {
	bool pingReady = false;
	if (pingOp) {
		if (pingReplied)
			pingReady = true;
	} else
		pingReady = true;
	return pingReady;
}

bool CallSessionPrivate::isUpdateAllowed (CallSession::State &nextState) const {
	switch (state) {
		case CallSession::State::IncomingReceived:
		case CallSession::State::PushIncomingReceived:
		case CallSession::State::IncomingEarlyMedia:
		case CallSession::State::OutgoingRinging:
		case CallSession::State::OutgoingEarlyMedia:
			nextState = CallSession::State::EarlyUpdating;
			break;
		case CallSession::State::Connected:
		case CallSession::State::StreamsRunning:
		case CallSession::State::PausedByRemote:
		case CallSession::State::UpdatedByRemote:
			nextState = CallSession::State::Updating;
			break;
		case CallSession::State::Paused:
			nextState = CallSession::State::Pausing;
			break;
		case CallSession::State::OutgoingProgress:
		case CallSession::State::Pausing:
		case CallSession::State::Resuming:
		case CallSession::State::Updating:
		case CallSession::State::EarlyUpdating:
			nextState = state;
			break;
		default:
			lError() << "Update is not allowed in [" << Utils::toString(state) << "] state";
			return false;
	}

	return true;
}

int CallSessionPrivate::restartInvite () {
	L_Q();
	createOp();
	return q->initiateOutgoing(subject);
}

/*
 * Called internally when reaching the Released state, to perform cleanups to break circular references.
**/
void CallSessionPrivate::setReleased () {
	L_Q();
	if (op) {
		/* Transfer the last error so that it can be obtained even in Released state */
		if (!nonOpError)
			linphone_error_info_from_sal_op(ei, op);
		/* So that we cannot have anymore upcalls for SAL concerning this call */
		op->release();
		op = nullptr;
	}
	referer = nullptr;
	transferTarget = nullptr;
	while (pendingActions.empty() == false) {
		pendingActions.pop();
	}

	if (listener)
		listener->onCallSessionSetReleased(q->getSharedFromThis());
}

/* This method is called internally to get rid of a call that was notified to the application,
 * because it reached the end or error state. It performs the following tasks:
 * - remove the call from the internal list of calls
 * - update the call logs accordingly
 */
void CallSessionPrivate::setTerminated() {
	L_Q();
	completeLog();
	if (listener)
		listener->onCallSessionSetTerminated(q->getSharedFromThis());
}

LinphoneStatus CallSessionPrivate::startAcceptUpdate (CallSession::State nextState, const string &stateInfo) {
	op->accept();
	setState(nextState, stateInfo);
	return 0;
}

LinphoneStatus CallSessionPrivate::startUpdate (const CallSession::UpdateMethod method, const string &subject) {
	L_Q();
	string newSubject(subject);

	if (newSubject.empty()) {

		LinphoneConference * conference = nullptr;
		if (listener) {
			conference = listener->getCallSessionConference(q->getSharedFromThis());
		}
		if (!conference) {
			if (isInConference())
				newSubject = "Conference";
			else if (q->getParams()->getPrivate()->getInternalCallUpdate())
				newSubject = "ICE processing concluded";
			else if (q->getParams()->getPrivate()->getNoUserConsent())
				newSubject = "Refreshing";
			else
				newSubject = "Media change";
		}
	}
	char * contactAddressStr = NULL;
	if (destProxy) {
		if (linphone_proxy_config_get_op(destProxy)) {
			/* Give a chance to update the contact address if connectivity has changed */
			contactAddressStr = sal_address_as_string(linphone_proxy_config_get_op(destProxy)->getContactAddress());

		} else if (linphone_core_conference_server_enabled(q->getCore()->getCCore()) && linphone_proxy_config_get_contact(destProxy)) {
			contactAddressStr = linphone_address_as_string(linphone_proxy_config_get_contact(destProxy));
		}
	} else {
		op->setContactAddress(nullptr);
	}

	if (contactAddressStr) {
		Address contactAddress(contactAddressStr);
		ms_free(contactAddressStr);
		q->updateContactAddress(contactAddress);
		op->setContactAddress(contactAddress.getInternalAddress());
	} else
		op->setContactAddress(nullptr);

	bool noUserConsent = q->getParams()->getPrivate()->getNoUserConsent();
	if (method != CallSession::UpdateMethod::Default) {
		noUserConsent = method == CallSession::UpdateMethod::Update;
	}

	return op->update(newSubject, noUserConsent);
}

void CallSessionPrivate::terminate () {
	if ((state == CallSession::State::IncomingReceived || state == CallSession::State::IncomingEarlyMedia)){
		LinphoneReason reason = linphone_error_info_get_reason(ei);
		if( reason == LinphoneReasonNone) {
			linphone_error_info_set_reason(ei, LinphoneReasonDeclined);
			nonOpError = true;
		}else if( reason != LinphoneReasonNotAnswered) {
			nonOpError = true;
		}
	}
	setState(CallSession::State::End, "Call terminated");

	if (op && !op->hasDialog()) {
		setState(CallSession::State::Released, "Call released");
	}
}

void CallSessionPrivate::updateCurrentParams () const {}

void CallSessionPrivate::setDestProxy (LinphoneProxyConfig *proxy){
	destProxy = proxy;
	currentParams->setAccount(proxy ? Account::toCpp(proxy->account)->getSharedFromThis() : nullptr);
}

// -----------------------------------------------------------------------------

void CallSessionPrivate::setBroken () {
	switch (state) {
		// For all the early states, we prefer to drop the call
		case CallSession::State::OutgoingInit:
		case CallSession::State::OutgoingProgress:
		case CallSession::State::OutgoingRinging:
		case CallSession::State::OutgoingEarlyMedia:
		case CallSession::State::IncomingReceived:
		case CallSession::State::PushIncomingReceived:
		case CallSession::State::IncomingEarlyMedia:
			// During the early states, the SAL layer reports the failure from the dialog or transaction layer,
			// hence, there is nothing special to do
		case CallSession::State::StreamsRunning:
		case CallSession::State::Updating:
		case CallSession::State::Pausing:
		case CallSession::State::Resuming:
		case CallSession::State::Paused:
		case CallSession::State::PausedByRemote:
		case CallSession::State::UpdatedByRemote:
			// During these states, the dialog is established. A failure of a transaction is not expected to close it.
			// Instead we have to repair the dialog by sending a reINVITE
			broken = true;
			needLocalIpRefresh = true;
			break;
		default:
			lError() << "CallSessionPrivate::setBroken(): unimplemented case";
			break;
	}
}

void CallSessionPrivate::setContactOp () {
	L_Q();
	LinphoneAddress *contact = getFixedContact();
	if (contact) {
		auto contactParams = q->getParams()->getPrivate()->getCustomContactParameters();
		for (auto it = contactParams.begin(); it != contactParams.end(); it++)
			linphone_address_set_param(contact, it->first.c_str(), it->second.empty() ? nullptr : it->second.c_str());
		char * contactAddressStr = linphone_address_as_string(contact);
		Address contactAddress(contactAddressStr);
		ms_free(contactAddressStr);
		// Do not try to set contact address if it is not valid
		if (contactAddress.isValid()) {
			q->updateContactAddress (contactAddress);
			if (isInConference()) {
				std::shared_ptr<MediaConference::Conference> conference = q->getCore()->findAudioVideoConference(ConferenceId(contactAddress, contactAddress));
				if (conference) {
					// Try to change conference address in order to add GRUU to it
					// Note that this operation may fail if the conference was previously created on the server
					conference->setConferenceAddress(contactAddress);
				}
			}

			#ifdef HAVE_DB_STORAGE
			auto &mainDb = q->getCore()->getPrivate()->mainDb;
			if (mainDb)  {
				const auto & confInfo = mainDb->getConferenceInfoFromURI(ConferenceAddress(*q->getRemoteAddress()));
				if (confInfo) {
					// me is admin if the organizer is the same as me
					contactAddress.setParam("admin", Utils::toString((confInfo->getOrganizer() == q->getLocalAddress())));
				}
			}
			#endif

			lInfo() << "Setting contact address for session " << this << " to " << contactAddress.asString();
			op->setContactAddress(contactAddress.getInternalAddress());
		} else {
			lWarning() << "Unable to set contact address for session " << this << " to " << contactAddress.asString() << " as it is not valid";
		}
		linphone_address_unref(contact);
	}
}

// -----------------------------------------------------------------------------

void CallSessionPrivate::onNetworkReachable (bool sipNetworkReachable, bool mediaNetworkReachable) {
	if (sipNetworkReachable)
		repairIfBroken();
	else
		setBroken();
}

void CallSessionPrivate::onRegistrationStateChanged (LinphoneProxyConfig *cfg, LinphoneRegistrationState cstate, const std::string &message) {
	//might be better to add callbacks on Account, but due to the lake of internal listener, it is dangerous to expose internal listeners to public object.
	if (cfg == destProxy && cstate == LinphoneRegistrationOk)
		repairIfBroken();
	/*else
		only repair call when the right proxy is in state connected*/
}

// -----------------------------------------------------------------------------

void CallSessionPrivate::completeLog () {
	L_Q();
	log->setDuration(computeDuration()); /* Store duration since connected */
	log->setErrorInfo(linphone_error_info_ref(ei));
	if (log->getStatus() == LinphoneCallMissed)
		q->getCore()->getCCore()->missed_calls++;
	q->getCore()->reportConferenceCallEvent(EventLog::Type::ConferenceCallEnded, log, nullptr);
}

void CallSessionPrivate::createOpTo (const LinphoneAddress *to) {
	L_Q();
	if (op)
		op->release();

	const auto & core = q->getCore()->getCCore();

	op = new SalCallOp(core->sal.get(), q->isCapabilityNegotiationEnabled());
	op->setUserPointer(q);
	if (params->getPrivate()->getReferer())
		op->setReferrer(params->getPrivate()->getReferer()->getPrivate()->getOp());
	linphone_configure_op(core, op, to, q->getParams()->getPrivate()->getCustomHeaders(), false);
	if (q->getParams()->getPrivacy() != LinphonePrivacyDefault)
		op->setPrivacy((SalPrivacyMask)q->getParams()->getPrivacy());
	/* else privacy might be set by proxy */
}

// -----------------------------------------------------------------------------

LinphoneAddress * CallSessionPrivate::getFixedContact () const {
	L_Q();
	LinphoneAddress *result = nullptr;
	if (op && op->getContactAddress()) {
		/* If already choosed, don't change it */
		return nullptr;
	} else if (pingOp && pingOp->getContactAddress()) {
		/* If the ping OPTIONS request succeeded use the contact guessed from the received, rport */
		lInfo() << "Contact has been fixed using OPTIONS";
		char *addr = sal_address_as_string(pingOp->getContactAddress());
		result = linphone_address_new(addr);
		ms_free(addr);
		return result;
	} else if (destProxy){
		const LinphoneAddress *addr = NULL;
		if (linphone_proxy_config_get_contact(destProxy)) {
			addr = linphone_proxy_config_get_contact(destProxy);
		} else if (linphone_core_conference_server_enabled(q->getCore()->getCCore())) {
			addr = linphone_proxy_config_get_contact(destProxy);
		} else {
			lError() << "Unable to retrieve contact address from proxy confguration for call session " << this << " (local address " << q->getLocalAddress().asString() << " remote address " <<  (q->getRemoteAddress() ? q->getRemoteAddress()->asString() : "Unknown") << ").";
		}
		if (addr && (linphone_proxy_config_get_op(destProxy) || (linphone_proxy_config_get_dependency(destProxy) != nullptr) || linphone_core_conference_server_enabled(q->getCore()->getCCore()))) {
			/* If using a proxy, use the contact address as guessed with the REGISTERs */
			lInfo() << "Contact has been fixed using proxy";
			result = linphone_address_clone(addr);
			return result;
		}
	}
	result = linphone_core_get_primary_contact_parsed(q->getCore()->getCCore());
	if (result) {
		/* Otherwise use supplied localip */
		linphone_address_set_domain(result, nullptr /* localip */);
		linphone_address_set_port(result, -1 /* linphone_core_get_sip_port(core) */);
		lInfo() << "Contact has not been fixed, stack will do";
	}
	return result;
}

// -----------------------------------------------------------------------------

void CallSessionPrivate::reinviteToRecoverFromConnectionLoss () {
	L_Q();
	lInfo() << "CallSession [" << q << "] is going to be updated (reINVITE) in order to recover from lost connectivity";
	q->update(params, CallSession::UpdateMethod::Invite);
}

void CallSessionPrivate::repairByInviteWithReplaces () {
	L_Q();
	lInfo() << "CallSession [" << q << "] is going to have a new INVITE replacing the previous one in order to recover from lost connectivity";
	string callId = op->getCallId();
	string fromTag = op->getLocalTag();
	string toTag = op->getRemoteTag();
	// Restore INVITE body if any, for example while creating a chat room
	Content content = Content(op->getLocalBody());

	op->killDialog();
	createOp();
	op->setReplaces(callId.c_str(), fromTag, toTag.empty()?"0":toTag); // empty tag is set to 0 as defined by rfc3891
	q->startInvite(nullptr, subject, &content); // Don't forget to set subject from call-session (and not from OP)
}

void CallSessionPrivate::repairIfBroken () {
	L_Q();

	try {
		LinphoneCore *lc = q->getCore()->getCCore();
		LinphoneConfig *config = linphone_core_get_config(lc);
		if (!linphone_config_get_int(config, "sip", "repair_broken_calls", 1) || !lc->media_network_state.global_state || !broken)
			return;
	} catch (const bad_weak_ptr &) {
		return; // Cannot repair if core is destroyed.
	}

	// If we are registered and this session has been broken due to a past network disconnection,
	// attempt to repair it

	// Make sure that the proxy from which we received this call, or to which we routed this call is registered first
	if (destProxy) {
		// In all other cases, ie no proxy config, or a proxy config for which no registration was requested,
		// we can start the call session repair immediately.
		if (linphone_proxy_config_register_enabled(destProxy)
			&& (linphone_proxy_config_get_state(destProxy) != LinphoneRegistrationOk))
			return;
	}

	SalErrorInfo sei;
	memset(&sei, 0, sizeof(sei));
	switch (state) {
		case CallSession::State::Updating:
		case CallSession::State::Pausing:
			if (op->dialogRequestPending()) {
				// Need to cancel first re-INVITE as described in section 5.5 of RFC 6141
				if (op->cancelInvite() == 0){
					reinviteOnCancelResponseRequested = true;
				}
			}
			break;
		case CallSession::State::StreamsRunning:
		case CallSession::State::Paused:
		case CallSession::State::PausedByRemote:
			if (!op->dialogRequestPending())
				reinviteToRecoverFromConnectionLoss();
			break;
		case CallSession::State::UpdatedByRemote:
			if (op->dialogRequestPending()) {
				sal_error_info_set(&sei, SalReasonServiceUnavailable, "SIP", 0, nullptr, nullptr);
				op->declineWithErrorInfo(&sei, nullptr);
			}
			reinviteToRecoverFromConnectionLoss();
			break;
		case CallSession::State::OutgoingInit:
		case CallSession::State::OutgoingProgress:
			repairByInviteWithReplaces();
			break;
		case CallSession::State::OutgoingEarlyMedia:
		case CallSession::State::OutgoingRinging:
			if (op->getRemoteTag() != nullptr){
				repairByInviteWithReplaces();
			}else{
				lWarning() << "No remote tag in last provisional response, no early dialog, so trying to cancel lost INVITE and will retry later.";
				if (op->cancelInvite() == 0){
					reinviteOnCancelResponseRequested = true;
				}
			}
			break;
		case CallSession::State::IncomingEarlyMedia:
		case CallSession::State::IncomingReceived:
		case CallSession::State::PushIncomingReceived:
			// Keep the call broken until a forked INVITE is received from the server
			break;
		default:
			lWarning() << "CallSessionPrivate::repairIfBroken: don't know what to do in state [" << Utils::toString(state);
			broken = false;
			break;
	}
	sal_error_info_reset(&sei);
}

// =============================================================================

CallSession::CallSession (const shared_ptr<Core> &core, const CallSessionParams *params, CallSessionListener *listener)
	: Object(*new CallSessionPrivate), CoreAccessor(core) {
	L_D();
	getCore()->getPrivate()->registerListener(d);
	d->listener = listener;
	if (params)
		d->setParams(new CallSessionParams(*params));
	d->init();
	lInfo() << "New CallSession [" << this << "] initialized (LinphoneCore version: " << linphone_core_get_version() << ")";
}

CallSession::CallSession (CallSessionPrivate &p, const shared_ptr<Core> &core) : Object(p), CoreAccessor(core) {
	L_D();
	getCore()->getPrivate()->registerListener(d);
	d->init();
}

CallSession::~CallSession () {
	L_D();
	try { //getCore may no longuer be available when deleting, specially in case of managed enviroment like java
		getCore()->getPrivate()->unregisterListener(d);
	} catch (const bad_weak_ptr &) {}
	if (d->currentParams)
		delete d->currentParams;
	if (d->params)
		delete d->params;
	if (d->remoteParams)
		delete d->remoteParams;
	if (d->ei)
		linphone_error_info_unref(d->ei);
	if (d->op)
		d->op->release();
}

// -----------------------------------------------------------------------------

void CallSession::setListener(CallSessionListener *listener){
	L_D();
	d->listener = listener;
}

void CallSession::setStateToEnded() {
	L_D();
	d->setState(CallSession::State::End, "Call ended");
}

void CallSession::acceptDefault(){
	accept();
}

LinphoneStatus CallSession::accept (const CallSessionParams *csp) {
	L_D();
	LinphoneStatus result = d->checkForAcceptation();
	if (result < 0) return result;
	d->accept(csp);
	return 0;
}

void CallSession::accepting () {
	mIsAccepting = true;
}

LinphoneStatus CallSession::acceptUpdate (const CallSessionParams *csp) {
	L_D();
	if (d->state != CallSession::State::UpdatedByRemote) {
		lError() << "CallSession::acceptUpdate(): invalid state " << Utils::toString(d->state) << " to call this method";
		return -1;
	}
	return d->acceptUpdate(csp, d->prevState, Utils::toString(d->prevState));
}

LinphoneProxyConfig * CallSession::getDestProxy (){
	L_D();
	return d->destProxy;
}

void CallSession::configure (LinphoneCallDir direction, LinphoneProxyConfig *cfg, SalCallOp *op, const Address &from, const Address &to) {
	L_D();
	d->direction = direction;
	d->setDestProxy(cfg);
	LinphoneAddress *fromAddr = linphone_address_new(from.asString().c_str());
	LinphoneAddress *toAddr = linphone_address_new(to.asString().c_str());

	const auto & core = getCore()->getCCore();
	if (!d->destProxy) {
		/* Try to define the destination proxy if it has not already been done to have a correct contact field in the SIP messages */
		d->setDestProxy( linphone_core_lookup_known_proxy(core, toAddr) );
	}

	d->log = CallLog::create(getCore(), direction, fromAddr, toAddr);

	if (op) {
		/* We already have an op for incoming calls */
		d->op = op;
		d->op->setUserPointer(this);
		op->enableCapabilityNegotiation (isCapabilityNegotiationEnabled());
		op->enableCnxIpTo0000IfSendOnly(
			!!linphone_config_get_default_int(
				linphone_core_get_config(core), "sip", "cnx_ip_to_0000_if_sendonly_enabled", 0
			)
		);
		d->log->setCallId(op->getCallId()); /* Must be known at that time */
	}

	if (direction == LinphoneCallOutgoing) {
		if (d->params->getPrivate()->getReferer())
			d->referer = d->params->getPrivate()->getReferer();
		d->startPing();
	} else if (direction == LinphoneCallIncoming) {
		d->setParams(new CallSessionParams());
		d->params->initDefault(getCore(), LinphoneCallIncoming);
	}
}

void CallSession::configure (LinphoneCallDir direction, const string &callid) {
	L_D();
	d->direction = direction;
	
	// Keeping a valid address while following https://www.ietf.org/rfc/rfc3323.txt guidelines.
	d->log = CallLog::create(getCore(), direction, linphone_address_new("Anonymous <sip:anonymous@anonymous.invalid>"), linphone_address_new("Anonymous <sip:anonymous@anonymous.invalid>"));
	d->log->setCallId(callid);
}

bool CallSession::isOpConfigured () {
	L_D();
	return d->op ? true : false;
}

LinphoneStatus CallSession::decline (LinphoneReason reason) {
	LinphoneErrorInfo *ei = linphone_error_info_new();
	linphone_error_info_set(ei, "SIP", reason, linphone_reason_to_error_code(reason), nullptr, nullptr);
	LinphoneStatus status = decline(ei);
	linphone_error_info_unref(ei);
	return status;
}

LinphoneStatus CallSession::decline (const LinphoneErrorInfo *ei) {
	L_D();
	if (d->state == CallSession::State::PushIncomingReceived && !d->op) {
		lInfo() << "[pushkit] Terminate CallSession [" << this << "]";
		linphone_error_info_set(d->ei, nullptr, LinphoneReasonDeclined, 3, "Declined", nullptr);
		d->terminate();
		d->setState(LinphonePrivate::CallSession::State::Released, "Call released");
		return 0;
	}
	
	SalErrorInfo sei;
	SalErrorInfo sub_sei;
	memset(&sei, 0, sizeof(sei));
	memset(&sub_sei, 0, sizeof(sub_sei));
	sei.sub_sei = &sub_sei;
	if ((d->state != CallSession::State::IncomingReceived) && (d->state != CallSession::State::IncomingEarlyMedia) && (d->state != CallSession::State::PushIncomingReceived)) {
		lError() << "Cannot decline a CallSession that is in state " << Utils::toString(d->state);
		return -1;
	}
	if (ei) {
		linphone_error_info_set(d->ei, nullptr, linphone_error_info_get_reason(ei), linphone_error_info_get_protocol_code(ei), linphone_error_info_get_phrase(ei), nullptr );
		linphone_error_info_to_sal(ei, &sei);
		d->op->declineWithErrorInfo(&sei , nullptr);
	} else
		d->op->decline(SalReasonDeclined);
	sal_error_info_reset(&sei);
	sal_error_info_reset(&sub_sei);
	d->terminate();
	return 0;
}

LinphoneStatus CallSession::declineNotAnswered (LinphoneReason reason) {
	L_D();
	d->log->setStatus(LinphoneCallMissed);
	d->nonOpError = true;
	linphone_error_info_set(d->ei, nullptr, reason, linphone_reason_to_error_code(reason), "Not answered", nullptr);
	return decline(reason);
}

LinphoneStatus CallSession::deferUpdate () {
	L_D();
	if (d->state != CallSession::State::UpdatedByRemote) {
		lError() << "CallSession::deferUpdate() not done in state CallSession::State::UpdatedByRemote";
		return -1;
	}
	d->deferUpdate = true;
	return 0;
}

const std::list<LinphoneMediaEncryption> CallSession::getSupportedEncryptions() const {
	L_D();
	if ((d->direction == LinphoneCallIncoming) && (d->state == CallSession::State::Idle)) {
		// If we are in the IncomingReceived state, we support all encryptions the core had enabled at compile time
		// In fact, the policy is to preliminary accept (i.e. send 180 Ringing) the wider possible range of offers.
		// Note that special treatment is dedicated to ZRTP as, for testing purposes, a core can have its member zrtp_not_available_simulation set to TRUE which prevent the core to accept calls with ZRTP encryptions
		// The application can then decline a call based on the call parameter the call was accepted with
		const auto core = getCore()->getCCore();
		const auto encList = linphone_core_get_supported_media_encryptions_at_compile_time();
		std::list<LinphoneMediaEncryption> encEnumList;
		for(bctbx_list_t * enc = encList;enc!=NULL;enc=enc->next){
			const auto encEnum = static_cast<LinphoneMediaEncryption>(LINPHONE_PTR_TO_INT(bctbx_list_get_data(enc)));
			// Do not add ZRTP if it is not supported by core even though the core was compile with it on
			if ((encEnum != LinphoneMediaEncryptionZRTP) || ((encEnum == LinphoneMediaEncryptionZRTP) && !core->zrtp_not_available_simulation)) {
				encEnumList.push_back(encEnum);
			}
		}

		if (encList) {
			bctbx_list_free(encList);
		}

		return encEnumList;
	} else if (getParams()) {
		return getParams()->getPrivate()->getSupportedEncryptions();
	}
	return getCore()->getSupportedMediaEncryptions();
}

bool CallSession::isCapabilityNegotiationEnabled() const {
	if (getParams()) {
		return getParams()->getPrivate()->capabilityNegotiationEnabled();
	}
	return !!linphone_core_capability_negociation_enabled(getCore()->getCCore());
}

bool CallSession::hasTransferPending () {
	L_D();
	return d->referPending;
}

void CallSession::initiateIncoming () {}

bool CallSession::initiateOutgoing (const string &subject, const Content *content) {
	L_D();
	bool defer = false;
	d->setState(CallSession::State::OutgoingInit, "Starting outgoing call");
	d->log->setStartTime(ms_time(nullptr));
	if (!d->destProxy)
		defer = d->startPing();
	return defer;
}

void CallSession::iterate (time_t currentRealTime, bool oneSecondElapsed) {
	L_D();
	int elapsed = (int)(currentRealTime - d->log->getStartTime());
	if ((d->state == CallSession::State::OutgoingInit) && (elapsed > getCore()->getCCore()->sip_conf.delayed_timeout)) {
		/* Start the call even if the OPTIONS reply did not arrive */
		startInvite(nullptr, "");
	}
	if ((d->state == CallSession::State::IncomingReceived) || (d->state == CallSession::State::IncomingEarlyMedia)) {
		if (d->listener)
			d->listener->onIncomingCallSessionTimeoutCheck(getSharedFromThis(), elapsed, oneSecondElapsed);
	}

	if (d->direction == LinphoneCallIncoming && !isOpConfigured()) {
		if (d->listener)
			d->listener->onPushCallSessionTimeoutCheck(getSharedFromThis(), elapsed);
	}

	if ((getCore()->getCCore()->sip_conf.in_call_timeout > 0) && (d->log->getConnectedTime() != 0)
		&& ((currentRealTime - d->log->getConnectedTime()) > getCore()->getCCore()->sip_conf.in_call_timeout)) {
		lInfo() << "In call timeout (" << getCore()->getCCore()->sip_conf.in_call_timeout << ")";
		terminate();
	}
}

LinphoneStatus CallSession::redirect (const string &redirectUri) {
	Address address(getCore()->interpretUrl(redirectUri));
	if (!address.isValid()) {
		/* Bad url */
		lError() << "Bad redirect URI: " << redirectUri;
		return -1;
	}
	return redirect(address);
}

LinphoneStatus CallSession::redirect (const Address &redirectAddr) {
	L_D();
	if (d->state != CallSession::State::IncomingReceived && d->state != CallSession::State::PushIncomingReceived) {
		lError() << "Unable to redirect call when in state " << d->state;
		return -1;
	}
	SalErrorInfo sei;
	memset(&sei, 0, sizeof(sei));
	sal_error_info_set(&sei, SalReasonRedirect, "SIP", 0, nullptr, nullptr);
	d->op->declineWithErrorInfo(&sei, redirectAddr.getInternalAddress(), ((getParams()->getPrivate()->getEndTime() < 0) ? 0 : getParams()->getPrivate()->getEndTime()));
	linphone_error_info_set(d->ei, nullptr, LinphoneReasonMovedPermanently, 302, "Call redirected", nullptr);
	d->nonOpError = true;
	d->terminate();
	sal_error_info_reset(&sei);
	return 0;
}

void CallSession::startIncomingNotification (bool notifyRinging) {
	L_D();
	if (d->state != CallSession::State::PushIncomingReceived) {
		startBasicIncomingNotification(notifyRinging);
	}
	if (d->deferIncomingNotification) {
		lInfo() << "Defer incoming notification";
		return;
	}
	d->startIncomingNotification();
}

void CallSession::startBasicIncomingNotification (bool notifyRinging) {
	L_D();
	d->notifyRinging = notifyRinging;
	if (d->listener) {
		d->listener->onIncomingCallSessionNotified(getSharedFromThis());
		d->listener->onBackgroundTaskToBeStarted(getSharedFromThis());
	}
	/* Prevent the CallSession from being destroyed while we are notifying, if the user declines within the state callback */
	shared_ptr<CallSession> ref = getSharedFromThis();
}

void CallSession::startPushIncomingNotification () {
	L_D();
	if (d->listener){
		d->listener->onIncomingCallSessionStarted(getSharedFromThis());
		d->listener->onStartRingtone(getSharedFromThis());
	}

	d->setState(CallSession::State::PushIncomingReceived, "Push notification received");
}


int CallSession::startInvite (const Address *destination, const string &subject, const Content *content) {
	L_D();
	d->subject = subject;
	/* Try to be best-effort in giving real local or routable contact address */
	d->setContactOp();
	string destinationStr;
	char *realUrl = nullptr;
	if (destination)
		destinationStr = destination->asString();
	else {
		realUrl = linphone_address_as_string(d->log->getToAddress());
		destinationStr = realUrl;
		ms_free(realUrl);
	}
	char *from = linphone_address_as_string(d->log->getFromAddress());
	/* Take a ref because sal_call() may destroy the CallSession if no SIP transport is available */
	shared_ptr<CallSession> ref = getSharedFromThis();
	if (content) {
		d->op->setLocalBody(*content);
	}

	// If a custom Content has been set in the call params, create a multipart body for the INVITE
	for (auto& content : d->params->getCustomContents()) {
		d->op->addAdditionalLocalBody(content);
	}

	int result = d->op->call(from, destinationStr, subject);
	ms_free(from);
	if (result < 0) {
		if ((d->state != CallSession::State::Error) && (d->state != CallSession::State::Released)) {
			// sal_call() may invoke call_failure() and call_released() SAL callbacks synchronously,
			// in which case there is no need to perform a state change here.
			d->setState(CallSession::State::Error, "Call failed");
		}
	} else {
		d->log->setCallId(d->op->getCallId()); /* Must be known at that time */
		d->setState(CallSession::State::OutgoingProgress, "Outgoing call in progress");
	}
	return result;
}

LinphoneStatus CallSession::terminate (const LinphoneErrorInfo *ei) {
	L_D();
	lInfo() << "Terminate CallSession [" << this << "] which is currently in state [" << Utils::toString(d->state) << "]";
	SalErrorInfo sei;
	memset(&sei, 0, sizeof(sei));
	switch (d->state) {
		case CallSession::State::Released:
		case CallSession::State::End:
		case CallSession::State::Error:
			lWarning() << "No need to terminate CallSession [" << this << "] in state [" << Utils::toString(d->state) << "]";
			return -1;
		case CallSession::State::IncomingReceived:
		case CallSession::State::PushIncomingReceived:
		case CallSession::State::IncomingEarlyMedia:
			return decline(ei);
		case CallSession::State::OutgoingInit:
			/* In state OutgoingInit, op has to be destroyed */
			d->op->release();
			d->op = nullptr;
			break;
		case CallSession::State::Idle:
			// Do nothing if trying to terminate call in idle state
			break;
		default:
			if (ei) {
				linphone_error_info_to_sal(ei, &sei);
				d->op->terminate(&sei);
				sal_error_info_reset(&sei);
			} else
				d->op->terminate();
			break;
	}

	d->terminate();
	return 0;
}

LinphoneStatus CallSession::transfer (const shared_ptr<CallSession> &dest) {
	L_D();
	int result = d->op->referWithReplaces(dest->getPrivate()->op);
	d->setTransferState(CallSession::State::OutgoingInit);
	return result;
}

LinphoneStatus CallSession::transfer (const Address &address) {
	L_D();
	if (!address.isValid()) {
		lError() << "Received invalid address " << address.asString() << " to transfer the call to";
		return -1;
	}
	d->op->refer(address.asString().c_str());
	d->setTransferState(CallSession::State::OutgoingInit);
	return 0;
}

LinphoneStatus CallSession::transfer (const string &dest) {
	Address address(getCore()->interpretUrl(dest));
	return transfer(address);
}

LinphoneStatus CallSession::update (const CallSessionParams *csp, const UpdateMethod method, const string &subject, const Content *content) {
	L_D();
	CallSession::State nextState;
	CallSession::State initialState = d->state;
	if (!d->isUpdateAllowed(nextState))
		return -1;
	if (d->currentParams == csp)
		lWarning() << "CallSession::update() is given the current params, this is probably not what you intend to do!";
	if (csp)
		d->setParams(new CallSessionParams(*csp));

	d->op->setLocalBody(content ? *content : Content());
	LinphoneStatus result = d->startUpdate(method, subject);
	if (result && (d->state != initialState)) {
		/* Restore initial state */
		d->setState(initialState, "Restore initial state");
	}
	return result;
}

// -----------------------------------------------------------------------------

LinphoneCallDir CallSession::getDirection () const {
	L_D();
	return d->direction;
}

const Address& CallSession::getDiversionAddress () const {
	L_D();
	if (d->op && d->op->getDiversionAddress()) {
		char *addrStr = sal_address_as_string(d->op->getDiversionAddress());
		d->diversionAddress = Address(addrStr);
		bctbx_free(addrStr);
	} else {
		d->diversionAddress = Address();
	}
	return d->diversionAddress;
}

int CallSession::getDuration () const {
	L_D();
	switch (d->state) {
		case CallSession::State::End:
		case CallSession::State::Error:
		case CallSession::State::Released:
			return d->log->getDuration();
		default:
			return d->computeDuration();
	}
}

const LinphoneErrorInfo * CallSession::getErrorInfo () const {
	L_D();
	if (!d->nonOpError)
		linphone_error_info_from_sal_op(d->ei, d->op);
	return d->ei;
}

const Address& CallSession::getLocalAddress () const {
	L_D();
	return (d->direction == LinphoneCallIncoming)
		? 
			(d->log->getToAddress() ? *L_GET_CPP_PTR_FROM_C_OBJECT(d->log->getToAddress()) : d->emptyAddress) :
			(d->log->getFromAddress() ? *L_GET_CPP_PTR_FROM_C_OBJECT(d->log->getFromAddress()) : d->emptyAddress);
}

shared_ptr<CallLog> CallSession::getLog () const {
	L_D();
	return d->log;
}

Address CallSession::getContactAddress() const {
	L_D();
	const auto op = d->getOp();
	char * contactAddressStr = NULL;
	if (op->getContactAddress()) {
		contactAddressStr = sal_address_as_string(op->getContactAddress());
	} else if (d->getDestProxy() && linphone_core_conference_server_enabled(getCore()->getCCore()) && linphone_proxy_config_get_contact(d->getDestProxy())) {
		contactAddressStr = linphone_address_as_string(linphone_proxy_config_get_contact(d->getDestProxy()));
	} else {
		lError() << "Unable to retrieve contact address from proxy confguration for call " << this << " (local address " << getLocalAddress().asString() << " remote address " <<  (getRemoteAddress() ? getRemoteAddress()->asString() : "Unknown") << ").";
	}
	if (contactAddressStr) {
		Address contactAddress(contactAddressStr);
		updateContactAddress(contactAddress);
		ms_free(contactAddressStr);
		return contactAddress;
	}
	return Address();
}

LinphoneReason CallSession::getReason () const {
	return linphone_error_info_get_reason(getErrorInfo());
}

shared_ptr<CallSession> CallSession::getReferer () const {
	L_D();
	return d->referer;
}

const string &CallSession::getReferTo () const {
	L_D();
	return d->referTo;
}

const Address &CallSession::getReferToAddress () const {
	L_D();
	return d->referToAddress;
}

const Address *CallSession::getRemoteAddress () const {
	L_D();
	const LinphoneAddress *address = (d->direction == LinphoneCallIncoming)
	? d->log->getFromAddress() : d->log->getToAddress();
	return address? L_GET_CPP_PTR_FROM_C_OBJECT(address) : nullptr;
}

const string &CallSession::getRemoteContact () const {
	L_D();
	if (d->op) {
		/* sal_op_get_remote_contact preserves header params */
		return d->op->getRemoteContact();
	}
	return d->emptyString;
}

const Address *CallSession::getRemoteContactAddress () const {
	L_D();
	if (!d->op) {
		return nullptr;
	}
	if (!d->op->getRemoteContactAddress()) {
		return nullptr;
	}
	char *addrStr = sal_address_as_string(d->op->getRemoteContactAddress());
	d->remoteContactAddress = Address(addrStr);
	bctbx_free(addrStr);
	return &d->remoteContactAddress;
}

const CallSessionParams * CallSession::getRemoteParams () {
	L_D();
	if (d->op){
		const SalCustomHeader *ch = d->op->getRecvCustomHeaders();
		if (ch) {
			/* Instanciate a remote_params only if a SIP message was received before (custom headers indicates this) */
			if (!d->remoteParams)
				d->remoteParams = new CallSessionParams();
			d->remoteParams->getPrivate()->setCustomHeaders(ch);
		}

		const list<Content> additionnalContents = d->op->getAdditionalRemoteBodies();
		for (auto& content : additionnalContents)
			d->remoteParams->addCustomContent(content);

		return d->remoteParams;
	}
	return nullptr;
}

CallSession::State CallSession::getState () const {
	L_D();
	return d->state;
}

CallSession::State CallSession::getPreviousState () const {
	L_D();
	return d->prevState;
}

const Address& CallSession::getToAddress () const {
	L_D();
	return *L_GET_CPP_PTR_FROM_C_OBJECT(d->log->getToAddress());
}

CallSession::State CallSession::getTransferState () const {
	L_D();
	return d->transferState;
}

shared_ptr<CallSession> CallSession::getTransferTarget () const {
	L_D();
	return d->transferTarget;
}

const char *CallSession::getToHeader (const string &name) const {
	L_D();
	if (d->op) {
		return sal_custom_header_find(d->op->getRecvCustomHeaders(), name.c_str());
	}
	return NULL;
}

// -----------------------------------------------------------------------------

const string &CallSession::getRemoteUserAgent () const {
	L_D();
	if (d->op)
		return d->op->getRemoteUserAgent();
	return d->emptyString;
}

shared_ptr<CallSession> CallSession::getReplacedCallSession () const {
	L_D();
	SalOp *replacedOp = d->op->getReplaces();
	if (!replacedOp)
		return nullptr;
	return static_cast<CallSession *>(replacedOp->getUserPointer())->getSharedFromThis();
}

CallSessionParams * CallSession::getCurrentParams () const {
	L_D();
	d->updateCurrentParams();
	return d->currentParams;
}

// -----------------------------------------------------------------------------

const CallSessionParams * CallSession::getParams () const {
	L_D();
	return d->params;
}

void CallSession::updateContactAddress (Address & contactAddress) const {
	L_D();

	const auto isInConference = d->isInConference();
	const std::string confId(d->getConferenceId());

	if (isInConference) {
		// Add conference ID
		if (!contactAddress.hasUriParam("conf-id")) {
			if (confId.empty() == false) {
				contactAddress.setUriParam("conf-id", confId);
			}
		}
		if (!contactAddress.hasParam("isfocus")) {
			// If in conference and contact address doesn't have isfocus
			contactAddress.setParam("isfocus");
		}
	} else {
		// If not in conference and contact address has isfocus
		if (contactAddress.hasUriParam("conf-id")) {
			contactAddress.removeUriParam("conf-id");
		}
		if (contactAddress.hasParam("isfocus")) {
			contactAddress.removeParam("isfocus");
		}
	}

	#ifdef HAVE_DB_STORAGE
	auto &mainDb = getCore()->getPrivate()->mainDb;
	if (mainDb)  {
		const auto & confInfo = mainDb->getConferenceInfoFromURI(ConferenceAddress(*getRemoteAddress()));
		if (confInfo) {
			// me is admin if the organizer is the same as me
			contactAddress.setParam("admin", Utils::toString((confInfo->getOrganizer() == getLocalAddress())));
		}
	}
	#endif

}

// -----------------------------------------------------------------------------

bool CallSession::isEarlyState (CallSession::State state) {
	switch (state) {
		case CallSession::State::Idle:
		case CallSession::State::OutgoingInit:
		case CallSession::State::OutgoingEarlyMedia:
		case CallSession::State::OutgoingRinging:
		case CallSession::State::OutgoingProgress:
		case CallSession::State::IncomingReceived:
		case CallSession::State::PushIncomingReceived:
		case CallSession::State::IncomingEarlyMedia:
		case CallSession::State::EarlyUpdatedByRemote:
		case CallSession::State::EarlyUpdating:
			return true;
		default:
			return false;
	}
}

void CallSession::addPendingAction(std::function<LinphoneStatus()> f) {
	L_D();
	d->pendingActions.push(f);
}

LINPHONE_END_NAMESPACE
