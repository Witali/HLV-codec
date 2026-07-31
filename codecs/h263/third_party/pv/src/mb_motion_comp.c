/* ------------------------------------------------------------------
 * Copyright (C) 1998-2009 PacketVideo
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either
 * express or implied.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 * -------------------------------------------------------------------
 */

#define LOG_TAG "m4v_h263"
#include <log/log.h>

/*
------------------------------------------------------------------------------
 INPUT AND OUTPUT DEFINITIONS

 Inputs:
    video = pointer to structure of type VideoDecData

 Local Stores/Buffers/Pointers Needed:
    roundtab16 = rounding table

 Global Stores/Buffers/Pointers Needed:
    None

 Outputs:
    None

 Pointers and Buffers Modified:
    video->currVop->yChan contents are the newly calculated luminance
      data
    video->currVop->uChan contents are the newly calculated chrominance
      b data
    video->currVop->vChan contents are the newly calculated chrominance
      r data
    video->pstprcTypCur contents are the updated semaphore propagation
      values

 Local Stores Modified:
    None

 Global Stores Modified:
    None

------------------------------------------------------------------------------
 FUNCTION DESCRIPTION

 This function performs high level motion compensation on the luminance and
 chrominance data. It sets up all the parameters required by the functions
 that perform luminance and chrominance prediction and it initializes the
 pointer to the post processing semaphores of a given block. It also checks
 the motion compensation mode in order to determine which luminance or
 chrominance prediction functions to call and determines how the post
 processing semaphores are updated.

*/


/*----------------------------------------------------------------------------
; INCLUDES
----------------------------------------------------------------------------*/
#include "mp4dec_lib.h"
#include "motion_comp.h"

#ifndef PV_H263_IRAM_MOTION_COMP
#define PV_H263_IRAM_MOTION_COMP 0
#endif

#if PV_H263_IRAM_MOTION_COMP
#include "esp_attr.h"
#define PV_H263_MOTION_COMP_ATTR IRAM_ATTR
#else
#define PV_H263_MOTION_COMP_ATTR
#endif
/*----------------------------------------------------------------------------
; MACROS
; Define module specific macros here
----------------------------------------------------------------------------*/


/*----------------------------------------------------------------------------
; DEFINES
; Include all pre-processor statements here. Include conditional
; compile variables also.
----------------------------------------------------------------------------*/


/*----------------------------------------------------------------------------
; LOCAL FUNCTION DEFINITIONS
; Function Prototype declaration
----------------------------------------------------------------------------*/


/*----------------------------------------------------------------------------
; LOCAL STORE/BUFFER/POINTER DEFINITIONS
; Variable declaration - defined here and used outside this module
----------------------------------------------------------------------------*/
/* 09/29/2000 bring this from mp4def.h */
// const static int roundtab4[] = {0,1,1,1};
// const static int roundtab8[] = {0,0,1,1,1,1,1,2};
/*** 10/30 for TPS */
// const static int roundtab12[] = {0,0,0,1,1,1,1,1,1,1,2,2};
/* 10/30 for TPS ***/
const static int roundtab16[] = {0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2};

#if PV_H263_STAGE_PROFILE && defined(ESP_PLATFORM)
#define COMPACT_PROFILE_PARAMETER , H263DecodeProfile *profile
#define COMPACT_PROFILE_ARGUMENT(video) \
    , (video)->videoDecControls->decodeProfile
#define COMPACT_PROFILE_FORWARD , profile
#define COMPACT_PROFILE_INCREMENT(field) do { \
        if (profile) ++profile->field;         \
    } while (0)
#else
#define COMPACT_PROFILE_PARAMETER
#define COMPACT_PROFILE_ARGUMENT(video)
#define COMPACT_PROFILE_FORWARD
#define COMPACT_PROFILE_INCREMENT(field) do { } while (0)
#endif

static int compact_clamp_coordinate(int value, int extent)
{
    if (value < 0) return 0;
    if (value >= extent) return extent - 1;
    return value;
}

static int compact_half_pixel_integer(int value, int *fraction)
{
    int remainder = value % 2;
    if (remainder < 0) remainder += 2;
    *fraction = remainder;
    return (value - remainder) / 2;
}

