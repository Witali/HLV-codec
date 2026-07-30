set(H263_ROOT "../../../../codecs/h263")
set(PV_ROOT "${H263_ROOT}/third_party/pv")

idf_component_register(
    SRCS
        "${H263_ROOT}/src/h263_3gp.c"
        "${PV_ROOT}/src/bitstream.c"
        "${PV_ROOT}/src/block_idct.c"
        "${PV_ROOT}/src/cal_dc_scaler.c"
        "${PV_ROOT}/src/combined_decode.c"
        "${PV_ROOT}/src/conceal.c"
        "${PV_ROOT}/src/datapart_decode.c"
        "${PV_ROOT}/src/dcac_prediction.c"
        "${PV_ROOT}/src/dec_pred_intra_dc.c"
        "${PV_ROOT}/src/get_pred_adv_b_add.c"
        "${PV_ROOT}/src/get_pred_outside.c"
        "${PV_ROOT}/src/idct.c"
        "${PV_ROOT}/src/idct_vca.c"
        "${PV_ROOT}/src/mb_motion_comp.c"
        "${PV_ROOT}/src/mb_utils.c"
        "${PV_ROOT}/src/packet_util.c"
        "${PV_ROOT}/src/post_filter.c"
        "${PV_ROOT}/src/pvdec_api.c"
        "${PV_ROOT}/src/scaling_tab.c"
        "${PV_ROOT}/src/vlc_decode.c"
        "${PV_ROOT}/src/vlc_dequant.c"
        "${PV_ROOT}/src/vlc_tab.c"
        "${PV_ROOT}/src/vop.c"
        "${PV_ROOT}/src/zigzag_tab.c"
        "h263_idct_optimized.S"
    INCLUDE_DIRS
        "${H263_ROOT}/include"
        "${PV_ROOT}/include"
    PRIV_INCLUDE_DIRS
        "${PV_ROOT}/src"
    REQUIRES
        compact_yuv420
)

set(PV_H263_XTENSA_IDCT_DC_INTRA ON CACHE BOOL
    "Use the Xtensa DC-only H.263 intra IDCT output kernel")
if(PV_H263_XTENSA_IDCT_DC_INTRA)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE
        PV_H263_XTENSA_IDCT_DC_INTRA=1
    )
else()
    target_compile_definitions(${COMPONENT_LIB} PRIVATE
        PV_H263_XTENSA_IDCT_DC_INTRA=0
    )
endif()

set(PV_H263_IRAM_BITSTREAM_FILL ON CACHE BOOL
    "Place the H.263 32-bit bitstream cache refill in IRAM")
set(PV_H263_IRAM_VLC_DEQUANT ON CACHE BOOL
    "Place the active H.263+ intra VLC/dequant helper in IRAM")
set(PV_H263_IRAM_INTRA_IDCT ON CACHE BOOL
    "Place the active H.263+ intra IDCT kernel in IRAM")
set(PV_H263_IRAM_MOTION_COMP ON CACHE BOOL
    "Place the MPEG-4/H.263 motion-compensation hot path in IRAM")
if(PV_H263_IRAM_BITSTREAM_FILL)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE
        PV_H263_IRAM_BITSTREAM_FILL=1
    )
else()
    target_compile_definitions(${COMPONENT_LIB} PRIVATE
        PV_H263_IRAM_BITSTREAM_FILL=0
    )
endif()

if(PV_H263_IRAM_VLC_DEQUANT)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE
        PV_H263_IRAM_VLC_DEQUANT=1
    )
else()
    target_compile_definitions(${COMPONENT_LIB} PRIVATE
        PV_H263_IRAM_VLC_DEQUANT=0
    )
endif()

if(PV_H263_IRAM_INTRA_IDCT)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE
        PV_H263_IRAM_INTRA_IDCT=1
    )
else()
    target_compile_definitions(${COMPONENT_LIB} PRIVATE
        PV_H263_IRAM_INTRA_IDCT=0
    )
endif()

if(PV_H263_IRAM_MOTION_COMP)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE
        PV_H263_IRAM_MOTION_COMP=1
    )
else()
    target_compile_definitions(${COMPONENT_LIB} PRIVATE
        PV_H263_IRAM_MOTION_COMP=0
    )
endif()

target_compile_features(${COMPONENT_LIB} PRIVATE c_std_99)
target_compile_options(${COMPONENT_LIB} PRIVATE
    -O3
    -Wno-unused-parameter
    -Wno-sign-compare
    -Wno-error=unused-value
)
