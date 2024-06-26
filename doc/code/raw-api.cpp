#include <Eigen/LU>
#include <iostream>
#include <cmath>
#include <atomic>
#include <thread>
#include <chrono>
#include <ffbidx/refine.h>

using std::cout;
using std::flush;
using std::atomic_bool;
using Mx3 = Eigen::MatrixX3f;
using M3 = Eigen::Matrix3f;
using Vx = Eigen::VectorXf;
using Eigen::Map;
using LU = Eigen::FullPivLU<M3>;
using namespace std::chrono_literals;
using namespace fast_feedback;
using ifss = refine::indexer_ifss<>;
using refine::best_cell;
constexpr float PI = 3.14159265358979323846;
constexpr float _60 = PI/3.0f;

void callback(void *data) {
    cout << '!' << flush;
    ((atomic_bool*)data)->store(true);
};

int main(int argc, char* argv[]) {
    float coords[3][10] = {
        {1.0, 4.0, 1.0, 2.0, 6.0, 4.0, 7.0, 8.0, 5.0, 9.0},
        {2.0, 5.0, 1.0, 1.0, 3.0, 5.0, 1.0, 2.0, 7.0, 8.0},
        {3.0, 6.0, 7.0, 4.0, 1.0, 2.0, 3.0, 6.0, 5.0, 8.0}
    };
    float base[3][3] = {
        {2.0, 0.0, 0.0},
        {0.0, 1.0, 0.0},
        {0.0, 0.0, 1.0}
    };
    float rot60z[3][3] = {
        {  cosf(_60), sinf(_60), 0 },
        { -sinf(_60), cosf(_60), 0 },
        {          0,         0, 1 }
    };
    Mx3 Coords = Map<Mx3>((float*)coords, 10, 3);
    M3 B0 = Map<M3>((float*)base, 3, 3);
    M3 R60z = Map<M3>((float*)rot60z, 3, 3);
    M3 B = B0 * R60z.transpose();
    LU lu(B);
    M3 Binv = lu.inverse();
    Mx3 Spots = Coords * Binv.transpose();
    Mx3 Cand(3*32, 3);
    Vx Score(32);

    cout << "True Coords:\n" << Coords
         << "\nB0:\n" << B0
         << "\nR60z:\n" << R60z
         << "\nB:\n" << B
         << "\nBinv:\n" << Binv
         << "\nSpots:\n" << Spots << '\n';

    input<> in{
        {&B0(0,0), &B0(0,1), &B0(0,2)},
        {&Spots(0,0), &Spots(0,1), &Spots(0,2)},
        1, 10, true, true
    };
    output<> out{
        &Cand(0,0), &Cand(0,1), &Cand(0,2),
        &Score(0), 32
    };
    config_persistent<> cp{
        32, 1, 10, 32, true
    };
    config_runtime<> cr{};
    refine::config_ifss<> ci{};

    memory_pin pin_b0{B0};
    memory_pin pin_spots{Spots};
    memory_pin pin_cand{Cand};
    memory_pin pin_score{Score};
    memory_pin pin_cr = memory_pin::on(cr);

    indexer<> indexer(cp);

    atomic_bool finished{false};
    indexer.index_start(in, out, cr, callback, (void*)&finished);
    while (! finished.load()) {
        cout << '.';
        std::this_thread::sleep_for(100us);
    }
    cout << '\n';
    indexer.index_end(out);
    ifss::refine(Spots, Cand, Score, ci, 0, 2);
    ifss::refine(Spots, Cand, Score, ci, 1, 2);
    unsigned best = best_cell(Score);

    cout << "Cand:\n" << Cand
         << "\nScore:\n" << Score
         << "\nBest: " << best << '\n';

    float sf = Score(best);
    M3 Bf = Cand.block(3*best, 0, 3, 3);
    bool viable = (sf < .001f);

    cout << "B found:\n" << Bf
         << "\nScore: " << sf << ", Viable: " << (viable?"yes":"no")
         << "\nComputed Coords:\n" << (Spots * Bf.transpose()) << '\n';
}
