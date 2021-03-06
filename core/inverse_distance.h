#pragma once
#ifdef WIN32
#pragma warning(disable:4503)
#endif // WIN32
#include <algorithm>
#include <exception>
#include <functional>

#include "compiler_compatiblity.h"
#include "utctime_utilities.h"
#include "geo_point.h"
/** \file
 * Contains all IDW related stuff, parameters, the IDW algorithm, IDW Models, and IDW Runner
 */
namespace shyft {
    namespace core {
		namespace inverse_distance {

	        /** \brief parameter is a simple place-holder for IDW parameters
			 *
			 *  The two most common
			 *  max_distance and the max_members to use for
	         * interpolation.
			 * Additionally it keep distance measure-factor,
			 * so that the IDW distance is computed as 1 over pow(euclid distance,distance_measure_factor)
	         */
	        struct parameter {
	            size_t max_members;///< number of members that can participate in one destination-cell
	            double max_distance;///< [meter] only neighbours within max distance is used for each destination-cell
                double distance_measure_factor;///< IDW distance is computed as 1 over pow(euclid distance,distance_measure_factor)
	            parameter(size_t max_members=10, double max_distance=200000.0,
                          double distance_measure_factor=2.0)
	             : max_members(max_members), max_distance(max_distance),
                   distance_measure_factor(distance_measure_factor) {}
	        };

	        /** \brief For temperature inverse distance, also provide default temperature gradient to be used
	         * when the gradient can not be computed.
			 * \sa temperature_model
	         */
	        struct temperature_parameter :public parameter {
	            double default_temp_gradient;///< unit [degC/m]
	            temperature_parameter(double default_gradient = -0.006, size_t max_members = 20, double max_distance = 200000.0)
	                : parameter(max_members, max_distance), default_temp_gradient(default_gradient) {}
	            double default_gradient() const { return default_temp_gradient; }
	        };

	        /*\brief For precipitation,the scaling model needs the increase in precipitation for each 100 meters.
			 * \sa temperature_model
	        */
	        struct precipitation_parameter: public parameter {
	            double scale_factor;
	            precipitation_parameter(double increase_pct_m=2, size_t max_members=20, double max_distance=200000.0)
	              : parameter(max_members, max_distance), scale_factor(1+increase_pct_m/100) {}
	            double precipitation_scale_factor() const { return scale_factor; } // mm/m,  0.5 mm increase pr. 100 m height
	        };


			 /** \brief Inverse Distance Weighted Interpolation
			 * The Inverse Distance Weighted algorithm.
			 *
			 * Adopted/extracted from Enki method idwtemp by Sjur Kolberg/Sintef.
			 *
			 * Transforms a set of time-series data at given source locations (X,Y,Z) to a
			 *  new set of time-series data at some target (grid) locations (X,Y,Z)
			 *  Using a weighted average of max n closest neighbors with available data to each target location.
			 *
			 *  The algorithm does not use neighbor locations that exceeds the maximum distance.
			 *
			 *  The algorithm can calculate and use the gradient for temperature data (as well as default fallback gradient )
			 *  to height adjust source temperatures to target temperature level. Ref. to M, Model template parameter for details.
			 *
			 *  notice that the last argument to the function is a setter function that takes D:value_type, size_t ix and value.
			 *  This implies that currently the Timeaxis should just return size_t for (), -
			 *   later we can fix this to be the return_type of function-expression of the timeaxis().
			 *  Until then, the IDWTimeAxis template is supplied at the end of this file
			 *  to ease mapping a TimeAxis that conforms to average_accessor/value standards.
			 *  Ref to IDWTest file for example
			 *
			 *  Preconditions:
			 *   -# There should be at least one source location with a valid value for all t.
			 *   -# Possible actions if not satisfied: set NaN at the output
			 *   -# Minimum distance unit is 1 { because we optimize using (x² + y² + z²) instead of sqrt(x² + y² + z²) }
			 *
			 * \tparam S iterator
			 *  Source (location)
			 *  Type that implements:
			 *    -# S.geo_point() const --> 3D point, x,y,z, specifies the location
			 *    -# S.value(type of T operator(i) ) -> returns value at the i'th period of timeaxis
			 * \tparam D iterator
			 *  Destination (location/area)
			 *  Type that implements:
			 *    -# D.geo_point() const --> 3D point, x,y,z etc.. (needed for calc dist, and height corrected temp)
			 *    -# D.set_value(type of T.operator(i), double value), function that is called to provide the computed value at the i'th timestep
			 *    -# other requirements dependent on the Model class could apply (like radiation slope factor, etc.)
			 * \tparam P
			 * Parameters for the algorithm, that supplies :
			 *    -# P.max_distance const --> double, - locations outside this distance is not available
			 *    -# P.max_members const --> size_t, - max number of members to use in weight
			 * \tparam T
			 * TimeAxis providing:
			 *  -#  T.size() const --> size_t, number of non-overlapping time intervals
			 *  -#  T(const size_t i) const --> period of a type, that is passed into  type with start, end
			 *
			 * \tparam M
			 * Temperature, Radiation, Precipitation Model providing:
			 *  -#  M::scale_computer, a class that accepts a P parameter in the constructor.
			 *      if M::scale_computer.is_source_based() is true, then
			 *       for each destination cell, it will add valid sources for that cell, then call compute to compute the
			 *       scaleValue that are passed into the M::transform function as described below.
			 *       This allows us to create models that compute temperature gradient based on the same sources that
			 *       we use for interpolating temperature into each cell.
			 *      Ref. to Model classes for more details.
			 *  -#  M::distance_measure, a static method that accepts three arguments; a,b of type of S|D.geo_point() and a measure parameter f, and returns the squared distance
			 *  -#  M::transform(sourcevalue, scalevalue, const S &source, const D& destination), --> source value transformed to destination level
			 *
			 * \sa BayesianKriging for more advanced interpolation
			 *
			 *
			 */

