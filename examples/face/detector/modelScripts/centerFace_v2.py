#-*-coding:utf-8-*-
from __future__ import print_function
import sys, os
import logging
try:
    caffe_root = '../../../../../caffe_train/'
    sys.path.insert(0, caffe_root + 'python')
    import caffe
except ImportError:
    logging.fatal("Cannot find caffe!")
from caffe.model_libs import *
from caffe.mobilenetv2_lib import *
from google.protobuf import text_format

import math
import os
import shutil
import stat
import subprocess


trainDataPath = "../../../../../dataset/facedata/wider_face/lmdb/wider_face_wider_train_lm_lmdb/"
valDataPath = "../../../../../dataset/facedata/wider_face/lmdb/wider_face_wider_val_lm_lmdb/"
labelmapPath = "../labelmap.prototxt"
resize_width = 640
resize_height = 640
resize = "{}x{}".format(resize_width, resize_height)
batch_sampler = [
    {
        'sampler': {
            'min_scale': 0.3,
            'max_scale': 1.0,
            'min_aspect_ratio': 0.3,
            'max_aspect_ratio': 1.0,
        },
        'sample_constraint': {
            'min_jaccard_overlap': 1.0,
        },
        'max_trials': 50,
        'max_sample': 1,
    },
    {
        'sampler': {
            'min_scale': 0.4,
            'max_scale': 1.0,
            'min_aspect_ratio': 0.4,
            'max_aspect_ratio': 1.0,
        },
        'sample_constraint': {
            'min_jaccard_overlap': 1.0,
        },
        'max_trials': 50,
        'max_sample': 1,
    },
    {
        'sampler': {
            'min_scale': 0.5,
            'max_scale': 1.0,
            'min_aspect_ratio': 0.5,
            'max_aspect_ratio': 1.0,
        },
        'sample_constraint': {
            'min_jaccard_overlap': 1.0,
        },
        'max_trials': 50,
        'max_sample': 1,
    },
    {
        'sampler': {
            'min_scale': 0.6,
            'max_scale': 1.0,
            'min_aspect_ratio': 0.6,
            'max_aspect_ratio': 1.0,
        },
        'sample_constraint': {
            'min_jaccard_overlap': 1.0,
            },
        'max_trials': 50,
        'max_sample': 1,
    },
    {
        'sampler': {
            'min_scale': 0.7,
            'max_scale': 1.0,
            'min_aspect_ratio': 0.7,
            'max_aspect_ratio': 1.0,
        },
        'sample_constraint': {
            'min_jaccard_overlap': 1.0,
            },
        'max_trials': 50,
        'max_sample': 1,
    },
    {
        'sampler': {
            'min_scale': 0.8,
            'max_scale': 1.0,
            'min_aspect_ratio': 0.8,
            'max_aspect_ratio': 1.0,
        },
        'sample_constraint': {
            'max_jaccard_overlap': 1.0,
        },
        'max_trials': 50,
        'max_sample': 1,
    },
    {
        'sampler': {
            'min_scale': 0.9,
            'max_scale': 1.0,
            'min_aspect_ratio': 0.9,
            'max_aspect_ratio': 1.0,
        },
        'sample_constraint': {
            'max_jaccard_overlap': 1.0,
        },
        'max_trials': 50,
        'max_sample': 1,
    },
]
scale = [16, 32, 64, 128, 256, 512]
data_anchor_sampler = {
        'scale': scale,
        'sample_constraint': {
            'min_object_coverage': 0.75
        },
        'max_sample': 1,
        'max_trials': 50,
}
bbox = [
    {
      'bbox_small_scale': 10,
      'bbox_large_scale': 15,
      'ancher_stride': 4,
    },
    {
      'bbox_small_scale': 15,
      'bbox_large_scale': 20,
      'ancher_stride': 4,
    },
    {
      'bbox_small_scale': 20,
      'bbox_large_scale': 40,
      'ancher_stride': 8,
    },
    {
      'bbox_small_scale': 40,
      'bbox_large_scale': 70,
      'ancher_stride': 8,
    },
    {
      'bbox_small_scale': 70,
      'bbox_large_scale': 110,
      'ancher_stride': 16,
    },
    {
      'bbox_small_scale': 110,
      'bbox_large_scale': 250,
      'ancher_stride': 32,
    },
    {
      'bbox_small_scale': 250,
      'bbox_large_scale': 400,
      'ancher_stride': 32,
    },
    {
      'bbox_small_scale': 400,
      'bbox_large_scale': 560,
      'ancher_stride': 32,
    },
]
bbox_sampler = {
    'box': bbox,
    'max_sample': 1,
    'max_trials': 50,
}
train_transform_param = {
    'mirror': True,
    'mean_value': [103.94, 116.78, 123.68],
    'scale': 0.007843,
    'resize_param': {
        'prob': 1,
        'resize_mode': P.Resize.WARP,
        'height': resize_height,
        'width': resize_width,
        'interp_mode': [
            P.Resize.LINEAR,
            P.Resize.AREA,
            P.Resize.NEAREST,
            P.Resize.CUBIC,
            P.Resize.LANCZOS4,
        ],
    },
    'distort_param': {
        'brightness_prob': 0.5,
        'brightness_delta': 32,
        'contrast_prob': 0.5,
        'contrast_lower': 0.5,
        'contrast_upper': 1.5,
        'hue_prob': 0.5,
        'hue_delta': 18,
        'saturation_prob': 0.5,
        'saturation_lower': 0.5,
        'saturation_upper': 1.5,
        'random_order_prob': 0.0,
    },
    'emit_constraint': {
        'emit_type': caffe_pb2.EmitConstraint.CENTER,
    }
}
test_transform_param = {
    'mean_value': [103.94, 116.78, 123.68],
    'scale': 0.007843,
    'resize_param': {
        'prob': 1,
        'resize_mode': P.Resize.WARP,
        'height': resize_height,
        'width': resize_width,
        'interp_mode': [P.Resize.LINEAR],
    },
}
base_learning_rate = 0.0005
Job_Name = "Centerface_v2"
mdoel_name = "ResideoCenterFace"
save_dir = "../prototxt/Full_{}".format(resize)
snapshot_dir = "../snapshot/{}".format(Job_Name)
train_net_file = "{}/{}_train.prototxt".format(save_dir, Job_Name)
test_net_file = "{}/{}_test.prototxt".format(save_dir, Job_Name)
deploy_net_file = "{}/{}_deploy.prototxt".format(save_dir, Job_Name)
solver_file = "{}/{}_solver.prototxt".format(save_dir, Job_Name)

