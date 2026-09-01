#pragma once

#ifdef OBS_PHASE2_TEST_HOOKS
#ifdef _WIN32
#define OBS_PHASE2_TEST_API __declspec(dllimport)
#else
#define OBS_PHASE2_TEST_API
#endif

extern "C" OBS_PHASE2_TEST_API void obs_phase2_test_fail_next_canvas_video_mix(void);

#undef OBS_PHASE2_TEST_API
#endif
