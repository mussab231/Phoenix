#pragma once

// Headless self-tests invoked from the command line (--self-test*, --checksum).
// Kept separate from main.cpp so the entry point stays small and the test
// suite can grow without cluttering startup code. Each function returns 0 on
// success and 1 on failure, printing a short verdict to stdout.
namespace SelfTest {
int runSelfTest();
int runMultiSegmentTest();
int runResumeTest();
int runQueueTest();
int runScheduleTest();
int runSleepTest();
int runSpeedLimitTest();
int runRetryTest();
int runListenTest();
int runProtoTest();
int runUrlMatchTest();
int runSettingsTest();
int runNativeHostTest();
} // namespace SelfTest

// Runs the browser native-messaging host loop (stdin/stdout JSON frames).
// Lives here because it shares the CLI dispatch table with the tests.
int runNativeHost(int argc, char** argv);