pretrain_model = "models/VGGNet/VGG_ILSVRC_16_layers_fc_reduced.caffemodel"

gpus = "0"
gpulist = gpus.split(",")
num_gpus = len(gpulist)
batch_size = 8
accum_batch_size = 8
iter_size = accum_batch_size / batch_size
solver_mode = P.Solver.CPU
device_id = 0
batch_size_per_device = batch_size
if num_gpus > 0:
    batch_size_per_device = int(math.ceil(float(batch_size) / num_gpus))
    iter_size = int(math.ceil(float(accum_batch_size) / (batch_size_per_device * num_gpus)))
    solver_mode = P.Solver.GPU
    device_id = int(gpulist[0])
normalization_mode = P.Loss.VALID
neg_pos_ratio = 3.
loc_weight = (neg_pos_ratio + 1.) / 4.
if normalization_mode == P.Loss.NONE:
    base_learning_rate /= batch_size_per_device
elif normalization_mode == P.Loss.VALID:
    base_learning_rate *= 25. / loc_weight
elif normalization_mode == P.Loss.FULL:
    base_learning_rate *= 2000.

refine_learning_rate = 5e-4

# Evaluate on whole test set.
num_test_image = 3219
test_batch_size = 1
# Ideally test_batch_size should be divisible by num_test_image,
# otherwise mAP will be slightly off the true value.
test_iter = int(math.ceil(float(num_test_image) / test_batch_size))

solver_param = {
    # Train parameters
    'base_lr': refine_learning_rate,#base_learning_rate,
    'weight_decay': 0.0005,
    'lr_policy': "multistep",
    'stepvalue': [140000, 190000],
    'gamma': 0.1,
    #'momentum': 0.9,
    'iter_size': iter_size,
    'max_iter': 220000,
    'snapshot': 5000,
    'display': 100,
    'average_loss': 10,
    'type': "Adam",
    'solver_mode': "GPU",
    'device_id': 0,
    'debug_info': False,
    'snapshot_after_train': True,
    # Test parameters
    'test_iter': [test_iter],
    'test_interval': 5000,
    'eval_type': "detection",
    'ap_version': "11point",
    'test_initialization': False,
}

Inverted_residual_setting = [[1, 16, 1, 1],
                                 [6, 24, 2, 2],
                                 [6, 32, 3, 2],
                                 [6, 64, 4, 2],
                                 [6, 96, 3, 1],
                                 [6, 160, 3, 2],
                                 [6, 320, 1, 1]]
use_branch= False

has_landmarks = True
detect_num_channels = 14

check_if_exist(trainDataPath)
check_if_exist(valDataPath)
check_if_exist(labelmapPath)
make_if_not_exist(save_dir)


# Create train.prototxt.
net = caffe.NetSpec()
net.data, net.label = CreateAnnotatedDataLayer(trainDataPath, batch_size=batch_size_per_device,
        train=True, output_label=True, label_map_file=labelmapPath,
        crop_type = P.AnnotatedData.CROP_BATCH,
        transform_param=train_transform_param, batch_sampler=batch_sampler, 
        data_anchor_sampler= data_anchor_sampler,bbox_sampler=None, has_landmarks = has_landmarks)

