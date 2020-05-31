#ifndef CAFFE_DETECTION_OUTPUT_LAYER_HPP_
#define CAFFE_DETECTION_OUTPUT_LAYER_HPP_

#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/regex.hpp>

#include <map>
#include <string>
#include <utility>
#include <vector>

#include "caffe/blob.hpp"
#include "caffe/data_transformer.hpp"
#include "caffe/layer.hpp"
#include "caffe/proto/caffe.pb.h"
#include "caffe/util/bbox_util.hpp"
#include "caffe/util/center_bbox_util.hpp"

using namespace boost::property_tree;  // NOLINT(build/namespaces)

namespace caffe {

/**
 * @brief Generate the detection output based on location and confidence
 * predictions by doing non maximum suppression.
 *
 * Intended for use with MultiBox detection method.
 *
 * NOTE: does not implement Backwards operation.
 */
template <typename Dtype>
class CenterGridOutputLayer : public Layer<Dtype> {
public:
    explicit CenterGridOutputLayer(const LayerParameter& param)
        : Layer<Dtype>(param) {}
    virtual void LayerSetUp(const vector<Blob<Dtype>*>& bottom,
        const vector<Blob<Dtype>*>& top);
    virtual void Reshape(const vector<Blob<Dtype>*>& bottom,
        const vector<Blob<Dtype>*>& top);

    virtual inline const char* type() const { return "CenterGridOutput"; }
    virtual inline int MinBottomBlobs() const { return 3; }
    virtual inline int ExactNumBottomBlobs() const { return -1; }
    virtual inline int ExactNumTopBlobs() const { return 1; }

protected:
    /**
     * @brief Do non maximum suppression (nms) on prediction results.
     *
     * @param bottom input Blob vector (at least 2)
     *   -# @f$ (N \times C1 \times 1 \times 1) @f$
     *      the location predictions with C1 predictions.
     *   -# @f$ (N \times C2 \times 1 \times 1) @f$
     *      the confidence predictions with C2 predictions.
     *   -# @f$ (N \times 2 \times C3 \times 1) @f$
     *      the prior bounding boxes with C3 values.
     * @param top output Blob vector (length 1)
     *   -# @f$ (1 \times 1 \times N \times 7) @f$
     *      N is the number of detections after nms, and each row is:
     *      [image_id, label, confidence, xmin, ymin, xmax, ymax]
     */
    virtual void Forward_cpu(const vector<Blob<Dtype>*>& bottom,
        const vector<Blob<Dtype>*>& top);
    /// @brief Not implemented
    virtual void Backward_cpu(const vector<Blob<Dtype>*>& top,
        const vector<bool>& propagate_down, const vector<Blob<Dtype>*>& bottom) {
    NOT_IMPLEMENTED;
    }
    int num_classes_;
    std::vector<int> anchor_scale_;
    int num_loc_classes_;
    int keep_top_k_;
    Dtype confidence_threshold_;
    int num_;
    std::map<int, std::vector<CenterNetInfo> > results_;
    int bottom_size_;
    std::vector<int> downRatio_;
    float nms_thresh_ ;
    DetectionOutputParameter_CLASS_TYPE class_type_;
    bool has_lm_;
};

}  // namespace caffe

#endif  // CAFFE_DETECTION_OUTPUT_LAYER_HPP_
