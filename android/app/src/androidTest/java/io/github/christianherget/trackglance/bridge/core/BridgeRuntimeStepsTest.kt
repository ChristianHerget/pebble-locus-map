package io.github.christianherget.trackglance.bridge.core

import io.github.christianherget.trackglance.bridge.locus.LocusBridgeGateway
import io.github.christianherget.trackglance.bridge.locus.RecordingProfilesResult
import io.github.christianherget.trackglance.bridge.pebble.PebbleMessages
import io.github.christianherget.trackglance.bridge.pebble.SnapshotRequestHandler
import io.github.christianherget.trackglance.bridge.pebble.TrustAdmission
import io.github.christianherget.trackglance.bridge.protocol.BridgeProtocol
import io.rebble.pebblekit2.common.model.PebbleDictionaryItem
import io.rebble.pebblekit2.common.model.WatchIdentifier
import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class BridgeRuntimeStepsTest {
    @Test
    fun type4AuthorityThenType12ReturnsSourceNeutralAccumulatedSteps() = runBlocking {
        val sender = RecordingSender()
        val locus = StepLocus()
        val runtime = runtime(sender = sender, locus = locus)
        val watch = WatchIdentifier("step-watch")
        try {
            runtime.watchAppOpened(watch, TEST_ADMISSION)
            sender.calls.clear()
            val request =
                mapOf(
                    BridgeProtocol.Key.VERSION.toUInt() to
                        PebbleDictionaryItem.Int32(BridgeProtocol.VERSION),
                    BridgeProtocol.Key.MESSAGE_TYPE.toUInt() to
                        PebbleDictionaryItem.Int32(
                            BridgeProtocol.MessageType.REQUEST_SNAPSHOT.wire
                        ),
                    BridgeProtocol.Key.SESSION_ID.toUInt() to PebbleDictionaryItem.UInt32(27u),
                )
            assertTrue(
                SnapshotRequestHandler(
                        establish = { runtime.establishWatchSession(watch, it, TEST_ADMISSION) },
                        recover = { runtime.recoverSnapshot(watch, TEST_ADMISSION) },
                    )
                    .handle(request)
            )
            assertEquals(
                listOf(BridgeProtocol.MessageType.SNAPSHOT.wire),
                sender.types(),
            )

            sender.calls.clear()
            assertTrue(
                runtime.handleStepDelta(
                    watch,
                    27,
                    0,
                    12,
                    locus.recordingStartMillis,
                    TEST_ADMISSION,
                )
            )
            assertEquals(12, BridgeState.status.value.lastWatchSteps)
            assertTrue(runtime.refresh(watch, TEST_ADMISSION))
            assertEquals(
                12,
                PebbleMessages.signed32(
                    sender.calls.last().dictionary,
                    BridgeProtocol.Key.STEPS,
                ),
            )

            assertTrue(
                runtime.handleStepDelta(
                    watch,
                    27,
                    1,
                    BridgeProtocol.UNAVAILABLE,
                    locus.recordingStartMillis,
                    TEST_ADMISSION,
                )
            )
            assertEquals(null, BridgeState.status.value.lastWatchSteps)
            sender.calls.clear()
            assertTrue(runtime.refresh(watch, TEST_ADMISSION))
            assertEquals(
                BridgeProtocol.UNAVAILABLE,
                PebbleMessages.signed32(
                    sender.calls.last().dictionary,
                    BridgeProtocol.Key.STEPS,
                ),
            )

            assertTrue(
                runtime.handleStepDelta(
                    watch,
                    27,
                    2,
                    0,
                    locus.recordingStartMillis,
                    TEST_ADMISSION,
                )
            )
            sender.calls.clear()
            assertTrue(runtime.refresh(watch, TEST_ADMISSION))
            assertEquals(
                12,
                PebbleMessages.signed32(
                    sender.calls.last().dictionary,
                    BridgeProtocol.Key.STEPS,
                ),
            )
        } finally {
            runtime.close()
        }
    }

    @Test
    fun stepIngressRejectsWrongAuthorityIdentityTrustRecordingAndReplay() = runBlocking {
        val locus = StepLocus()
        val runtime = runtime(sender = RecordingSender(), locus = locus)
        val watch = WatchIdentifier("step-watch")
        try {
            runtime.watchAppOpened(watch, TEST_ADMISSION)
            assertTrue(runtime.establishWatchSession(watch, 27, TEST_ADMISSION))
            assertFalse(
                runtime.handleStepDelta(
                    watch,
                    28,
                    0,
                    1,
                    locus.recordingStartMillis,
                    TEST_ADMISSION,
                )
            )
            assertFalse(
                runtime.handleStepDelta(
                    WatchIdentifier("other"),
                    27,
                    0,
                    1,
                    locus.recordingStartMillis,
                    TEST_ADMISSION,
                )
            )
            assertFalse(
                runtime.handleStepDelta(
                    watch,
                    27,
                    0,
                    1,
                    locus.recordingStartMillis,
                    TrustAdmission(1),
                )
            )
            assertFalse(
                runtime.handleStepDelta(
                    watch,
                    27,
                    0,
                    1,
                    locus.recordingStartMillis + 1,
                    TEST_ADMISSION,
                )
            )
            locus.state = BridgeProtocol.RecordingState.STOPPED
            assertFalse(
                runtime.handleStepDelta(
                    watch,
                    27,
                    0,
                    1,
                    locus.recordingStartMillis,
                    TEST_ADMISSION,
                )
            )
            locus.state = BridgeProtocol.RecordingState.PAUSED
            assertTrue(
                runtime.handleStepDelta(
                    watch,
                    27,
                    0,
                    1,
                    locus.recordingStartMillis,
                    TEST_ADMISSION,
                )
            )
            assertFalse(
                runtime.handleStepDelta(
                    watch,
                    27,
                    0,
                    1,
                    locus.recordingStartMillis,
                    TEST_ADMISSION,
                )
            )
        } finally {
            runtime.close()
        }
    }

    private class StepLocus : LocusBridgeGateway {
        val recordingStartMillis = 123_456L
        var state = BridgeProtocol.RecordingState.RECORDING

        override fun readSnapshot(nowMillis: Long) =
            BridgeProtocol.Snapshot(
                state = state,
                sampledAtEpochSeconds = nowMillis / 1_000,
                recordingStartMillis =
                    recordingStartMillis.takeIf {
                        state == BridgeProtocol.RecordingState.RECORDING ||
                            state == BridgeProtocol.RecordingState.PAUSED
                    },
                steps = null,
            )

        override fun sendHeartRate(bpm: Int) = false

        override fun recordingProfiles() =
            RecordingProfilesResult.Success(emptyList<BridgeProtocol.RecordingProfile>())

        override fun execute(
            command: BridgeProtocol.Command,
            profileName: String?,
            waypointName: String?,
        ) = BridgeProtocol.Result.FAILED
    }
}
