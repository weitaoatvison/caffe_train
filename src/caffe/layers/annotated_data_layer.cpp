#ifdef USE_OPENCV
#include <opencv2/core/core.hpp>
#endif  // USE_OPENCV
#include <stdint.h>

#include <algorithm>
#include <map>
#include <vector>

#include "caffe/data_transformer.hpp"
#include "caffe/layers/annotated_data_layer.hpp"
#include "caffe/util/benchmark.hpp"
#include "caffe/util/sampler.hpp"

namespace caffe {

template <typename Dtype>
AnnotatedDataLayer<Dtype>::AnnotatedDataLayer(const LayerParameter& param)
  : BasePrefetchingDataLayer<Dtype>(param),
    reader_(param) {
}

template <typename Dtype>
AnnotatedDataLayer<Dtype>::~AnnotatedDataLayer() {
    this->StopInternalThread();
}

template <typename Dtype>
void AnnotatedDataLayer<Dtype>::DataLayerSetUp(
    const vector<Blob<Dtype>*>& bottom, const vector<Blob<Dtype>*>& top) {
    const int batch_size = this->layer_param_.data_param().batch_size();
    const AnnotatedDataParameter& anno_data_param =
        this->layer_param_.annotated_data_param();
    for (int i = 0; i < anno_data_param.batch_sampler_size(); ++i) {
        batch_samplers_.push_back(anno_data_param.batch_sampler(i));
    }
    if(anno_data_param.has_data_anchor_sampler()){
        data_anchor_samplers_.push_back(anno_data_param.data_anchor_sampler());
    }
    if(anno_data_param.has_bbox_sampler()){
        int num_scale = anno_data_param.bbox_sampler().box_size();
        for(int i = 0; i < num_scale; i++){
        bbox_large_scale_.push_back(anno_data_param.bbox_sampler().box(i).bbox_large_scale());
        bbox_small_scale_.push_back(anno_data_param.bbox_sampler().box(i).bbox_small_scale());
        anchor_stride_.push_back(anno_data_param.bbox_sampler().box(i).ancher_stride());
        }
    }
    label_map_file_ = anno_data_param.label_map_file();
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
    YoloFormat_ = anno_data_param.yoloformat();
    crop_type_ = anno_data_param.crop_type();
    has_landmarks_ = anno_data_param.has_landmarks();

    // Read a data point, and use it to initialize the top blob.
    AnnotatedDatum& anno_datum = *(reader_.full().peek());

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
    LOG(INFO) << "output data size: " << top[0]->num() << 
                "," << top[0]->channels() << 
                "," << top[0]->height() << ","<< top[0]->width();
    // label
    if (this->output_labels_) {
        has_anno_type_ = anno_datum.has_type() || anno_data_param.has_anno_type();
        vector<int> label_shape(4, 1);
        if (has_anno_type_) {
            anno_type_ = anno_datum.type();
            if (anno_data_param.has_anno_type()) {
                // If anno_type is provided in AnnotatedDataParameter, replace
                // the type stored in each individual AnnotatedDatum.
                LOG(WARNING) << "type stored in AnnotatedDatum is shadowed.";
                anno_type_ = anno_data_param.anno_type();
            }
            // Infer the label shape from anno_datum.AnnotationGroup().
            int num_bboxes = 0;
            if (anno_type_ == AnnotatedDatum_AnnotationType_BBOX) {
                // Since the number of bboxes can be different for each image,
                // we store the bbox information in a specific format. In specific:
                // All bboxes are stored in one spatial plane (num and channels are 1)
                // And each row contains one and only one box in the following format:
                // [item_id, group_label, instance_id, xmin, ymin, xmax, ymax, diff]
                // Note: Refer to caffe.proto for details about group_label and
                // instance_id.
                for (int g = 0; g < anno_datum.annotation_group_size(); ++g) {
                    num_bboxes += anno_datum.annotation_group(g).annotation_size();
                }
                label_shape[0] = YoloFormat_ ? batch_size : 1;
                label_shape[1] = 1;
                // BasePrefetchingDataLayer<Dtype>::LayerSetUp() requires to call
                // cpu_data and gpu_data for consistent prefetch thread. Thus we make
                // sure there is at least one bbox.
                label_shape[2] = std::max(num_bboxes, 1);
                label_shape[3] = 8 + (has_landmarks_ ? 11 : 0);
                
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
    batch_id = 0;
    jj = 0;
}

// This function is called on prefetch thread
template<typename Dtype>
void AnnotatedDataLayer<Dtype>::load_batch(Batch<Dtype>* batch) {
    CPUTimer batch_timer;
    batch_timer.Start();
    double read_time = 0;
    double trans_time = 0;
    CPUTimer timer;
    CHECK(batch->data_.count());
    CHECK(this->transformed_data_.count());

    // Reshape according to the first anno_datum of each batch
    // on single input batches allows for inputs of varying dimension.
    const int batch_size = this->layer_param_.data_param().batch_size();
    const AnnotatedDataParameter& anno_data_param =
        this->layer_param_.annotated_data_param();
    const TransformationParameter& transform_param =
    this->layer_param_.transform_param();
    AnnotatedDatum& anno_datum = *(reader_.full().peek());
    // Use data_transformer to infer the expected blob shape from anno_datum.
    vector<int> top_shape =
        this->data_transformer_->InferBlobShape(anno_datum.datum());
    this->transformed_data_.Reshape(top_shape);
    // Reshape batch according to the batch_size.
    top_shape[0] = batch_size;
    batch->data_.Reshape(top_shape);

    Dtype* top_data = batch->data_.mutable_cpu_data();
    Dtype* top_label = NULL;  // suppress warnings about uninitialized variables
    if (this->output_labels_ && !has_anno_type_) {
        top_label = batch->label_.mutable_cpu_data();
    }
    // Store transformed annotation.
    map<int, vector<AnnotationGroup> > all_anno;
    int num_bboxes = 0;
    for (int item_id = 0; item_id < batch_size; ++item_id) {
        timer.Start();
        // get a anno_datum
        AnnotatedDatum& anno_datum = *(reader_.full().pop("Waiting for data"));
        read_time += timer.MicroSeconds();
        timer.Start();
        AnnotatedDatum distort_datum;
        AnnotatedDatum* expand_datum = NULL;
        AnnotatedDatum* resized_anno_datum = NULL;
        bool do_resize = false;
        if (transform_param.has_distort_param()) {
            distort_datum.CopyFrom(anno_datum);
            this->data_transformer_->DistortImage(anno_datum.datum(),
                                                distort_datum.mutable_datum());
            if (transform_param.has_expand_param()) {
                expand_datum = new AnnotatedDatum();
                this->data_transformer_->ExpandImage(distort_datum, expand_datum);
            } else {
                expand_datum = &distort_datum;
            }
        } else {
            if (transform_param.has_expand_param()) {
                expand_datum = new AnnotatedDatum();
                this->data_transformer_->ExpandImage(anno_datum, expand_datum);
            } else {
                expand_datum = &anno_datum;
            }
        }
        AnnotatedDatum* sampled_datum = NULL;
        bool has_sampled = false;
        bool CropSample = false;
        vector<NormalizedBBox> sampled_bboxes;
        sampled_bboxes.clear();
        if(crop_type_ == AnnotatedDataParameter_CROP_TYPE_CROP_BATCH){
            if (batch_samplers_.size() > 0) {
                GenerateBatchSamples(*expand_datum, batch_samplers_, &sampled_bboxes);
                CropSample = true;
            } else {
                sampled_datum = expand_datum;
            }
        }
        else if(crop_type_ == AnnotatedDataParameter_CROP_TYPE_CROP_JITTER){
            GenerateJitterSamples(*expand_datum, 0.1, &sampled_bboxes);
            CropSample = true;
        }
        else if(crop_type_ == AnnotatedDataParameter_CROP_TYPE_CROP_ANCHOR){
            if(data_anchor_samplers_.size() > 0){
                GenerateBatchDataAnchorSamples(*expand_datum, data_anchor_samplers_, &sampled_bboxes);
                int rand_idx = caffe_rng_rand() % sampled_bboxes.size();
                sampled_datum = new AnnotatedDatum();
                this->data_transformer_->CropImage_Sampling(*expand_datum,
                                                    sampled_bboxes[rand_idx],
                                                    sampled_datum);
                has_sampled = true;
            }else{
                sampled_datum = expand_datum;
            }
        }
        else if(crop_type_ == AnnotatedDataParameter_CROP_TYPE_CROP_GT_BBOX){
            if (anno_data_param.has_bbox_sampler()) {
                resized_anno_datum = new AnnotatedDatum();
                do_resize = true;
                GenerateLFFDSample(*expand_datum, &sampled_bboxes, 
                                bbox_small_scale_, bbox_large_scale_, anchor_stride_,
                                resized_anno_datum, transform_param, do_resize);
                CHECK_GT(resized_anno_datum->datum().channels(), 0);
                sampled_datum = new AnnotatedDatum();
                this->data_transformer_->CropImage_Sampling(*resized_anno_datum,
                                                sampled_bboxes[0], sampled_datum);
                has_sampled = true;
            } else {
                sampled_datum = expand_datum;
            }
        }
        else if(crop_type_ == AnnotatedDataParameter_CROP_TYPE_CROP_DEFAULT){
            sampled_datum = expand_datum;
        }
        if(CropSample){        
            if (sampled_bboxes.size() > 0) {
                int rand_idx = caffe_rng_rand() % sampled_bboxes.size();
                sampled_datum = new AnnotatedDatum();
                this->data_transformer_->CropImage(*expand_datum,
                                                    sampled_bboxes[rand_idx],
                                                    sampled_datum);
                has_sampled = true;
            } else {
                sampled_datum = expand_datum;
            }
        }
        CHECK(sampled_datum != NULL);
        timer.Start();
        vector<int> shape = this->data_transformer_->InferBlobShape(sampled_datum->datum());
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
        // Apply data transformations (mirror, scale, crop...)
        int offset = batch->data_.offset(item_id);
        this->transformed_data_.set_cpu_data(top_data + offset);
        vector<AnnotationGroup> transformed_anno_vec;
        if (this->output_labels_) {
            if (has_anno_type_) {
                CHECK(sampled_datum->has_type()) << "Some datum misses AnnotationType.";
                if (anno_data_param.has_anno_type()) {
                    sampled_datum->set_type(anno_type_);
                } else {
                    CHECK_EQ(anno_type_, sampled_datum->type()) << "Different AnnotationType.";
                }
                // Transform datum and annotation_group at the same time
                transformed_anno_vec.clear();
                this->data_transformer_->Transform(*sampled_datum,
                                                    &(this->transformed_data_),
                                                    &transformed_anno_vec);
                if (anno_type_ == AnnotatedDatum_AnnotationType_BBOX) {
                    // Count the number of bboxes.
                    for (int g = 0; g < transformed_anno_vec.size(); ++g) {
                        num_bboxes += transformed_anno_vec[g].annotation_size();
                    }
                } else {
                    LOG(FATAL) << "Unknown annotation type.";
                }
                all_anno[item_id] = transformed_anno_vec;
            } else {
                this->data_transformer_->Transform(sampled_datum->datum(),
                                                    &(this->transformed_data_));
                // Otherwise, store the label from datum.
                CHECK(sampled_datum->datum().has_label()) << "Cannot find any label.";
                top_label[item_id] = sampled_datum->datum().label();
            }
        } 
        else {
            this->data_transformer_->Transform(sampled_datum->datum(), &(this->transformed_data_));
        }
        # ifdef BOOL_TEST_DATA
        cv::Mat cropImage;
        std::string save_folder = "../anchorTestImage";
        std::string prefix_imgName = "crop_image";
        
        std::string saved_img_name = save_folder + "/"+ prefix_imgName + "_"+ std::to_string(batch_id)+ "_"+ std::to_string(item_id) 
                                        + "_" + std::to_string(jj) +".jpg";
        const Dtype* data = this->transformed_data_.cpu_data();
        int Trans_Height = this->transformed_data_.height();
        int Trans_Width = this->transformed_data_.width();
        cropImage = cv::Mat(Trans_Height, Trans_Width, CV_8UC3);
        for(int row = 0; row < Trans_Height; row++){
            unsigned char *ImgData = cropImage.ptr<uchar>(row);
            for(int col = 0; col < Trans_Width; col++){
            ImgData[3 * col + 0] = static_cast<uchar>(data[0 * Trans_Height * Trans_Width + row * Trans_Width + col]);
            ImgData[3 * col + 1] = static_cast<uchar>(data[1 * Trans_Height * Trans_Width + row * Trans_Width + col]);
            ImgData[3 * col + 2] = static_cast<uchar>(data[2 * Trans_Height * Trans_Width + row * Trans_Width + col]);
            }
        }
        int Crop_Height = cropImage.rows;
        int Crop_Width = cropImage.cols;
        int num_gt_box = 0;
        for (int g = 0; g < transformed_anno_vec.size(); ++g) {
            const AnnotationGroup& anno_group = transformed_anno_vec[g];
            for (int a = 0; a < anno_group.annotation_size(); ++a) {
            const Annotation& anno = anno_group.annotation(a);
            const NormalizedBBox& bbox = anno.bbox();
            int xmin = int(bbox.xmin() * Crop_Width);
            int ymin = int(bbox.ymin() * Crop_Height);
            int xmax = int(bbox.xmax() * Crop_Width);
            int ymax = int(bbox.ymax() * Crop_Height);
            cv::rectangle(cropImage, cv::Point2i(xmin, ymin), cv::Point2i(xmax, ymax), cv::Scalar(255,0,0), 1, 1, 0);
            if(has_landmarks_){
                if(anno.has_lm()){
                    const AnnoFaceLandmarks& project_facemark = anno.face_lm();
                    point lefteye = project_facemark.lefteye();
                    point righteye = project_facemark.righteye();
                    point nose = project_facemark.nose();
                    point leftmouth = project_facemark.leftmouth();
                    point rightmouth = project_facemark.rightmouth();
                    vector<cv::Point2i> lm(5);
                    lm[0] = cv::Point2i(int(lefteye.x() * Crop_Width), int(lefteye.y() * Crop_Height));
                    lm[1] = cv::Point2i(int(righteye.x() * Crop_Width), int(righteye.y() * Crop_Height));
                    lm[2] = cv::Point2i(int(nose.x() * Crop_Width), int(nose.y() * Crop_Height));
                    lm[3] = cv::Point2i(int(leftmouth.x() * Crop_Width), int(leftmouth.y() * Crop_Height));
                    lm[4] = cv::Point2i(int(rightmouth.x() * Crop_Width), int(rightmouth.y() * Crop_Height));
                    for(unsigned ii = 0; ii < 5; ii++)
                        cv::circle(cropImage, lm[ii], 1,  cv::Scalar(0,255,0), 1, 1, 0);
                }
            }
            num_gt_box++;
            }
        }
        if(num_gt_box==0)
            LOG(INFO)<<"no gt boxes";
        cv::imwrite(saved_img_name, cropImage);
        LOG(INFO)<<"*** Datum Write Into Jpg File Sucessfully! ***";
        jj ++ ;
        if(jj == 200){
            LOG(FATAL)<<"We have completed 100 times images crop testd!";
        }
        #endif
        // clear memory
        if (has_sampled) {
            delete sampled_datum;
        }
        if (transform_param.has_expand_param()) {
            delete expand_datum;
        }
        if(do_resize){
            delete resized_anno_datum;
        }
        trans_time += timer.MicroSeconds();
        reader_.free().push(const_cast<AnnotatedDatum*>(&anno_datum));
    }
    batch_id++;
    // Store "rich" annotation if needed.
    if (this->output_labels_ && has_anno_type_) {
        vector<int> label_shape(4);
        if (anno_type_ == AnnotatedDatum_AnnotationType_BBOX) 
        {
            label_shape[0] = YoloFormat_ ? batch_size : 1;
            label_shape[1] = 1;
            int label_last_channels = 8 + (has_landmarks_ ? 11 : 0);
            label_shape[3] = label_last_channels;
            if (num_bboxes == 0) {
                // Store all -1 in the label.
                label_shape[2] = 1;
                batch->label_.Reshape(label_shape);
                caffe_set<Dtype>(label_last_channels, -1, batch->label_.mutable_cpu_data());
            }else{
                // Reshape the label and store the annotation.
                label_shape[2] = num_bboxes;
                batch->label_.Reshape(label_shape);
                top_label = batch->label_.mutable_cpu_data();
                if (YoloFormat_)
                    caffe_set<Dtype>(label_last_channels * num_bboxes * batch_size, -1, 
                                                    batch->label_.mutable_cpu_data());
                fill_label(top_label, batch_size, all_anno, num_bboxes, label_last_channels);
            }
        } else {
            LOG(FATAL) << "Unknown annotation type.";
        }
    }
    timer.Stop();
    batch_timer.Stop();
    DLOG(INFO) << "Prefetch batch: " << batch_timer.MilliSeconds() << " ms.";
    DLOG(INFO) << "     Read time: " << read_time / 1000 << " ms.";
    DLOG(INFO) << "Transform time: " << trans_time / 1000 << " ms.";
}

template <typename Dtype>
void AnnotatedDataLayer<Dtype>::fill_label(Dtype* top_label, int batch_size, 
                std::map<int, vector<AnnotationGroup> > all_anno, 
                int num_bboxes, 
                int label_last_channels){
    int idx = 0;
    for (int item_id = 0; item_id < batch_size; ++item_id) {
        const vector<AnnotationGroup>& anno_vec = all_anno[item_id];
        if(YoloFormat_)
            idx = item_id * num_bboxes * label_last_channels ;
        for (int g = 0; g < anno_vec.size(); ++g) {
            const AnnotationGroup& anno_group = anno_vec[g];
            for (int a = 0; a < anno_group.annotation_size(); ++a) {
                const Annotation& anno = anno_group.annotation(a);
                const NormalizedBBox& bbox = anno.bbox();
                top_label[idx++] = item_id;
                top_label[idx++] = anno_group.group_label();
                top_label[idx++] = anno.instance_id();
                top_label[idx++] = bbox.xmin();
                top_label[idx++] = bbox.ymin();
                top_label[idx++] = bbox.xmax();
                top_label[idx++] = bbox.ymax();
                top_label[idx++] = bbox.difficult();
                if(has_landmarks_){
                    const AnnoFaceLandmarks& lm = anno.face_lm();
                    top_label[idx++] = anno.has_lm();
                    top_label[idx++] = lm.lefteye().x();
                    top_label[idx++] = lm.lefteye().y();
                    top_label[idx++] = lm.righteye().x();
                    top_label[idx++] = lm.righteye().y();
                    top_label[idx++] = lm.nose().x();
                    top_label[idx++] = lm.nose().y();
                    top_label[idx++] = lm.leftmouth().x();
                    top_label[idx++] = lm.leftmouth().y();
                    top_label[idx++] = lm.rightmouth().x();
                    top_label[idx++] = lm.rightmouth().y();
                }
            }
        }
    }
}

template <typename Dtype>
void AnnotatedDataLayer<Dtype>::check_landmarks_value(AnnoFaceLandmarks lmarks){
    CHECK_LE(lmarks.lefteye().x(), 1.);
    CHECK_LE(lmarks.lefteye().y(), 1.);
    CHECK_LE(lmarks.righteye().x(), 1.);
    CHECK_LE(lmarks.righteye().y(), 1.);
    CHECK_LE(lmarks.nose().x(), 1.);
    CHECK_LE(lmarks.nose().y(), 1.);
    CHECK_LE(lmarks.leftmouth().x(), 1.);
    CHECK_LE(lmarks.leftmouth().y(), 1.);
    CHECK_LE(lmarks.rightmouth().x(), 1.);
    CHECK_LE(lmarks.rightmouth().y(), 1.);
    CHECK_GE(lmarks.lefteye().x(), 0.);
    CHECK_GE(lmarks.lefteye().y(), 0.);
    CHECK_GE(lmarks.righteye().x(), 0.);
    CHECK_GE(lmarks.righteye().y(), 0.);
    CHECK_GE(lmarks.nose().x(), 0.);
    CHECK_GE(lmarks.nose().y(), 0.);
    CHECK_GE(lmarks.leftmouth().x(), 0.);
    CHECK_GE(lmarks.leftmouth().y(), 0.);
    CHECK_GE(lmarks.rightmouth().x(), 0.);
    CHECK_GE(lmarks.rightmouth().y(), 0.);
}

INSTANTIATE_CLASS(AnnotatedDataLayer);
REGISTER_LAYER_CLASS(AnnotatedData);

} // namespace caffe