#include "test_pch.h"
#include "pt_gs_k_test.h"
#include "core/pt_gs_k.h"
#include "mocks.h"
#include "core/timeseries.h"
#include "core/utctime_utilities.h"

// Some typedefs for clarity
using namespace shyft::core;
using namespace shyft::timeseries;

using namespace shyft::core::pt_gs_k;
using namespace shyfttest;
using namespace shyfttest::mock;

namespace pt = shyft::core::priestley_taylor;
namespace gs = shyft::core::gamma_snow;
namespace kr = shyft::core::kirchner;
namespace ae = shyft::core::actual_evapotranspiration;
namespace pc = shyft::core::precipitation_correction;

typedef TSPointTarget<point_timeaxis> catchment_t;

void pt_gs_k_test::test_call_stack()
{
    xpts_t temp;
    xpts_t prec;
    xpts_t rel_hum;
    xpts_t wind_speed;
    xpts_t radiation;

    calendar cal;
    utctime t0 = cal.time(YMDhms(2014, 8, 1, 0, 0, 0));
    size_t n_ts_points = 3;
    utctimespan dt=deltahours(24);
    utctime t1 = t0 + n_ts_points*dt;
    shyfttest::create_time_series(temp, prec, rel_hum, wind_speed, radiation, t0, dt, n_ts_points);

    utctime model_dt = deltahours(24);
    vector<utctime> times;
    for (utctime i=t0; i <= t1; i += model_dt)
        times.emplace_back(i);

    point_timeaxis time_axis(times.cbegin(), times.cend());

    // Initialize parameters
    pt::parameter pt_param;
    gs::parameter gs_param;
    ae::parameter ae_param;
    kr::parameter k_param;
    //CellParameter c_param;
    pc::parameter p_corr_param;

    // Initialize the state vectors
    kr::state kirchner_state{5.0};
    gs::state gs_state(0.0, 1.0, 0.0, 1.0/(gs_param.snow_cv*gs_param.snow_cv), 10.0, -1.0, 0.0, 0.0);

    // Initialize response
    //response response;

    // Initialize collectors
    shyfttest::mock::PTGSKResponseCollector response_collector(time_axis.size());
    shyfttest::mock::StateCollector<point_timeaxis> state_collector(time_axis);

    state state{gs_state, kirchner_state};
    parameter parameter{pt_param, gs_param, ae_param, k_param,  p_corr_param};
    geo_cell_data geo_cell;
    pt_gs_k::run_pt_gs_k<shyft::timeseries::direct_accessor,
                         response>(geo_cell, parameter, time_axis, temp, prec, wind_speed,
                                     rel_hum, radiation, state, state_collector, response_collector);
}

void pt_gs_k_test::test_raster_call_stack()
{
    //using shyfttest::TSSource;

    typedef MCell<response, state, parameter, xpts_t> PTGSKCell;
    calendar cal;
    utctime t0 = cal.time(YMDhms(2014, 8, 1, 0, 0, 0));
    const int nhours=1;
    utctimespan dt=1*deltahours(nhours);
    utctimespan model_dt = 1*deltahours(nhours);
    size_t n_ts_points = 365;
    utctime t1 = t0 + n_ts_points*dt;

    vector<utctime> times;
    for (utctime i=t0; i <= t1; i += model_dt)
        times.emplace_back(i);
    shyft::timeseries::point_timeaxis time_axis(times.cbegin(), times.cend());

    // 10 catchments numbered from 0 to 9.
    std::vector<catchment_t> catchment_discharge;
    catchment_discharge.reserve(10);
    for (size_t i = 0; i < 10; ++i)
        catchment_discharge.emplace_back(time_axis);

    size_t n_dests = 10*100;
    std::vector<PTGSKCell> model_cells;
    model_cells.reserve(n_dests);

    pt::parameter pt_param;
    gs::parameter gs_param;
    ae::parameter ae_param;
    kr::parameter k_param;
    pc::parameter p_corr_param;

    xpts_t temp;
    xpts_t prec;
    xpts_t rel_hum;
    xpts_t wind_speed;
    xpts_t radiation;

    kr::state kirchner_state{5.0};
    gs::state gs_state(0.6, 1.0, 0.0, 1.0/(gs_param.snow_cv*gs_param.snow_cv), 10.0, -1.0, 0.0, 0.0);
    state state{gs_state, kirchner_state};


    parameter parameter{pt_param, gs_param, ae_param, k_param, p_corr_param};

    for (size_t i = 0; i < n_dests; ++i) {
        shyfttest::create_time_series(temp, prec, rel_hum, wind_speed, radiation, t0, dt, n_ts_points);
        state.gs.albedo += 0.3*(double)i/(n_dests - 1); // Make the snow albedo differ at each destination.
        model_cells.emplace_back(temp, prec, wind_speed, rel_hum, radiation, state, parameter, i % 3);
    }


    const std::clock_t start = std::clock();
    for_each(model_cells.begin(), model_cells.end(), [&time_axis, &catchment_discharge] (PTGSKCell& d) mutable {
        auto time = time_axis(0).start;

        shyfttest::mock::StateCollector<point_timeaxis> sc(time_axis);
        shyfttest::mock::DischargeCollector<point_timeaxis> rc(1000 * 1000, time_axis);
        //PTGSKResponseCollector rc(time_axis.size());

        pt_gs_k::run_pt_gs_k<shyft::timeseries::direct_accessor, response>(d.geo_cell_info(), d.parameter(), time_axis,
              d.temperature(),
              d.precipitation(),
              d.wind_speed(),
              d.rel_hum(),
              d.radiation(),
              d.get_state(time),
              sc,
              rc
              );
    });

    for (size_t i=0; i < 3; ++i)
        std::cout << "Catchment "<< i << " first total discharge = " << catchment_discharge.at(i).value(0) << std::endl;
    for (size_t i=0; i < 3; ++i)
        std::cout << "Catchment "<< i << " second total discharge = " << catchment_discharge.at(i).value(1) << std::endl;
    for (size_t i=0; i < 3; ++i)
        std::cout << "Catchment "<< i << " third total discharge = " << catchment_discharge.at(i).value(2) << std::endl;

    const std::clock_t total = std::clock() - start;
    std::cout << "One year and " << n_dests << " destinatons with catchment discharge aggregation took: " << 1000*(total)/(double)(CLOCKS_PER_SEC) << " ms" << std::endl;


}

// TODO: Write a mass balance test for checking that the fraction calculations are correct.
