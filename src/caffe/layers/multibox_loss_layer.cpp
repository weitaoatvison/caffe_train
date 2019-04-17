#include <algorithm>
#include <map>
#include <utility>
#include <vector>

#include "caffe/layers/multibox_loss_layer.hpp"
#include "caffe/util/math_functions.hpp"

namespace caffe {

template <typename Dtype>
void MultiBoxLossLayer<Dtype>::LayerSetUp(const vector<Blob<Dtype>*>& bottom,
      const vector<Blob<Dtype>*>& top) {
  num_cnt_ = 0;
  LossLayer<Dtype>::LayerSetUp(bottom, top);
  if (this->layer_param_.propagate_down_size() == 0) {
    this->layer_param_.add_propagate_down(true);
    this->layer_param_.add_propagate_down(true);
    this->layer_param_.add_propagate_down(false);
    this->layer_param_.add_propagate_down(true);
    this->layer_param_.add_propagate_down(true);
    this->layer_param_.add_propagate_down(false);
  }
  const MultiBoxLossParameter& multibox_loss_param =
      this->layer_param_.multibox_loss_param();
  multibox_loss_param_ = this->layer_param_.multibox_loss_param();

  num_ = bottom[0]->num();
  num_priors_ = bottom[2]->height() / 4;
  // Get other parameters.
  CHECK(multibox_loss_param.has_num_classes()) << "Must provide num_classes.";
  CHECK(multibox_loss_param.has_num_blur()) << "Must prodived num_blur";
  CHECK(multibox_loss_param.has_num_occlusion()) << "Must provide num_occlusson";
  num_classes_ = multibox_loss_param.num_classes();
  num_blur_ = multibox_loss_param.num_blur();
  num_occlusion_ = multibox_loss_param.num_occlusion();
  CHECK_GE(num_classes_, 1) << "num_classes should not be less than 1.";
  share_location_ = multibox_loss_param.share_location();
  loc_classes_ = share_location_ ? 1 : num_classes_;
  background_label_id_ = multibox_loss_param.background_label_id();
  use_difficult_gt_ = multibox_loss_param.use_difficult_gt();
  mining_type_ = multibox_loss_param.mining_type();
  if (multibox_loss_param.has_do_neg_mining()) {
    LOG(WARNING) << "do_neg_mining is deprecated, use mining_type instead.";
    do_neg_mining_ = multibox_loss_param.do_neg_mining();
    CHECK_EQ(do_neg_mining_,
             mining_type_ != MultiBoxLossParameter_MiningType_NONE);
  }
  do_neg_mining_ = mining_type_ != MultiBoxLossParameter_MiningType_NONE;

  if (!this->layer_param_.loss_param().has_normalization() &&
      this->layer_param_.loss_param().has_normalize()) {
    normalization_ = this->layer_param_.loss_param().normalize() ?
                     LossParameter_NormalizationMode_VALID :
                     LossParameter_NormalizationMode_BATCH_SIZE;
  } else {
    normalization_ = this->layer_param_.loss_param().normalization();
  }

  if (do_neg_mining_) {
    CHECK(share_location_)
        << "Currently only support negative mining if share_location is true.";
  }

  vector<int> loss_shape(1, 1);
  // Set up localization loss layer.
  loc_weight_ = multibox_loss_param.loc_weight();
  loc_loss_type_ = multibox_loss_param.loc_loss_type();
  // fake shape.
  vector<int> loc_shape(1, 1);
  loc_shape.push_back(4); //loc_shape:{ 1, 4}
  loc_pred_.Reshape(loc_shape); //loc_shape:{ }
  loc_gt_.Reshape(loc_shape);
  loc_bottom_vec_.push_back(&loc_pred_);
  loc_bottom_vec_.push_back(&loc_gt_);
  loc_loss_.Reshape(loss_shape);
  loc_top_vec_.push_back(&loc_loss_);
  if (loc_loss_type_ == MultiBoxLossParameter_LocLossType_L2) {
    LayerParameter layer_param;
    layer_param.set_name(this->layer_param_.name() + "_l2_loc");
    layer_param.set_type("EuclideanLoss");
    layer_param.add_loss_weight(loc_weight_);
    loc_loss_layer_ = LayerRegistry<Dtype>::CreateLayer(layer_param);
    loc_loss_layer_->SetUp(loc_bottom_vec_, loc_top_vec_);
  } else if (loc_loss_type_ == MultiBoxLossParameter_LocLossType_SMOOTH_L1) {
    LayerParameter layer_param;
    layer_param.set_name(this->layer_param_.name() + "_smooth_L1_loc");
    layer_param.set_type("SmoothL1Loss");
    layer_param.add_loss_weight(loc_weight_);
    loc_loss_layer_ = LayerRegistry<Dtype>::CreateLayer(layer_param);
    loc_loss_layer_->SetUp(loc_bottom_vec_, loc_top_vec_);
  } else {
    LOG(FATAL) << "Unknown localization loss type.";
  }
  // Set up confidence loss layer.
  conf_loss_type_ = multibox_loss_param.conf_loss_type();
  conf_bottom_vec_.push_back(&conf_pred_);
  conf_bottom_vec_.push_back(&conf_gt_);
  conf_loss_.Reshape(loss_shape);
  conf_top_vec_.push_back(&conf_loss_);
  if (conf_loss_type_ == MultiBoxLossParameter_ConfLossType_SOFTMAX) {
    CHECK_GE(background_label_id_, 0)
        << "background_label_id should be within [0, num_classes) for Softmax.";
    CHECK_LT(background_label_id_, num_classes_)
        << "background_label_id should be within [0, num_classes) for Softmax.";
    LayerParameter layer_param;
    layer_param.set_name(this->layer_param_.name() + "_softmax_conf");
    layer_param.set_type("SoftmaxWithLoss");
    layer_param.add_loss_weight(Dtype(1.));
    layer_param.mutable_loss_param()->set_normalization(
        LossParameter_NormalizationMode_NONE);
    SoftmaxParameter* softmax_param = layer_param.mutable_softmax_param();
    softmax_param->set_axis(1);
    // Fake reshape.
    vector<int> conf_shape(1, 1);
    conf_gt_.Reshape(conf_shape);
    conf_shape.push_back(num_classes_);
    conf_pred_.Reshape(conf_shape);
    conf_loss_layer_ = LayerRegistry<Dtype>::CreateLayer(layer_param);
    conf_loss_layer_->SetUp(conf_bottom_vec_, conf_top_vec_);
  } else if (conf_loss_type_ == MultiBoxLossParameter_ConfLossType_LOGISTIC) {
    LayerParameter layer_param;
    layer_param.set_name(this->layer_param_.name() + "_logistic_conf");
    layer_param.set_type("SigmoidCrossEntropyLoss");
    layer_param.add_loss_weight(Dtype(1.));
    // Fake reshape.
    vector<int> conf_shape(1, 1);
    conf_shape.push_back(num_classes_);
    conf_gt_.Reshape(conf_shape);
    conf_pred_.Reshape(conf_shape);
    conf_loss_layer_ = LayerRegistry<Dtype>::CreateLayer(layer_param);
    conf_loss_layer_->SetUp(conf_bottom_vec_, conf_top_vec_);
  } else {
    LOG(FATAL) << "Unknown confidence loss type.";
  }
  // Set up blur confidence loss layer.
  conf_blur_loss_type_ = multibox_loss_param.conf_blur_loss_type();
  conf_blur_bottom_vec_.push_back(&conf_blur_pred_);
  conf_blur_bottom_vec_.push_back(&conf_blur_gt_);
  conf_blur_loss_.Reshape(loss_shape);
  conf_blur_top_vec_.push_back(&conf_blur_loss_);
  if (conf_blur_loss_type_ == MultiBoxLossParameter_ConfLossType_SOFTMAX) {
    LayerParameter layer_param;
    layer_param.set_name(this->layer_param_.name() + "_softmax_blur_conf");
    layer_param.set_type("SoftmaxWithLoss");
    layer_param.add_loss_weight(Dtype(1.));
    layer_param.mutable_loss_param()->set_normalization(
        LossParameter_NormalizationMode_NONE);
    SoftmaxParameter* softmax_param = layer_param.mutable_softmax_param();
    softmax_param->set_axis(1);
    // Fake reshape.
    vector<int> conf_blur_shape(1, 1);
    conf_blur_gt_.Reshape(conf_blur_shape);
    conf_blur_shape.push_back(num_blur_);
    conf_blur_pred_.Reshape(conf_blur_shape);
    conf_blur_loss_layer_ = LayerRegistry<Dtype>::CreateLayer(layer_param);
    conf_blur_loss_layer_->SetUp(conf_blur_bottom_vec_, conf_blur_top_vec_);
  } else if (conf_loss_type_ == MultiBoxLossParameter_ConfLossType_LOGISTIC) {
    LayerParameter layer_param;
    layer_param.set_name(this->layer_param_.name() + "_logistic_blur_conf");
    layer_param.set_type("SigmoidCrossEntropyLoss");
    layer_param.add_loss_weight(Dtype(1.));
    // Fake reshape.
    vector<int> conf_blur_shape(1, 1);
    conf_blur_shape.push_back(num_blur_);
    conf_blur_gt_.Reshape(conf_blur_shape);
    conf_blur_pred_.Reshape(conf_blur_shape);
    conf_blur_loss_layer_ = LayerRegistry<Dtype>::CreateLayer(layer_param);
    conf_blur_loss_layer_->SetUp(conf_blur_bottom_vec_, conf_blur_top_vec_);
  } else {
    LOG(FATAL) << "Unknown confidence loss type.";
  }
  // Set up occl confidence loss layer.
  conf_occlussion_loss_type_ = multibox_loss_param.conf_occlu_loss_type();
  conf_occlussion_bottom_vec_.push_back(&conf_occlussion_pred_);
  conf_occlussion_bottom_vec_.push_back(&conf_occlussion_gt_);
  conf_occlussion_loss_.Reshape(loss_shape);
  conf_occlussion_top_vec_.push_back(&conf_occlussion_loss_);
  if (conf_occlussion_loss_type_ == MultiBoxLossParameter_ConfLossType_SOFTMAX) {
    LayerParameter layer_param;
    layer_param.set_name(this->layer_param_.name() + "_softmax_occlu_conf");
    layer_param.set_type("SoftmaxWithLoss");
    layer_param.add_loss_weight(Dtype(1.));
    layer_param.mutable_loss_param()->set_normalization(
        LossParameter_NormalizationMode_NONE);
    SoftmaxParameter* softmax_param = layer_param.mutable_softmax_param();
    softmax_param->set_axis(1);
    // Fake reshape.
    vector<int> conf_occlu_shape(1, 1);
    conf_occlussion_gt_.Reshape(conf_occlu_shape);
    conf_occlu_shape.push_back(num_occlusion_);
    conf_occlussion_pred_.Reshape(conf_occlu_shape);
    conf_occlussion_loss_layer_ = LayerRegistry<Dtype>::CreateLayer(layer_param);
    conf_occlussion_loss_layer_->SetUp(conf_occlussion_bottom_vec_, conf_occlussion_top_vec_);
  } else if (conf_occlussion_loss_type_ == MultiBoxLossParameter_ConfLossType_LOGISTIC) {
    LayerParameter layer_param;
    layer_param.set_name(this->layer_param_.name() + "_logistic_occlu_conf");
    layer_param.set_type("SigmoidCrossEntropyLoss");
    layer_param.add_loss_weight(Dtype(1.));
    // Fake reshape.
    vector<int> conf_occlu_shape(1, 1);
    conf_occlu_shape.push_back(num_occlusion_);
    conf_occlussion_gt_.Reshape(conf_occlu_shape);
    conf_occlussion_pred_.Reshape(conf_occlu_shape);
    conf_occlussion_loss_layer_ = LayerRegistry<Dtype>::CreateLayer(layer_param);
    conf_occlussion_loss_layer_->SetUp(conf_occlussion_bottom_vec_, conf_occlussion_top_vec_);
  } else {
    LOG(FATAL) << "Unknown confidence loss type.";
  }
}

template <typename Dtype>
void MultiBoxLossLayer<Dtype>::Reshape(const vector<Blob<Dtype>*>& bottom,
      const vector<Blob<Dtype>*>& top) {
  LossLayer<Dtype>::Reshape(bottom, top);
  num_ = bottom[0]->num();
  num_priors_ = bottom[2]->height() / 4;
  num_gt_ = bottom[5]->height();
  CHECK_EQ(bottom[0]->num(), bottom[1]->num());
  CHECK_EQ(num_priors_ * loc_classes_ * 4, bottom[0]->channels())
      << "Number of priors must match number of location predictions.";
  CHECK_EQ(num_priors_ * num_classes_, bottom[1]->channels())
      << "Number of priors must match number of confidence predictions.";
  CHECK_EQ(num_priors_ * num_blur_, bottom[3]->channels())
      << "Number of priors must match number of blur confidence predictions.";
  CHECK_EQ(num_priors_ * num_occlusion_, bottom[4]->channels())
      << "NUmber of priors must match number of occlusion confidence perdictions.";
}

template <typename Dtype>
void MultiBoxLossLayer<Dtype>::Forward_cpu(const vector<Blob<Dtype>*>& bottom,
    const vector<Blob<Dtype>*>& top) {
  const Dtype* loc_data = bottom[0]->cpu_data();
  const Dtype* conf_data = bottom[1]->cpu_data();
  const Dtype* prior_data = bottom[2]->cpu_data();
  const Dtype* blur_data = bottom[3]->cpu_data();
  const Dtype* occl_data = bottom[4]->cpu_data();
  const Dtype* gt_data = bottom[5]->cpu_data();

  #if 0
  LOG(INFO)<< "loss compute start printf &&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&& num_gt_: "<<num_gt_;
  for(int ii=0; ii < num_gt_; ii++)
  {
    int id = ii*10;
    if (gt_data[id] == -1) {
      continue;
    }

    LOG(INFO) <<"LABEL batch_id: "<<gt_data[id]<<" anno_label: "<<gt_data[id+1]
              <<" anno.instance_id: "<<gt_data[id+2];
    LOG(INFO)  <<"LABEL bbox->xmin: "<<gt_data[id+3]<<" bbox->ymin: "<<gt_data[id+4]
              <<" bbox->xmax: "<<gt_data[id+5]<<" bbox->ymax: "<<gt_data[id+6]
              <<" bbox->blur: "<<gt_data[id+7]<<" bbox->occlusion: "<<gt_data[id+8];
  }
  LOG(INFO)<< "loss compute finished **************************************************** end ";
  
  #endif 

  // Retrieve all ground truth.
  map<int, vector<NormalizedBBox> > all_gt_bboxes;
  GetGroundTruth(gt_data, num_gt_, background_label_id_, use_difficult_gt_,
                 &all_gt_bboxes);

  // Retrieve all prior bboxes. It is same within a batch since we assume all
  // images in a batch are of same dimension.
  vector<NormalizedBBox> prior_bboxes;
  vector<vector<float> > prior_variances;
  GetPriorBBoxes(prior_data, num_priors_, &prior_bboxes, &prior_variances);

  // Retrieve all predictions.
  vector<LabelBBox> all_loc_preds;
  GetLocPredictions(loc_data, num_, num_priors_, loc_classes_, share_location_,
                    &all_loc_preds);

  // Find matches between source bboxes and ground truth bboxes.
  vector<map<int, vector<float> > > all_match_overlaps;
  FindMatches(all_loc_preds, all_gt_bboxes, prior_bboxes, prior_variances,
              multibox_loss_param_, &all_match_overlaps, &all_match_indices_);

  num_matches_ = 0;
  int num_negs = 0;
  // Sample hard negative (and positive) examples based on mining type.
  MineHardExamples(*bottom[1], all_loc_preds, all_gt_bboxes, prior_bboxes,
                   prior_variances, all_match_overlaps, multibox_loss_param_,
                   &num_matches_, &num_negs, &all_match_indices_,
                   &all_neg_indices_);

  if (num_matches_ >= 1) {
    // Form data to pass on to loc_loss_layer_.
    vector<int> loc_shape(2);
    loc_shape[0] = 1;
    loc_shape[1] = num_matches_ * 4;
    loc_pred_.Reshape(loc_shape);
    loc_gt_.Reshape(loc_shape);
    Dtype* loc_pred_data = loc_pred_.mutable_cpu_data();
    Dtype* loc_gt_data = loc_gt_.mutable_cpu_data();
    EncodeLocPrediction(all_loc_preds, all_gt_bboxes, all_match_indices_,
                        prior_bboxes, prior_variances, multibox_loss_param_,
                        loc_pred_data, loc_gt_data);
    loc_loss_layer_->Reshape(loc_bottom_vec_, loc_top_vec_);
    loc_loss_layer_->Forward(loc_bottom_vec_, loc_top_vec_);
  } else {
    loc_loss_.mutable_cpu_data()[0] = 0;
  }

  /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
  // Form data to pass on to conf_loss_layer_.
  if (do_neg_mining_) {
    num_conf_ = num_matches_ + num_negs;
  } else {
    num_conf_ = num_ * num_priors_;
  }
  if (num_conf_ >= 1) {
    /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~calss confidence loss layer~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
    // Reshape the confidence data.
    vector<int> conf_shape;
    if (conf_loss_type_ == MultiBoxLossParameter_ConfLossType_SOFTMAX) {
      conf_shape.push_back(num_conf_);
      conf_gt_.Reshape(conf_shape);
      conf_shape.push_back(num_classes_);
      conf_pred_.Reshape(conf_shape);
    } else if (conf_loss_type_ == MultiBoxLossParameter_ConfLossType_LOGISTIC) {
      conf_shape.push_back(1);
      conf_shape.push_back(num_conf_);
      conf_shape.push_back(num_classes_);
      conf_gt_.Reshape(conf_shape);
      conf_pred_.Reshape(conf_shape);
    } else {
      LOG(FATAL) << "Unknown confidence loss type.";
    }
    if (!do_neg_mining_) {
      // Consider all scores.
      // Share data and diff with bottom[1].
      CHECK_EQ(conf_pred_.count(), bottom[1]->count());
      conf_pred_.ShareData(*(bottom[1]));
    }
    Dtype* conf_pred_data = conf_pred_.mutable_cpu_data();
    Dtype* conf_gt_data = conf_gt_.mutable_cpu_data();
    caffe_set(conf_gt_.count(), Dtype(background_label_id_), conf_gt_data);
    EncodeConfPrediction(conf_data, num_, num_priors_, multibox_loss_param_,
                         all_match_indices_, all_neg_indices_, all_gt_bboxes,
                         conf_pred_data, conf_gt_data);
    conf_loss_layer_->Reshape(conf_bottom_vec_, conf_top_vec_);
    conf_loss_layer_->Forward(conf_bottom_vec_, conf_top_vec_);

     /*~~~~~~~~~~~~~~~~~~~~~~blur loss layer  ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
     // Reshape the blur confidence data.
    vector<int> conf_blur_shape;
    if (conf_blur_loss_type_ == MultiBoxLossParameter_ConfLossType_SOFTMAX) {
      conf_blur_shape.push_back(num_matches_);
      conf_blur_gt_.Reshape(conf_blur_shape);
      conf_blur_shape.push_back(num_blur_);
      conf_blur_pred_.Reshape(conf_blur_shape);
    } else if (conf_blur_loss_type_ == MultiBoxLossParameter_ConfLossType_LOGISTIC) {
      conf_blur_shape.push_back(1);
      conf_blur_shape.push_back(num_matches_);
      conf_blur_shape.push_back(num_blur_);
      conf_blur_gt_.Reshape(conf_blur_shape);
      conf_blur_pred_.Reshape(conf_blur_shape);
    } else {
      LOG(FATAL) << "Unknown confidence loss type.";
    }
    if (!do_neg_mining_) {
      //Share data and diff with bottom[3].
      CHECK_EQ(conf_pred_.count(), bottom[3]->count());
      conf_pred_.ShareData(*(bottom[3]));
    }
    Dtype* conf_blur_pred_data = conf_blur_pred_.mutable_cpu_data();
    Dtype* conf_blur_gt_data = conf_blur_gt_.mutable_cpu_data();
    caffe_set(conf_blur_gt_.count(), Dtype(0), conf_blur_gt_data);
    EncodeBlurConfPrediction(blur_data, num_, num_priors_, multibox_loss_param_,
                         all_match_indices_, all_neg_indices_, all_gt_bboxes,
                         conf_blur_pred_data, conf_blur_gt_data);
    conf_blur_loss_layer_->Reshape(conf_blur_bottom_vec_, conf_blur_top_vec_);
    conf_blur_loss_layer_->Forward(conf_blur_bottom_vec_, conf_blur_top_vec_);

    /*~~~~~~~~~~~~~~~~~~~~~~~occlussion_loss_layer~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
    // conf occlussion layer
    vector<int> conf_occlussion_shape;
    if (conf_occlussion_loss_type_ == MultiBoxLossParameter_ConfLossType_SOFTMAX) {
      conf_occlussion_shape.push_back(num_matches_);
      conf_occlussion_gt_.Reshape(conf_occlussion_shape);
      conf_occlussion_shape.push_back(num_occlusion_);
      conf_occlussion_pred_.Reshape(conf_occlussion_shape);
    } else if (conf_occlussion_loss_type_ == MultiBoxLossParameter_ConfLossType_LOGISTIC) {
      conf_occlussion_shape.push_back(1);
      conf_occlussion_shape.push_back(num_matches_);
      conf_occlussion_shape.push_back(num_occlusion_);
      conf_occlussion_gt_.Reshape(conf_occlussion_shape);
      conf_occlussion_pred_.Reshape(conf_occlussion_shape);
    } else {
      LOG(FATAL) << "Unknown confidence loss type.";
    }
    if (!do_neg_mining_) {
      // Consider all scores.
      //t Share daa and diff with bottom[1].
      CHECK_EQ(conf_occlussion_pred_.count(), bottom[4]->count());
      conf_occlussion_pred_.ShareData(*(bottom[4]));
    }
    Dtype* conf_occl_pred_data = conf_occlussion_pred_.mutable_cpu_data();
    Dtype* conf_occl_gt_data = conf_occlussion_gt_.mutable_cpu_data();
    caffe_set(conf_occlussion_gt_.count(), Dtype(0), conf_occl_gt_data);
    EncodeOcclusConfPrediction(occl_data, num_, num_priors_, multibox_loss_param_,
                         all_match_indices_, all_neg_indices_, all_gt_bboxes,
                         conf_occl_pred_data, conf_occl_gt_data);
    conf_occlussion_loss_layer_->Reshape(conf_occlussion_bottom_vec_, conf_occlussion_top_vec_);
    conf_occlussion_loss_layer_->Forward(conf_occlussion_bottom_vec_, conf_occlussion_top_vec_);
  } else {
    conf_loss_.mutable_cpu_data()[0] = 0;
    conf_blur_loss_.mutable_cpu_data()[0] = 0;
    conf_occlussion_loss_.mutable_cpu_data()[0] = 0;
  }
  /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
  top[0]->mutable_cpu_data()[0] = 0;
  Dtype normalizer = LossLayer<Dtype>::GetNormalizer(
        normalization_, num_, num_priors_, num_matches_);
  if (this->layer_param_.propagate_down(0)) {
    top[0]->mutable_cpu_data()[0] +=
        loc_weight_ * loc_loss_.cpu_data()[0] / normalizer;
  }
  if (this->layer_param_.propagate_down(1)) {
    top[0]->mutable_cpu_data()[0] += 
          conf_loss_.cpu_data()[0] / normalizer;
  }
  if(this->layer_param_.propagate_down(3)) {
    top[0]->mutable_cpu_data()[0] += 
          0.5*conf_blur_loss_.cpu_data()[0] / normalizer;
  }
  if(this->layer_param_.propagate_down(4)) {
    top[0]->mutable_cpu_data()[0] += 
          0.5*conf_occlussion_loss_.cpu_data()[0] / normalizer;
  }
  #if 0
  LOG(INFO)<<"num_matches_: "<<num_matches_<<" num_gtBoxes: "<<num_gt_<<" num_conf_: "<<num_conf_;
  LOG(INFO)<<"origin loc_loss_: "<< loc_loss_.cpu_data()[0];
  LOG(INFO)<<"origin conf_loss_: "<<conf_loss_.cpu_data()[0];
  LOG(INFO)<<"origin conf_blur_loss_: "<<conf_blur_loss_.cpu_data()[0];
  LOG(INFO)<<"origin conf_occlussion_loss_: " <<conf_occlussion_loss_.cpu_data()[0];
  LOG(INFO)<<"total ~~~~~~~~~~~~~~~~~~loss: "<<top[0]->mutable_cpu_data()[0]<<" normalizer: "<<normalizer;
  #endif
}

template <typename Dtype>
void MultiBoxLossLayer<Dtype>::Backward_cpu(const vector<Blob<Dtype>*>& top,
    const vector<bool>& propagate_down,
    const vector<Blob<Dtype>*>& bottom) {
  if (propagate_down[2]) {
    LOG(FATAL) << this->type()
        << " Layer cannot backpropagate to prior inputs.";
  }
  if (propagate_down[5]) {
    LOG(FATAL) << this->type()
        << " Layer cannot backpropagate to label inputs.";
  }
  // Back propagate on location prediction.
  if (propagate_down[0]) {
    Dtype* loc_bottom_diff = bottom[0]->mutable_cpu_diff();
    caffe_set(bottom[0]->count(), Dtype(0), loc_bottom_diff);
    if (num_matches_ >= 1) {
      vector<bool> loc_propagate_down;
      // Only back propagate on prediction, not ground truth.
      loc_propagate_down.push_back(true);
      loc_propagate_down.push_back(false);
      loc_loss_layer_->Backward(loc_top_vec_, loc_propagate_down,
                                loc_bottom_vec_);
      // Scale gradient.
      Dtype normalizer = LossLayer<Dtype>::GetNormalizer(
          normalization_, num_, num_priors_, num_matches_);
      Dtype loss_weight = top[0]->cpu_diff()[0] / normalizer;
      caffe_scal(loc_pred_.count(), loss_weight, loc_pred_.mutable_cpu_diff());
      // Copy gradient back to bottom[0].
      const Dtype* loc_pred_diff = loc_pred_.cpu_diff();
      int count = 0;
      for (int i = 0; i < num_; ++i) {
        for (map<int, vector<int> >::iterator it =
             all_match_indices_[i].begin();
             it != all_match_indices_[i].end(); ++it) {
          const int label = share_location_ ? 0 : it->first;
          const vector<int>& match_index = it->second;
          for (int j = 0; j < match_index.size(); ++j) {
            if (match_index[j] <= -1) {
              continue;
            }
            // Copy the diff to the right place.
            int start_idx = loc_classes_ * 4 * j + label * 4;
            caffe_copy<Dtype>(4, loc_pred_diff + count * 4,
                              loc_bottom_diff + start_idx);
            ++count;
          }
        }
        loc_bottom_diff += bottom[0]->offset(1);
      }
    }
  }

  // Back propagate on confidence prediction.
  if (propagate_down[1]) {
    Dtype* conf_bottom_diff = bottom[1]->mutable_cpu_diff();
    caffe_set(bottom[1]->count(), Dtype(0), conf_bottom_diff);
    if (num_conf_ >= 1) {
      vector<bool> conf_propagate_down;
      // Only back propagate on prediction, not ground truth.
      conf_propagate_down.push_back(true);
      conf_propagate_down.push_back(false);
      conf_loss_layer_->Backward(conf_top_vec_, conf_propagate_down,
                                 conf_bottom_vec_);
      // Scale gradient.
      Dtype normalizer = LossLayer<Dtype>::GetNormalizer(
          normalization_, num_, num_priors_, num_matches_);
      Dtype loss_weight = top[0]->cpu_diff()[0] / normalizer;
      caffe_scal(conf_pred_.count(), loss_weight,
                 conf_pred_.mutable_cpu_diff());
      // Copy gradient back to bottom[1].
      const Dtype* conf_pred_diff = conf_pred_.cpu_diff();
      if (do_neg_mining_) {
        int count = 0;
        for (int i = 0; i < num_; ++i) {
          // Copy matched (positive) bboxes scores' diff.
          const map<int, vector<int> >& match_indices = all_match_indices_[i];
          for (map<int, vector<int> >::const_iterator it =
               match_indices.begin(); it != match_indices.end(); ++it) {
            const vector<int>& match_index = it->second;
            CHECK_EQ(match_index.size(), num_priors_);
            for (int j = 0; j < num_priors_; ++j) {
              if (match_index[j] <= -1) {
                continue;
              }
              // Copy the diff to the right place.
              caffe_copy<Dtype>(num_classes_,
                                conf_pred_diff + count * num_classes_,
                                conf_bottom_diff + j * num_classes_);
              ++count;
            }
          }
          // Copy negative bboxes scores' diff.
          for (int n = 0; n < all_neg_indices_[i].size(); ++n) {
            int j = all_neg_indices_[i][n];
            CHECK_LT(j, num_priors_);
            caffe_copy<Dtype>(num_classes_,
                              conf_pred_diff + count * num_classes_,
                              conf_bottom_diff + j * num_classes_);
            ++count;
          }
          conf_bottom_diff += bottom[1]->offset(1);
        }
      } else {
        bottom[1]->ShareDiff(conf_pred_);
      }
    }
  }

  // Back propagate on blur prediction.
  if (propagate_down[3]) {
    Dtype* conf_blur_bottom_diff = bottom[3]->mutable_cpu_diff();
    caffe_set(bottom[3]->count(), Dtype(0), conf_blur_bottom_diff);
    if (num_conf_ >= 1) {
      vector<bool> conf_blur_propagate_down;
      // Only back propagate on prediction, not ground truth.
      conf_blur_propagate_down.push_back(true);
      conf_blur_propagate_down.push_back(false);
      conf_blur_loss_layer_->Backward(conf_blur_top_vec_, conf_blur_propagate_down,
                                 conf_blur_bottom_vec_);
      // Scale gradient.
      Dtype normalizer = LossLayer<Dtype>::GetNormalizer(
          normalization_, num_, num_priors_, num_matches_);
      Dtype loss_weight = top[0]->cpu_diff()[0] / normalizer;
      caffe_scal(conf_blur_pred_.count(), loss_weight,
                 conf_blur_pred_.mutable_cpu_diff());
      // Copy gradient back to bottom[1].
      const Dtype* conf_blur_pred_diff = conf_blur_pred_.cpu_diff();
      if (do_neg_mining_) {
        int count = 0;
        for (int i = 0; i < num_; ++i) {
          // Copy matched (positive) bboxes scores' diff.
          const map<int, vector<int> >& match_indices = all_match_indices_[i];
          for (map<int, vector<int> >::const_iterator it =
               match_indices.begin(); it != match_indices.end(); ++it) {
            const vector<int>& match_index = it->second;
            CHECK_EQ(match_index.size(), num_priors_);
            for (int j = 0; j < num_priors_; ++j) {
              if (match_index[j] <= -1) {
                continue;
              }
              // Copy the diff to the right place.
              caffe_copy<Dtype>(num_blur_,
                                conf_blur_pred_diff + count * num_blur_,
                                conf_blur_bottom_diff + j * num_blur_);
              ++count;
            }
          }
          conf_blur_bottom_diff += bottom[3]->offset(1);
        }
      } else {
        // The diff is already computed and stored.
        bottom[3]->ShareDiff(conf_blur_pred_);
      }
    }
  }

  // Back propagate on occlussion prediction.
  if (propagate_down[4]) {
    Dtype* conf_occl_bottom_diff = bottom[4]->mutable_cpu_diff();
    caffe_set(bottom[4]->count(), Dtype(0), conf_occl_bottom_diff);
    if (num_conf_ >= 1) {
      vector<bool> conf_occl_propagate_down;
      // Only back propagate on prediction, not ground truth.
      conf_occl_propagate_down.push_back(true);
      conf_occl_propagate_down.push_back(false);
      conf_occlussion_loss_layer_->Backward(conf_occlussion_top_vec_, conf_occl_propagate_down,
                                 conf_occlussion_bottom_vec_);
      // Scale gradient.
      Dtype normalizer = LossLayer<Dtype>::GetNormalizer(
          normalization_, num_, num_priors_, num_matches_);
      Dtype loss_weight = top[0]->cpu_diff()[0] / normalizer;
      caffe_scal(conf_occlussion_pred_.count(), loss_weight,
                 conf_occlussion_pred_.mutable_cpu_diff());
      // Copy gradient back to bottom[4].
      const Dtype* conf_occl_pred_diff = conf_occlussion_pred_.cpu_diff();
      if (do_neg_mining_) {
        int count = 0;
        for (int i = 0; i < num_; ++i) {
          // Copy matched (positive) bboxes scores' diff.
          const map<int, vector<int> >& match_indices = all_match_indices_[i];
          for (map<int, vector<int> >::const_iterator it =
               match_indices.begin(); it != match_indices.end(); ++it) {
            const vector<int>& match_index = it->second;
            CHECK_EQ(match_index.size(), num_priors_);
            for (int j = 0; j < num_priors_; ++j) {
              if (match_index[j] <= -1) {
                continue;
              }
              // Copy the diff to the right place.
              caffe_copy<Dtype>(num_occlusion_,
                                conf_occl_pred_diff + count * num_occlusion_,
                                conf_occl_bottom_diff + j * num_occlusion_);
              ++count;
            }
          }
          conf_occl_bottom_diff += bottom[4]->offset(1);
        }
      } else {
        // The diff is already computed and stored.
        bottom[4]->ShareDiff(conf_occlussion_pred_);
      }
    }
  }
  // After backward, remove match statistics.
  all_match_indices_.clear();
  all_neg_indices_.clear();
}

INSTANTIATE_CLASS(MultiBoxLossLayer);
REGISTER_LAYER_CLASS(MultiBoxLoss);

}  // namespace caffe
