// Provide a quick and dirty c-wrapper
// for crystfel integration

#include <iostream>
#include <algorithm>
#include <string>
#include <map>
#include <cstdlib>
#include <cassert>
#include <ffbidx/refine.h>
#include "ffbidx/c-wrapper.h"

namespace {

    struct viable_cell_config {
        float threshold = .02f;
        unsigned n_spots = 9u;
    };

    std::mutex config_mutex;
    bool config_ok = false;

    fast_feedback::config_persistent<float> cpers{};
    fast_feedback::config_runtime<float> crt{};
    fast_feedback::refine::config_ifss<float> cifss{};
    viable_cell_config cvc{};

    struct value_t {
        union {
            float f;
            unsigned u;
        };
        char t;
    };

    value_t make_value(const unsigned& val) { value_t v; v.u=val; v.t='u'; return v; }
    value_t make_value(const float& val) { value_t v; v.f=val; v.t='f'; return v; }

    template<typename Out>
    Out& operator<< (Out& out, const value_t& val)
    {
        switch (val.t) {
            case 'f':
                out << val.f; break;
            case 'u':
                out << val.u; break;
            default:
                out << "val/" << val.t; break;
        };
        return out;
    }

    std::map<std::string, value_t> param = {
        {"cvc_threshold", make_value(.02f)},
        {"cpers_max_output_cells", make_value(32u)},
        {"crt_num_sample_points", make_value(32u*1024u)},
        {"ciffs_min_spots", make_value(6u)},
    };

    template<typename T>
    void parse_val(T& val, const std::string& str)
    {
        if (! (std::istringstream{str} >> val).eof()) {
            std::cerr << "unable to parse: " << str << '\n';
        }
    }

    void set_conf()
    {
        std::lock_guard config_lock(config_mutex);
        if (config_ok)
            return;

        constexpr const char* pvar_name = "FFBIDX_PARAMS";
        const char* env_c = std::getenv(pvar_name);

        if (env_c != nullptr) {
            std::string env{env_c};
            std::string::size_type start=0, end=0;

            while (end != std::string::npos) {
                if ((end = env.find('=', start)) == std::string::npos) {
                    std::cerr << "unable to parse " << pvar_name << " at " << env.substr(start) << '\n';
                    std::exit(-1);
                };

                std::string key = env.substr(start, end);
                auto entry = param.find(key);
                if (entry == param.end()) {
                    std::cerr << pvar_name << " no such key: " << key << '\n';
                    std::exit(-1);
                }

                start = end + 1;
                end = env.find(' ', start);
                std::string val = env.substr(start, end);
                switch (entry->second.t) {
                    case 'f':
                        parse_val(entry->second.f, val); break;
                    case 'u':
                        parse_val(entry->second.u, val); break;
                    default:
                        break;
                }
                std::cout << entry->first << '=' << entry->second << '\n';

                start = end + 1;
            }
        }

        for (const auto& entry : param) {
            if (entry.first == "cvc_threshold") {
                cvc.threshold = entry.second.f;
            } else if (entry.first == "cpers_max_output_cells") {
                cpers.max_output_cells = entry.second.u;
            } else if (entry.first == "crt_num_sample_points") {
                crt.num_sample_points = entry.second.u;
            } else if (entry.first == "ciffs_min_spots") {
                cifss.min_spots = entry.second.u;
            }
        }

        config_ok = true;
    }

    int index_step(ffbidx_indexer ptr, float cell[9], float *x, float *y, float *z, unsigned nspots)
    {
        using indexer = fast_feedback::refine::indexer<float>;

        try {
            indexer* idx = (indexer*)ptr.ptr;
            std::copy(&cell[0], &cell[3], &idx->iCellX());
            std::copy(&cell[3], &cell[6], &idx->iCellY());
            std::copy(&cell[6], &cell[9], &idx->iCellZ());

            unsigned n = std::min(idx->max_spots(), nspots);
            std::copy(&x[0], &x[n], &idx->spotX());
            std::copy(&y[0], &y[n], &idx->spotY());
            std::copy(&z[0], &z[n], &idx->spotZ());

            idx->index(1u, n);
        } catch (std::exception& ex) {
            std::cerr << "Error: " << ex.what() << '\n';
            return 1;
        } catch (...) {
            return 1;
        }

        return 0;
    }

