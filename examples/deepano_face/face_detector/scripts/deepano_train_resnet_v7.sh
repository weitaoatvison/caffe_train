#!/bin/sh
if ! test -f ../prototxt/deepano_face_train_v7.prototxt ;then
	echo "error: ../prototxt/deepano_face_train_v7.prototxt does not exit."
	echo "please generate your own model prototxt primarily."
        exit 1
fi
if ! test -f ../prototxt/deepano_face_test_v7.prototxt ;then
	echo "error: ../prototxt/deepano_face_test_v7.prototxt does not exit."
	echo "please generate your own model prototxt primarily."
        exit 1
fi
../../../../build/tools/caffe train --solver=../solver/deepano_solver_train_v7.prototxt -gpu 0 \
--snapshot=../snapshot/face_detector_v7_iter_69224.solverstate