			template<class M, class S, class D, class T, class P, class F>
			void run_interpolation(S source_begin, S source_end,
								  D destination_begin, D destination_end,
								   const T& timeAxis, const P& parameter ,
								   F&& dest_set_value) // in short, a setter function for the result..
								   //std::function< void(typename D::value_type& ,size_t ,double ) > dest_set_value ) // in short, a setter function for the result..
			{
				using namespace std;
				typedef typename S::value_type const * source_pointer;
				typedef typename S::value_type source_t;

				struct source_weight {
					source_weight( source_pointer source=nullptr, double weight=0): source(source), weight(weight) {}
					source_pointer source;
					double weight;
					// Source::TsAccessor(S,TimeAxis) should go into this one..
					// at least if we plan for parallel computing in this alg.
				};


				static const double max_weight = 1.0; // Used in place of inf for weights
				const double min_weight = 1.0/M::distance_measure(typename source_t::geo_point_t(0.0),
                                          typename source_t::geo_point_t(parameter.max_distance),
                                          parameter.distance_measure_factor);
				const size_t source_count = distance(source_begin, source_end);
				const size_t destination_count = distance(destination_begin, destination_end);

				typedef vector<source_weight> source_weight_list;
				vector<source_weight_list> cell_neighbours;
				cell_neighbours.reserve(destination_count);
				//
				// 1. create cell_ neighbors,
				//    that is; for each destination cell,
				//     - a list of sources with weights that are within reaching distance
				//
				source_weight_list swl; swl.reserve(source_count);
				size_t max_entries = parameter.max_members;
				for(auto destination=destination_begin; destination != destination_end; ++destination) { // for each destination, create a unique SourceWeightList
					auto destination_point = destination->mid_point();
					swl.clear();
					for_each(source_begin, source_end,  // First, just create unsorted SourceWeightList
							 [&]
							 (const typename S::value_type& source) {
							   double weight = min(max_weight, 1.0/M::distance_measure(destination_point,
                                                source.mid_point(),
                                                parameter.distance_measure_factor));
							   if(weight >= min_weight) // max distance value tranformed to minimum weight, so we only use those near enough
									swl.emplace_back(&source, weight);
							 }
					);
					// now source weight list,swl, contains only sources that can be used for this dest. cell.
					//TODO: fix rare issue that if we get NaNs, and there are more sources in range than max_entries
					//      then this approach using partial sort + truncate at max_entries, will not promote those truncated
					//      even if they are in range.
					if(swl.size() > max_entries ) {// drop sorting if less than needed elements.. (maybe thats already done in partial sort ?)
						partial_sort(begin(swl), begin(swl) + max_entries, end(swl), [](const source_weight& a, const source_weight &b) {return a.weight>b.weight; });  // partial sort the list (later: only if max_entries>usable_sources.size..)
						swl.resize(max_entries);// get rid of left-overs
					}

					cell_neighbours.emplace_back(swl);  // done with this destination source, n_dest x n_source allocs
				}

				//
				// 2. for each destination, do the IDW
				//     using cell_neighbors that keeps a list of reachable sources
				//     Only use sources that provides a valid value using isfinite()
				//    if the supplied Model::scale_computer (gradient..) require it, also
				//    feed that model with valid source values to get a computed scale factor.
				//
				vector<double> destination_scale; destination_scale.reserve(destination_count);
				bool first_time_scale_calc=true;

				for (size_t i=0; i < timeAxis.size(); ++i) {
					auto period_i = timeAxis(i);

					// compute gradient, scale whatever, based on available sources..
					if( M::scale_computer::is_source_based() || first_time_scale_calc) {
						destination_scale.clear();
						for(size_t j=0; j < cell_neighbours.size(); ++j) {
							auto cell_neighbor = cell_neighbours[j];
							typename M::scale_computer gc(parameter);
							for_each(begin(cell_neighbor), end(cell_neighbor),
								[&, period_i]
								(const source_weight &sw) {
									 double source_value=sw.source->value(period_i);
									 if(isfinite(source_value)) // only use valid source values
									   gc.add(*(sw.source), period_i);
								}
							);
							destination_scale.emplace_back(gc.compute());//could pass (destination_begin +j)->mid_point(), so we know the dest position ?
							first_time_scale_calc=false;// all models except temperature(due to gradient) are one time only,
						}
					}
					//
					// Now that we got the destination_computer in place, we can just iterate over
					//
					for(size_t j=0;j<cell_neighbours.size();++j) {
						double sum_weights=0, sum_weight_value=0;
						auto &cell_neighbor = cell_neighbours[j];
						auto destination = destination_begin + j;
						double computed_scale = destination_scale[j];

						for_each(begin(cell_neighbor), end(cell_neighbor),
							[&, period_i]
							(const source_weight &sw) {
								double source_value = sw.source->value(period_i);
								if(isfinite(source_value)) { // only use valid source values
									sum_weight_value += sw.weight*M::transform(source_value, computed_scale, *(sw.source), *destination);
									sum_weights += sw.weight;
								}
							}
						);
						dest_set_value(*destination,period_i,sum_weight_value/sum_weights);
					}
				}
			}
#ifndef SWIG
			/** \brief temperature_gradient_scale_computer
			* based on a number of geo-located temperature-sources, compute the temperature gradient.
			*
			* Usage in the context of IDW-models,
			* Linear least square calculation of temperature gradient,
			* see http://mathworld.wolfram.com/LastSqueresFitting.html
			*/
			struct temperature_gradient_scale_computer {
			    struct temp_point{
			        temp_point(const geo_point p,double t):point(p),temperature(t){}
			        geo_point point;
			        double temperature;
                };
				static bool is_source_based() { return true; }
				template <typename P>
				temperature_gradient_scale_computer(const P&p) : default_gradient(p.default_gradient()) { clear(); }
				template<typename T,typename S> void add(const S &s, T tx) {
				    pt.emplace_back(s.mid_point(),s.value(tx));
				}
				double compute() const {
				    if(pt.size()>1) {
                        double s_h=0;
                        double s_ht=0;
                        double s_t=0;
                        double s_hh=0;
                        size_t n=pt.size();
                        size_t mx_i=0;size_t mn_i=0;
                        for(size_t i=0;i<n;++i) {
                            double t=pt[i].temperature;
                            double h=pt[i].point.z;
                            s_h += h; s_ht += h*t; s_t += t; s_hh += h*h;
                            if(h<pt[mn_i].point.z)mn_i=i;
                            else if(h>pt[mx_i].point.z)mx_i=i;
                        }
                        double mi_mx_dz=pt[mx_i].point.z - pt[mn_i].point.z;
                        const double minimum_z_distance=50.0;
                        if(mi_mx_dz > minimum_z_distance) {
                            //double linear_best_fit= (n*s_ht - s_h*s_t) / (n*s_hh - s_h*s_h) ;
                            // min -max z ?
                            //double mi_mx_grad=
                            return ( pt[mx_i].temperature - pt[mn_i].temperature)/(mi_mx_dz);
                            //return linear_best_fit;
                        }
                        return default_gradient;
				    } else {
                        return default_gradient;
				    }
				}
				void clear() { pt.clear(); }
			private:
				double default_gradient;
				std::vector<temp_point> pt;
			};

