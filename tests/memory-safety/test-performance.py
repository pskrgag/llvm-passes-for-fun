import subprocess

from testsupport import run, subtest, run_project_executable, assert_contains
from time import time

MAX_SCALING_FACTOR = 12


def run_perf_test(n: int, m: int) -> float:
    start = time()
    run_project_executable("memory-safety/build/performance.out", args=[str(n), str(m)], stdout=subprocess.PIPE)
    end = time()
    return end - start


def main() -> None:
    with subtest("check in bounds"):
        result = run_project_executable("memory-safety/build/performance.out", args=["100", "1000", "1"],
                                        stdout=subprocess.PIPE,
                                        stderr=subprocess.PIPE,
                                        check=False)
        if result.returncode == 0:
            raise Exception("Expected program to exit with exit code != 0")
        assert_contains(result.stderr, "Illegal memory access")

    with subtest("check performance"):
        durations_1 = []
        for i in range(10):
            duration = run_perf_test(1000, 10000)
            durations_1.append(duration)

        durations_2 = []
        for i in range(10):
            duration = run_perf_test(10000, 10000)
            durations_2.append(duration)

        durations_3 = []
        for i in range(10):
            duration = run_perf_test(1000, 100000)
            durations_3.append(duration)

        duration_1 = sum(durations_1) / len(durations_1)
        duration_2 = sum(durations_2) / len(durations_2)
        duration_3 = sum(durations_3) / len(durations_3)

        scaling_factor = duration_2 / duration_1
        print(f"Scaling factor: {scaling_factor}")
        if scaling_factor > MAX_SCALING_FACTOR:
            raise Exception(f"Performance test failed: {scaling_factor} < {MAX_SCALING_FACTOR}")

        scaling_factor = duration_3 / duration_1
        print(f"Scaling factor: {scaling_factor}")
        if scaling_factor > MAX_SCALING_FACTOR:
            raise Exception(f"Performance test failed: {scaling_factor} < {MAX_SCALING_FACTOR}")


if __name__ == "__main__":
    main()
