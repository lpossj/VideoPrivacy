#include "video_privacy/mosaic.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

#include <opencv2/imgproc.hpp>

namespace video_privacy
{

cv::Rect expandFaceBox(
    const cv::Rect &box,
    const cv::Size &frameSize,
    float marginRatio)
{
    const int marginX = static_cast<int>(std::round(box.width * marginRatio));
    const int marginY = static_cast<int>(std::round(box.height * marginRatio));
    cv::Rect expanded(
        box.x - marginX,
        box.y - marginY,
        box.width + marginX * 2,
        box.height + marginY * 2);
    return expanded & cv::Rect(0, 0, frameSize.width, frameSize.height);
}

void applyMosaic(
    cv::Mat &frame,
    const cv::Rect &faceBox,
    int mosaicStrength)
{
    // The GUI exposes a linear 1-100 strength slider.  The value is used as
    // the approximate mosaic block size in pixels: a larger value produces
    // larger blocks and removes more facial detail.
    mosaicStrength = std::clamp(mosaicStrength, 1, 100);

    const cv::Rect mosaicBox = expandFaceBox(faceBox, frame.size());
    if (mosaicBox.width <= 1 || mosaicBox.height <= 1)
    {
        return;
    }

    cv::Mat faceRegion = frame(mosaicBox);
    const int smallWidth = std::max(1,
        static_cast<int>(std::ceil(
            static_cast<double>(faceRegion.cols) / mosaicStrength)));
    const int smallHeight = std::max(1,
        static_cast<int>(std::ceil(
            static_cast<double>(faceRegion.rows) / mosaicStrength)));
    cv::Mat smallImage;
    cv::resize(
        faceRegion,
        smallImage,
        cv::Size(smallWidth, smallHeight),
        0.0,
        0.0,
        cv::INTER_AREA);
    cv::resize(
        smallImage,
        faceRegion,
        faceRegion.size(),
        0.0,
        0.0,
        cv::INTER_NEAREST);
}

} // namespace video_privacy
