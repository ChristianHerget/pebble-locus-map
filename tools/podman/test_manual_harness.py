import subprocess
import unittest
from script_test_support import (
    ROOT,
    PODMAN_TEST,
    WEB_CONTAINERFILE,
)
ANDROID_FRAME = ROOT / "tools" / "podman" / "android_frame.py"

MANUAL_STAGE = ROOT / "tools" / "podman" / "manual-stage.sh"

MANUAL_DASHBOARD = ROOT / "tools" / "podman" / "manual-dashboard" / "App.tsx"

MANUAL_VITE_PATCH = ROOT / "tools" / "podman" / "manual-dashboard" / "vite-lab-api.patch"


class ManualLabHarnessTest(unittest.TestCase):
    def run_manual_parse(self, *arguments: str):
        return subprocess.run(
            ["bash", str(PODMAN_TEST), "manual", *arguments],
            capture_output=True,
            text=True,
            check=False,
        )

    def test_manual_requires_one_supported_platform_before_host_preflight(self):
        missing = self.run_manual_parse("--locus-apks", "/tmp")
        self.assertNotEqual(missing.returncode, 0)
        self.assertIn("manual requires --platform emery or --platform gabbro", missing.stderr)

        unsupported = self.run_manual_parse("--platform", "chalk", "--locus-apks", "/tmp")
        self.assertNotEqual(unsupported.returncode, 0)
        self.assertIn("manual requires --platform emery or --platform gabbro", unsupported.stderr)

        duplicate = self.run_manual_parse(
            "--platform", "emery", "--platform", "gabbro", "--locus-apks", "/tmp"
        )
        self.assertNotEqual(duplicate.returncode, 0)
        self.assertIn("--platform may be specified only once", duplicate.stderr)

    def test_manual_is_the_only_runtime_that_publishes_the_dashboard(self):
        source = PODMAN_TEST.read_text(encoding="utf-8")
        manual = source.split("manual_lab() {", 1)[1].split("\nstatic_tests() {", 1)[0]
        android = source.split("run_android_tests() {", 1)[1].split("\nrun_e2e_platform() {", 1)[0]
        e2e = source.split("run_e2e_platform() {", 1)[1].split("\ne2e_tests() {", 1)[0]

        self.assertIn('create_test_pod "$ACTIVE_POD" true', manual)
        self.assertIn("TRACKGLANCE_WEB_MODE=manual", source)
        self.assertNotIn("start_manual_web", android)
        self.assertNotIn("start_manual_web", e2e)
        self.assertIn("--publish 127.0.0.1:5173:5173", source)

    def test_manual_clones_and_protects_the_golden_state(self):
        source = PODMAN_TEST.read_text(encoding="utf-8")
        manual = source.split("manual_lab() {", 1)[1].split("\nstatic_tests() {", 1)[0]
        clone = source.split("clone_volume() {", 1)[1].split("\ngolden_marker() {", 1)[0]
        cleanup = source.split("cleanup_active() {", 1)[1].split("\ntrap cleanup_active", 1)[0]

        self.assertIn('MANUAL_GOLDEN_MARKER=$(golden_marker)', manual)
        self.assertIn('clone_volume "$GOLDEN_VOLUME" "$ACTIVE_DATA_VOLUME"', manual)
        self.assertIn('--volume "$source:/source:ro"', clone)
        self.assertIn('"$ACTIVE_DATA_VOLUME" "$ACTIVE_WATCH_VOLUME" "$ACTIVE_RUNTIME_VOLUME"', cleanup)
        self.assertIn('current_golden_marker=$(golden_marker', cleanup)

    def test_manual_web_image_is_versioned_and_proxies_only_an_internal_api(self):
        containerfile = WEB_CONTAINERFILE.read_text(encoding="utf-8")
        proxy_patch = MANUAL_VITE_PATCH.read_text(encoding="utf-8")
        frame_server = ANDROID_FRAME.read_text(encoding="utf-8")

        self.assertIn('LABEL io.trackglance.manual-lab="1"', containerfile)
        self.assertIn("git -C /opt/aemu apply --check", containerfile)
        self.assertIn("npm run build --prefix /opt/aemu/js/example", containerfile)
        self.assertIn("'/lab-api'", proxy_patch)
        self.assertIn("http://127.0.0.1:8081", proxy_patch)
        self.assertNotIn("0.0.0.0:8081", proxy_patch)
        self.assertIn('default="127.0.0.1"', frame_server)
        self.assertIn("width=540", frame_server)
        self.assertIn("getScreenshot", frame_server)
        self.assertIn("getDisplayConfigurations", frame_server)

    def test_dashboard_has_official_keyboard_mappings_and_ignores_text_editing(self):
        dashboard = MANUAL_DASHBOARD.read_text(encoding="utf-8")
        for mapping in (
            "q: 'back'", "ArrowLeft: 'back'", "w: 'up'", "ArrowUp: 'up'",
            "s: 'select'", "ArrowRight: 'select'", "x: 'down'", "ArrowDown: 'down'",
        ):
            self.assertIn(mapping, dashboard)
        self.assertIn("event.repeat", dashboard)
        self.assertIn("target?.isContentEditable", dashboard)
        self.assertIn("['INPUT', 'SELECT', 'TEXTAREA']", dashboard)
        self.assertIn("<canvas", dashboard)

    def test_manual_setup_installs_current_artifacts_and_finishes_stopped(self):
        stage = MANUAL_STAGE.read_text(encoding="utf-8")
        self.assertIn("install -r \"$bridge_apk\"", stage)
        installation = stage.split('install -r "$bridge_apk"', 1)[1]
        self.assertLess(
            installation.index("am force-stop menion.android.locus"),
            installation.index("adb_device shell am force-stop app.trackglance.bridge"),
        )
        self.assertLess(
            installation.index("am force-stop menion.android.locus"),
            installation.index("foreground_locus"),
        )
        self.assertIn("cache/trackglance.pbw", stage)
        self.assertIn("ADD_QEMU_WATCH", stage)
        self.assertNotIn("monkey -p coredevices.coreapp", stage)
        self.assertIn("complete_coreapp_onboarding", stage)
        self.assertLess(stage.rindex("foreground_locus"), stage.rindex("wait_status recording_state STOPPED"))
        self.assertIn('> "$ready_file"', stage)
