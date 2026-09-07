package io.github.christianherget.trackglance.bridge.core

import io.github.christianherget.trackglance.bridge.locus.CommandExecution
import io.github.christianherget.trackglance.bridge.locus.LocusBridgeGateway
import io.github.christianherget.trackglance.bridge.locus.RecordingProfilesResult
import io.github.christianherget.trackglance.bridge.pebble.PebbleDictionarySender
import io.github.christianherget.trackglance.bridge.pebble.PebbleMessages
import io.github.christianherget.trackglance.bridge.pebble.SerializedCoreSessionLeases
import io.github.christianherget.trackglance.bridge.pebble.TrustAdmission
import io.github.christianherget.trackglance.bridge.pebble.TrustLeaseResult
import io.github.christianherget.trackglance.bridge.protocol.BridgeProtocol
import io.rebble.pebblekit2.common.model.PebbleDictionary
import io.rebble.pebblekit2.common.model.TransmissionResult
import io.rebble.pebblekit2.common.model.WatchIdentifier
import java.util.concurrent.CopyOnWriteArrayList
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicLong
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.async
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.withTimeout
import kotlinx.coroutines.yield
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class BridgeRuntimeConnectionAuthorityTest {
    @Test
    fun commandFromAnExpiredConnectionSessionFailsBeforeMutation() = runBlocking {
        val currentGeneration = AtomicLong(1)
        val currentAdmission = { TrustAdmission(currentGeneration.get()) }
        val sender = AdmissionRecordingSender(currentAdmission)
        val locus = StateChangingLocus()
        val mutationGateReached = CompletableDeferred<Unit>()
        val releaseMutationGate = CompletableDeferred<Unit>()
        val runtime =
            runtime(
                sender = sender,
                locus = locus,
                trustedMutationGate = { admission, block ->
                    mutationGateReached.complete(Unit)
                    releaseMutationGate.await()
                    if (admission != currentAdmission()) {
                        TrustLeaseResult.Stale
                    } else {
                        block()
                        TrustLeaseResult.Admitted(Unit)
                    }
                },
                admissionCurrent = { it == currentAdmission() },
            )
        val watch = WatchIdentifier("watch")
        val sessionA = TrustAdmission(1)
        val sessionB = TrustAdmission(2)
        try {
            val oldCommand = async {
                runtime.handleCommand(
                    watch,
                    8,
                    1,
                    BridgeProtocol.Command.START,
                    "Hiking",
                    null,
                    sessionA,
                )
            }
            mutationGateReached.await()
            currentGeneration.set(sessionB.generation)
            releaseMutationGate.complete(Unit)

            assertFalse(oldCommand.await())
            assertEquals(0, locus.executions)
            assertTrue(sender.calls.isEmpty())

            // The begun record was durably completed FAILED, so B may retry it without action.
            assertTrue(
                runtime.handleCommand(
                    watch,
                    8,
                    1,
                    BridgeProtocol.Command.START,
                    "Hiking",
                    null,
                    sessionB,
                )
            )
            assertEquals(0, locus.executions)
            assertEquals(listOf(sessionB, sessionB), sender.calls.map { it.admission })
            assertEquals(
                listOf(
                    BridgeProtocol.MessageType.SNAPSHOT.wire,
                    BridgeProtocol.MessageType.COMMAND_RESULT.wire,
                ),
                sender.calls.map { call ->
                    PebbleMessages.signed32(
                        call.dictionary,
                        BridgeProtocol.Key.MESSAGE_TYPE,
                    )
                },
            )
        } finally {
            releaseMutationGate.complete(Unit)
            runtime.close()
        }
    }

    @Test
    fun revocationWaitsOnlyForTheExactLocusActionNotConfirmationOrDelivery() = runBlocking {
        val leases = SerializedCoreSessionLeases()
        val currentGeneration = AtomicLong(1)
        val currentAdmission = { TrustAdmission(currentGeneration.get()) }
        val sender = AdmissionRecordingSender(currentAdmission, leases)
        val locus = BlockingCommandConfirmationLocus()
        val runtime =
            runtime(
                sender = sender,
                locus = locus,
                scope = CoroutineScope(SupervisorJob() + Dispatchers.Default),
                ioDispatcher = Dispatchers.Default,
                trustedMutationGate = { admission, block ->
                    leases.withInbound {
                        if (admission != currentAdmission()) {
                            TrustLeaseResult.Stale
                        } else {
                            block()
                            TrustLeaseResult.Admitted(Unit)
                        }
                    }
                },
                admissionCurrent = { it == currentAdmission() },
            )
        val sessionA = TrustAdmission(1)
        try {
            val command =
                async(start = kotlinx.coroutines.CoroutineStart.UNDISPATCHED) {
                    runtime.handleCommand(
                        WatchIdentifier("watch"),
                        8,
                        1,
                        BridgeProtocol.Command.START,
                        "Hiking",
                        null,
                        sessionA,
                    )
                }
            withTimeout(5_000) { locus.actionStarted.await() }
            val revoke = async {
                leases.mutateSession {
                    currentGeneration.incrementAndGet()
                    runtime.companionTrustLost()
                }
            }
            yield()
            assertFalse(revoke.isCompleted)

            locus.releaseAction.countDown()
            withTimeout(5_000) { locus.confirmationReadStarted.await() }
            withTimeout(2_000) { revoke.await() }
            assertFalse(command.isCompleted)

            locus.releaseConfirmationRead.countDown()
            assertFalse(command.await())
            assertEquals(1, locus.executions)
            assertTrue(sender.calls.isEmpty())
        } finally {
            locus.releaseAction.countDown()
            locus.releaseConfirmationRead.countDown()
            runtime.close()
        }
    }

    @Test
    fun selectionLossClearsTheActiveWatchSoARealReopenStartsPollingAgain() {
        val sender = RecordingSender()
        val runtime = runtime(sender = sender)
        val watch = WatchIdentifier("watch")
        try {
            runtime.watchAppOpened(watch)
            val callsBeforeLoss = sender.calls.size
            assertTrue(callsBeforeLoss > 0)
            assertTrue(BridgeState.status.value.watchAppOpen)

            runtime.companionTrustLost()
            assertFalse(BridgeState.status.value.watchAppOpen)

            runtime.watchAppOpened(watch)
            assertTrue(BridgeState.status.value.watchAppOpen)
            assertTrue(sender.calls.size > callsBeforeLoss)
        } finally {
            runtime.close()
        }
    }

    @Test
    fun connectionResetCancelsOldPollingBeforeTheNewSessionReopens() = runBlocking {
        val currentGeneration = AtomicLong(1)
        val currentAdmission = { TrustAdmission(currentGeneration.get()) }
        val sender = AdmissionRecordingSender(currentAdmission)
        val locus = FirstReadBlockingLocus()
        val runtime =
            runtime(
                sender = sender,
                locus = locus,
                scope = CoroutineScope(SupervisorJob() + Dispatchers.Default),
                ioDispatcher = Dispatchers.Default,
                admissionCurrent = { it == currentAdmission() },
            )
        val sessionA = TrustAdmission(1)
        val sessionB = TrustAdmission(2)
        try {
            runtime.watchAppOpened(WatchIdentifier("watch-a"), sessionA)
            assertTrue(locus.firstReadStarted.await(5, TimeUnit.SECONDS))
            // This creates the separate immediate-refresh child while A's poll owns serialization.
            runtime.watchAppOpened(WatchIdentifier("watch-a-2"), sessionA)

            currentGeneration.set(sessionB.generation)
            runtime.companionTrustLost()
            runtime.watchAppOpened(WatchIdentifier("watch-b"), sessionB)
            locus.releaseFirstRead.countDown()

            withTimeout(5_000) {
                while (sender.calls.isEmpty()) yield()
            }
            assertEquals(listOf(sessionB), sender.calls.map { it.admission }.distinct())
            assertEquals(
                setOf(WatchIdentifier("watch-b")),
                sender.calls.flatMap { it.watches }.toSet(),
            )
        } finally {
            locus.releaseFirstRead.countDown()
            runtime.close()
        }
    }

    @Test
    fun staleSnapshotPublicationCannotOverwriteNewSessionDiagnostics() = runBlocking {
        val currentGeneration = AtomicLong(1)
        val currentAdmission = { TrustAdmission(currentGeneration.get()) }
        val sender = AdmissionRecordingSender(currentAdmission)
        val publicationReached = CompletableDeferred<Unit>()
        val releasePublication = CompletableDeferred<Unit>()
        val runtime =
            runtime(
                sender = sender,
                locus = StateChangingLocus(),
                admissionCurrent = { it == currentAdmission() },
                trustedPublicationGate = { admission, block ->
                    publicationReached.complete(Unit)
                    releasePublication.await()
                    if (admission != currentAdmission()) {
                        TrustLeaseResult.Stale
                    } else {
                        block()
                        TrustLeaseResult.Admitted(Unit)
                    }
                },
            )
        val sessionA = TrustAdmission(1)
        try {
            val staleRefresh = async {
                runtime.refresh(WatchIdentifier("watch"), sessionA)
            }
            publicationReached.await()
            currentGeneration.incrementAndGet()
            BridgeState.update {
                it.copy(
                    recordingState = BridgeProtocol.RecordingState.PAUSED,
                    lastError = BridgeFailure.technical("new-session-diagnostics"),
                )
            }
            releasePublication.complete(Unit)

            assertFalse(staleRefresh.await())
            assertEquals(
                BridgeProtocol.RecordingState.PAUSED,
                BridgeState.status.value.recordingState,
            )
            assertEquals(
                "new-session-diagnostics",
                BridgeState.status.value.lastError?.technicalDetail,
            )
            assertTrue(sender.calls.isEmpty())
        } finally {
            releasePublication.complete(Unit)
            runtime.close()
        }
    }

    private class BlockingCommandConfirmationLocus : LocusBridgeGateway {
        val actionStarted = CompletableDeferred<Unit>()
        val releaseAction = CountDownLatch(1)
        val confirmationReadStarted = CompletableDeferred<Unit>()
        val releaseConfirmationRead = CountDownLatch(1)
        var executions = 0

        override fun readSnapshot(nowMillis: Long): BridgeProtocol.Snapshot {
            confirmationReadStarted.complete(Unit)
            check(releaseConfirmationRead.await(5, TimeUnit.SECONDS)) {
                "test confirmation read was not released"
            }
            return BridgeProtocol.Snapshot(
                BridgeProtocol.RecordingState.RECORDING,
                nowMillis / 1_000,
            )
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
            executions++
            actionStarted.complete(Unit)
            check(releaseAction.await(5, TimeUnit.SECONDS)) { "test action was not released" }
            return CommandExecution(
                BridgeProtocol.Result.OK,
                BridgeProtocol.RecordingState.RECORDING,
            )
        }
    }

    private class FirstReadBlockingLocus : LocusBridgeGateway {
        val firstReadStarted = CountDownLatch(1)
        val releaseFirstRead = CountDownLatch(1)
        private val reads = AtomicLong()

        override fun readSnapshot(nowMillis: Long): BridgeProtocol.Snapshot {
            if (reads.incrementAndGet() == 1L) {
                firstReadStarted.countDown()
                check(releaseFirstRead.await(5, TimeUnit.SECONDS)) { "test read was not released" }
            }
            return BridgeProtocol.Snapshot(
                BridgeProtocol.RecordingState.STOPPED,
                nowMillis / 1_000,
            )
        }

        override fun sendHeartRate(bpm: Int) = false

        override fun recordingProfiles() =
            RecordingProfilesResult.Success(emptyList<BridgeProtocol.RecordingProfile>())

        override fun execute(
            command: BridgeProtocol.Command,
            profileName: String?,
            waypointName: String?,
        ) = BridgeProtocol.Result.FAILED
    }

    private class AdmissionRecordingSender(
        private val currentAdmission: () -> TrustAdmission,
        private val leases: SerializedCoreSessionLeases? = null,
    ) : PebbleDictionarySender {
        data class Call(
            val dictionary: PebbleDictionary,
            val watches: List<WatchIdentifier>,
            val admission: TrustAdmission,
        )

        val calls = CopyOnWriteArrayList<Call>()

        override suspend fun send(
            dictionary: PebbleDictionary,
            watch: WatchIdentifier,
            admission: TrustAdmission,
        ): TransmissionResult? {
            val deliver = {
                if (admission != currentAdmission()) {
                    null
                } else {
                    calls += Call(dictionary, listOf(watch), admission)
                    TransmissionResult.Success
                }
            }
            return leases?.withOutbound(deliver) ?: deliver()
        }

        override fun close() = Unit
    }
}
