# PR2561 attempt 2 A5 hang

Job 115287600597, runner 238DT-npu-a5-1, head 98e4da4f. A3 UT/ST/DeepSeek passed this attempt. macOS UT remains queued at observation.

Task task_20261013_201132_19834925619 acquired devices4/5/6/7 at 12:14:26Z. TestA5ThreeRunCapacity PID2077311 on device5 started12:14:40Z, remained active until the1200s parent timeout at12:34:26Z, exit124. Descendants2078075/2078146. By matching five selected nodeids against four completed jobs, the outstanding method is test_a_device_result_feeds_the_next_two_runs. This is an inference: HUNG label itself omits the method nodeid. test_run was skipped (the scheduler PASS labels child exit0), other3 methods passed.

HUNG output contains only pytest collection header, no stack dump or native failure details. No AIV/507018 signature in this log. Artifact listing still contains only network1 from attempt1. The preceding depth-two case passed on device5. This was execution timeout, not task queue timeout.

Attempted read-only SSH through existing A5Pod alias. Host key changed; strict verification blocked the connection. Asked user to verify mapping and presented ED25519 fingerprint; did not change known_hosts or disable checking. No runtime code modified, no blind rerun started.
