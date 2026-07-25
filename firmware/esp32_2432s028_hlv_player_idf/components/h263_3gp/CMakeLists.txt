set(H263_ROOT "../../../../codecs/h263")
set(PV_ROOT "${H263_ROOT}/third_party/pv")

idf_component_register(
    SRCS
        "${H263_ROOT}/src/h263_3gp.cpp"
        "${PV_ROOT}/src/bitstream.cpp"
        "${PV_ROOT}/src/block_idct.cpp"
        "${PV_ROOT}/src/cal_dc_scaler.cpp"
        "${PV_ROOT}/src/combined_decode.cpp"
        "${PV_ROOT}/src/conceal.cpp"
        "${PV_ROOT}/src/datapart_decode.cpp"
        "${PV_ROOT}/src/dcac_prediction.cpp"
        "${PV_ROOT}/src/dec_pred_intra_dc.cpp"
        "${PV_ROOT}/src/get_pred_adv_b_add.cpp"
        "${PV_ROOT}/src/get_pred_outside.cpp"
        "${PV_ROOT}/src/idct.cpp"
        "${PV_ROOT}/src/idct_vca.cpp"
        "${PV_ROOT}/src/mb_motion_comp.cpp"
        "${PV_ROOT}/src/mb_utils.cpp"
        "${PV_ROOT}/src/packet_util.cpp"
        "${PV_ROOT}/src/post_filter.cpp"
        "${PV_ROOT}/src/pvdec_api.cpp"
        "${PV_ROOT}/src/scaling_tab.cpp"
        "${PV_ROOT}/src/vlc_decode.cpp"
        "${PV_ROOT}/src/vlc_dequant.cpp"
        "${PV_ROOT}/src/vlc_tab.cpp"
        "${PV_ROOT}/src/vop.cpp"
        "${PV_ROOT}/src/zigzag_tab.cpp"
    INCLUDE_DIRS
        "${H263_ROOT}/include"
        "${PV_ROOT}/include"
    PRIV_INCLUDE_DIRS
        "${PV_ROOT}/src"
)

target_compile_features(${COMPONENT_LIB} PRIVATE cxx_std_17)
target_compile_options(${COMPONENT_LIB} PRIVATE
    -O3
    -Wno-unused-parameter
    -Wno-sign-compare
    -Wno-error=unused-value
)
