#include <algorithm>
#include <fstream>  // NOLINT(readability/streams)
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "boost/filesystem.hpp"
#include "boost/foreach.hpp"

#include "caffe/layers/detection_centernet_output_layer.hpp"

namespace caffe {

bool compare_score(CenterNetInfo a, CenterNetInfo b){
    return a.score() > b.score();
}

template <typename Dtype>
void CenternetDetectionOutputLayer<Dtype>::LayerSetUp(const vector<Blob<Dtype>*>& bottom,
      const vector<Blob<Dtype>*>& top) {
    const DetectionOutputParameter& detection_output_param =
        this->layer_param_.detection_output_param();
    CHECK(detection_output_param.has_num_classes()) << "Must specify num_classes";
    num_classes_ = detection_output_param.num_classes();
    share_location_ = detection_output_param.share_location();
    num_loc_classes_ = share_location_ ? 1 : num_classes_;

    keep_top_k_ = detection_output_param.keep_top_k();
    confidence_threshold_ = detection_output_param.has_confidence_threshold() ?
        detection_output_param.confidence_threshold() : -FLT_MAX;
    nms_thresh_ = detection_output_param.nms_thresh();
    has_lm_ = detection_output_param.has_lm();
}

template <typename Dtype>
void CenternetDetectionOutputLayer<Dtype>::Reshape(const vector<Blob<Dtype>*>& bottom,
      const vector<Blob<Dtype>*>& top) {
  
    //CHECK_EQ(bottom[0]->num(), bottom[1]->num());
    //CHECK_EQ(bottom[0]->channels(), bottom[1]->channels());
    CHECK_EQ(bottom[1]->channels() , num_classes_ - 1); // add background to classes
    if(has_lm_){
        CHECK_EQ(bottom[0]->channels(), 14);
        vector<int> top_shape(2, 1);
        top_shape.push_back(1);
        top_shape.push_back(17);
        top[0]->Reshape(top_shape);
    }else{
        CHECK_EQ(bottom[0]->channels(), 4);
        vector<int> top_shape(2, 1);
        top_shape.push_back(1);
        top_shape.push_back(7);
        top[0]->Reshape(top_shape);
    }
}

template <typename Dtype>
void CenternetDetectionOutputLayer<Dtype>::Forward_cpu(
    const vector<Blob<Dtype>*>& bottom, const vector<Blob<Dtype>*>& top) {
    const Dtype* loc_data = bottom[0]->cpu_data();
    const int output_height = bottom[0]->height();
    const int output_width = bottom[0]->width();
    num_ =  bottom[0]->num();
    int loc_channels = bottom[0]->channels();
    const Dtype* conf_data = bottom[1]->cpu_data();
    const int classes = bottom[1]->channels();
    results_.clear();
    get_topK(conf_data, loc_data, output_height, output_width, classes, num_, &results_, loc_channels,
                        has_lm_,
                        confidence_threshold_, nms_thresh_);

    int num_kept = 0;

    std::map<int, vector<CenterNetInfo > > ::iterator iter;
    int count = 0;
    #if 1
    for(iter = results_.begin(); iter != results_.end(); iter++){
        int num_det = iter->second.size();
        if(keep_top_k_ > 0 && num_det > keep_top_k_){
            std::sort(iter->second.begin(), iter->second.end(), compare_score);
            iter->second.resize(keep_top_k_);
            num_kept += keep_top_k_;
        }else{
            num_kept += num_det;
        }
    }
    #else
    for(iter = results_.begin(); iter != results_.end(); iter++){
        int num_det = iter->second.size();
        num_kept += num_det;
    }
    #endif
    vector<int> top_shape(2, 1);
    top_shape.push_back(num_kept);
    if(has_lm_)
        top_shape.push_back(17);
    else
        top_shape.push_back(7);
    Dtype* top_data;
    if (num_kept == 0) {
        top_shape[2] = num_;
        top[0]->Reshape(top_shape);
        top_data = top[0]->mutable_cpu_data();
        caffe_set<Dtype>(top[0]->count(), -1, top_data);
        for (int i = 0; i < num_; ++i) {
            top_data[0] = i;
            if(has_lm_)
                top_data += 17;
            else
                top_data += 7;
        }
    } else {
        top[0]->Reshape(top_shape);
        top_data = top[0]->mutable_cpu_data();
    }
  
    for(int i = 0; i < num_; i++){
        if(results_.find(i) != results_.end()){
            std::vector<CenterNetInfo > result_temp = results_.find(i)->second;
            for(unsigned j = 0; j < result_temp.size(); ++j){
                if(has_lm_){
                    top_data[count * 17] = i;
                    top_data[count * 17 + 1] = result_temp[j].class_id() + 1;
                    top_data[count * 17 + 2] = result_temp[j].score();
                    top_data[count * 17 + 3] = result_temp[j].xmin();
                    top_data[count * 17 + 4] = result_temp[j].ymin();
                    top_data[count * 17 + 5] = result_temp[j].xmax();
                    top_data[count * 17 + 6] = result_temp[j].ymax();
                    top_data[count * 17 + 7] = result_temp[j].marks().lefteye().x();
                    top_data[count * 17 + 8] = result_temp[j].marks().lefteye().y();
                    top_data[count * 17 + 9] = result_temp[j].marks().righteye().x();
                    top_data[count * 17 + 10] = result_temp[j].marks().righteye().y();
                    top_data[count * 17 + 11] = result_temp[j].marks().nose().x();
                    top_data[count * 17 + 12] = result_temp[j].marks().nose().y();
                    top_data[count * 17 + 13] = result_temp[j].marks().leftmouth().x();
                    top_data[count * 17 + 14] = result_temp[j].marks().leftmouth().y();
                    top_data[count * 17 + 15] = result_temp[j].marks().rightmouth().x();
                    top_data[count * 17 + 16] = result_temp[j].marks().rightmouth().y();
                }else{
                    top_data[count * 7] = i;
                    top_data[count * 7 + 1] = result_temp[j].class_id() + 1;
                    top_data[count * 7 + 2] = result_temp[j].score();
                    top_data[count * 7 + 3] = result_temp[j].xmin();
                    top_data[count * 7 + 4] = result_temp[j].ymin();
                    top_data[count * 7 + 5] = result_temp[j].xmax();
                    top_data[count * 7 + 6] = result_temp[j].ymax();
                }
                ++count;
            }
        }
    }
}

#ifdef CPU_ONLY
STUB_GPU_FORWARD(CenternetDetectionOutputLayer, Forward);
#endif

INSTANTIATE_CLASS(CenternetDetectionOutputLayer);
REGISTER_LAYER_CLASS(CenternetDetectionOutput);

}  // namespace caffe
