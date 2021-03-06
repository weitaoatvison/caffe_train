python3 src/train_softmax.py \
--logs_base_dir ./train/logs/ \
--models_base_dir ./train/models/ \
--data_dir ../../../../dataset/facedata/recognition/vggface2_align_train/ \
--image_size 160 \
--model_def models.inception_resnet_v1 \
--lfw_dir ../../../../dataset/facedata/recognition/lfw/lfw_160/ \
--optimizer ADAM \
--learning_rate -1 \
--max_nrof_epochs 150 \
--keep_probability 0.8 \
--use_fixed_image_standardization \
--learning_rate_schedule_file data/learning_rate_schedule_classifier_casia.txt \
--weight_decay 5e-4 \
--embedding_size 512 \
--lfw_distance_metric 1 \
--lfw_use_flipped_images \
--lfw_subtract_mean \
--validation_set_split_ratio 0.05 \
--validate_every_n_epochs 5 \
--batch_size 30 \
--max_nrof_epochs 50000 \
--epoch_size 30 \
--prelogits_norm_loss_factor 5e-4
