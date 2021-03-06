#ifdef USE_OPENCV
#include <opencv2/core/core.hpp>
#endif  // USE_OPENCV
#include <stdint.h>

#include <algorithm>
#include <map>
#include <vector>

#include "caffe/data_transformer.hpp"
#include "caffe/layers/ccpd_data_layer.hpp"
#include "caffe/util/benchmark.hpp"

namespace caffe{

template <typename Dtype>
ccpdDataLayer<Dtype>::ccpdDataLayer(const LayerParameter & param)
    : BasePrefetchingDataLayer<Dtype>(param),
    reader_(param){
}

template <typename Dtype>
ccpdDataLayer<Dtype>::~ccpdDataLayer(){
    this->StopInternalThread();
}

template <typename Dtype>
void ccpdDataLayer<Dtype>::DataLayerSetUp(const vector<Blob<Dtype>*>& bottom, const vector<Blob<Dtype>*>& top){
    const int batch_size = this->layer_param_.data_param().batch_size();
    // Make sure dimension is consistent within batch.
    const TransformationParameter& transform_param =
    this->layer_param_.transform_param();
    if (transform_param.has_resize_param()) {
    if (transform_param.resize_param().resize_mode() ==
        ResizeParameter_Resize_mode_FIT_SMALL_SIZE) {
        CHECK_EQ(batch_size, 1)
        << "Only support batch size of 1 for FIT_SMALL_SIZE.";
    }
    }
    // Read a data point, and use it to initialize the top blob.
    AnnotatedCCpdDatum& anno_datum = *(reader_.full().peek());

    // Use data_transformer to infer the expected blob shape from anno_datum.
    vector<int> top_shape =
        this->data_transformer_->InferBlobShape(anno_datum.datum());
    this->transformed_data_.Reshape(top_shape);
    // Reshape top[0] and prefetch_data according to the batch_size.
    top_shape[0] = batch_size;
    top[0]->Reshape(top_shape);
    for (int i = 0; i < this->PREFETCH_COUNT; ++i) {
        this->prefetch_[i].data_.Reshape(top_shape);
    }
    LOG(INFO) << "output data size: " << top[0]->num() << ","
        << top[0]->channels() << "," << top[0]->height() << ","
        << top[0]->width();
    // label
    if (this->output_labels_) {
        has_anno_type_ = anno_datum.has_type();
        vector<int> label_shape(2, 1);
        if (has_anno_type_) {
            anno_type_ = anno_datum.type();
            if (anno_type_ == AnnotatedCCpdDatum_AnnotationType_CCPD) {
            // Since the number of lanmarks of one person can be constant for each image,
            // and another point is each image only have one person face, so we store the lable 
            // in a specific formate. In specific:
                label_shape[0] = batch_size;
                label_shape[1] = 7;
            } else {
                LOG(FATAL) << "Unknown annotation type.";
            }
        } else {
            label_shape[0] = batch_size;
        }
        top[1]->Reshape(label_shape);
        for (int i = 0; i < this->PREFETCH_COUNT; ++i) {
            this->prefetch_[i].label_.Reshape(label_shape);
        }
    }
}

// This function is called on prefetch thread
template<typename Dtype>
void ccpdDataLayer<Dtype>::load_batch(Batch<Dtype>* batch) {
    CPUTimer batch_timer;
    batch_timer.Start();
    double read_time = 0;
    double trans_time = 0;
    CPUTimer timer;
    CHECK(batch->data_.count());
    CHECK(this->transformed_data_.count());

    // Reshape according to the first datum of each batch
    // on single input batches allows for inputs of varying dimension.
    const int batch_size = this->layer_param_.data_param().batch_size();
    const TransformationParameter& transform_param = this->layer_param_.transform_param();
    AnnotatedCCpdDatum& anno_datum = *(reader_.full().peek());
    // Use data_transformer to infer the expected blob shape from datum.
    vector<int> top_shape = this->data_transformer_->InferBlobShape(anno_datum.datum());
    this->transformed_data_.Reshape(top_shape);
    // Reshape batch according to the batch_size.
    top_shape[0] = batch_size;
    batch->data_.Reshape(top_shape);

    Dtype* top_data = batch->data_.mutable_cpu_data();
    Dtype* top_label = NULL;  // suppress warnings about uninitialized variables

      // Store transformed annotation.
    map<int, LicensePlate > all_anno;

    if (this->output_labels_ && !has_anno_type_) {
        top_label = batch->label_.mutable_cpu_data();
    }
    for (int item_id = 0; item_id < batch_size; ++item_id) {
        timer.Start();
        // get a anno_datum
        AnnotatedCCpdDatum& anno_datum = *(reader_.full().pop("Waiting for data"));
        #if 0
        LOG(INFO)<<" START READ RAW ANNODATUM=================================================";
         LOG(INFO) <<" chi: "<<anno_datum.lpnumber().chichracter()<<" eng: "<<anno_datum.lpnumber().engchracter()
                        <<" let1: "<<anno_datum.lpnumber().letternum_1()<<" let2: "<<anno_datum.lpnumber().letternum_2()
                        <<" let3: "<<anno_datum.lpnumber().letternum_3()<<" let4: "<<anno_datum.lpnumber().letternum_4();
        LOG(INFO)<<" END READ RAW ANNODATUM+++++++++++++++++++++++++++++++++++++++++++++++++++";
    #endif
        AnnotatedCCpdDatum distort_datum;
        AnnotatedCCpdDatum* expand_datum = NULL;
        if (transform_param.has_distort_param()) {
            distort_datum.CopyFrom(anno_datum);
            this->data_transformer_->DistortImage(anno_datum.datum(),
                                                    distort_datum.mutable_datum());
            if (transform_param.has_expand_param()) {
                expand_datum = new AnnotatedCCpdDatum();
                this->data_transformer_->ExpandImage(distort_datum, expand_datum);
            } else {
                expand_datum = &distort_datum;
            }
            } else {
            if (transform_param.has_expand_param()) {
                expand_datum = new AnnotatedCCpdDatum();
                this->data_transformer_->ExpandImage(anno_datum, expand_datum);
            } else {
                expand_datum = &anno_datum;
            }
        }
        timer.Start();
        vector<int> shape =
            this->data_transformer_->InferBlobShape(expand_datum->datum());
        if (transform_param.has_resize_param()) {
            if (transform_param.resize_param().resize_mode() ==
                ResizeParameter_Resize_mode_FIT_SMALL_SIZE) {
                this->transformed_data_.Reshape(shape);
                batch->data_.Reshape(shape);
                top_data = batch->data_.mutable_cpu_data();
            } else {
                CHECK(std::equal(top_shape.begin() + 1, top_shape.begin() + 4,
                    shape.begin() + 1));
            }
        } else {
        CHECK(std::equal(top_shape.begin() + 1, top_shape.begin() + 4,
                shape.begin() + 1));
        }
        read_time += timer.MicroSeconds();
        // Apply data transformations (mirror, scale, crop...)
        int offset = batch->data_.offset(item_id);
        this->transformed_data_.set_cpu_data(top_data + offset);
        LicensePlate transformed_anno_vec;
        if (this->output_labels_) {
            if (has_anno_type_) {
                // Transform datum and annotation_group at the same time
                this->data_transformer_->Transform(*expand_datum,
                                                &(this->transformed_data_),
                                                &transformed_anno_vec);
                all_anno[item_id] = transformed_anno_vec;
            } else {
                this->data_transformer_->Transform(expand_datum->datum(),
                                                &(this->transformed_data_));
                // Otherwise, store the label from datum.
                // CHECK(expand_datum->datum().has_label()) << "Cannot find any label.";
                // top_label[item_id] = expand_datum->datum().label();
            }
        } else {
            this->data_transformer_->Transform(expand_datum->datum(),
                                            &(this->transformed_data_));
        }
        // clear memory
        if (transform_param.has_expand_param()) {
            delete expand_datum;
        }
        trans_time += timer.MicroSeconds();
        reader_.free().push(const_cast<AnnotatedCCpdDatum*>(&anno_datum));
    }

    // store "rich " landmark, face attributes
    if (this->output_labels_ && has_anno_type_) {
        vector<int> label_shape(2);
        if (anno_type_ == AnnotatedCCpdDatum_AnnotationType_CCPD) {
            // Reshape the label and store the annotation.
            label_shape[0] = batch_size;
            label_shape[1] = 7;
            batch->label_.Reshape(label_shape);
            top_label = batch->label_.mutable_cpu_data();
            int idx = 0;
            for (int item_id = 0; item_id < batch_size; ++item_id) {
                LicensePlate lp = all_anno[item_id];
                top_label[idx++] = lp.chichracter();
                top_label[idx++] = lp.engchracter();
                top_label[idx++] = lp.letternum_1();
                top_label[idx++] = lp.letternum_2();
                top_label[idx++] = lp.letternum_3();
                top_label[idx++] = lp.letternum_4();
                top_label[idx++] = lp.letternum_5();
            }
        } else {
            LOG(FATAL) << "Unknown annotation type.";
        }
    }
#if 0
  const Dtype* top_label_data = batch->label_.cpu_data();
  for(int ii=0; ii < batch_size; ii++)
  {
    int id = ii*7;
    LOG(INFO) <<" chi: "<<top_label_data[id+0]
              <<" eng: "<<top_label_data[id+1]<<" let1: "<<top_label_data[id+2]<<" let2: "<<top_label_data[id+3]
              <<" let3: "<<top_label_data[id+4]<<" let4: "<<top_label_data[id+5]
              <<" let5: "<<top_label_data[id+6];
  }
  LOG(INFO)<< "finished **************************************************** end ";
#endif 

    timer.Stop();
    batch_timer.Stop();
    DLOG(INFO) << "Prefetch batch: " << batch_timer.MilliSeconds() << " ms.";
    DLOG(INFO) << "     Read time: " << read_time / 1000 << " ms.";
    DLOG(INFO) << "Transform time: " << trans_time / 1000 << " ms.";
}

INSTANTIATE_CLASS(ccpdDataLayer);
REGISTER_LAYER_CLASS(ccpdData);

} //namespace caffe