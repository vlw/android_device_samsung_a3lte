# One kernel configuration, seven hardware-revision trees, three QCDT images.
A3_DTB_F := msm8916-sec-a3-eur-r00 msm8916-sec-a3-eur-r01
A3_DTB_FU := msm8916-sec-a3u-eur-r01
A3_DTB_H := msm8916-sec-a33g-eur-r00 msm8916-sec-a33g-eur-r01 \
    msm8916-sec-a33g-eur-r03 msm8916-sec-a33g-eur-r04
A3_DTB_NAMES := $(A3_DTB_F) $(A3_DTB_FU) $(A3_DTB_H)
TARGET_KERNEL_ADDITIONAL_FLAGS += DTB_NAMES="$(A3_DTB_NAMES)"
