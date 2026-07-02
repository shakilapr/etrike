// g++ -std=c++17 -include test_compat.h -I. -I../src -I../../shared test_physics_0_0_4.cpp ../src/physics_model.cpp -o test_physics_0_0_4 && ./test_physics_0_0_4

#include "test_compat.h"
#include <cstdio>
#include <cmath>
#include <algorithm>
#include "config.h"
#include "physics_model.h"

static int pass=0, fail=0;
#define CHECK(cond) do { if(cond){pass++;}else{fail++;fprintf(stderr,"FAIL %s:%d\n",__FILE__,__LINE__);} } while(0)
#define NEAR(a,b,tol) CHECK(std::abs((double)(a)-(double)(b))<(double)(tol))

int main(){
    printf("\n=== Physics 0.0.4: Dynamic Clamp & Following Error ===\n\n");
    using namespace rt;

    printf("-- Dynamic Angle Clamp --\n");
    {float r=compute_dynamic_limit(0);NEAR(r,40.0f,0.1f);printf("  0mm/s -> %.1f deg\n",r);}
    {float r=compute_dynamic_limit(555);NEAR(r,40.0f,0.1f);printf("  555mm/s(2km/h) -> %.1f deg\n",r);}
    {float r=compute_dynamic_limit(2000);NEAR(r,32.1f,0.5f);printf("  2000mm/s -> %.1f deg\n",r);}
    {float r=compute_dynamic_limit(5000);NEAR(r,15.7f,0.5f);printf("  5000mm/s -> %.1f deg\n",r);}
    {float r=compute_dynamic_limit(6944);NEAR(r,5.0f,0.2f);printf("  6944mm/s(25km/h) -> %.1f deg\n",r);}
    {float r=compute_dynamic_limit(8000);NEAR(r,5.0f,0.1f);printf("  8000mm/s -> %.1f deg (clamped)\n",r);}

    printf("\n-- Following Error Threshold --\n");
    {float r=compute_following_error_threshold(0);NEAR(r,10.0f,0.1f);printf("  0mm/s -> %.1f deg\n",r);}
    {float r=compute_following_error_threshold(555);NEAR(r,10.0f,0.1f);printf("  555mm/s -> %.1f deg\n",r);}
    {float r=compute_following_error_threshold(5000);NEAR(r,3.9f,0.5f);printf("  5000mm/s -> %.1f deg\n",r);}
    {float r=compute_following_error_threshold(6944);NEAR(r,2.0f,0.1f);printf("  6944mm/s -> %.1f deg\n",r);}
    {float r=compute_following_error_threshold(-2000);NEAR(r,compute_following_error_threshold(2000),0.01f);printf("  -2000mm/s == +2000mm/s: %.1f\n",r);}

    printf("\n-- Monotonicity & Boundaries --\n");
    CHECK(compute_dynamic_limit(-1000)<=40.0f && compute_dynamic_limit(-1000)>=5.0f);
    CHECK(compute_dynamic_limit(10000)>=5.0f && compute_dynamic_limit(10000)<=40.0f);
    float prev=compute_dynamic_limit(0);
    for(float s=100;s<=10000;s+=100){float cur=compute_dynamic_limit(s);CHECK(cur<=prev+0.001f);prev=cur;}
    printf("  ok monotonic, clamp [5,40]\n");

    printf("\n-- Obstacle Brake Curve --\n");
    CHECK(PhysicsModel::obstacle_to_kpa(shared::kObstacleStopMM) == shared::kObstacleMaxKpa);
    CHECK(PhysicsModel::obstacle_to_kpa(shared::kObstacleClearMM) == 0);
    CHECK(PhysicsModel::obstacle_to_kpa(0) == shared::kObstacleMaxKpa);
    CHECK(PhysicsModel::obstacle_to_kpa(10'000) == 0);
    {
        unsigned mid = (shared::kObstacleStopMM + shared::kObstacleClearMM) / 2;
        NEAR(PhysicsModel::obstacle_to_kpa(mid), shared::kObstacleMaxKpa / 2, 2);
        printf("  midpoint %umm -> %ld kPa\n", mid, long(PhysicsModel::obstacle_to_kpa(mid)));
    }

    printf("\n=== %d pass, %d fail ===\n",pass,fail);
    return fail?1:0;
}
