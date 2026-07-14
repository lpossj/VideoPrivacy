#pragma once

#include <opencv2/core/mat.hpp>
#include <opencv2/core/types.hpp>

namespace video_privacy
{

cv::Rect expandFaceBox(
    const cv::Rect &box,
    const cv::Size &frameSize,
    float marginRatio = 0.18F);

void applyMosaic(
    cv::Mat &frame,
    const cv::Rect &faceBox,
    int mosaicStrength);

} // namespace video_privacy
