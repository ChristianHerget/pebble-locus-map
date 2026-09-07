import os
import subprocess
import tempfile
import textwrap
import unittest
from pathlib import Path
from script_test_support import (
    PODMAN_TEST,
)



class CleanupScopeTest(unittest.TestCase):
    def test_clean_revalidates_every_pod_and_volume_prefix(self):
        with tempfile.TemporaryDirectory() as directory:
            temporary = Path(directory)
            log = temporary / "removed"
            environment = {
                **os.environ,
                "PODMAN_TEST_SCRIPT": str(PODMAN_TEST),
                "PODMAN_REMOVAL_LOG": str(log),
                "EMPTY_BUILD_ROOT": str(temporary / "absent"),
            }
            script = textwrap.dedent(
                """\
                source "$PODMAN_TEST_SCRIPT"
                BUILD_ROOT=$EMPTY_BUILD_ROOT
                podman() {
                  if [[ "$1 $2" == "pod ps" ]]; then
                    printf '%s\n' trackglance-owned backup-trackglance-old
                  elif [[ "$1 $2" == "pod rm" ]]; then
                    printf 'pod:%s\n' "$4" >> "$PODMAN_REMOVAL_LOG"
                  elif [[ "$1 $2" == "volume ls" ]]; then
                    printf '%s\n' trackglance-cache backup-trackglance-cache
                  elif [[ "$1 $2" == "volume rm" ]]; then
                    printf 'volume:%s\n' "$4" >> "$PODMAN_REMOVAL_LOG"
                  elif [[ "$1 $2" == "image exists" || "$1 $2" == "network exists" ]]; then
                    return 1
                  fi
                }
                clean
                """
            )
            result = subprocess.run(
                ["bash", "-euo", "pipefail", "-c", script],
                env=environment,
                capture_output=True,
                text=True,
                check=False,
            )
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(
                log.read_text(encoding="utf-8").splitlines(),
                ["pod:trackglance-owned", "volume:trackglance-cache"],
            )

    def test_docker_clean_uses_the_generator_to_remove_root_owned_outputs_first(self):
        with tempfile.TemporaryDirectory() as directory:
            temporary = Path(directory)
            build_root = temporary / "build" / "podman"
            build_root.mkdir(parents=True)
            (build_root / "generated").mkdir()
            (build_root / "generated" / "output").write_text("generated", encoding="utf-8")
            log = temporary / "docker-log"
            environment = {
                **os.environ,
                "PODMAN_TEST_SCRIPT": str(PODMAN_TEST),
                "TEST_PROJECT_DIR": str(temporary),
                "DOCKER_LOG": str(log),
                "ACCEPTANCE_CONTAINER_ENGINE": "docker",
            }
            script = textwrap.dedent(
                """\
                source "$PODMAN_TEST_SCRIPT"
                PROJECT_DIR=$TEST_PROJECT_DIR
                BUILD_ROOT=$PROJECT_DIR/build/podman
                docker() {
                  if [[ "$1 $2" == "container ls" || "$1 $2" == "volume ls" ]]; then
                    return 0
                  elif [[ "$1 $2" == "image inspect" ]]; then
                    [[ "$3" == "$GENERATOR_IMAGE" ]]
                  elif [[ "$1" == run ]]; then
                    printf 'run-cleaner\n' >> "$DOCKER_LOG"
                    find "$BUILD_ROOT" -mindepth 1 -delete
                  elif [[ "$1 $2" == "image rm" ]]; then
                    printf 'remove-image\n' >> "$DOCKER_LOG"
                  elif [[ "$1 $2" == "network inspect" ]]; then
                    return 1
                  fi
                }
                clean
                """
            )
            result = subprocess.run(
                ["bash", "-euo", "pipefail", "-c", script],
                env=environment,
                capture_output=True,
                text=True,
                check=False,
            )
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertFalse(build_root.exists())
            self.assertEqual(
                log.read_text(encoding="utf-8").splitlines(),
                ["run-cleaner", "remove-image"],
            )


    def run_cleanup(self, temporary, failure, host_cleanup=False):
        build_root = temporary / "build" / "podman"
        build_root.mkdir(parents=True)
        (build_root / "output").write_text("generated")
        log = temporary / "commands"
        script = r'''source "$PODMAN_TEST_SCRIPT"
            PROJECT_DIR=$TEST_PROJECT_DIR
            BUILD_ROOT=$PROJECT_DIR/build/podman
            find() {
              printf 'find %s\n' "$*" >> "$COMMAND_LOG"
              if [[ "$HOST_CLEANUP" == 1 && "$FAIL_CLEANUP" == 1 ]]; then return 23; fi
              command find "$@"
            }
            docker() {
              printf '%s\n' "$*" >> "$COMMAND_LOG"
              case "$1 $2" in
                'image inspect') [[ "$HOST_CLEANUP" != 1 && "$3" == "$ACCEPTANCE_RUNNER_IMAGE" ]] ;;
                'run --rm')
                  if [[ "$FAIL_CLEANUP" == 1 ]]; then return 23; fi
                  find "$BUILD_ROOT" -mindepth 1 -delete ;;
                'network inspect') return 1 ;;
              esac
            }
            clean
        '''
        result = subprocess.run(["bash", "-euo", "pipefail", "-c", script],
            env={**os.environ, "PODMAN_TEST_SCRIPT": str(PODMAN_TEST),
                 "TEST_PROJECT_DIR": str(temporary), "COMMAND_LOG": str(log),
                 "ACCEPTANCE_CONTAINER_ENGINE": "docker", "FAIL_CLEANUP": str(failure),
                 "HOST_CLEANUP": "1" if host_cleanup else "0"},
            capture_output=True, text=True, check=False)
        return result, log.read_text().splitlines(), build_root

    def test_docker_cleanup_uses_the_published_runner_when_generator_is_absent(self):
        with tempfile.TemporaryDirectory() as directory:
            result, commands, build_root = self.run_cleanup(Path(directory), 0)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertFalse(build_root.exists())
            run = next(command for command in commands if command.startswith("run "))
            self.assertRegex(run, r"^run --rm --volume .+/build/podman:/target ghcr.io/.+@sha256:[0-9a-f]{64} find /target -mindepth 1 -delete$")
            self.assertLess(commands.index(run), next(i for i, c in enumerate(commands) if c.startswith("image rm")))

    def test_cleanup_does_not_mask_a_failed_artifact_deletion(self):
        for host_cleanup in (False, True):
            with self.subTest(host_cleanup=host_cleanup), tempfile.TemporaryDirectory() as directory:
                result, commands, build_root = self.run_cleanup(Path(directory), 1, host_cleanup)
                self.assertEqual(result.returncode, 23, result.stderr)
                self.assertTrue((build_root / "output").exists())
                if host_cleanup:
                    self.assertIn(f"find {build_root} -depth -delete", commands)
                    self.assertFalse(any(command.startswith("run ") for command in commands))
                else:
                    self.assertTrue(any(command.startswith("image rm") for command in commands))
