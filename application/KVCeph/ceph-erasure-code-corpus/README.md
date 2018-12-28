ceph-erasure-code-corpus
========================

Objects erasure encoded by Ceph

To check all prior sets of data from the ceph/src directory:

DIRECTORY=../../ceph-erasure-code-corpus \
  ../qa/workunits/erasure-code/encode-decode-non-regression.sh

It will clone https://github.com/ceph/ceph-erasure-code-corpus.git

To check a set of data from the ceph/src directory:

../../ceph-erasure-code-corpus/XXXX/non-regression.sh

To create a new set of data from the ceph/src directory:

ACTION=--create ../../ceph-erasure-code-corpus/XXXX/non-regression.sh

