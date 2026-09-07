package io.github.christianherget.trackglance.bridge.core

import io.github.christianherget.trackglance.bridge.locus.CommandExecution
import io.github.christianherget.trackglance.bridge.locus.LocusBridgeGateway
import io.github.christianherget.trackglance.bridge.locus.RecordingProfilesResult
import io.github.christianherget.trackglance.bridge.pebble.PebbleDictionarySender
import io.github.christianherget.trackglance.bridge.pebble.PebbleMessages
import io.github.christianherget.trackglance.bridge.pebble.TrustAdmission
import io.github.christianherget.trackglance.bridge.protocol.BridgeProtocol
import io.rebble.pebblekit2.common.model.PebbleDictionary
import io.rebble.pebblekit2.common.model.TransmissionResult
import io.rebble.pebblekit2.common.model.WatchIdentifier
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.async
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.yield
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class BridgeRuntimeCommandsTest {
    @Test
    fun commandResultsAndRefreshesReturnOnlyToTheirSourceWatch() = runBlocking {
        val sender = RecordingSender()
        val locus = FakeLocus()
        val runtime = runtime(sender = sender, locus = locus)
        val watchA = WatchIdentifier("watch-a")
        val watchB = WatchIdentifier("watch-b")
        try {
            assertTrue(
                runtime.handleCommand(watchA, 7, 1, BridgeProtocol.Command.STOP_SAVE, null, null)
            )
            assertTrue(
                runtime.handleCommand(
                    watchA,
                    7,
                    1,
                    BridgeProtocol.Command.STOP_SAVE,
                    "ignored",
                    null,
                )
            )
            assertTrue(
                runtime.handleCommand(watchB, 7, 1, BridgeProtocol.Command.STOP_SAVE, null, null)
            )

            assertEquals(2, locus.executions)
            assertTrue(sender.calls.take(4).all { it.watches == listOf(watchA) })
            assertTrue(sender.calls.drop(4).take(2).all { it.watches == listOf(watchB) })

            assertTrue(
                runtime.handleCommand(watchA, 7, 1, BridgeProtocol.Command.START, "Hiking", null)
            )
            assertEquals(2, locus.executions)
            assertEquals(
                BridgeProtocol.Result.FAILED.wire,
                PebbleMessages.signed32(sender.calls.last().dictionary, BridgeProtocol.Key.RESULT),
            )
            assertEquals(BridgeProtocol.Command.START, BridgeState.status.value.lastCommand)
            assertEquals(BridgeProtocol.Result.FAILED, BridgeState.status.value.lastCommandResult)
            assertEquals(null, BridgeState.status.value.lastWaypointName)
        } finally {
            runtime.close()
        }
    }

    @Test
    fun commandResultFollowsANewerAcceptedSnapshotAndLateOldDeliveryIsRejected() = runBlocking {
        val sender = ReceiverOrderingSender()
        val locus = StateChangingLocus()
        val runtime = runtime(sender = sender, locus = locus, maxAttempts = 2)
        val watch = WatchIdentifier("ordered-watch")
        try {
            val oldSnapshot = async { runtime.refresh(listOf(watch)) }
            sender.firstSnapshotAttemptStarted.await()
            val command = async {
                runtime.handleCommand(watch, 8, 1, BridgeProtocol.Command.START, "Hiking", null)
            }
            yield()

            assertEquals(0, locus.executions)
            assertFalse(command.isCompleted)

            sender.releaseFirstSnapshotAttempt.complete(Unit)
            assertFalse(oldSnapshot.await())
            assertTrue(command.await())

            assertEquals(
                listOf(
                    BridgeProtocol.MessageType.SNAPSHOT.wire,
                    BridgeProtocol.MessageType.SNAPSHOT.wire,
                    BridgeProtocol.MessageType.SNAPSHOT.wire,
                    BridgeProtocol.MessageType.COMMAND_RESULT.wire,
                ),
                sender.attemptTypes,
            )
            assertEquals(sender.snapshotEpochs[0], sender.snapshotEpochs[1])
            assertTrue(sender.snapshotEpochs[2] > sender.snapshotEpochs[0])
            assertEquals(sender.snapshotEpochs[2], sender.epochAcceptedBeforeResult)
            assertFalse(sender.deliverDelayedPreCommandSnapshot())
        } finally {
            runtime.close()
        }
    }

    @Test
    fun commandResultIsNotIssuedWhenThePostCommandSnapshotCannotBeDelivered() = runBlocking {
        val sender = SnapshotFailingSender()
        val locus = StateChangingLocus()
        val runtime = runtime(sender = sender, locus = locus)
        try {
            assertFalse(
                runtime.handleCommand(
                    WatchIdentifier("watch"),
                    8,
                    1,
                    BridgeProtocol.Command.START,
                    "Hiking",
                    null,
                )
            )

            assertEquals(1, locus.executions)
            assertEquals(
                listOf(BridgeProtocol.MessageType.SNAPSHOT.wire),
                sender.attemptTypes,
            )
        } finally {
            runtime.close()
        }
    }

    @Test
    fun delayedLocusTransitionIsObservedBeforeAnOkCommandResult() = runBlocking {
        val sender = RecordingSender()
        val locus = DelayedStateChangingLocus(transitionAfterPostExecuteReads = 3)
        val runtime = runtime(sender = sender, locus = locus)
        try {
            assertTrue(
                runtime.handleCommand(
                    WatchIdentifier("watch"),
                    8,
                    1,
                    BridgeProtocol.Command.START,
                    "Hiking",
                    null,
                )
            )

            val barrierIndex =
                sender.calls.indexOfFirst { call ->
                    PebbleMessages.signed32(
                        call.dictionary,
                        BridgeProtocol.Key.MESSAGE_TYPE,
                    ) == BridgeProtocol.MessageType.SNAPSHOT.wire
                }
            val resultIndex =
                sender.calls.indexOfFirst { call ->
                    PebbleMessages.signed32(
                        call.dictionary,
                        BridgeProtocol.Key.MESSAGE_TYPE,
                    ) == BridgeProtocol.MessageType.COMMAND_RESULT.wire
                }
            assertTrue(barrierIndex in 0 until resultIndex)
            assertEquals(
                BridgeProtocol.RecordingState.RECORDING.wire,
                PebbleMessages.signed32(
                    sender.calls[barrierIndex].dictionary,
                    BridgeProtocol.Key.RECORDING_STATE,
                ),
            )
            assertEquals(
                BridgeProtocol.Result.OK.wire,
                PebbleMessages.signed32(
                    sender.calls[resultIndex].dictionary,
                    BridgeProtocol.Key.RESULT,
                ),
            )
            assertEquals(1, locus.executions)
            assertEquals(3, locus.postExecuteReads)
            assertEquals(2, sender.calls.size)
        } finally {
            runtime.close()
        }
    }

    @Test
    fun unconfirmedTransitionReturnsFailedAndDedupeSkipsObsoleteTargetPolling() = runBlocking {
        val sender = RecordingSender()
        val locus = DelayedStateChangingLocus(transitionAfterPostExecuteReads = null)
        val runtime = runtime(sender = sender, locus = locus)
        val watch = WatchIdentifier("watch")
        try {
            assertTrue(
                runtime.handleCommand(
                    watch,
                    8,
                    1,
                    BridgeProtocol.Command.START,
                    "Hiking",
                    null,
                )
            )
            val firstResult =
                sender.calls.single { call ->
                    PebbleMessages.signed32(
                        call.dictionary,
                        BridgeProtocol.Key.MESSAGE_TYPE,
                    ) == BridgeProtocol.MessageType.COMMAND_RESULT.wire
                }
            assertEquals(
                BridgeProtocol.Result.FAILED.wire,
                PebbleMessages.signed32(firstResult.dictionary, BridgeProtocol.Key.RESULT),
            )
            val firstBarrier =
                sender.calls.first { call ->
                    PebbleMessages.signed32(
                        call.dictionary,
                        BridgeProtocol.Key.MESSAGE_TYPE,
                    ) == BridgeProtocol.MessageType.SNAPSHOT.wire
                }
            assertEquals(
                BridgeProtocol.RecordingState.STOPPED.wire,
                PebbleMessages.signed32(
                    firstBarrier.dictionary,
                    BridgeProtocol.Key.RECORDING_STATE,
                ),
            )
            val readsAfterFirstAttempt = locus.postExecuteReads
            assertTrue(readsAfterFirstAttempt > 1)

            sender.calls.clear()
            assertTrue(
                runtime.handleCommand(
                    watch,
                    8,
                    1,
                    BridgeProtocol.Command.START,
                    "Hiking",
                    null,
                )
            )

            assertEquals(1, locus.executions)
            assertTrue(locus.postExecuteReads - readsAfterFirstAttempt <= 2)
            assertEquals(
                BridgeProtocol.Result.FAILED.wire,
                PebbleMessages.signed32(
                    sender.calls
                        .single { call ->
                            PebbleMessages.signed32(
                                call.dictionary,
                                BridgeProtocol.Key.MESSAGE_TYPE,
                            ) == BridgeProtocol.MessageType.COMMAND_RESULT.wire
                        }
                        .dictionary,
                    BridgeProtocol.Key.RESULT,
                ),
            )
        } finally {
            runtime.close()
        }
    }

    @Test
    fun pauseResumeUsesTheTargetFromTheGatewaysExactRoutingDecision() = runBlocking {
        val sender = RecordingSender()
        val locus = PauseResumeRoutingRaceLocus()
        val runtime = runtime(sender = sender, locus = locus)
        try {
            assertTrue(
                runtime.handleCommand(
                    WatchIdentifier("watch"),
                    8,
                    1,
                    BridgeProtocol.Command.PAUSE_RESUME,
                    null,
                    null,
                )
            )

            val barrier =
                sender.calls.first { call ->
                    PebbleMessages.signed32(
                        call.dictionary,
                        BridgeProtocol.Key.MESSAGE_TYPE,
                    ) == BridgeProtocol.MessageType.SNAPSHOT.wire
                }
            assertEquals(
                BridgeProtocol.RecordingState.RECORDING.wire,
                PebbleMessages.signed32(barrier.dictionary, BridgeProtocol.Key.RECORDING_STATE),
            )
            assertEquals(2, locus.postExecuteReads)
            assertEquals(
                BridgeProtocol.Result.OK.wire,
                PebbleMessages.signed32(
                    sender.calls
                        .single { call ->
                            PebbleMessages.signed32(
                                call.dictionary,
                                BridgeProtocol.Key.MESSAGE_TYPE,
                            ) == BridgeProtocol.MessageType.COMMAND_RESULT.wire
                        }
                        .dictionary,
                    BridgeProtocol.Key.RESULT,
                ),
            )
        } finally {
            runtime.close()
        }
    }

    private class DelayedStateChangingLocus(private val transitionAfterPostExecuteReads: Int?) :
        LocusBridgeGateway {
        var executions = 0
        var postExecuteReads = 0
        private var executeIssued = false
        private var state = BridgeProtocol.RecordingState.STOPPED

        override fun readSnapshot(nowMillis: Long): BridgeProtocol.Snapshot {
            if (executeIssued && state == BridgeProtocol.RecordingState.STOPPED) {
                postExecuteReads++
                if (
                    transitionAfterPostExecuteReads != null &&
                        postExecuteReads >= transitionAfterPostExecuteReads
                ) {
                    state = BridgeProtocol.RecordingState.RECORDING
                }
            }
            return BridgeProtocol.Snapshot(
                state = state,
                sampledAtEpochSeconds = nowMillis / 1_000,
            )
        }

        override fun sendHeartRate(bpm: Int) = false

        override fun recordingProfiles() =
            RecordingProfilesResult.Success(listOf(BridgeProtocol.RecordingProfile(1, "Hiking")))

        override fun execute(
            command: BridgeProtocol.Command,
            profileName: String?,
            waypointName: String?,
        ): BridgeProtocol.Result {
            executions++
            executeIssued = true
            return BridgeProtocol.Result.OK
        }

        override fun executeWithExpectedState(
            command: BridgeProtocol.Command,
            profileName: String?,
            waypointName: String?,
        ): CommandExecution =
            CommandExecution(
                execute(command, profileName, waypointName),
                BridgeProtocol.RecordingState.RECORDING,
            )
    }

    /**
     * Models the UI changing RECORDING to PAUSED before Locus routes the same command as Resume.
     */
    private class PauseResumeRoutingRaceLocus : LocusBridgeGateway {
        var postExecuteReads = 0
        private var routed = false

        override fun readSnapshot(nowMillis: Long): BridgeProtocol.Snapshot {
            val state =
                if (!routed) {
                    BridgeProtocol.RecordingState.RECORDING
                } else {
                    postExecuteReads++
                    if (postExecuteReads == 1) {
                        BridgeProtocol.RecordingState.PAUSED
                    } else {
                        BridgeProtocol.RecordingState.RECORDING
                    }
                }
            return BridgeProtocol.Snapshot(state, nowMillis / 1_000)
        }

        override fun sendHeartRate(bpm: Int) = false

        override fun recordingProfiles() =
            RecordingProfilesResult.Success(emptyList<BridgeProtocol.RecordingProfile>())

        override fun execute(
            command: BridgeProtocol.Command,
            profileName: String?,
            waypointName: String?,
        ): BridgeProtocol.Result = error("Runtime must use the enriched routing result")

        override fun executeWithExpectedState(
            command: BridgeProtocol.Command,
            profileName: String?,
            waypointName: String?,
        ): CommandExecution {
            routed = true
            return CommandExecution(
                BridgeProtocol.Result.OK,
                BridgeProtocol.RecordingState.RECORDING,
            )
        }
    }

    private class ReceiverOrderingSender : PebbleDictionarySender {
        val firstSnapshotAttemptStarted = CompletableDeferred<Unit>()
        val releaseFirstSnapshotAttempt = CompletableDeferred<Unit>()
        val attemptTypes = mutableListOf<Int>()
        val snapshotEpochs = mutableListOf<Long>()
        var epochAcceptedBeforeResult: Long? = null
            private set

        private var snapshotAttempts = 0
        private var acceptedEpoch: Long? = null
        private var delayedPreCommandEpoch: Long? = null

        override suspend fun send(
            dictionary: PebbleDictionary,
            watch: WatchIdentifier,
            admission: TrustAdmission,
        ): TransmissionResult? {
            val type =
                requireNotNull(PebbleMessages.signed32(dictionary, BridgeProtocol.Key.MESSAGE_TYPE))
            attemptTypes += type
            if (type == BridgeProtocol.MessageType.SNAPSHOT.wire) {
                val epoch =
                    requireNotNull(
                        PebbleMessages.unsigned32(
                            dictionary,
                            BridgeProtocol.Key.SAMPLE_EPOCH_SECONDS,
                        )
                    )
                snapshotEpochs += epoch
                snapshotAttempts++
                if (snapshotAttempts == 1) {
                    delayedPreCommandEpoch = epoch
                    firstSnapshotAttemptStarted.complete(Unit)
                    releaseFirstSnapshotAttempt.await()
                    return null
                }
                if (snapshotAttempts == 2) return null
                acceptedEpoch = epoch
            } else if (type == BridgeProtocol.MessageType.COMMAND_RESULT.wire) {
                epochAcceptedBeforeResult = acceptedEpoch
            }
            return TransmissionResult.Success
        }

        fun deliverDelayedPreCommandSnapshot(): Boolean {
            val delayed = requireNotNull(delayedPreCommandEpoch)
            val floor = requireNotNull(acceptedEpoch)
            return (delayed >= floor).also { accepted ->
                if (accepted) acceptedEpoch = delayed
            }
        }

        override fun close() = Unit
    }

    private class SnapshotFailingSender : PebbleDictionarySender {
        val attemptTypes = mutableListOf<Int>()

        override suspend fun send(
            dictionary: PebbleDictionary,
            watch: WatchIdentifier,
            admission: TrustAdmission,
        ): TransmissionResult? {
            val type =
                requireNotNull(PebbleMessages.signed32(dictionary, BridgeProtocol.Key.MESSAGE_TYPE))
            attemptTypes += type
            return if (type == BridgeProtocol.MessageType.SNAPSHOT.wire) {
                null
            } else {
                TransmissionResult.Success
            }
        }

        override fun close() = Unit
    }
}