			/** \brief temperature_gradient_scale_computer that always returns default gradient
			* based on a number of geo-located temperature-sources, compute the temperature gradient.
			*/
			struct temperature_default_gradient_scale_computer {
				static bool is_source_based() { return false; }
				template <typename P>
				temperature_default_gradient_scale_computer(const P&p) : default_gradient(p.default_gradient()) { ; }
				template<typename T, typename S>
				void add(const S &s, T tx) {}
				double compute() const {return default_gradient;}
				void clear() { }
			private:
				double default_gradient;
			};
#endif

			/** \brief Provide a minimal temperature model for the IDW algorithm,
			 *  providing functionality for transformation of source temperature to
			 *  destination location using computed temperature gradient, based on
			 *  nearby sources for that temperature location.
			 *  TODO: Add more robust handling for source stations, value range etc.
			 * \tparam S
			 *   Source ref. IDWInterpolation template parameter
			 * \tparam D
			 *   Destination ref. IDWInterpolation template parameter
			 * \tparam P
			 *   Configuration Parameters, here we just need the default_gradient (C/m):
			 *    -# .default_gradient() const;
			 * \tparam G
			 *  GeoPoint type that supports:
			 *    -# static member G::distance_measure(a,b,p)
			 *    -# .z (todo maybe use .height()const instead
			 *
			 *  Implemented IDW required members are:
			 *   -# class scale_computer with .add(S,t) and .compute(), including default gradient if to few points
			 *   -# static method .distance_measure(GeoPointType,GeoPointType, double)
			 *   -# static method .transform(double sourceValue,double scale,const S&source,const D& destination)
			 *  \sa IDWTemperatureParameter for how to provide parameters to the IDW using this model
			 */
			template <class S,class D,class P,class G, class SC>
			struct temperature_model {
				typedef SC scale_computer;
				static inline double distance_measure(const G &a, const G &b, double f) {
					return G::distance_measure(a, b, f);
				}
				static inline double transform(double sourceValue, double gradient, const S& s, const D& d) {
					return sourceValue + gradient*(d.mid_point().z - s.mid_point().z);
				}
			};

