package io.github.christianherget.trackglance.bridge.core

import io.github.christianherget.trackglance.bridge.locus.LocusBridgeGateway
import io.github.christianherget.trackglance.bridge.locus.RecordingProfilesResult
import io.github.christianherget.trackglance.bridge.pebble.SerializedCoreSessionLeases
import io.github.christianherget.trackglance.bridge.protocol.BridgeProtocol
import io.rebble.pebblekit2.common.model.WatchIdentifier
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicLong
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.async
import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class BridgeRuntimeHeartRateTest {
    @Test
    fun heartRateConsumerSurvivesOneSampleFailureAndRoutesTheNextUpdate() {
        val sender = RecordingSender()
        val locus = FakeLocus(heartRateFailures = 1)
        val runtime = runtime(sender = sender, locus = locus)
        val watch = WatchIdentifier("heart-rate-watch")
        try {
            assertTrue(runtime.handleHeartRate(watch, 9, 1, 120, 1_000))
            assertTrue(runtime.handleHeartRate(watch, 9, 2, 121, 1_000))

            assertEquals(2, locus.heartRateCalls)
            assertEquals(1, sender.calls.size)
            assertEquals(listOf(watch), sender.calls.single().watches)
        } finally {
            runtime.close()
        }
    }

    @Test
    fun queuedHeartRateSampleIsDroppedAcrossConnectionReset() = runBlocking {
        val sender = RecordingSender()
        val locus = FakeLocus()
        val leases = SerializedCoreSessionLeases()
        val consumerDequeuedSample = CompletableDeferred<Unit>()
        val releaseConsumer = CompletableDeferred<Unit>()
        val consumerFinished = CompletableDeferred<Unit>()
        val runtime =
            runtime(
                sender = sender,
                locus = locus,
                trustedWorkLease = { block ->
                    consumerDequeuedSample.complete(Unit)
                    try {
                        releaseConsumer.await()
                        leases.withInbound(block)
                    } finally {
                        consumerFinished.complete(Unit)
                    }
                },
            )
        try {
            assertTrue(runtime.handleHeartRate(WatchIdentifier("watch"), 9, 1, 120, 1_000))
            consumerDequeuedSample.await()

            // A connection reset uses this same inbound->outbound boundary.
            leases.mutateSession { runtime.companionTrustLost() }
            releaseConsumer.complete(Unit)
            consumerFinished.await()

            assertEquals(0, locus.heartRateCalls)
            assertTrue(sender.calls.isEmpty())
        } finally {
            runtime.close()
        }
    }

    @Test
    fun queuedHeartRateSampleIsDroppedWhenTheDeferredSelectionGuardIsNowFalse() = runBlocking {
        val sender = RecordingSender()
        val locus = FakeLocus()
        val leases = SerializedCoreSessionLeases()
        val consumerReachedGuard = CompletableDeferred<Unit>()
        val releaseGuard = CompletableDeferred<Unit>()
        val consumerFinished = CompletableDeferred<Unit>()
        var trusted = true
        lateinit var runtime: BridgeRuntime
        runtime =
            runtime(
                sender = sender,
                locus = locus,
                trustedWorkLease = { block ->
                    consumerReachedGuard.complete(Unit)
                    releaseGuard.await()
                    try {
                        leases.withInbound {
                            if (trusted) block() else runtime.companionTrustLost()
                        }
                    } finally {
                        consumerFinished.complete(Unit)
                    }
                },
            )
        try {
            assertTrue(runtime.handleHeartRate(WatchIdentifier("watch"), 9, 1, 120, 1_000))
            consumerReachedGuard.await()

            trusted = false
            releaseGuard.complete(Unit)
            consumerFinished.await()

            assertEquals(0, locus.heartRateCalls)
            assertTrue(sender.calls.isEmpty())
        } finally {
            runtime.close()
        }
    }

    @Test
    fun revocationWaitsForAnAdmittedHeartRateMutationToFinish() = runBlocking {
        val sender = RecordingSender()
        val locus = BlockingHeartRateLocus()
        val leases = SerializedCoreSessionLeases()
        val currentGeneration = AtomicLong(TEST_ADMISSION.generation)
        val runtime =
            runtime(
                sender = sender,
                locus = locus,
                scope = CoroutineScope(SupervisorJob() + Dispatchers.Default),
                ioDispatcher = Dispatchers.Default,
                trustedWorkLease = leases::withInbound,
                admissionCurrent = { it.generation == currentGeneration.get() },
            )
        try {
            assertTrue(runtime.handleHeartRate(WatchIdentifier("watch"), 9, 1, 120, 1_000))
            assertTrue(locus.mutationStarted.await(15, TimeUnit.SECONDS))
            val revocation =
                async(start = kotlinx.coroutines.CoroutineStart.UNDISPATCHED) {
                    currentGeneration.incrementAndGet()
                    leases.mutateSession {
                        runtime.companionTrustLost()
                    }
                }
            assertFalse(revocation.isCompleted)

            locus.releaseMutation.countDown()
            revocation.await()

            assertEquals(1, locus.heartRateCalls)
            assertTrue(sender.calls.isEmpty())
        } finally {
            locus.releaseMutation.countDown()
            runtime.close()
        }
    }

    private class BlockingHeartRateLocus : LocusBridgeGateway {
        val mutationStarted = CountDownLatch(1)
        val releaseMutation = CountDownLatch(1)
        var heartRateCalls = 0
        private var currentHeartRate: Int? = null

        override fun readSnapshot(nowMillis: Long) =
            BridgeProtocol.Snapshot(
                state = BridgeProtocol.RecordingState.RECORDING,
                sampledAtEpochSeconds = nowMillis / 1_000,
                currentHeartRate = currentHeartRate,
            )

        override fun sendHeartRate(bpm: Int): Boolean {
            heartRateCalls++
            mutationStarted.countDown()
            check(releaseMutation.await(5, TimeUnit.SECONDS)) { "test mutation was not released" }
            currentHeartRate = bpm
            return true
        }

        override fun recordingProfiles() =
            RecordingProfilesResult.Success(emptyList<BridgeProtocol.RecordingProfile>())

        override fun execute(
            command: BridgeProtocol.Command,
            profileName: String?,
            waypointName: String?,
        ) = BridgeProtocol.Result.FAILED
    }
}