static PV_H263_MOTION_COMP_ATTR void
CompactReferenceInteriorHalfPixel(
    const CompactYuv420Plane *plane,
    int source_x,
    int source_y,
    int fractional_x,
    int fractional_y,
    uint8 *prediction,
    int prediction_stride,
    int round1,
    int block_size)
{
    uint8 samples_a[17];
    uint8 samples_b[17];
    uint8 *current = samples_a;
    uint8 *next = samples_b;
    const int sample_width = block_size + fractional_x;
    int row;
    int column;

    compact_yuv420_unpack_corrected_samples(
        plane->data + (size_t)source_y * plane->stride,
        source_x, source_y, plane->bits, plane->correction,
        plane->correction_stride, current, sample_width);

    for (row = 0; row < block_size; ++row)
    {
        uint8 *output = prediction + row * prediction_stride;
        if (fractional_y)
        {
            const int next_y = source_y + row + 1;
            compact_yuv420_unpack_corrected_samples(
                plane->data + (size_t)next_y * plane->stride,
                source_x, next_y, plane->bits, plane->correction,
                plane->correction_stride, next, sample_width);
        }

        if (fractional_x && !fractional_y)
        {
            for (column = 0; column < block_size; ++column)
            {
                output[column] = (uint8)(
                    (current[column] + current[column + 1] +
                     round1) >> 1);
            }
            if (row + 1 < block_size)
            {
                const int next_y = source_y + row + 1;
                compact_yuv420_unpack_corrected_samples(
                    plane->data + (size_t)next_y * plane->stride,
                    source_x, next_y, plane->bits, plane->correction,
                    plane->correction_stride, current, sample_width);
            }
        }
        else if (!fractional_x)
        {
            for (column = 0; column < block_size; ++column)
            {
                output[column] = (uint8)(
                    (current[column] + next[column] +
                     round1) >> 1);
            }
        }
        else
        {
            for (column = 0; column < block_size; ++column)
            {
                output[column] = (uint8)(
                    (current[column] + current[column + 1] +
                     next[column] + next[column + 1] +
                     round1 + 1) >> 2);
            }
        }

        if (fractional_y)
        {
            uint8 *swap = current;
            current = next;
            next = swap;
        }
    }
}

static PV_H263_MOTION_COMP_ATTR void CompactReferenceCopy(
    const CompactYuv420Plane *plane,
    int source_x,
    int source_y,
    uint8 *destination,
    int destination_stride,
    int width,
    int height
    COMPACT_PROFILE_PARAMETER)
{
    int row;
    COMPACT_PROFILE_INCREMENT(compact_copy_calls);
    for (row = 0; row < height; ++row)
    {
        int y = source_y + row;
        compact_yuv420_unpack_corrected_samples(
            plane->data + (size_t)y * plane->stride,
            source_x, y, plane->bits, plane->correction,
            plane->correction_stride,
            destination + row * destination_stride, width);
    }
}