		   /** \brief Provide a minimal model for IDW,
			*  RadiationModel::scale_computer does nothing(returns 1.0)
			*  the RadiationModel::transform returns sourceValue (radiation) * destination.slope_factor(),
			*   that is, it tries to translate the radiation at source into the destination, taking into account the
			*   destination slopefactor (assume slopefactor 1.0 at source ?)
			*/
			template <class S, class D, class P, class G>
			struct radiation_model {
#ifndef SWIG
				struct scale_computer {
					static  bool is_source_based() { return false; }
					scale_computer(const P&) {}
					void add(const S &, utctime) {}
					double compute() const { return 1.0; }
				};
#endif
				static inline double distance_measure(const G &a, const G &b, double f) {
					return G::distance_measure(a, b, f);
				}
				static inline double transform(double sourceValue, double gradient, const S& s, const D& d) {
					return sourceValue*d.slope_factor();
				}
			};

		   /** \brief Provide a minimal precipitation model for IDW,
			*  PrecipitationModel::scale_computer provides the constant precipitation_gradient relaying it to the
			*  transform function through the compute() result.
			*  the PrecipitationModel::transform returns sourceValue (precip) +  gradient*(d.z-s.z),
			*   that is, it tries to translate the precipitation at source into the destination, taking into account the
			*   precipitation gradient
			* \sa IDWPrecipitationParameter
			*/
			template <class S, class D, class P, class G>
			struct precipitation_model {
#ifndef SWIG
				struct scale_computer {
					static  bool is_source_based() { return false; }

					double precipitation_gradient;
					scale_computer(const P& p) : precipitation_gradient(p.precipitation_scale_factor()) {}
					void add(const S &, utctime) {}
					double compute() const { return precipitation_gradient; }
				};
#endif
				static inline double distance_measure(const G &a, const G &b, double f) {
					return G::distance_measure(a, b, f);
				}
				static inline double transform(double precipitation, double scalefactor, const S& s, const D& d) {
					//const double zero_precipitation = 1e-6;
					//return abs(precipitation)<zero_precipitation ? 0.0 : max(0.0, precipitation + gradient*(d.geo_point().z - s.geo_point().z));
					return precipitation* pow(scalefactor, (d.mid_point().z - s.mid_point().z) / 100.0);
				}
			};

			/** \brief Provide a minimal model for IDW,
			*  WindSpeedModel::scale_computer does nothing(returns 1.0)
			*  the WindSpeedModel::transform returns sourceValue ;
			*/
			template <class S, class D, class P, class G>
			struct wind_speed_model {
#ifndef SWIG
				struct scale_computer {
					static bool is_source_based() { return false; }
					scale_computer(const P&) {}
					void add(const S &, utctime) {}
					double compute() const { return 1.0; }
				};
#endif
				static inline double distance_measure(const G &a, const G &b, double f) {
					return G::distance_measure(a, b, f);
				}
				static inline double transform(double sourceValue, double gradient, const S& s, const D& d) {
					return sourceValue;
				}
			};

