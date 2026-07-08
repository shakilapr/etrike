#include <unity.h>
#include <cmath>
#include <algorithm>
#include "physics_model.h"

using namespace rt;

void setUp(void) {}
void tearDown(void) {}

void test_physics_dynamic_angle_clamp(void) {
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 40.0f, compute_dynamic_limit(0));
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 40.0f, compute_dynamic_limit(555));
    TEST_ASSERT_FLOAT_WITHIN(0.5f, 32.1f, compute_dynamic_limit(2000));
    TEST_ASSERT_FLOAT_WITHIN(0.5f, 15.7f, compute_dynamic_limit(5000));
    TEST_ASSERT_FLOAT_WITHIN(0.2f, 5.0f, compute_dynamic_limit(6944));
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 5.0f, compute_dynamic_limit(8000));
}

void test_physics_following_error_threshold(void) {
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 10.0f, compute_following_error_threshold(0));
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 10.0f, compute_following_error_threshold(555));
    TEST_ASSERT_FLOAT_WITHIN(0.5f, 3.9f, compute_following_error_threshold(5000));
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 2.0f, compute_following_error_threshold(6944));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, compute_following_error_threshold(2000), compute_following_error_threshold(-2000));
}

void test_physics_monotonicity_and_boundaries(void) {
    TEST_ASSERT_TRUE(compute_dynamic_limit(-1000) <= 40.0f && compute_dynamic_limit(-1000) >= 5.0f);
    TEST_ASSERT_TRUE(compute_dynamic_limit(10000) >= 5.0f && compute_dynamic_limit(10000) <= 40.0f);
    
    float prev = compute_dynamic_limit(0);
    for(float s = 100; s <= 10000; s += 100) {
        float cur = compute_dynamic_limit(s);
        TEST_ASSERT_TRUE(cur <= prev + 0.001f);
        prev = cur;
    }
}

void test_physics_obstacle_brake_curve(void) {
    TEST_ASSERT_EQUAL(shared::kObstacleMaxKpa, PhysicsModel::obstacle_to_kpa(shared::kObstacleStopMM));
    TEST_ASSERT_EQUAL(0, PhysicsModel::obstacle_to_kpa(shared::kObstacleClearMM));
    TEST_ASSERT_EQUAL(shared::kObstacleMaxKpa, PhysicsModel::obstacle_to_kpa(0));
    TEST_ASSERT_EQUAL(0, PhysicsModel::obstacle_to_kpa(10000));
    
    unsigned mid = (shared::kObstacleStopMM + shared::kObstacleClearMM) / 2;
    // Unity doesn't have an integer range assert directly, but we can check absolute difference
    long expected_mid_kpa = shared::kObstacleMaxKpa / 2;
    long actual_mid_kpa = PhysicsModel::obstacle_to_kpa(mid);
    TEST_ASSERT_TRUE(std::abs(actual_mid_kpa - expected_mid_kpa) <= 2);
}

extern "C" void app_main() {
    UNITY_BEGIN();
    RUN_TEST(test_physics_dynamic_angle_clamp);
    RUN_TEST(test_physics_following_error_threshold);
    RUN_TEST(test_physics_monotonicity_and_boundaries);
    RUN_TEST(test_physics_obstacle_brake_curve);
    UNITY_END();
}

#if defined(HOST_BUILD) || defined(NATIVE_TEST_ENV) || !defined(ESP_PLATFORM)
int main(int argc, char **argv) {
    app_main();
    return 0;
}
#endif