static PV_H263_MOTION_COMP_ATTR void CompactReferencePrediction(
    const CompactYuv420Plane *plane,
    int xpred,
    int ypred,
    uint8 *prediction,
    int prediction_stride,
    int round1
    COMPACT_PROFILE_PARAMETER)
{
    int fractional_x;
    int fractional_y;
    int source_x = compact_half_pixel_integer(xpred, &fractional_x);
    int source_y = compact_half_pixel_integer(ypred, &fractional_y);
    int patch_width = B_SIZE + fractional_x;
    int patch_height = B_SIZE + fractional_y;
    uint8 patch[(B_SIZE + 1) * (B_SIZE + 1)];
    int row;
    int column;
    int edge_prediction =
        source_x < 0 || source_x + patch_width > plane->width ||
        source_y < 0 || source_y + patch_height > plane->height;

    COMPACT_PROFILE_INCREMENT(compact_prediction8_calls);
    if (!fractional_x && !fractional_y)
        COMPACT_PROFILE_INCREMENT(compact_integer_predictions);
    else if (fractional_x && !fractional_y)
        COMPACT_PROFILE_INCREMENT(compact_horizontal_predictions);
    else if (!fractional_x && fractional_y)
        COMPACT_PROFILE_INCREMENT(compact_vertical_predictions);
    else
        COMPACT_PROFILE_INCREMENT(compact_diagonal_predictions);
    if (edge_prediction)
        COMPACT_PROFILE_INCREMENT(compact_edge_predictions);

    if (!fractional_x && !fractional_y &&
            source_x >= 0 && source_x + B_SIZE <= plane->width &&
            source_y >= 0 && source_y + B_SIZE <= plane->height)
    {
        CompactReferenceCopy(
            plane, source_x, source_y, prediction,
            prediction_stride, B_SIZE, B_SIZE
            COMPACT_PROFILE_FORWARD);
        return;
    }

    if (!edge_prediction)
    {
        CompactReferenceInteriorHalfPixel(
            plane, source_x, source_y, fractional_x, fractional_y,
            prediction, prediction_stride, round1, B_SIZE);
        return;
    }

    for (row = 0; row < patch_height; ++row)
    {
        int y = compact_clamp_coordinate(
            source_y + row, plane->height);
        uint8 *patch_row = patch + row * patch_width;
        const uint8 *packed_row =
            plane->data + (size_t)y * plane->stride;
        if (source_x >= 0 &&
                source_x + patch_width <= plane->width)
        {
            compact_yuv420_unpack_corrected_samples(
                packed_row, source_x, y, plane->bits,
                plane->correction, plane->correction_stride,
                patch_row, patch_width);
        }
        else
        {
            for (column = 0; column < patch_width; ++column)
            {
                int x = compact_clamp_coordinate(
                    source_x + column, plane->width);
                patch_row[column] =
                    compact_yuv420_corrected_sample(
                        packed_row, x, y, plane->bits,
                        plane->correction,
                        plane->correction_stride);
            }
        }
    }

    for (row = 0; row < B_SIZE; ++row)
    {
        const uint8 *patch_row = patch + row * patch_width;
        const uint8 *next_row = patch_row + patch_width;
        uint8 *output = prediction + row * prediction_stride;
        for (column = 0; column < B_SIZE; ++column)
        {
            if (!fractional_x && !fractional_y)
            {
                output[column] = patch_row[column];
            }
            else if (fractional_x && !fractional_y)
            {
                output[column] = (uint8)(
                    (patch_row[column] + patch_row[column + 1] +
                     round1) >> 1);
            }
            else if (!fractional_x && fractional_y)
            {
                output[column] = (uint8)(
                    (patch_row[column] + next_row[column] +
                     round1) >> 1);
            }
            else
            {
                output[column] = (uint8)(
                    (patch_row[column] + patch_row[column + 1] +
                     next_row[column] + next_row[column + 1] +
                     round1 + 1) >> 2);
            }
        }
    }
}