net, class_out, box_out = CenterFaceMobilenetV2Body(net= net, from_layer= 'data', detect_num = detect_num_channels
                                                    , Inverted_residual_setting= Inverted_residual_setting,
                                                    use_branch= use_branch)

from_layers = []
if use_branch:
    from_layers.append(net[box_out[0]])
    from_layers.append(net[box_out[1]])
    from_layers.append(net[box_out[2]])
else:
    from_layers.append(net[box_out])
from_layers.append(net[class_out])
from_layers.append(net.label)
CenterFaceObjectLoss(net= net, stageidx= 0, from_layers= from_layers, has_lm= has_landmarks, use_branch= use_branch)

with open(train_net_file, 'w') as f:
    print('name: "{}_train"'.format("CenterFace"), file=f)
    print(net.to_proto(), file=f)

# 创建test.prototxt
net = caffe.NetSpec()
net.data, net.label = CreateAnnotatedDataLayer(valDataPath, batch_size=test_batch_size,
        train=False, output_label=True, label_map_file=labelmapPath,
        transform_param=test_transform_param, has_landmarks = has_landmarks)

net, class_out, box_out = CenterFaceMobilenetV2Body(net, from_layer = 'data', Use_BN= True, 
                        Inverted_residual_setting= Inverted_residual_setting,
                        use_global_stats= True, detect_num = detect_num_channels, 
                        use_branch= use_branch)

Sigmoid_layer = "{}_Sigmoid".format(class_out)
net[Sigmoid_layer] = L.Sigmoid(net[class_out], in_place= False)


DetectListLayer = []
if use_branch:
    out_detect = []
    out_detect.append(net[box_out[0]])
    out_detect.append(net[box_out[1]])
    out_detect.append(net[box_out[2]])
    Concat_out = "detect_concat_1x1"
    net[Concat_out] = L.Concat(*out_detect, axis=1)
    DetectListLayer.append(net[Concat_out])
else:
    DetectListLayer.append(net[box_out])
DetectListLayer.append(net[Sigmoid_layer])
CenterFaceObjectDetect(net, from_layers = DetectListLayer, has_lm= has_landmarks, keep_top_k = 1000)

det_eval_param = {
    'num_classes': 2,
    'background_label_id': 0,
    'overlap_threshold': 0.5,
    'evaluate_difficult_gt': False,
	'has_lm': has_landmarks
}
net.detection_eval = L.DetectionEvaluate(net.detection_out, net.label,
    detection_evaluate_param=det_eval_param,
    include=dict(phase=caffe_pb2.Phase.Value('TEST')))

with open(test_net_file, 'w') as f:
    print('name: "{}_test"'.format('CenterFace'), file=f)
    print(net.to_proto(), file=f)

#创建 deploy.prototxt, 移除数据层和最后一层评价层
deploy_net = net
with open(deploy_net_file, 'w') as f:
    net_param = deploy_net.to_proto()
    # Remove the first (AnnotatedData) and last (DetectionEvaluate) layer from test net.
    del net_param.layer[0]
    del net_param.layer[-1]
    net_param.name = '{}_deploy'.format('CenterFace')
    net_param.input.extend(['data'])
    net_param.input_shape.extend([
        caffe_pb2.BlobShape(dim=[1, 3, resize_height, resize_width])])
    print(net_param, file=f)

#创建 solver.prototxt
solver = caffe_pb2.SolverParameter(
        train_net=train_net_file,
        test_net=[test_net_file],
        snapshot_prefix=snapshot_dir,
        **solver_param)
with open(solver_file, 'w') as f:
    print(solver, file=f)


#创建 train_center_face.sh

# Create job file.
train_src_param = '# --snapshot={}_0_iter_{}.solverstate '.format(snapshot_dir, 5000)
job_file = "../train_scripts/train_{}.sh".format('center_face_v2')
with open(job_file, 'w') as f:
    f.write('#!/bin/sh \n')
    f.write('if ! test -f {} ;then \n'.format(train_net_file))
    f.write('   echo "error: {} does not exit." \n'.format(train_net_file))
    f.write('   echo "please generate your own model prototxt primarily." \n')
    f.write('   exit 1 \n')
    f.write('fi\n')
    f.write('if ! test -f {} ;then \n'.format(test_net_file))
    f.write('   echo "error: {} does not exit." \n'.format(test_net_file))
    f.write('   echo "please generate your own model prototxt primarily." \n')
    f.write('   exit 1 \n')
    f.write('fi\n')
    f.write('../../../../build/tools/caffe train --solver={} --gpu {} \\\n'.format(solver_file, 0))
    f.write(train_src_param)
