#!/usr/bin/env python3
"""Build, test, install and package the CLI framework against a pinned Core SDK.

CLI owns no tools of its own; it publishes the OpenMS::CLI target that every
console product links, so this driver checks the exported package rather than a
tool manifest.
"""

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile
import time


def archive_install(prefix: Path, output: Path, name: str) -> Path:
    """Create a checksummed archive while preserving library symlinks."""
    output.mkdir(parents=True, exist_ok=True)
    archive = output / f"{name}.tar.gz"
    with tarfile.open(archive, "w:gz") as stream:
        stream.add(prefix, arcname=name)
    digest = hashlib.sha256(archive.read_bytes()).hexdigest()
    archive.with_suffix(".gz.sha256").write_text(
        f"{digest}  {archive.name}\n", encoding="utf-8")
    return archive


def core_prefix(extracted: Path) -> Path:
    """Find the single Core SDK root extracted by the workflow."""
    candidates = [path for path in extracted.iterdir()
                  if path.is_dir() and (path / "source-revision.txt").is_file()]
    if len(candidates) != 1:
        raise ValueError(f"Expected one extracted Core SDK, found {len(candidates)}")
    return candidates[0]


def check_install(prefix: Path) -> None:
    """Fail unless the CMake package other repositories consume was installed."""
    configs = sorted(prefix.glob("lib/cmake/OpenMSCLI/*Config.cmake"))
    if not configs:
        raise ValueError("OpenMSCLI CMake package configuration was not installed")
    headers = sorted(prefix.glob("include/OpenMS/APPLICATIONS/TOPPBase.h"))
    if not headers:
        raise ValueError("TOPPBase.h was not installed")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--platform", required=True)
    parser.add_argument("--core-dir", required=True, type=Path)
    parser.add_argument("--work-dir", required=True, type=Path)
    parser.add_argument("--jobs", type=int, default=2)
    args = parser.parse_args()
    # Parallel launches of freshly built binaries stall ~25 s on the Mac Studio runner (in
    # syspolicyd); serial launches do not, so its workflow asks for serial tests. Builds stay parallel.
    test_jobs = "1" if os.environ.get("OPENMS4_SERIAL_TESTS") == "1" else str(args.jobs)
    source = Path(__file__).resolve().parents[2]
    work = args.work_dir.resolve()
    if args.jobs < 1 or (work.exists() and any(work.iterdir())):
        parser.error("--jobs must be positive and --work-dir must be empty")
    results = work / "results"
    results.mkdir(parents=True)
    core = core_prefix(args.core_dir.resolve())
    build, install = work / "cli-build", work / "cli"
    dependencies = Path(os.environ["CONDA_PREFIX"]).resolve()
    windows = sys.platform == "win32"
    dependency_prefix = dependencies / "Library" if windows else dependencies
    generator = "Visual Studio 17 2022" if windows else "Ninja"
    configuration = "Release"
    env = os.environ.copy()
    env["PATH"] = os.pathsep.join(
        [str(dependency_prefix / "bin"), str(core / "bin")]) + os.pathsep + env["PATH"]
    library_dirs = os.pathsep.join([str(dependency_prefix / "lib"), str(core / "lib")])
    if sys.platform == "linux":
        env["LD_LIBRARY_PATH"] = library_dirs
    elif sys.platform == "darwin":
        env["DYLD_FALLBACK_LIBRARY_PATH"] = library_dirs
    commands = []

    def run(name: str, command: list[str]) -> None:
        started = time.monotonic()
        print(f"\n--- {name} ---", flush=True)
        log = results / f"{name}.log"
        with log.open("w", encoding="utf-8") as stream:
            process = subprocess.Popen(command, cwd=source, env=env, text=True,
                                       encoding="utf-8", errors="replace",
                                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
            for line in process.stdout:
                stream.write(line)
                print(line, end="", flush=True)
            code = process.wait()
        commands.append({"name": name, "command": command, "returncode": code,
                         "elapsed_seconds": round(time.monotonic() - started, 3)})
        (results / "commands.json").write_text(
            json.dumps(commands, indent=2) + "\n", encoding="utf-8")
        if code:
            raise subprocess.CalledProcessError(code, command)

    common = ["-G", generator, f"-DCMAKE_BUILD_TYPE={configuration}",
              "-DOPENMS4_REQUIRE_CLEAN_SOURCE=ON"]
    if windows:
        common += ["-A", "x64", "-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL"]
    elif sys.platform == "darwin":
        common += [f"-DOpenMP_ROOT={dependency_prefix.as_posix()}",
                   f"-DCURL_ROOT={dependency_prefix.as_posix()}",
                   "-DCMAKE_FIND_FRAMEWORK=LAST"]
    run("driver-tests", [sys.executable, "-m", "unittest", "discover", "-s", "tools/ci", "-v"])
    run("configure", ["cmake", "-S", str(source), "-B", str(build),
                      f"-DCMAKE_INSTALL_PREFIX={install.as_posix()}",
                      f"-DCMAKE_PREFIX_PATH={core.as_posix()};{dependency_prefix.as_posix()}",
                      "-DOPENMS4_WARNINGS_AS_ERRORS=ON", *common])
    run("build", ["cmake", "--build", str(build), "--config", configuration,
                  "--parallel", str(args.jobs)])
    run("test", ["ctest", "--test-dir", str(build), "-C", configuration,
                 "--output-on-failure", "--no-tests=error", "--parallel", test_jobs])
    run("install", ["cmake", "--install", str(build), "--config", configuration])
    check_install(install)
    revision = subprocess.check_output(
        ["git", "rev-parse", "HEAD"], cwd=source, text=True).strip()
    shutil.copyfile(source / "dependencies.lock.json", install / "dependencies.lock.json")
    (install / "source-revision.txt").write_text(revision + "\n", encoding="utf-8")
    archive_install(install, work / "dist",
                    f"OpenMS4-cli-{args.platform}-Release-{revision[:12]}")


if __name__ == "__main__":
    main()