static PV_H263_MOTION_COMP_ATTR void CompactReferencePrediction16(
    const CompactYuv420Plane *plane,
    int xpred,
    int ypred,
    uint8 *prediction,
    int prediction_stride,
    int round1
    COMPACT_PROFILE_PARAMETER)
{
    int fractional_x;
    int fractional_y;
    int source_x = compact_half_pixel_integer(xpred, &fractional_x);
    int source_y = compact_half_pixel_integer(ypred, &fractional_y);
    int patch_width = 16 + fractional_x;
    int patch_height = 16 + fractional_y;
    uint8 patch[17 * 17];
    int row;
    int column;
    int edge_prediction =
        source_x < 0 || source_x + patch_width > plane->width ||
        source_y < 0 || source_y + patch_height > plane->height;

    COMPACT_PROFILE_INCREMENT(compact_prediction16_calls);
    if (!fractional_x && !fractional_y)
        COMPACT_PROFILE_INCREMENT(compact_integer_predictions);
    else if (fractional_x && !fractional_y)
        COMPACT_PROFILE_INCREMENT(compact_horizontal_predictions);
    else if (!fractional_x && fractional_y)
        COMPACT_PROFILE_INCREMENT(compact_vertical_predictions);
    else
        COMPACT_PROFILE_INCREMENT(compact_diagonal_predictions);
    if (edge_prediction)
        COMPACT_PROFILE_INCREMENT(compact_edge_predictions);

    if (!fractional_x && !fractional_y &&
            source_x >= 0 && source_x + 16 <= plane->width &&
            source_y >= 0 && source_y + 16 <= plane->height)
    {
        CompactReferenceCopy(
            plane, source_x, source_y, prediction,
            prediction_stride, 16, 16
            COMPACT_PROFILE_FORWARD);
        return;
    }

    if (!edge_prediction)
    {
        CompactReferenceInteriorHalfPixel(
            plane, source_x, source_y, fractional_x, fractional_y,
            prediction, prediction_stride, round1, 16);
        return;
    }

    for (row = 0; row < patch_height; ++row)
    {
        int y = compact_clamp_coordinate(
            source_y + row, plane->height);
        uint8 *patch_row = patch + row * patch_width;
        const uint8 *packed_row =
            plane->data + (size_t)y * plane->stride;
        if (source_x >= 0 &&
                source_x + patch_width <= plane->width)
        {
            compact_yuv420_unpack_corrected_samples(
                packed_row, source_x, y, plane->bits,
                plane->correction, plane->correction_stride,
                patch_row, patch_width);
        }
        else
        {
            for (column = 0; column < patch_width; ++column)
            {
                int x = compact_clamp_coordinate(
                    source_x + column, plane->width);
                patch_row[column] =
                    compact_yuv420_corrected_sample(
                        packed_row, x, y, plane->bits,
                        plane->correction,
                        plane->correction_stride);
            }
        }
    }

    for (row = 0; row < 16; ++row)
    {
        const uint8 *patch_row = patch + row * patch_width;
        const uint8 *next_row = patch_row + patch_width;
        uint8 *output = prediction + row * prediction_stride;
        for (column = 0; column < 16; ++column)
        {
            if (fractional_x && !fractional_y)
            {
                output[column] = (uint8)(
                    (patch_row[column] + patch_row[column + 1] +
                     round1) >> 1);
            }
            else if (!fractional_x && fractional_y)
            {
                output[column] = (uint8)(
                    (patch_row[column] + next_row[column] +
                     round1) >> 1);
            }
            else if (fractional_x && fractional_y)
            {
                output[column] = (uint8)(
                    (patch_row[column] + patch_row[column + 1] +
                     next_row[column] + next_row[column + 1] +
                     round1 + 1) >> 2);
            }
            else
            {
                output[column] = patch_row[column];
            }
        }
    }
}

/*----------------------------------------------------------------------------
; EXTERNAL FUNCTION REFERENCES
; Declare functions defined elsewhere and referenced in this module
----------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------
; EXTERNAL GLOBAL STORE/BUFFER/POINTER REFERENCES
; Declare variables used in this module but defined elsewhere
----------------------------------------------------------------------------*/


/*----------------------------------------------------------------------------
; FUNCTION CODE
----------------------------------------------------------------------------*/

/** modified 3 August 2005 to do prediction and put the results in
video->mblock->pred_block, no adding with residue */

