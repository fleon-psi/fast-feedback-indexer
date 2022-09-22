#include <cstdlib>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <vector>
#include <array>
#include "simple_data.h"
#include "indexer.h"

namespace {

    constexpr struct success_type final {} success;
    constexpr struct failure_type final {} failure;

    template <typename stream>
    [[noreturn]] stream& operator<< (stream& out, [[maybe_unused]] const success_type& data)
    {
        out.flush();
        std::exit((EXIT_SUCCESS));
    }

    template <typename stream>
    [[noreturn]] stream& operator<< (stream& out, [[maybe_unused]] const failure_type& data)
    {
        out.flush();
        std::exit((EXIT_FAILURE));
    }

} // namespace

int main (int argc, char *argv[])
{
    using namespace simple_data;

    try {
        if (argc <= 3)
            throw std::runtime_error("missing arguments <file name> <number of kept candidate vectors> <number of half sphere sample points>");

        fast_feedback::config_runtime<float> crt{};         // default runtime config
        {
            std::istringstream iss(argv[3]);
            iss >> crt.num_sample_points;
            if (! iss)
                throw std::runtime_error("unable to parse second argument: number of half sphere sample points");
            std::cout << "n_samples=" << crt.num_sample_points << '\n';
        }

        fast_feedback::config_persistent<float> cpers{};    // default persistent config
        {
            std::istringstream iss(argv[2]);
            iss >> cpers.num_candidate_vectors;
            if (! iss)
                throw std::runtime_error("unable to parse second argument: number of kept candidate vectors");
            std::cout << "n_samples=" << cpers.num_candidate_vectors << '\n';
        }

        SimpleData<float, raise> data(argv[1]);         // read simple data file

        std::vector<float> x(data.spots.size() + 3);    // coordinate containers
        std::vector<float> y(data.spots.size() + 3);
        std::vector<float> z(data.spots.size() + 3);
        unsigned i=0;
        for (const auto& coord : data.unit_cell) {      // copy cell coordinates
            x[i] = coord.x;
            y[i] = coord.y;
            z[i] = coord.z;
            std::cout << "input" << i << ": " << x[i] << ", " << y[i] << ", " << z[i] << '\n';
            i++;
        }
        for (const auto& coord : data.spots) {          // copy spot coordinates
            x[i] = coord.x;
            y[i] = coord.y;
            z[i] = coord.z;
            i++;            
        }

        std::array<float, 3*3> buf;                     // output coordinate container
        fast_feedback::indexer indexer{cpers};          // indexer object

        fast_feedback::memory_pin pin_x{x};             // pin input coordinate containers
        fast_feedback::memory_pin pin_y{y};
        fast_feedback::memory_pin pin_z{z};
        fast_feedback::memory_pin pin_buf{buf};         // pin output coordinate container
        fast_feedback::memory_pin pin_crt{fast_feedback::memory_pin::on(crt)};  // pin runtime config memory

        fast_feedback::input<float> in{x.data(), y.data(), z.data(), 1u, i-3u}; // create indexer input object
        fast_feedback::output<float> out{&buf[0], &buf[3], &buf[6], 0u};        // create indexer output object

        indexer.index(in, out, crt);                                            // run indexer

        for (unsigned i=0u; i<out.n_cells; ++i) {
            auto off = 3u * i;
            Eigen::Matrix<float, 3, 3> A;
            A << out.x[off + 0], out.x[off + 1], out.x[off + 2],
                 out.y[off + 0], out.y[off + 1], out.y[off + 2],
                 out.z[off + 0], out.z[off + 1], out.z[off + 2];
            std::cout << "Cell " << i << ":\n";
            std::cout << A << '\n';
        }

        std::cout << "done\n" << success;
    } catch (std::exception& ex) {
        std::cerr << "Test failed: " << ex.what() << '\n' << failure;
    }
}