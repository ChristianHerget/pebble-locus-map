package io.github.christianherget.trackglance.bridge.core

import io.github.christianherget.trackglance.bridge.pebble.PebbleDictionarySender
import io.github.christianherget.trackglance.bridge.pebble.PebbleMessages
import io.github.christianherget.trackglance.bridge.pebble.TrustAdmission
import io.github.christianherget.trackglance.bridge.protocol.BridgeProtocol
import io.rebble.pebblekit2.common.model.PebbleDictionary
import io.rebble.pebblekit2.common.model.TransmissionResult
import io.rebble.pebblekit2.common.model.WatchIdentifier
import java.util.concurrent.CopyOnWriteArrayList
import kotlinx.coroutines.coroutineScope
import kotlinx.coroutines.launch
import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class BridgeRuntimeProfilesContextTest {
    @Test
    fun profileTransfersForTheActiveWatchAreSerialized() = runBlocking {
        val sender = RecordingSender(yieldDuringSend = true)
        val locus =
            FakeLocus(profiles = listOf(BridgeProtocol.RecordingProfile(1, "x".repeat(200))))
        val runtime = runtime(sender = sender, locus = locus)
        val watch = WatchIdentifier("watch")
        try {
            coroutineScope {
                launch { assertTrue(runtime.sendRecordingProfiles(watch)) }
                launch { assertTrue(runtime.sendRecordingProfiles(watch)) }
            }

            val transferIds =
                sender.calls.map {
                    PebbleMessages.signed32(it.dictionary, BridgeProtocol.Key.TRANSFER_ID)
                }
            assertEquals(2, transferIds.distinct().size)
            assertEquals(
                2,
                transferIds.zipWithNext().count { (first, second) -> first != second } + 1,
            )
            assertTrue(sender.calls.all { it.watches == listOf(watch) })
        } finally {
            runtime.close()
        }
    }

    @Test
    fun unresolvedActiveProfileRefreshesCatalogBeforeSendingContext() = runBlocking {
        val sender = RecordingSender()
        val locus =
            FakeLocus(
                profiles = listOf(BridgeProtocol.RecordingProfile(42, "Trail run")),
                activeProfileName = "Trail run",
            )
        val runtime = runtime(sender, locus)
        try {
            assertTrue(runtime.refresh(listOf(WatchIdentifier("watch"))))
            assertEquals(1, locus.profileQueries)
            assertEquals(
                listOf(
                    BridgeProtocol.MessageType.SNAPSHOT.wire,
                    BridgeProtocol.MessageType.PROFILE_LIST_CHUNK.wire,
                    BridgeProtocol.MessageType.RECORDING_CONTEXT.wire,
                ),
                sender.calls.map {
                    PebbleMessages.signed32(it.dictionary, BridgeProtocol.Key.MESSAGE_TYPE)
                },
            )
            assertEquals(
                "42",
                PebbleMessages.string(
                    sender.calls.last().dictionary,
                    BridgeProtocol.Key.LOCUS_PROFILE_ID,
                ),
            )

            sender.calls.clear()
            assertTrue(runtime.refresh(listOf(WatchIdentifier("watch"))))
            assertEquals(1, locus.profileQueries)
            assertEquals(1, sender.calls.size)
            assertEquals(
                BridgeProtocol.MessageType.SNAPSHOT.wire,
                PebbleMessages.signed32(
                    sender.calls.single().dictionary,
                    BridgeProtocol.Key.MESSAGE_TYPE,
                ),
            )

            sender.calls.clear()
            assertTrue(runtime.recoverSnapshot(WatchIdentifier("watch"), TEST_ADMISSION))
            assertEquals(
                listOf(
                    BridgeProtocol.MessageType.SNAPSHOT.wire,
                    BridgeProtocol.MessageType.RECORDING_CONTEXT.wire,
                ),
                sender.calls.map {
                    PebbleMessages.signed32(it.dictionary, BridgeProtocol.Key.MESSAGE_TYPE)
                },
            )
        } finally {
            runtime.close()
        }
    }

    @Test
    fun failedContextDeliveryRemainsPendingAfterSnapshotSuccess() = runBlocking {
        val sender = FailFirstContextSender()
        val locus =
            FakeLocus(
                profiles = listOf(BridgeProtocol.RecordingProfile(42, "Trail run")),
                activeProfileName = "Trail run",
            )
        val runtime = runtime(sender, locus)
        try {
            assertFalse(runtime.refresh(listOf(WatchIdentifier("watch"))))
            assertEquals(
                listOf(
                    BridgeProtocol.MessageType.SNAPSHOT.wire,
                    BridgeProtocol.MessageType.PROFILE_LIST_CHUNK.wire,
                    BridgeProtocol.MessageType.RECORDING_CONTEXT.wire,
                ),
                sender.types,
            )

            sender.types.clear()
            assertTrue(runtime.refresh(listOf(WatchIdentifier("watch"))))
            assertEquals(
                listOf(
                    BridgeProtocol.MessageType.SNAPSHOT.wire,
                    BridgeProtocol.MessageType.RECORDING_CONTEXT.wire,
                ),
                sender.types,
            )
        } finally {
            runtime.close()
        }
    }

    @Test
    fun pauseResumeAndNameOnlyChangesRemainSnapshotOnlyButProfileIdChangeResendsContext() =
        runBlocking {
            val sender = RecordingSender()
            val locus =
                FakeLocus(
                    profiles =
                        listOf(
                            BridgeProtocol.RecordingProfile(42, "Trail run"),
                            BridgeProtocol.RecordingProfile(43, "Road run"),
                        ),
                    activeProfileName = "Trail run",
                )
            val runtime = runtime(sender, locus)
            try {
                assertTrue(runtime.refresh(listOf(WatchIdentifier("watch"))))

                sender.calls.clear()
                locus.state = BridgeProtocol.RecordingState.PAUSED
                assertTrue(runtime.refresh(listOf(WatchIdentifier("watch"))))
                assertEquals(listOf(BridgeProtocol.MessageType.SNAPSHOT.wire), sender.types())

                sender.calls.clear()
                locus.activeProfileName = "Trail renamed"
                locus.profiles =
                    listOf(
                        BridgeProtocol.RecordingProfile(42, "Trail renamed"),
                        BridgeProtocol.RecordingProfile(43, "Road run"),
                    )
                assertTrue(runtime.refresh(listOf(WatchIdentifier("watch"))))
                assertEquals(
                    listOf(
                        BridgeProtocol.MessageType.SNAPSHOT.wire,
                        BridgeProtocol.MessageType.PROFILE_LIST_CHUNK.wire,
                    ),
                    sender.types(),
                )

                sender.calls.clear()
                locus.activeProfileName = "Road run"
                assertTrue(runtime.refresh(listOf(WatchIdentifier("watch"))))
                assertEquals(
                    listOf(
                        BridgeProtocol.MessageType.SNAPSHOT.wire,
                        BridgeProtocol.MessageType.RECORDING_CONTEXT.wire,
                    ),
                    sender.types(),
                )
            } finally {
                runtime.close()
            }
        }

    @Test
    fun stoppedSnapshotInvalidatesContextBeforeSnapshotDeliveryFailure() = runBlocking {
        val sender = FailStoppedSnapshotSender()
        val locus =
            FakeLocus(
                profiles = listOf(BridgeProtocol.RecordingProfile(42, "Trail run")),
                activeProfileName = "Trail run",
            )
        val runtime = runtime(sender, locus)
        try {
            assertTrue(runtime.refresh(listOf(WatchIdentifier("watch"))))
            sender.types.clear()

            locus.state = BridgeProtocol.RecordingState.STOPPED
            sender.failNextSnapshot = true
            assertFalse(runtime.refresh(listOf(WatchIdentifier("watch"))))

            locus.state = BridgeProtocol.RecordingState.RECORDING
            sender.types.clear()
            assertTrue(runtime.refresh(listOf(WatchIdentifier("watch"))))
            assertEquals(
                listOf(
                    BridgeProtocol.MessageType.SNAPSHOT.wire,
                    BridgeProtocol.MessageType.RECORDING_CONTEXT.wire,
                ),
                sender.types,
            )
        } finally {
            runtime.close()
        }
    }

    @Test
    fun failedOrInvalidProfileQueriesNeverSendAnAuthoritativeEmptyTransfer() = runBlocking {
        val oversized =
            (0 until 40).map { index ->
                BridgeProtocol.RecordingProfile(
                    (index + 1).toLong(),
                    "profile-$index-${"x".repeat(240)}",
                )
            }
        val failures =
            listOf(
                FakeLocus(profileFailure = "Locus is unavailable"),
                FakeLocus(throwProfileQuery = true),
                FakeLocus(profiles = listOf(BridgeProtocol.RecordingProfile(1, "broken\nname"))),
                FakeLocus(
                    profiles =
                        listOf(
                            BridgeProtocol.RecordingProfile(1, "Hiking"),
                            BridgeProtocol.RecordingProfile(1, "Renamed"),
                        )
                ),
                FakeLocus(profiles = oversized),
            )

        failures.forEachIndexed { index, locus ->
            val sender = RecordingSender()
            val runtime = runtime(sender = sender, locus = locus)
            try {
                assertFalse(runtime.sendRecordingProfiles(WatchIdentifier("watch-$index")))
                assertTrue(sender.calls.isEmpty())
            } finally {
                runtime.close()
            }
        }
    }

    @Test
    fun successfulEmptyProfileQuerySendsTheAuthoritativeEmptyResult() = runBlocking {
        val sender = RecordingSender()
        val runtime = runtime(sender, FakeLocus(profiles = emptyList()))
        try {
            assertTrue(runtime.sendRecordingProfiles(WatchIdentifier("watch")))
            assertEquals(1, sender.calls.size)
            assertEquals(
                BridgeProtocol.Result.FAILED.wire,
                PebbleMessages.signed32(
                    sender.calls.single().dictionary,
                    BridgeProtocol.Key.RESULT,
                ),
            )
            assertEquals(
                "",
                PebbleMessages.string(
                    sender.calls.single().dictionary,
                    BridgeProtocol.Key.CHUNK_DATA,
                ),
            )
        } finally {
            runtime.close()
        }
    }

    @Test
    fun rejectedProfileQueryDoesNotConsumeATransferIdentifier() = runBlocking {
        val sender = RecordingSender()
        val locus =
            FakeLocus(
                profiles =
                    listOf(
                        BridgeProtocol.RecordingProfile(1, "Hiking"),
                        BridgeProtocol.RecordingProfile(1, "Renamed"),
                    )
            )
        val runtime = runtime(sender, locus)
        try {
            assertFalse(runtime.sendRecordingProfiles(WatchIdentifier("watch")))
            locus.profiles = listOf(BridgeProtocol.RecordingProfile(1, "Hiking"))
            assertTrue(runtime.sendRecordingProfiles(WatchIdentifier("watch")))
            assertEquals(
                0,
                PebbleMessages.signed32(
                    sender.calls.single().dictionary,
                    BridgeProtocol.Key.TRANSFER_ID,
                ),
            )
        } finally {
            runtime.close()
        }
    }

    private class FailFirstContextSender : PebbleDictionarySender {
        val types = CopyOnWriteArrayList<Int>()
        private var failed = false

        override suspend fun send(
            dictionary: PebbleDictionary,
            watch: WatchIdentifier,
            admission: TrustAdmission,
        ): TransmissionResult? {
            val type =
                requireNotNull(PebbleMessages.signed32(dictionary, BridgeProtocol.Key.MESSAGE_TYPE))
            types += type
            if (type == BridgeProtocol.MessageType.RECORDING_CONTEXT.wire && !failed) {
                failed = true
                return null
            }
            return TransmissionResult.Success
        }

        override fun close() = Unit
    }

    private class FailStoppedSnapshotSender : PebbleDictionarySender {
        val types = CopyOnWriteArrayList<Int>()
        var failNextSnapshot = false

        override suspend fun send(
            dictionary: PebbleDictionary,
            watch: WatchIdentifier,
            admission: TrustAdmission,
        ): TransmissionResult? {
            val type =
                requireNotNull(PebbleMessages.signed32(dictionary, BridgeProtocol.Key.MESSAGE_TYPE))
            types += type
            if (type == BridgeProtocol.MessageType.SNAPSHOT.wire && failNextSnapshot) {
                failNextSnapshot = false
                return null
            }
            return TransmissionResult.Success
        }

        override fun close() = Unit
    }
}
