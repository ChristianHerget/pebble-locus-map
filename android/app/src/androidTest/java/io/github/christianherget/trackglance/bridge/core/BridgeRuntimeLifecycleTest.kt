package io.github.christianherget.trackglance.bridge.core

import io.github.christianherget.trackglance.bridge.protocol.BridgeProtocol
import io.rebble.pebblekit2.common.model.WatchIdentifier
import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class BridgeRuntimeLifecycleTest {
    @Test
    fun openingAnotherWatchReplacesTheActiveLifecycle() {
        val sender = RecordingSender()
        val runtime = runtime(sender = sender)
        val watchA = WatchIdentifier("watch-a")
        val watchB = WatchIdentifier("watch-b")
        try {
            runtime.watchAppOpened(watchA)
            runtime.watchAppOpened(watchB)
            assertTrue(BridgeState.status.value.watchAppOpen)
            assertEquals(listOf(watchA), sender.calls[0].watches)
            assertEquals(listOf(watchB), sender.calls[1].watches)

            runtime.watchAppClosed(watchA)
            assertTrue(BridgeState.status.value.watchAppOpen)
            runtime.watchAppClosed(watchB)
            assertFalse(BridgeState.status.value.watchAppOpen)
        } finally {
            runtime.close()
        }
    }

    @Test
    fun replacementRequiresContextAndLateOldWatchCloseDoesNotInvalidateReplacement() = runBlocking {
        val sender = RecordingSender()
        val locus =
            FakeLocus(
                profiles = listOf(BridgeProtocol.RecordingProfile(42, "Trail run")),
                activeProfileName = "Trail run",
            )
        val runtime = runtime(sender = sender, locus = locus)
        val watchA = WatchIdentifier("watch-a")
        val watchB = WatchIdentifier("watch-b")
        try {
            runtime.watchAppOpened(watchA)
            assertTrue(sender.types().contains(BridgeProtocol.MessageType.RECORDING_CONTEXT.wire))

            sender.calls.clear()
            runtime.watchAppOpened(watchB)
            assertTrue(sender.types().contains(BridgeProtocol.MessageType.RECORDING_CONTEXT.wire))
            assertTrue(sender.calls.all { it.watches == listOf(watchB) })

            runtime.watchAppClosed(watchA)
            sender.calls.clear()
            assertTrue(runtime.refresh(listOf(watchB)))
            assertEquals(listOf(BridgeProtocol.MessageType.SNAPSHOT.wire), sender.types())
        } finally {
            runtime.close()
        }
    }

    @Test
    fun inboundMessageAfterProcessRestartRecoversTheOpenWatchLifecycle() {
        val sender = RecordingSender()
        val runtime = runtime(sender = sender)
        val watch = WatchIdentifier("watch-after-restart")
        try {
            runtime.watchObserved(watch)
            assertTrue(BridgeState.status.value.watchAppOpen)
            assertEquals(listOf(watch), sender.calls.single().watches)
        } finally {
            runtime.watchAppClosed(watch)
            runtime.close()
        }
    }
}