PV_H263_MOTION_COMP_ATTR void  MBMotionComp(
    VideoDecData *video,
    int CBP
)
{

    /*----------------------------------------------------------------------------
    ; Define all local variables
    ----------------------------------------------------------------------------*/
    /* Previous Video Object Plane */
    Vop *prev = video->prevVop;

    /* Current Macroblock (MB) in the VOP */
    int mbnum = video->mbnum;

    /* Number of MB per data row */
    int MB_in_width = video->nMBPerRow;
    int ypos, xpos;
    PIXEL *c_comp, *c_prev;
    PIXEL *cu_comp, *cu_prev;
    PIXEL *cv_comp, *cv_prev;
    int height, width, pred_width;
    int imv, mvwidth;
    int32 offset, output_offset;
    uint8 mode;
    uint8 *pred_block, *pred;

    /* Motion vector (dx,dy) in half-pel resolution */
    int dx, dy;

    MOT px[4], py[4];
    int xpred, ypred;
    int xsum;
    int round1;
    int compact_luma_complete = 0;
    const CompactYuv420Frame *compact_reference =
        video->videoDecControls->compactReference;
    /*----------------------------------------------------------------------------
    ; Function body here
    ----------------------------------------------------------------------------*/
    /* Set rounding type */
    /* change from array to single 09/29/2000 */
    round1 = (int)(1 - video->currVop->roundingType);

    /* width of luminance data in pixels (y axis) */
    width = video->width;

    /* heigth of luminance data in pixels (x axis) */
    height = video->height;

    /* number of blocks per row */
    mvwidth = MB_in_width << 1;

    /* starting y position in current MB; origin of MB */
    ypos = video->mbnum_row << 4 ;
    /* starting x position in current MB; origin of MB */
    xpos = video->mbnum_col << 4 ;

    /* offset to (x,y) position in current luminance MB */
    /* in pixel resolution                              */
    /* ypos*width -> row, +x -> column */
    offset = (int32)ypos * width + xpos;
    output_offset = PVCurrentOutputOffset(video, xpos, ypos);

    /* get mode for current MB */
    mode = video->headerInfo.Mode[mbnum];

    /* block index */
    /* imv = (xpos/8) + ((ypos/8) * mvwidth) */
    imv = (offset >> 6) - (xpos >> 6) + (xpos >> 3);
    if (mode & INTER_1VMASK)
    {
        dx = px[0] = px[1] = px[2] = px[3] = video->motX[imv];
        dy = py[0] = py[1] = py[2] = py[3] = video->motY[imv];
        if ((dx & 3) == 0)
        {
            dx = dx >> 1;
        }
        else
        {
            /* x component of MV is or'ed for rounding (?) */
            dx = (dx >> 1) | 1;
        }

        /* y component of motion vector; divide by 2 for to */
        /* convert to full-pel resolution.                  */
        if ((dy & 3) == 0)
        {
            dy = dy >> 1;
        }
        else
        {
            /* y component of MV is or'ed for rounding (?) */
            dy = (dy >> 1) | 1;
        }
    }
    else
    {
        px[0] = video->motX[imv];
        px[1] = video->motX[imv+1];
        px[2] = video->motX[imv+mvwidth];
        px[3] = video->motX[imv+mvwidth+1];
        xsum = px[0] + px[1] + px[2] + px[3];
        dx = PV_SIGN(xsum) * (roundtab16[(PV_ABS(xsum)) & 0xF] +
                              (((PV_ABS(xsum)) >> 4) << 1));
        py[0] = video->motY[imv];
        py[1] = video->motY[imv+1];
        py[2] = video->motY[imv+mvwidth];
        py[3] = video->motY[imv+mvwidth+1];
        xsum = py[0] + py[1] + py[2] + py[3];
        dy = PV_SIGN(xsum) * (roundtab16[(PV_ABS(xsum)) & 0xF] +
                              (((PV_ABS(xsum)) >> 4) << 1));
    }

    /* Pointer to previous luminance frame */
    c_prev  = prev->yChan;
    if (!c_prev && !compact_reference) {
        ALOGE("b/35269635");
        android_errorWriteLog(0x534e4554, "35269635");
        return;
    }

    pred_block = video->mblock->pred_block;

    /* some blocks have no residue or INTER4V */
    /*if (mode == MODE_INTER4V)   05/08/15 */
    /* Motion Compensation for an 8x8 block within a MB */
    /* (4 MV per MB) */



    /* Call function that performs luminance prediction */
    /*      luminance_pred_mode_inter4v(xpos, ypos, px, py, c_prev,
                    video->mblock->pred_block, width, height,
                    round1, mvwidth, &xsum, &ysum);*/
    c_comp = video->currVop->yChan + output_offset;


    xpred = (int)((xpos << 1) + px[0]);
    ypred = (int)((ypos << 1) + py[0]);

    if (compact_reference && (mode & INTER_1VMASK))
    {
        const int luma_cbp = CBP & 0x3c;
        if (luma_cbp == 0)
        {
            CompactReferencePrediction16(
                &compact_reference->y, xpred, ypred,
                c_comp, width, round1
                COMPACT_PROFILE_ARGUMENT(video));
            compact_luma_complete = 1;
        }
    }

    if ((CBP >> 5)&1)
    {
        pred = pred_block;
        pred_width = 16;
    }
    else
    {
        pred = c_comp;
        pred_width = width;
    }

    /* check whether the MV points outside the frame */
    if (compact_reference)
    {
        if (!compact_luma_complete)
        {
            CompactReferencePrediction(
                &compact_reference->y, xpred, ypred,
                pred, pred_width, round1
                COMPACT_PROFILE_ARGUMENT(video));
        }
    }
    else if (xpred >= 0 && xpred <= ((width << 1) - (2*B_SIZE)) &&
            ypred >= 0 && ypred <= ((height << 1) - (2*B_SIZE)))
    {   /*****************************/
        /* (x,y) is inside the frame */
        /*****************************/
        ;
        GetPredAdvBTable[ypred&1][xpred&1](c_prev + (xpred >> 1) + ((ypred >> 1)*width),
                                           pred, width, (pred_width << 1) | round1);
    }
    else
    {   /******************************/
        /* (x,y) is outside the frame */
        /******************************/
        GetPredOutside(xpred, ypred, c_prev,
                       pred, width, height, round1, pred_width);
    }


    /* Compute prediction values over current luminance MB */
    /* (blocks 1); add motion vector prior to input;       */
    /* add 8 to x_pos to advance to next block         */
    xpred = (int)(((xpos + B_SIZE) << 1) + px[1]);
    ypred = (int)((ypos << 1) + py[1]);

    if ((CBP >> 4)&1)
    {
        pred = pred_block + 8;
        pred_width = 16;
    }
    else
    {
        pred = c_comp + 8;
        pred_width = width;
    }

    /* check whether the MV points outside the frame */
    if (compact_reference)
    {
        if (!compact_luma_complete)
        {
            CompactReferencePrediction(
                &compact_reference->y, xpred, ypred,
                pred, pred_width, round1
                COMPACT_PROFILE_ARGUMENT(video));
        }
    }
    else if (xpred >= 0 && xpred <= ((width << 1) - (2*B_SIZE)) &&
            ypred >= 0 && ypred <= ((height << 1) - (2*B_SIZE)))
    {   /*****************************/
        /* (x,y) is inside the frame */
        /*****************************/
        GetPredAdvBTable[ypred&1][xpred&1](c_prev + (xpred >> 1) + ((ypred >> 1)*width),
                                           pred, width, (pred_width << 1) | round1);
    }
    else
    {   /******************************/
        /* (x,y) is outside the frame */
        /******************************/
        GetPredOutside(xpred, ypred, c_prev,
                       pred, width, height, round1, pred_width);
    }



    /* Compute prediction values over current luminance MB */
    /* (blocks 2); add motion vector prior to input        */
    /* add 8 to y_pos to advance to block on next row      */
    xpred = (int)((xpos << 1) + px[2]);
    ypred = (int)(((ypos + B_SIZE) << 1) + py[2]);

    if ((CBP >> 3)&1)
    {
        pred = pred_block + 128;
        pred_width = 16;
    }
    else
    {
        pred = c_comp + (width << 3);
        pred_width = width;
    }

    /* check whether the MV points outside the frame */
    if (compact_reference)
    {
        if (!compact_luma_complete)
        {
            CompactReferencePrediction(
                &compact_reference->y, xpred, ypred,
                pred, pred_width, round1
                COMPACT_PROFILE_ARGUMENT(video));
        }
    }
    else if (xpred >= 0 && xpred <= ((width << 1) - (2*B_SIZE)) &&
            ypred >= 0 && ypred <= ((height << 1) - (2*B_SIZE)))
    {   /*****************************/
        /* (x,y) is inside the frame */
        /*****************************/
        GetPredAdvBTable[ypred&1][xpred&1](c_prev + (xpred >> 1) + ((ypred >> 1)*width),
                                           pred, width, (pred_width << 1) | round1);
    }
    else
    {   /******************************/
        /* (x,y) is outside the frame */
        /******************************/
        GetPredOutside(xpred, ypred, c_prev,
                       pred, width, height, round1, pred_width);
    }



    /* Compute prediction values over current luminance MB */
    /* (blocks 3); add motion vector prior to input;       */
    /* add 8 to x_pos and y_pos to advance to next block   */
    /* on next row                         */
    xpred = (int)(((xpos + B_SIZE) << 1) + px[3]);
    ypred = (int)(((ypos + B_SIZE) << 1) + py[3]);

    if ((CBP >> 2)&1)
    {
        pred = pred_block + 136;
        pred_width = 16;
    }
    else
    {
        pred = c_comp + (width << 3) + 8;
        pred_width = width;
    }

    /* check whether the MV points outside the frame */
    if (compact_reference)
    {
        if (!compact_luma_complete)
        {
            CompactReferencePrediction(
                &compact_reference->y, xpred, ypred,
                pred, pred_width, round1
                COMPACT_PROFILE_ARGUMENT(video));
        }
    }
    else if (xpred >= 0 && xpred <= ((width << 1) - (2*B_SIZE)) &&
            ypred >= 0 && ypred <= ((height << 1) - (2*B_SIZE)))
    {   /*****************************/
        /* (x,y) is inside the frame */
        /*****************************/
        GetPredAdvBTable[ypred&1][xpred&1](c_prev + (xpred >> 1) + ((ypred >> 1)*width),
                                           pred, width, (pred_width << 1) | round1);
    }
    else
    {   /******************************/
        /* (x,y) is outside the frame */
        /******************************/
        GetPredOutside(xpred, ypred, c_prev,
                       pred, width, height, round1, pred_width);
    }
    /* Call function to set de-blocking and de-ringing */
    /*   semaphores for luminance                      */



    /* xpred and ypred calculation for Chrominance is */
    /* in full-pel resolution.                        */

    /* Chrominance */
    /* width of chrominance data in pixels (y axis) */
    width >>= 1;

    /* heigth of chrominance data in pixels (x axis) */
    height >>= 1;

    /* Pointer to previous chrominance b frame */
    cu_prev = prev->uChan;

    /* Pointer to previous chrominance r frame */
    cv_prev = prev->vChan;

    /* x position in prediction data offset by motion vector */
    /* xpred calculation for Chrominance is in full-pel      */
    /* resolution.                                           */
    xpred = xpos + dx;

    /* y position in prediction data offset by motion vector */
    /* ypred calculation for Chrominance is in full-pel      */
    /* resolution.                                           */
    ypred = ypos + dy;

    cu_comp = video->currVop->uChan +
              (output_offset >> 2) + (xpos >> 2);
    cv_comp = video->currVop->vChan +
              (output_offset >> 2) + (xpos >> 2);

    /* Call function that performs chrominance prediction */
    /*      chrominance_pred(xpred, ypred, cu_prev, cv_prev,
            pred_block, width_uv, height_uv,
            round1);*/
    if (compact_reference)
    {
        if ((CBP >> 1)&1)
        {
            pred = pred_block + 256;
            pred_width = 16;
        }
        else
        {
            pred = cu_comp;
            pred_width = width;
        }
        CompactReferencePrediction(
            &compact_reference->u, xpred, ypred,
            pred, pred_width, round1
            COMPACT_PROFILE_ARGUMENT(video));

        if (CBP&1)
        {
            pred = pred_block + 264;
            pred_width = 16;
        }
        else
        {
            pred = cv_comp;
            pred_width = width;
        }
        CompactReferencePrediction(
            &compact_reference->v, xpred, ypred,
            pred, pred_width, round1
            COMPACT_PROFILE_ARGUMENT(video));
        return;
    }
    if (xpred >= 0 && xpred <= ((width << 1) - (2*B_SIZE)) && ypred >= 0 &&
            ypred <= ((height << 1) - (2*B_SIZE)))
    {
        /*****************************/
        /* (x,y) is inside the frame */
        /*****************************/
        if ((CBP >> 1)&1)
        {
            pred = pred_block + 256;
            pred_width = 16;
        }
        else
        {
            pred = cu_comp;
            pred_width = width;
        }

        /* Compute prediction for Chrominance b (block[4]) */
        GetPredAdvBTable[ypred&1][xpred&1](cu_prev + (xpred >> 1) + ((ypred >> 1)*width),
                                           pred, width, (pred_width << 1) | round1);

        if (CBP&1)
        {
            pred = pred_block + 264;
            pred_width = 16;
        }
        else
        {
            pred = cv_comp;
            pred_width = width;
        }
        /* Compute prediction for Chrominance r (block[5]) */
        GetPredAdvBTable[ypred&1][xpred&1](cv_prev + (xpred >> 1) + ((ypred >> 1)*width),
                                           pred, width, (pred_width << 1) | round1);

        return ;
    }
    else
    {
        /******************************/
        /* (x,y) is outside the frame */
        /******************************/
        if ((CBP >> 1)&1)
        {
            pred = pred_block + 256;
            pred_width = 16;
        }
        else
        {
            pred = cu_comp;
            pred_width = width;
        }

        /* Compute prediction for Chrominance b (block[4]) */
        GetPredOutside(xpred, ypred,    cu_prev,
                       pred, width, height, round1, pred_width);

        if (CBP&1)
        {
            pred = pred_block + 264;
            pred_width = 16;
        }
        else
        {
            pred = cv_comp;
            pred_width = width;
        }

        /* Compute prediction for Chrominance r (block[5]) */
        GetPredOutside(xpred, ypred,    cv_prev,
                       pred, width, height, round1, pred_width);

        return ;
    }

}

