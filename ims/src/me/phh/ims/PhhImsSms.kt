//SPDX-License-Identifier: GPL-2.0
package me.phh.ims

import android.telephony.Rlog
import android.telephony.SmsManager
import android.telephony.ims.stub.ImsSmsImplBase
import me.phh.sip.SipHandler
import java.util.concurrent.atomic.AtomicBoolean
import java.util.concurrent.atomic.AtomicInteger

// handle sms from device to ims
// frameworks/base/telephony/java/android/telephony/ims/stub/ImsSmsImplBase.java
class PhhImsSms(val slotId: Int) : ImsSmsImplBase() {
    companion object {
        private const val TAG = "Phh ImsSms"
    }

    lateinit var sipHandler: SipHandler
    private val nextMessageRef = AtomicInteger(0)

    // phone -> outside API
    override fun sendSms(
        token: Int,
        messageRef: Int,
        format: String?,
        smsc: String?,
        isRetry: Boolean,
        pdu: ByteArray
    ) {
        // Socket failure and the RP timer may race. Complete each Android token once.
        val completed = AtomicBoolean(false)
        // Android initially supplies TP-MR=0. Assign it before sending, then return
        // that same value so delivery reports match the pending Android tracker.
        val sentRef = if (isRetry) messageRef and 0xff else nextMessageRef.getAndIncrement() and 0xff
        fun fail(status: Int, reason: Int) {
            if (completed.compareAndSet(false, true)) {
                onSendSmsResultError(token, sentRef, status, reason, RESULT_NO_NETWORK_ERROR)
            }
        }
        try {
            // called when android tries to send a sms?
            Rlog.d(TAG, "$slotId sendSms $token, $messageRef, $format, $smsc")
            if (format != "3gpp") {
                // we only know how to send 3gpp formatted sms.
                // Android should do that correctly, error if not that will
                // properly display 'message not sent' in messaging app
                onSendSmsResultError(
                    token,
                    messageRef,
                    ImsSmsImplBase.SEND_STATUS_ERROR,
                    SmsManager.RESULT_INVALID_SMS_FORMAT,
                    RESULT_NO_NETWORK_ERROR
                )
                return
            }
            if (::sipHandler.isInitialized == false) {
                onSendSmsResultError(
                    token,
                    messageRef,
                    ImsSmsImplBase.SEND_STATUS_ERROR_FALLBACK,
                    SmsManager.RESULT_ERROR_NO_SERVICE,
                    RESULT_NO_NETWORK_ERROR
                )
                return
            }
            require(pdu.size >= 2 && (pdu[0].toInt() and 3) == 1) { "Expected SMS-SUBMIT TPDU" }
            val outgoingPdu = pdu.clone()
            outgoingPdu[1] = sentRef.toByte()
            sipHandler.sendSms(
                smsc,
                outgoingPdu,
                sentRef,
                {
                    // success cb
                    if (completed.compareAndSet(false, true)) onSendSmsResultSuccess(token, sentRef)
                },
                {
                    fail(SEND_STATUS_ERROR_FALLBACK, SmsManager.RESULT_ERROR_GENERIC_FAILURE)
                }
            )
        } catch(t: Throwable) {
            android.util.Log.e(TAG, "Failed sending sms", t)
            fail(SEND_STATUS_ERROR_FALLBACK, SmsManager.RESULT_ERROR_NO_SERVICE)
        }
    }
    override fun acknowledgeSms(token: Int, messageRef: Int, result: Int) {
        // called when android acks a received sms
        Rlog.d(TAG, "$slotId acknowledgeSms $token, $messageRef, $result")

        // open notification on error?
        val error = result != ImsSmsImplBase.DELIVER_STATUS_OK
        if (::sipHandler.isInitialized) sipHandler.sendSmsAck(token, messageRef, error)
    }
    override fun acknowledgeSmsReport(token: Int, messageRef: Int, result: Int) {
        // called when android acks onSmsStatusReportReceived?
        Rlog.d(TAG, "$slotId acknowledgeSmsReport $token, $messageRef, $result")
        if (::sipHandler.isInitialized) {
            sipHandler.sendSmsAck(token, messageRef, result != STATUS_REPORT_STATUS_OK)
        }
    }
    override fun onReady() {
        // should not do anything before this is called
        Rlog.d(TAG, "$slotId onReady")
    }
    // outside -> phone API
    // on message received from ims call onSmsReceived(token, format, pdu)
    // on sms successfully sent to ims call onSendSmsResultSuccess(token, sentRef)
    // on error onSendSmsResultError(token, sentRef, status, reason, networkErrorCode)
    // on status report of a send message is received call
    //      onSmsStatusReportReceived(token, format, pdu)

}
