

#ifndef DYNSLAM_INFINITAMDRIVER_H
#define DYNSLAM_INFINITAMDRIVER_H

#include <opencv/cv.h>
#include <iostream>

#include <pangolin/pangolin.h>
#include "../InfiniTAM/InfiniTAM/ITMLib/Engine/ITMMainEngine.h"
#include "ImageSourceEngine.h"

namespace dynslam {
namespace drivers {

template<typename T>
ORUtils::Vector2<T> ToItmVec(const cv::Vec<T, 2> in) {
  return ORUtils::Vector2<T>(in[0], in[1]);
}

template<typename T>
ORUtils::Vector3<T> ToItmVec(const cv::Vec<T, 3> in) {
  return ORUtils::Vector3<T>(in[0], in[1], in[2]);
}

template<typename T>
ORUtils::Vector4<T> ToItmVec(const cv::Vec<T, 4> in) {
  return ORUtils::Vector4<T>(in[0], in[1], in[2], in[3]);
}

/// \brief Interfaces between DynSLAM and InfiniTAM.
class InfiniTamDriver : public ITMMainEngine {
public:
  // TODO(andrei): We may need to add another layer of abstraction above the drivers to get the best
  // modularity possible.
  static InfiniTamDriver* Build(
      const string &dataset_root,
      const ITMRGBDCalib& calib,
      const cv::Vec2i& rgb_image_size,
      const cv::Vec2i& depth_image_size
  ) {
    ITMLibSettings *settings = new ITMLibSettings();

    const string calib_fpath = dataset_root + "/itm-calib.txt";
    const string rgb_image_format = dataset_root + "/precomputed-depth/Frames/%04i.ppm";
    const string depth_image_format = dataset_root + "/precomputed-depth/Frames/%04i.pgm";

    InfiniTamDriver *driver = new InfiniTamDriver(
        settings,
        new ITMRGBDCalib(calib),
        ToItmVec(rgb_image_size),
        ToItmVec(depth_image_size));
    return driver;
  }

  InfiniTamDriver(const ITMLibSettings* settings,
                  const ITMRGBDCalib* calib,
                  const Vector2i& imgSize_rgb,
                  const Vector2i& imgSize_d);

  void ProcessFrame(ITMUChar4Image *rgbImage, ITMShortImage *rawDepthImage,
                    ITMIMUMeasurement *imuMeasurement = nullptr) override;

  void UpdateView(ITMUChar4Image *rgbImage, ITMShortImage *rawDepthImage) {
    // * If 'view' is null, this allocates its RGB and depth buffers.
    // * Afterwards, it converts the depth map we give it into a float depth map (we may be able to
    //   skip this step in our case, since we have control over how our depth map is computed).
    // * It then filters the shit out of the depth map (maybe we could skip this?) using five steps
    //   of bilateral filtering.
    this->viewBuilder->UpdateView(&view, rgbImage, rawDepthImage, settings->useBilateralFilter,
                                  settings->modelSensorNoise);
  }

  // used by the instance reconstruction
  void SetView(ITMView *view) {
    if (this->view) {
      // TODO(andrei): These views should be memory managed by the tracker. Make sure this is right.
//      delete this->view;
    }

    this->view = view;
  }

  void Track() {
    // Consider leveraging sparse scene flow here, for dynamic instances, maybe?
    this->trackingController->Track(this->trackingState, this->view);
  }

  void Integrate() {
    this->denseMapper->ProcessFrame(
      // We already generate our new view when splitting the input based on the segmentation.
      // The tracking state is kept up-to-date by the tracker.
      // The scene actually holds the voxel hash. It's almost a vanilla struct.
      // The render state is used for things like raycasting
      this->view, this->trackingState, this->scene, this->renderState_live);
  }

  void PrepareNextStep() {
    // This may not be necessary if we're using ground truth VO.
    this->trackingController->Prepare(this->trackingState, this->view, this->renderState_live);
  }

  const ITMLibSettings* GetSettings() const {
    return settings;
  }

  // Not const because 'ITMMainEngine's implementation is not const either.
  void GetImage(
      ITMUChar4Image *out,
      GetImageType get_image_type,
      const pangolin::OpenGlMatrix &model_view = pangolin::IdentityMatrix()
  ) {
    // TODO helper function for this
    Matrix4f M;
    for(int i = 0; i < 16; ++i) {
      M.m[i] = static_cast<float>(model_view.m[i]);
    }

    ITMPose itm_freeview_pose;
    itm_freeview_pose.SetM(M);

    if (nullptr != this->view) {
      ITMIntrinsics intrinsics = this->view->calib->intrinsics_d;
      ITMMainEngine::GetImage(
          out,
          get_image_type,
          &itm_freeview_pose,
          &intrinsics);
    }
    else {
      // We're before the very first frame, so no raycast is available yet.
//      std::cerr << "Warning: no raycast available yet." << endl;
    }
  }

};

} // namespace drivers
} // namespace dynslam


#endif //DYNSLAM_INFINITAMDRIVER_H
