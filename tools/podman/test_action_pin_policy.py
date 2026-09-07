import re
import unittest
from script_test_support import (
    PUBLISH_RELEASE_WORKFLOW,
    CODEQL_WORKFLOW,
    ACTION_MAJORS,
    checked_action_pins,
    checked_codeql_pins,
)



class ActionPinPolicyTest(unittest.TestCase):
    def test_actual_deploy_pages_5_0_1_update_is_accepted(self):
        source = PUBLISH_RELEASE_WORKFLOW.read_text(encoding="utf-8")
        pin = "368f82528645a54fb793d4d04e342629a3f51346"
        updated, count = re.subn(
            r"actions/deploy-pages@[^\n]+",
            f"actions/deploy-pages@{pin} # v5.0.1",
            source,
        )
        self.assertEqual(count, 1)
        self.assertEqual(checked_action_pins(updated, "actions/deploy-pages"), [(pin, "v5.0.1")])

    def test_each_action_accepts_patch_and_minor_updates_and_both_step_layouts(self):
        for action, major in ACTION_MAJORS.items():
            for version in (f"v{major}.0.2", f"v{major}.42.0"):
                with self.subTest(action=action, version=version):
                    pin = f"{action}@{'aB' * 20} # {version}"
                    source = f"      - uses: {pin}\n      - name: Example\n        uses: {pin}\n"
                    self.assertEqual(
                        checked_action_pins(source, action),
                        [("aB" * 20, version)] * 2,
                    )

    def test_each_action_rejects_every_malformed_occurrence(self):
        sha = "a" * 40
        for action, major in ACTION_MAJORS.items():
            invalid_pins = (
                f"v{major}.0.0 # v{major}.0.0",
                f"{'a' * 7} # v{major}.0.0",
                f"{'a' * 39} # v{major}.0.0",
                f"{'a' * 41} # v{major}.0.0",
                f"{'g' * 40} # v{major}.0.0",
                sha,
                f"{sha} # v{major - 1}.0.0",
                f"{sha} # v{major + 1}.0.0",
                f"{sha} # v{major}",
                f"{sha} # v{major}.0.0-rc.1",
                f"{sha} # v{major}.01.0",
                f"{sha} # v{major}.0.0 trailing text",
                "",
            )
            valid = f"- uses: {action}@{sha} # v{major}.0.0\n"
            for pin in invalid_pins:
                for prefix in ("", valid):
                    with self.subTest(action=action, pin=pin, duplicate=bool(prefix)):
                        with self.assertRaisesRegex(AssertionError, "Invalid pin"):
                            checked_action_pins(f"{prefix}- uses: {action}@{pin}\n", action)
            with self.subTest(action=action, missing_ref=True):
                with self.assertRaisesRegex(AssertionError, "Invalid pin"):
                    checked_action_pins(f"{valid}- uses: {action}\n", action)

    def test_missing_actions_cannot_be_satisfied_by_comments_strings_or_other_actions(self):
        for action, major in ACTION_MAJORS.items():
            pin = f"{action}@{'a' * 40} # v{major}.0.0"
            sources = (
                "",
                f"# - uses: {pin}\n",
                f"- name: uses: {pin}\n",
                f"- uses: other/{pin}\n",
                f"- run: |\n    uses: {pin}\n",
                f"- name: Example\n  run: >-\n    uses: {pin}\n",
            )
            for source in sources:
                with self.subTest(action=action, source=source):
                    with self.assertRaisesRegex(AssertionError, "Missing required action"):
                        checked_action_pins(source, action)

    def test_active_declaration_after_block_scalar_is_checked(self):
        source = (
            "- run: |\n    uses: actions/deploy-pages@v5\n"
            f"- uses: actions/deploy-pages@{'a' * 40} # v5.1.0\n"
        )
        self.assertEqual(checked_action_pins(source, "actions/deploy-pages"), [("a" * 40, "v5.1.0")])

    def test_codeql_accepts_coordinated_updates(self):
        source = CODEQL_WORKFLOW.read_text(encoding="utf-8")
        updated, count = re.subn(
            r"(github/codeql-action/(?:init|analyze))@[^\n]+",
            rf"\1@{'b' * 40} # v4.42.0",
            source,
        )
        self.assertEqual(count, 4)
        checked_codeql_pins(updated)

    def test_codeql_rejects_missing_extra_or_inconsistent_steps(self):
        init = f"- uses: github/codeql-action/init@{'a' * 40} # v4.42.0\n"
        analyze = init.replace("/init@", "/analyze@")
        valid = init * 2 + analyze * 2
        invalid_sources = (
            init + analyze * 2,
            init * 2 + analyze,
            init * 3 + analyze,
            valid + init,
            valid + init.replace("/init@", "/upload-sarif@"),
            valid.replace("a" * 40, "b" * 40, 1),
            valid.replace("v4.42.0", "v4.42.1", 1),
            valid.replace("a" * 40, "v4", 1),
        )
        for source in invalid_sources:
            with self.subTest(source=source):
                with self.assertRaises(AssertionError):
                    checked_codeql_pins(source)
