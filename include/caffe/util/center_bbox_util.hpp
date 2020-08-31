#ifndef CAFFE_UTIL_BBOX_UTIL_H_CENTER_HEATMAP_
#define CAFFE_UTIL_BBOX_UTIL_H_CENTER_HEATMAP_

#include <stdint.h>
#include <cmath>  // for std::fabs and std::signbit
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "glog/logging.h"

#include "caffe/caffe.hpp"

namespace caffe {
typedef struct _YoloScoreShow{
      float avg_iou;
      float recall;
      float recall75;
      float avg_cat;
      float avg_obj;
      float avg_anyobj;
      int count;
      int class_count;
}YoloScoreShow;


#define NMS_UNION 1
#define NMS_MIN  2

template <typename Dtype>
void EncodeTruthAndPredictions(Dtype* gt_loc_offest_data, Dtype* pred_loc_offest_data,
                                Dtype* gt_loc_wh_data, Dtype* pred_loc_wh_data,
                                Dtype* gt_lm_data, Dtype* pred_lm_data,
                                const int output_width, const int output_height, 
                                bool share_location, const Dtype* channel_loc_data,
                                const int num_channels, 
                                const std::map<int, vector<std::pair<NormalizedBBox, AnnoFaceLandmarks> > >& all_gt_bboxes, 
                                bool has_lm);


template <typename Dtype>
void CopyDiffToBottom(const Dtype* pre_offset_diff, const Dtype* pre_wh_diff, const int output_width, 
                        const int output_height, bool has_lm, const Dtype* lm_pre_diff,
                        bool share_location, Dtype* bottom_diff, const int num_channels,
                        const std::map<int, vector<std::pair<NormalizedBBox, AnnoFaceLandmarks> > >& all_gt_bboxes);

template <typename Dtype>
void get_topK(const Dtype* keep_max_data, const Dtype* loc_data, const int output_height
                , const int output_width, const int channels, const int num_batch
                , std::map<int, std::vector<CenterNetInfo > >* results
                , const int loc_channels, bool has_lm,  Dtype conf_thresh, Dtype nms_thresh);      


template <typename Dtype>
void _nms_heatmap(const Dtype* conf_data, Dtype* keep_max_data, const int output_height
                  , const int output_width, const int channels, const int num_batch);

template <typename Dtype>
void GenerateBatchHeatmap(const std::map<int, vector<std::pair<NormalizedBBox, AnnoFaceLandmarks> > >& all_gt_bboxes, Dtype* gt_heatmap, 
                            const int num_classes_, const int output_width, const int output_height);


void hard_nms(std::vector<CenterNetInfo>& input, std::vector<CenterNetInfo>* output, float nmsthreshold = 0.3,
                              int type=NMS_UNION);


void soft_nms(std::vector<CenterNetInfo>& input, std::vector<CenterNetInfo>* output, 
                        float sigma=0.5, float Nt=0.5, 
                        float threshold=0.001, unsigned int type=2);


template <typename Dtype>
void EncodeYoloObject(const int batch_size, const int num_channels, const int num_classes,
                          const int output_width, const int output_height, 
                          const int net_width, const int net_height,
                          Dtype* channel_pred_data,
                          const std::map<int, vector<std::pair<NormalizedBBox, AnnoFaceLandmarks> > >& all_gt_bboxes,
                          const std::vector<int> mask_bias, std::vector<std::pair<Dtype, Dtype> >& bias_scale, 
                          Dtype* bottom_diff, Dtype ignore_thresh, YoloScoreShow *Score);

template <typename Dtype>
void GetYoloGroundTruth(const Dtype* gt_data, int num_gt,
                    const int background_label_id, const bool use_difficult_gt, bool has_lm,
                    std::map<int, vector<std::pair<NormalizedBBox, AnnoFaceLandmarks> > >* all_gt_bboxes, 
                    int batch_size);

template <typename Dtype>
Dtype EncodeCenterGridObjectSigmoidLoss(const int batch_size, const int num_channels, const int num_classes,
                          const int output_width, const int output_height, 
                          const float downRatio,
                          Dtype* channel_pred_data, const int anchor_scale, 
                          std::pair<int, int> loc_truth_scale,
                          const std::map<int, vector<std::pair<NormalizedBBox, AnnoFaceLandmarks> > >& all_gt_bboxes,
                          Dtype* class_label, Dtype* bottom_diff, 
                          Dtype ignore_thresh, int *count_postive, Dtype *loc_loss_value);

template <typename Dtype>
void GetCenterGridObjectResultSigmoid(const int batch_size, const int num_channels, const int num_classes,
                          const int output_width, const int output_height, 
                          const float downRatio,
                          Dtype* channel_pred_data, const int anchor_scale, Dtype conf_thresh, 
                          std::map<int, std::vector<CenterNetInfo > >* results);

template <typename Dtype> 
Dtype EncodeCenterGridObjectSoftMaxLoss(const int batch_size, const int num_channels, const int num_classes,
                          const int output_width, const int output_height, 
                          const float downRatio, std::vector<int>postive_batch,
                          std::vector<Dtype> batch_sample_loss,
                          Dtype* channel_pred_data, const int anchor_scale, 
                          std::pair<int, int> loc_truth_scale,
                          const std::map<int, vector<std::pair<NormalizedBBox, AnnoFaceLandmarks> > >& all_gt_bboxes,
                          Dtype* class_label, Dtype* bottom_diff, 
                          int *count_postive, Dtype *loc_loss_value, int *match_num_gt_box, bool has_lm, Dtype* lm_loss_value);

template <typename Dtype> 
void GetCenterGridObjectResultSoftMax(const int batch_size, const int num_channels, const int num_classes,
                          const int output_width, const int output_height, 
                          const float downRatio,
                          Dtype* channel_pred_data, const int anchor_scale, Dtype conf_thresh, 
                          std::map<int, std::vector<CenterNetInfo > >* results, bool has_lm);

template <typename Dtype> 
Dtype EncodeOverlapObjectSigmoidLoss(const int batch_size, const int num_channels, const int num_classes,
                          const int output_width, const int output_height, 
                          const int downRatio,
                          Dtype* channel_pred_data, const int anchor_scale, 
                          std::pair<int, int> loc_truth_scale,
                          const std::map<int, vector<NormalizedBBox> >& all_gt_bboxes,
                          Dtype* class_label, Dtype* bottom_diff, 
                          Dtype ignore_thresh, int *count_postive, Dtype *loc_loss_value);

template <typename Dtype>
void GetCenterOverlapResultSigmoid(const int batch_size, const int num_channels, const int num_classes,
                          const int output_width, const int output_height, 
                          const int downRatio,
                          Dtype* channel_pred_data, const int anchor_scale, Dtype conf_thresh, 
                          std::map<int, std::vector<CenterNetInfo > >* results);

}  // namespace caffe

#endif  // CAFFE_UTIL_BBOX_UTIL_H_