			/** \brief Provide a minimal model for IDW,
			*  RelHumModel::scale_computer does nothing(returns 1.0)
			*  the RelHumModel::transform returns sourceValue ;
			*/
			template <class S, class D, class P, class G>
			struct rel_hum_model {
#ifndef SWIG
				struct scale_computer {
					static  bool is_source_based() { return false; }
					scale_computer(const P&) {}
					void add(const S &, utctime) {}
					double compute() const { return 1.0; }
				};
#endif
				static inline double distance_measure(const G &a, const G &b, double f) {
					return G::distance_measure(a, b, f);
				}
				static inline double transform(double sourceValue, double gradient, const S& s, const D& d) {
					return sourceValue;
				}
			};


			/** \brief A special time axis for the IDW algorithms
			 *  that utilizes the fact that we have a known, common timeaxis,
			 *  where each period/time is identified by index.
			 *  ref note on destination setter for the IDW.
			 */
			template <class TA>
			class idw_timeaxis {
				size_t n;
			public:
				idw_timeaxis(TA time_axis) :n(time_axis.size()) {}
				size_t size() const { return n; }
				size_t operator()(const size_t i) const { return i; }
			};

			/** \brief run interpolation step, for a given IDW model, sources and parameters.
			*  run_idw_interpolation of supplied sources to destination locations/cells, over a range as specified by timeaxis, based on supplied templatized parameters.
			*
			*  \note this is run in multicore mode, and it it safe because each thread, works on private or const data, and writes to different cells.
			*
			* \tparam IDWModel IDW model class, ref. IDW.h
			* \tparam IDWModelSource IDW source class, ref IDW.h for requirements.
			* \tparam ApiSource Api source, ref EnkiApiServiceImpl.h
			* \tparam P IDW parameter, specific for a given model, ref IDW.h
			* \tparam D IDW destination ref IDW.h
			* \tparam ResultSetter lambda for writing results back to destination, (Destination,size_t idx,double value)
			*
			*/
			template<typename IDWModel, typename IDWModelSource, typename ApiSource, typename P, typename D, typename ResultSetter, typename TimeAxis>
			void run_interpolation(const TimeAxis &ta, ApiSource const & api_sources, const P& parameters, D &cells, ResultSetter&& result_setter) {
				using namespace std;
				/// 1. make a vector of ts-accessors for the sources. Notice that this vector needs to be modified, since the accessor 'remembers'
				///    the last position. It is essential for performance, -but again-, then each thread needs it's own copy of the sources.
				///    Since the accessors just have a const *reference* to the underlying TS; there is no memory involved, so copy is no problem.

				vector<IDWModelSource> src; src.reserve(api_sources.size());
				for_each(begin(api_sources), end(api_sources), [&src, &ta](typename  ApiSource::value_type const & s) { src.emplace_back(s, ta); });

				idw_timeaxis<TimeAxis> idw_ta(ta);
				/// 2. Create a set of futures, for the threads that we want to run
				vector<future<void>> calcs;
				size_t n_cells = distance(begin(cells), end(cells));
				///    - and figure out a suitable ncore number. Using single cpu 4..8 core shows we can have more threads than cores, and gain speed.
				size_t ncore = thread::hardware_concurrency()*4;  /// 4 found to be ok for 1 cpu 4..8 cores(subject to change later)
				if(ncore == 0) ncore = 4;//in case of not available, default to 4,
				size_t thread_cell_count = 1 + n_cells/ncore;
				auto cells_iterator = begin(cells);
				for (size_t i = 0; i < n_cells;) {
					size_t n = thread_cell_count;
					if (i + n > n_cells) n = n_cells - i;// Figure out a cell-partition to compute
					calcs.emplace_back( /// spawn a thread to run IDW on this part of the cells, using *all* sources (later we could speculate in sources needed)
						async(launch::async, [src, cells_iterator, &idw_ta, &parameters, &result_setter, n]() { /// capture src by value, we *want* a copy of that..
                            run_interpolation<IDWModel>(begin(src), end(src), cells_iterator, cells_iterator+n, idw_ta, parameters, result_setter);
							})
					);
					cells_iterator = cells_iterator+n;
					i = i + n;
				}
				///3. wait for the IDW computation threads to end.
				for_each(begin(calcs), end(calcs), [](std::future<void>& f) { f.get(); });
			}
		} // namespace  inverse_distance
    } // Namespace core
} // Namespace shyft


/* vim: set filetype=cpp: */
