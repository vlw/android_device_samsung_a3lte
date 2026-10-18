//SPDX-License-Identifier: GPL-2.0
package me.phh.sip

import android.media.MediaCodec
import android.telephony.Rlog

data class SipUplinkEncoderDrainState(
    val firstPacket: Boolean,
    val realFrameCount: Int,
)

object SipUplinkAudioEncoder {
    fun queuePcmInput(
        logTag: String,
        encoder: SipMediaCodec,
        buffer: ByteArray,
        size: Int,
        gainQ8: Int,
        logInput: Boolean,
    ) {
        if (logInput) {
            val allZero = buffer.take(size.coerceAtLeast(0)).all { it == 0.toByte() }
            Rlog.d(logTag, "AudioRecord.read nRead=$size allZero=$allZero (bufferSize=${buffer.size})")
        }

        val inBufIdx = encoder.dequeueInputBuffer(-1)
        val inBuf = encoder.getInputBuffer(inBufIdx)!!
        inBuf.clear()
        if (size > 0) {
            SipUplinkGain.applyInPlace(
                buffer = buffer,
                size = size,
                gainQ8 = gainQ8,
            )
        }
        inBuf.put(buffer, 0, size)

        // Fake timestamp but it is not appearing in the output stream anyway
        encoder.queueInputBuffer(inBufIdx, 0, size, System.nanoTime() / 1000, 0)
    }

    fun drainEncodedOutput(
        logTag: String,
        encoder: SipMediaCodec,
        audioCodec: NegotiatedAudioCodec,
        firstPacket: Boolean,
        realFrameCount: Int,
        nextSequenceNumber: () -> Int,
        nextTimestamp: () -> Int,
        sendFrame: (
            sequenceNumber: Int,
            timestamp: Int,
            storageFrame: ByteArray,
            marker: Boolean,
            frameType: Int,
            frameSize: Int,
            realFrameCount: Int,
        ) -> Boolean,
    ): SipUplinkEncoderDrainState {
        var currentFirstPacket = firstPacket
        var currentRealFrameCount = realFrameCount

        // Drain all output frames the encoder produced for this input.
        // Use -1 (block) on the first call so we always wait for the async
        // C2 encoder to finish; use 0 on subsequent calls to collect any
        // additional frames without stalling.  Without draining, the output
        // queue fills up and dequeueInputBuffer(-1) deadlocks.
        val outBufInfo = MediaCodec.BufferInfo()
        var drainTimeout = -1L
        var outCount = 0
        while (true) {
            val outBufIdx = encoder.dequeueOutputBuffer(outBufInfo, drainTimeout)
            drainTimeout = 0L
            if (outBufIdx == MediaCodec.INFO_OUTPUT_FORMAT_CHANGED) {
                Rlog.d(logTag, "Encoder output format changed")
                continue
            }
            if (outBufIdx < 0) {
                if (outCount > 0) Rlog.d(logTag, "Drained $outCount output buffers")
                break
            }
            outCount++

            val outBuf = encoder.getOutputBuffer(outBufIdx)!!

            val encoderData = ByteArray(outBufInfo.size)
            outBuf.get(encoderData)
            encoder.releaseOutputBuffer(outBufIdx, false)

            if (currentRealFrameCount == 0) {
                Rlog.d(logTag, "First encoder output: size=${outBufInfo.size} raw=${encoderData.take(32).joinToString(" ") { "%02x".format(it) }}")
            }

            var bufPos = 0
            while (bufPos < outBufInfo.size) {
                val ft = if (audioCodec.isEvs) encoderData[bufPos].toInt() and 0x3f
                    else (encoderData[bufPos].toUByte().toInt() shr 3) and 0xf
                val frameSize = SipAmrRtpPayload.storageFrameSizeBytes(audioCodec, ft)
                if (frameSize == null) {
                    Rlog.w(logTag, "Skipping encoder frame with unsupported AMR FT=$ft codec=${audioCodec.name}")
                    break
                }
                if (outBufInfo.size - bufPos < frameSize) break

                if (audioCodec.isEvs && (ft and 15)==15) {
                    nextTimestamp() // preserve the elapsed frame time during DTX
                    currentFirstPacket=true
                    bufPos+=frameSize
                    continue
                }
                val sequenceNumber = nextSequenceNumber()
                val timestamp = nextTimestamp()
                val storageFrame = encoderData.copyOfRange(bufPos, bufPos + frameSize)
                if (!sendFrame(
                    sequenceNumber,
                    timestamp,
                    storageFrame,
                    currentFirstPacket && (!audioCodec.isEvs || EvsFrames.speech(ft)),
                    ft,
                    frameSize,
                    currentRealFrameCount,
                )) break

                currentFirstPacket = audioCodec.isEvs && !EvsFrames.speech(ft)
                currentRealFrameCount++
                bufPos += frameSize
            }
        }

        return SipUplinkEncoderDrainState(
            firstPacket = currentFirstPacket,
            realFrameCount = currentRealFrameCount,
        )
    }
}
