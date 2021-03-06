#include "test_pch.h"
#include "actual_evapotranspiration_test.h"
#include "core/actual_evapotranspiration.h"
#include "core/utctime_utilities.h"

namespace shyfttest {
    const double EPS = 1.0e-8;
}

using namespace shyft::core;
using namespace shyft::core::actual_evapotranspiration;
void actual_evapotranspiration_test::test_water() {
    const double sca = 0.0;
    const double pot_evap = 5.0; // [mm/h]
    const double scale_factor = 1.0;
    const utctime dt = deltahours(1);
    double act_evap;
	act_evap = calculate_step(0.0, pot_evap, scale_factor, sca, dt);
    TS_ASSERT_DELTA(act_evap, 0.0, shyfttest::EPS);

	act_evap = calculate_step(1.0e8, pot_evap, scale_factor, sca, dt);
    TS_ASSERT_DELTA(act_evap, pot_evap, shyfttest::EPS);
}

void actual_evapotranspiration_test::test_snow() {
    const double water = 5.0;
    const double pot_evap = 5.0; // [mm/h]
    const double scale_factor = 1.0;
    const utctime dt = deltahours(1);
	double act_evap_no_snow = calculate_step(water, pot_evap, scale_factor, 0.0, dt);
	double act_evap_some_snow = calculate_step(water, pot_evap, scale_factor, 0.1, dt);

    TS_ASSERT(act_evap_no_snow > act_evap_some_snow );
}

void actual_evapotranspiration_test::test_scale_factor() {
    const double water = 5.0;
    const double sca = 0.5;
    const double pot_evap = 5.0; // [mm/h]
    const utctime dt = deltahours(1);
	double act_evap_small_scale = calculate_step(water, pot_evap, 0.5, sca, dt);
	double act_evap_large_scale = calculate_step(water, pot_evap, 1.5, sca, dt);

    TS_ASSERT(act_evap_small_scale > act_evap_large_scale);

}
