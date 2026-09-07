import os
import subprocess
import tempfile
import textwrap
import unittest
from script_test_support import (
    ROOT,
    PODMAN_TEST,
    WEB_CONTAINERFILE,
    EMULATOR_ENTRYPOINT,
)
GENERATOR_CONTAINERFILE = ROOT / "tools" / "podman" / "Containerfile.generator"

EMULATOR_PATH_PATCH = ROOT / "tools" / "podman" / "patches" / "android-emulator-dest-path.patch"

RELEASE_MANIFEST_POLICY = ROOT / "tools" / "podman" / "check_release_manifest.py"


class StaticPreflightTest(unittest.TestCase):
    def test_large_emulator_downloads_resume_and_retry_transport_failures(self):
        source = PODMAN_TEST.read_text(encoding="utf-8")
        generator = source.split("build_emulator_image() {", 1)[1].split(
            "\n}", 1
        )[0]

        self.assertEqual(generator.count("--retry-all-errors"), 2)
        self.assertEqual(generator.count("--continue-at -"), 2)
        system_checksum = 'echo "$ANDROID_SYSTEM_IMAGE_SHA256  $system_zip"'
        system_download = 'curl --fail --location'
        self.assertLess(
            generator.index(system_checksum),
            generator.index(system_download, generator.index(system_checksum)),
        )

    def test_emulator_handles_the_fallback_discovery_path(self):
        entrypoint = EMULATOR_ENTRYPOINT.read_text(encoding="utf-8")
        self.assertIn("/root/.android/avd/running", entrypoint)
        self.assertIn('path=$avd_home/MediumPhone.avd', entrypoint)
        self.assertIn("-no-metrics", entrypoint)
        self.assertNotIn("-no-snapshot-save", entrypoint)

    def test_pinned_emulator_generator_applies_its_dest_path_compatibility_patch(self):
        containerfile = GENERATOR_CONTAINERFILE.read_text(encoding="utf-8")
        patch = EMULATOR_PATH_PATCH.read_text(encoding="utf-8")
        self.assertIn("git -C /opt/aemu apply --check", containerfile)
        self.assertIn('Path(args.dest) / "sys_img"', patch)
        self.assertIn("Path(args.dest)", patch)

    def test_webrtc_image_generates_the_javascript_protocol_module(self):
        containerfile = WEB_CONTAINERFILE.read_text(encoding="utf-8")
        self.assertIn("protobuf-compiler", containerfile)
        self.assertIn("libprotobuf-dev", containerfile)
        self.assertIn("--js_out=import_style=commonjs,binary:/opt/aemu/js/src/proto", containerfile)
        self.assertIn("emulator_controller.proto", containerfile)
        self.assertIn("ws://127.0.0.1:8080", containerfile)
        self.assertIn("http://127.0.0.1:8080", containerfile)

    def test_static_path_keeps_development_dependencies_in_a_container(self):
        source = PODMAN_TEST.read_text(encoding="utf-8")
        doctor_body = source.split("doctor_static() {", 1)[1].split("\n}", 1)[0]
        static_body = source.split("static_tests() {", 1)[1].split("\n}", 1)[0]
        self.assertIn("select_static_engine", doctor_body)
        self.assertNotIn("command -v java", doctor_body)
        self.assertNotIn("command -v pebble", doctor_body)
        self.assertIn("docker", source.split("select_static_engine() {", 1)[1].split("\n}", 1)[0])
        self.assertLess(
            static_body.index("npm ci --prefix watchapp"),
            static_body.index("npm test --prefix watchapp"),
        )
        self.assertIn("actionlint .github/workflows/*.yml", static_body)

    def test_sanitized_diagnostics_allowlist_excludes_installable_binaries(self):
        source = PODMAN_TEST.read_text(encoding="utf-8")
        diagnostics = source.split("stage_acceptance_diagnostics() {", 1)[1].split(
            "\nreport_acceptance_resources() {", 1
        )[0]
        for allowed in ("*.log", "*.txt", "*.jsonl", "*.xml", "*.png", "*.ppm"):
            self.assertIn(allowed, diagnostics)
        for forbidden in ("*.apk", "*.pbw", "*.p12", "*.keystore"):
            self.assertIn(forbidden, diagnostics)

    def test_diagnostics_staging_copies_only_the_allowlist(self):
        with tempfile.TemporaryDirectory() as directory:
            environment = {**os.environ, "DIAGNOSTICS_TEST_ROOT": directory}
            script = textwrap.dedent(
                """\
                source "$PODMAN_TEST"
                PROJECT_DIR=$DIAGNOSTICS_TEST_ROOT
                BUILD_ROOT=$PROJECT_DIR/build/podman
                ACCEPTANCE_DIAGNOSTICS_ROOT=$PROJECT_DIR/build/acceptance-diagnostics
                CHECK_LOG_DIR=$PROJECT_DIR/build/check-logs/test
                mkdir -p "$CHECK_LOG_DIR"
                printf '%s\n' "$BUILD_ROOT/run" > "$CHECK_LOG_DIR/runs.txt"
                mkdir -p "$BUILD_ROOT/run/results"
                printf 'result\n' > "$BUILD_ROOT/run/results/test.xml"
                printf 'log\n' > "$BUILD_ROOT/run/runtime.log"
                printf 'private\n' > "$BUILD_ROOT/run/locus.apk"
                printf 'watch\n' > "$BUILD_ROOT/run/watch.pbw"
                mkdir -p "$BUILD_ROOT/stale" "$BUILD_ROOT/run/fixtures" "$BUILD_ROOT/run/caches"
                printf 'stale\n' > "$BUILD_ROOT/stale/runtime.log"
                printf 'private\n' > "$BUILD_ROOT/run/fixtures/private.xml"
                printf 'cache\n' > "$BUILD_ROOT/run/caches/state.txt"
                printf 'key\n' > "$BUILD_ROOT/run/key.p12"
                ln -s "$BUILD_ROOT/run/key.p12" "$BUILD_ROOT/run/key.txt"
                stage_acceptance_diagnostics
                test -f "$CHECK_LOG_DIR/diagnostics/run/results/test.xml"
                test -f "$CHECK_LOG_DIR/diagnostics/run/runtime.log"
                test ! -e "$CHECK_LOG_DIR/diagnostics/run/locus.apk"
                test ! -e "$CHECK_LOG_DIR/diagnostics/run/watch.pbw"
                test ! -e "$CHECK_LOG_DIR/diagnostics/stale"
                test ! -e "$CHECK_LOG_DIR/diagnostics/run/fixtures"
                test ! -e "$CHECK_LOG_DIR/diagnostics/run/caches"
                test ! -e "$CHECK_LOG_DIR/diagnostics/run/key.p12"
                test ! -e "$CHECK_LOG_DIR/diagnostics/run/key.txt"
                """
            )
            result = subprocess.run(
                ["bash", "-euo", "pipefail", "-c", script],
                env={**environment, "PODMAN_TEST": str(PODMAN_TEST)},
                capture_output=True,
                text=True,
                check=False,
            )
            self.assertEqual(result.returncode, 0, result.stderr)

    def test_doctor_requires_an_explicit_scope(self):
        result = subprocess.run(
            ["bash", str(PODMAN_TEST), "doctor"],
            capture_output=True,
            text=True,
            check=False,
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("doctor requires static or acceptance", result.stderr)

    def test_image_refresh_is_explicit(self):
        source = PODMAN_TEST.read_text(encoding="utf-8")
        self.assertIn("PULL_POLICY=missing", source)
        self.assertIn("PULL_POLICY=always", source)
        self.assertNotIn("podman build --pull=always", source)

    def test_release_manifest_policy_reads_the_compiled_apk_without_pipefail(self):
        source = PODMAN_TEST.read_text(encoding="utf-8")
        release_body = source.split("release_check() {", 1)[1].split("\n}", 1)[0]
        self.assertIn('aapt2 dump badging "$release_apk" > "$badging_file"', release_body)
        self.assertIn(
            'aapt2 dump xmltree --file AndroidManifest.xml "$release_apk" > "$manifest_file"',
            release_body,
        )
        self.assertIn("python3 tools/podman/check_release_manifest.py", release_body)
        self.assertIn("--debug-manifest android/app/src/debug/AndroidManifest.xml", release_body)
        self.assertIn('--target-sdk "$expected_target"', release_body)
        self.assertIn('rm -f "$badging_file" "$manifest_file"', release_body)
        self.assertNotIn('aapt2 dump badging "$release_apk" |', release_body)
        self.assertTrue(RELEASE_MANIFEST_POLICY.is_file())
