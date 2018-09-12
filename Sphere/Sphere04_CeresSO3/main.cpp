#include "G2OReader.h"
#include "PoseGraph3DErrorTerm.h"
#include "SO3Parameterization.h"
#include "ceres/ceres.h"
#include "gflags/gflags.h"
#include "glog/logging.h"
#include "types.h"

using namespace ceres;
using namespace std;

DEFINE_string(inputFile, "../../data/sphere2500.g2o", "pose graph definition filename in g2o format");

// save poses to the file with format: ID p_x p_y p_z q_x q_y q_z q_w
bool savePose(const string& filename, const MapOfPoses& poses) {
    fstream fs(filename, ios::out);
    if (!fs.is_open()) {
        LOG(ERROR) << "cannot create file \"" << filename << "\"";
        return false;
    }
    for (auto& p : poses) {
        fs << p.first << " " << p.second.p.transpose() << " " << p.second.R.params().x() << " "
           << p.second.R.params().y() << " " << p.second.R.params().z() << " " << p.second.R.params().w() << endl;
    }
    fs.close();
    return true;
}

int main(int argc, char* argv[]) {
    google::ParseCommandLineFlags(&argc, &argv, true);
    google::InitGoogleLogging(argv[0]);

    // check input
    CHECK(!FLAGS_inputFile.empty()) << "need to specify the input filename";

    // read data from file
    MapOfPoses poses;
    VectorOfConstaints constraints;
    if (!readG2OFile(FLAGS_inputFile, poses, constraints)) {
        LOG(FATAL) << "read data from file failed";
    }
    cout << "number of poses: " << poses.size() << endl;
    cout << "number of constraints: " << constraints.size() << endl;

    // save original poses
    savePose("./sphere_original.txt", poses);

    // build problem
    Problem problem;
    LossFunction* lossFunction = nullptr;
    // use ProductParameterization, note that the order must be [R, p]. The order of [p, R] will be error
    LocalParameterization* poseParameterization =
        new ProductParameterization(new SO3Parameterization, new IdentityParameterization(3));
    for (auto& c : constraints) {
        auto itPoseBegin = poses.find(c.idBegin);
        CHECK(itPoseBegin != poses.end()) << "Pose with ID = " << c.idBegin << " not found";
        auto itPoseEnd = poses.find(c.idEnd);
        CHECK(itPoseEnd != poses.end()) << "Pose with ID = " << c.idEnd << " not found";

        const Eigen::Matrix<double, 6, 6> sqrtInformation = c.information.llt().matrixL();
        CostFunction* costFunction = PoseGraph3DErrorTerm::create(c.t_be, sqrtInformation);
        problem.AddResidualBlock(costFunction, lossFunction, itPoseBegin->second.data(), itPoseEnd->second.data());
        problem.SetParameterization(itPoseBegin->second.R.data(), poseParameterization);
        problem.SetParameterization(itPoseEnd->second.R.data(), poseParameterization);
    }
    // constrain the gauge freedom by setting one of the poses as constant so the optimizer cannot change it
    auto itPoseStart = poses.begin();
    CHECK(itPoseStart != poses.end()) << "There are no poses";
    cout << "start poses: p = " << itPoseStart->second.p.transpose()
         << ", q = " << itPoseStart->second.R.params().transpose() << endl;
    problem.SetParameterBlockConstant(itPoseStart->second.data());

    // solve problem
    Solver::Options options;
    options.minimizer_progress_to_stdout = true;
    options.max_num_iterations = 200;
    options.linear_solver_type = ceres::SPARSE_NORMAL_CHOLESKY;
    Solver::Summary summary;
    Solve(options, &problem, &summary);
    cout << summary.FullReport() << endl;

    // save optimized poses
    savePose("./sphere_optimized.txt", poses);

    google::ShutdownGoogleLogging();
    google::ShutDownCommandLineFlags();
    return 0;
}