/*** special function for skipped macroblock,  Aug 15, 2005 */
void  SkippedMBMotionComp(
    VideoDecData *video
)
{
    Vop *prev = video->prevVop;
    Vop *comp;
    int ypos, xpos;
    PIXEL *c_comp, *c_prev;
    PIXEL *cu_comp, *cu_prev;
    PIXEL *cv_comp, *cv_prev;
    int width, width_uv;
    int32 offset, output_offset;
    const CompactYuv420Frame *compact_reference =
        video->videoDecControls->compactReference;

    width = video->width;
    width_uv  = width >> 1;
    ypos = video->mbnum_row << 4 ;
    xpos = video->mbnum_col << 4 ;
    offset = (int32)ypos * width + xpos;
    output_offset = PVCurrentOutputOffset(video, xpos, ypos);


    /* zero motion compensation for previous frame */
    /*mby*width + mbx;*/
    c_prev  = prev->yChan;
    if (!c_prev && !compact_reference) {
        ALOGE("b/35269635");
        android_errorWriteLog(0x534e4554, "35269635");
        return;
    }
    if (compact_reference)
    {
        cu_prev = NULL;
        cv_prev = NULL;
    }
    else
    {
        c_prev += offset;
        /*by*width_uv + bx;*/
        cu_prev = prev->uChan + (offset >> 2) + (xpos >> 2);
        /*by*width_uv + bx;*/
        cv_prev = prev->vChan + (offset >> 2) + (xpos >> 2);
    }

    comp = video->currVop;

    c_comp  = comp->yChan + output_offset;
    cu_comp =
        comp->uChan + (output_offset >> 2) + (xpos >> 2);
    cv_comp =
        comp->vChan + (output_offset >> 2) + (xpos >> 2);

    if (compact_reference)
    {
        /*
         * The compact-output row commit copies MODE_SKIPPED blocks directly
         * from compact_reference. No residual reconstruction or current-frame
         * prediction reads these rolling byte-plane samples, so unpacking
         * them here would only create data that the packer deliberately
         * ignores.
         */
        return;
    }

    /* Copy previous reconstructed frame into the current frame */
    PutSKIPPED_MB(c_comp,  c_prev, width);
    PutSKIPPED_B(cu_comp, cu_prev, width_uv);
    PutSKIPPED_B(cv_comp, cv_prev, width_uv);

    /*  10/24/2000 post_processing semaphore generation */
    /*----------------------------------------------------------------------------
    ; Return nothing or data or data pointer
    ----------------------------------------------------------------------------*/

    return;
}