    int is_viable_cell(ffbidx_indexer ptr, float cell[9])
    {
        using namespace Eigen;
        using indexer = fast_feedback::refine::indexer<float>;

        int indexable;
        try {
            indexer* idx = (indexer*)ptr.ptr;
            unsigned best =  fast_feedback::refine::best_cell(idx->oScoreV());

            const Matrix<float, 3, 3>& ocell = idx->oCell(best);
            Map<Matrix<float, 3, 3>> mcell{cell};
            mcell = ocell;

            indexable = fast_feedback::refine::is_viable_cell(ocell, idx->Spots(), cvc.threshold, cvc.n_spots);
        } catch (std::exception& ex) {
            std::cerr << "Error: " << ex.what() << '\n';
            return -1;
        } catch (...) {
            return -1;
        }

        return indexable;
    }

}

extern "C" {

    int allocate_fast_indexer(ffbidx_indexer* idx, ffbidx_settings* settings)
    {
        using indexer = fast_feedback::refine::indexer<float>;

        idx->ptr = nullptr;

        try {
            cpers.max_spots = settings->max_spots;
            set_conf();
            idx->ptr = new indexer{cpers, crt};
        } catch (std::exception& ex) {
            std::cerr << "Error: " << ex.what() << '\n';
        } catch (...) {
            // ignore
        }

        return idx->ptr == nullptr;
    }

    void free_fast_indexer(ffbidx_indexer ptr)
    {
        using indexer = fast_feedback::refine::indexer<float>;

        indexer* idx = (indexer*)ptr.ptr;
        delete idx;
        ptr.ptr = nullptr;
    }

    // cell: 0-3: x-coords, 3-6: y-coords, 6-9: z-coords
    int index_raw(ffbidx_indexer ptr, float cell[9], float *x, float *y, float *z, unsigned nspots)
    {
        if (index_step(ptr, cell, x, y, z, nspots) != 0)
            return -1;
        
        return is_viable_cell(ptr, cell);
    }

    int index_refined(ffbidx_indexer ptr, float cell[9], float *x, float *y, float *z, unsigned nspots)
    {
        using indexer = fast_feedback::refine::indexer<float>;
        using ifss = fast_feedback::refine::indexer_ifss<float>;

        if (index_step(ptr, cell, x, y, z, nspots) != 0)
            return -1;

        try {
            indexer* idx = (indexer*)ptr.ptr;

            ifss::refine(idx->Spots(), idx->oCellM(), idx->oScoreV(), cifss);
        } catch (std::exception& ex) {
            std::cerr << "Error: " << ex.what() << '\n';
            return -1;
        } catch (...) {
            return -1;
        }

        return is_viable_cell(ptr, cell);
    }

} // extern "C"

int fast_feedback_crystfel(struct ffbidx_settings *settings, float cell[9], float *x, float *y, float *z, unsigned nspots) {
    using namespace Eigen;
    using ifss = fast_feedback::refine::indexer_ifss<float>;

    try {
        cpers.max_spots = settings->max_spots;
        set_conf();
        unsigned n = std::min(cpers.max_spots, nspots);

        ifss indexer{cpers, crt, cifss};
        
        std::copy(&cell[0], &cell[3], &indexer.iCellX());
        std::copy(&cell[3], &cell[6], &indexer.iCellY());
        std::copy(&cell[6], &cell[9], &indexer.iCellZ());

        std::copy(&x[0], &x[n], &indexer.spotX());
        std::copy(&y[0], &y[n], &indexer.spotY());
        std::copy(&z[0], &z[n], &indexer.spotZ());

        indexer.index(1, n);

        auto best_cell_id = fast_feedback::refine::best_cell(indexer.oScoreV());
        auto ocell = indexer.oCell(best_cell_id);

        Map<Matrix<float, 3, 3>> mcell{cell};
        mcell = ocell;
        return fast_feedback::refine::is_viable_cell(ocell, indexer.Spots(), cvc.threshold, cvc.n_spots);
    } catch (std::exception& ex) {
        std::cerr << "Error: " << ex.what() << '\n';
        return 0;
    } catch (...) {
        std::cerr << "Unknown error: " << '\n';
        return 0;
    }

}
