/*
 * Copyright 2000 ATI Technologies Inc., Markham, Ontario, and
 *                VA Linux Systems Inc., Fremont, California.
 *
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation on the rights to use, copy, modify, merge,
 * publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial
 * portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NON-INFRINGEMENT.  IN NO EVENT SHALL ATI, VA LINUX SYSTEMS AND/OR
 * THEIR SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include <stdio.h>

/* X and server generic header files */
#include "xf86.h"
#include "xf86_OSproc.h"
#include "fbdevhw.h"
#include "vgaHW.h"
#include "xf86Modes.h"

/* Driver data structures */
#include "radeon.h"
#include "radeon_reg.h"
#include "radeon_macros.h"
#include "radeon_probe.h"
#include "radeon_version.h"

#ifdef XF86DRI
#define _XF86DRI_SERVER_
#include "radeon_dri.h"
#include "radeon_sarea.h"
#include "sarea.h"
#endif

void radeon_crtc_load_lut(xf86CrtcPtr crtc);

static void
radeon_crtc_dpms(xf86CrtcPtr crtc, int mode)
{
    int mask;
    ScrnInfoPtr pScrn = crtc->scrn;
    RADEONCrtcPrivatePtr radeon_crtc = crtc->driver_private;
    RADEONInfoPtr info = RADEONPTR(pScrn);
    unsigned char *RADEONMMIO = info->MMIO;
    
    mask = radeon_crtc->crtc_id ? (RADEON_CRTC2_DISP_DIS | RADEON_CRTC2_VSYNC_DIS | RADEON_CRTC2_HSYNC_DIS | RADEON_CRTC2_DISP_REQ_EN_B) : (RADEON_CRTC_DISPLAY_DIS | RADEON_CRTC_HSYNC_DIS | RADEON_CRTC_VSYNC_DIS);


    switch(mode) {
    case DPMSModeOn:
	if (radeon_crtc->crtc_id) {
	    OUTREGP(RADEON_CRTC2_GEN_CNTL, 0, ~mask);
	} else {
	    OUTREGP(RADEON_CRTC_GEN_CNTL, 0, ~RADEON_CRTC_DISP_REQ_EN_B);
	    OUTREGP(RADEON_CRTC_EXT_CNTL, 0, ~mask);
	}
	break;
    case DPMSModeStandby:
	if (radeon_crtc->crtc_id) {
	    OUTREGP(RADEON_CRTC2_GEN_CNTL, (RADEON_CRTC2_DISP_DIS | RADEON_CRTC2_HSYNC_DIS), ~mask);
	} else {
	    OUTREGP(RADEON_CRTC_GEN_CNTL, 0, ~RADEON_CRTC_DISP_REQ_EN_B);
	    OUTREGP(RADEON_CRTC_EXT_CNTL, (RADEON_CRTC_DISPLAY_DIS | RADEON_CRTC_HSYNC_DIS), ~mask);
	}
	break;
    case DPMSModeSuspend:
	if (radeon_crtc->crtc_id) {
	    OUTREGP(RADEON_CRTC2_GEN_CNTL, (RADEON_CRTC2_DISP_DIS | RADEON_CRTC2_VSYNC_DIS), ~mask);
	} else {
	    OUTREGP(RADEON_CRTC_GEN_CNTL, 0, ~RADEON_CRTC_DISP_REQ_EN_B);
	    OUTREGP(RADEON_CRTC_EXT_CNTL, (RADEON_CRTC_DISPLAY_DIS | RADEON_CRTC_VSYNC_DIS), ~mask);
	}
	break;
    case DPMSModeOff:
	if (radeon_crtc->crtc_id) {
	    OUTREGP(RADEON_CRTC2_GEN_CNTL, mask, ~mask);
	} else {
	    OUTREGP(RADEON_CRTC_GEN_CNTL, RADEON_CRTC_DISP_REQ_EN_B, ~RADEON_CRTC_DISP_REQ_EN_B);
	    OUTREGP(RADEON_CRTC_EXT_CNTL, mask, ~mask);
	}
	break;
    }
  
    if (mode != DPMSModeOff)
	radeon_crtc_load_lut(crtc);  
}

static Bool
radeon_crtc_mode_fixup(xf86CrtcPtr crtc, DisplayModePtr mode,
		     DisplayModePtr adjusted_mode)
{
    return TRUE;
}

static void
radeon_crtc_mode_prepare(xf86CrtcPtr crtc)
{
    radeon_crtc_dpms(crtc, DPMSModeOff);
}

/* Define common registers for requested video mode */
static void
RADEONInitCommonRegisters(RADEONSavePtr save, RADEONInfoPtr info)
{
    save->ovr_clr            = 0;
    save->ovr_wid_left_right = 0;
    save->ovr_wid_top_bottom = 0;
    save->ov0_scale_cntl     = 0;
    save->subpic_cntl        = 0;
    save->viph_control       = 0;
    save->i2c_cntl_1         = 0;
    save->rbbm_soft_reset    = 0;
    save->cap0_trig_cntl     = 0;
    save->cap1_trig_cntl     = 0;
    save->bus_cntl           = info->BusCntl;
    /*
     * If bursts are enabled, turn on discards
     * Radeon doesn't have write bursts
     */
    if (save->bus_cntl & (RADEON_BUS_READ_BURST))
	save->bus_cntl |= RADEON_BUS_RD_DISCARD_EN;
}

/* Define CRTC registers for requested video mode */
static Bool
RADEONInitCrtcRegisters(xf86CrtcPtr crtc, RADEONSavePtr save,
			DisplayModePtr mode, int x, int y)
{
    ScrnInfoPtr pScrn = crtc->scrn;
    RADEONInfoPtr  info       = RADEONPTR(pScrn);
    //RADEONEntPtr pRADEONEnt   = RADEONEntPriv(pScrn);
    unsigned char *RADEONMMIO = info->MMIO;
    int    format;
    int    hsync_start;
    int    hsync_wid;
    int    vsync_wid;
    int    Base;
#ifdef XF86DRI
    RADEONSAREAPrivPtr pSAREAPriv;
    XF86DRISAREAPtr pSAREA;
#endif

    switch (info->CurrentLayout.pixel_code) {
    case 4:  format = 1; break;
    case 8:  format = 2; break;
    case 15: format = 3; break;      /*  555 */
    case 16: format = 4; break;      /*  565 */
    case 24: format = 5; break;      /*  RGB */
    case 32: format = 6; break;      /* xRGB */
    default:
	xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
		   "Unsupported pixel depth (%d)\n",
		   info->CurrentLayout.bitsPerPixel);
	return FALSE;
    }

    save->bios_4_scratch = info->SavedReg.bios_4_scratch;
    save->crtc_gen_cntl = (RADEON_CRTC_EXT_DISP_EN
			   | RADEON_CRTC_EN
			   | (format << 8)
			   | ((mode->Flags & V_DBLSCAN)
			      ? RADEON_CRTC_DBL_SCAN_EN
			      : 0)
			   | ((mode->Flags & V_CSYNC)
			      ? RADEON_CRTC_CSYNC_EN
			      : 0)
			   | ((mode->Flags & V_INTERLACE)
			      ? RADEON_CRTC_INTERLACE_EN
			      : 0));

    save->crtc_ext_cntl |= (RADEON_XCRT_CNT_EN|
			    RADEON_CRTC_VSYNC_DIS |
			    RADEON_CRTC_HSYNC_DIS |
			    RADEON_CRTC_DISPLAY_DIS);

    save->surface_cntl = 0;
    save->disp_merge_cntl = info->SavedReg.disp_merge_cntl;
    save->disp_merge_cntl &= ~RADEON_DISP_RGB_OFFSET_EN;

#if X_BYTE_ORDER == X_BIG_ENDIAN
    /* We must set both apertures as they can be both used to map the entire
     * video memory. -BenH.
     */
    switch (pScrn->bitsPerPixel) {
    case 16:
	save->surface_cntl |= RADEON_NONSURF_AP0_SWP_16BPP;
	save->surface_cntl |= RADEON_NONSURF_AP1_SWP_16BPP;
	break;

    case 32:
	save->surface_cntl |= RADEON_NONSURF_AP0_SWP_32BPP;
	save->surface_cntl |= RADEON_NONSURF_AP1_SWP_32BPP;
	break;
    }
#endif

    save->crtc_more_cntl = 0;
    if ((info->ChipFamily == CHIP_FAMILY_RS100) ||
        (info->ChipFamily == CHIP_FAMILY_RS200)) {
        /* This is to workaround the asic bug for RMX, some versions
           of BIOS dosen't have this register initialized correctly.
	*/
        save->crtc_more_cntl |= RADEON_CRTC_H_CUTOFF_ACTIVE_EN;
    }

    save->crtc_h_total_disp = ((((mode->CrtcHTotal / 8) - 1) & 0x3ff)
			       | ((((mode->CrtcHDisplay / 8) - 1) & 0x1ff)
				  << 16));

    hsync_wid = (mode->CrtcHSyncEnd - mode->CrtcHSyncStart) / 8;
    if (!hsync_wid)       hsync_wid = 1;
    if (hsync_wid > 0x3f) hsync_wid = 0x3f;
    hsync_start = mode->CrtcHSyncStart - 8;

    save->crtc_h_sync_strt_wid = ((hsync_start & 0x1fff)
				  | (hsync_wid << 16)
				  | ((mode->Flags & V_NHSYNC)
				     ? RADEON_CRTC_H_SYNC_POL
				     : 0));

				/* This works for double scan mode. */
    save->crtc_v_total_disp = (((mode->CrtcVTotal - 1) & 0xffff)
			       | ((mode->CrtcVDisplay - 1) << 16));

    vsync_wid = mode->CrtcVSyncEnd - mode->CrtcVSyncStart;
    if (!vsync_wid)       vsync_wid = 1;
    if (vsync_wid > 0x1f) vsync_wid = 0x1f;

    save->crtc_v_sync_strt_wid = (((mode->CrtcVSyncStart - 1) & 0xfff)
				  | (vsync_wid << 16)
				  | ((mode->Flags & V_NVSYNC)
				     ? RADEON_CRTC_V_SYNC_POL
				     : 0));

    save->crtc_offset      = pScrn->fbOffset;
#ifdef XF86DRI
    if (info->allowPageFlip)
	save->crtc_offset_cntl = RADEON_CRTC_OFFSET_FLIP_CNTL;
#endif

    if (info->tilingEnabled) {
       if (IS_R300_VARIANT)
          save->crtc_offset_cntl |= (R300_CRTC_X_Y_MODE_EN |
				     R300_CRTC_MICRO_TILE_BUFFER_DIS |
				     R300_CRTC_MACRO_TILE_EN);
       else
          save->crtc_offset_cntl |= RADEON_CRTC_TILE_EN;
    }
    else {
       if (IS_R300_VARIANT)
          save->crtc_offset_cntl &= ~(R300_CRTC_X_Y_MODE_EN |
				      R300_CRTC_MICRO_TILE_BUFFER_DIS |
				      R300_CRTC_MACRO_TILE_EN);
       else
          save->crtc_offset_cntl &= ~RADEON_CRTC_TILE_EN;
    }

    save->crtc_pitch  = (((pScrn->displayWidth * pScrn->bitsPerPixel) +
			  ((pScrn->bitsPerPixel * 8) -1)) /
			 (pScrn->bitsPerPixel * 8));
    save->crtc_pitch |= save->crtc_pitch << 16;
    
    save->fp_h_sync_strt_wid = save->crtc_h_sync_strt_wid;
    save->fp_v_sync_strt_wid = save->crtc_v_sync_strt_wid;
    save->fp_crtc_h_total_disp = save->crtc_h_total_disp;
    save->fp_crtc_v_total_disp = save->crtc_v_total_disp;

    Base = pScrn->fbOffset;

    if (info->tilingEnabled) {
        if (IS_R300_VARIANT) {
	/* On r300/r400 when tiling is enabled crtc_offset is set to the address of
	 * the surface.  the x/y offsets are handled by the X_Y tile reg for each crtc
	 * Makes tiling MUCH easier.
	 */
             save->crtc_tile_x0_y0 = x | (y << 16);
             Base &= ~0x7ff;
         } else {
	     /* note we cannot really simply use the info->ModeReg.crtc_offset_cntl value, since the
		drm might have set FLIP_CNTL since we wrote that. Unfortunately FLIP_CNTL causes
		flickering when scrolling vertically in a virtual screen, possibly because crtc will
		pick up the new offset value at the end of each scanline, but the new offset_cntl value
		only after a vsync. We'd probably need to wait (in drm) for vsync and only then update
		OFFSET and OFFSET_CNTL, if the y coord has changed. Seems hard to fix. */
	     save->crtc_offset_cntl = INREG(RADEON_CRTC_OFFSET_CNTL) & ~0xf;
#if 0
	     /* try to get rid of flickering when scrolling at least for 2d */
#ifdef XF86DRI
	     if (!info->have3DWindows)
#endif
		 save->crtc_offset_cntl &= ~RADEON_CRTC_OFFSET_FLIP_CNTL;
#endif
	     
             int byteshift = info->CurrentLayout.bitsPerPixel >> 4;
             /* crtc uses 256(bytes)x8 "half-tile" start addresses? */
             int tile_addr = (((y >> 3) * info->CurrentLayout.displayWidth + x) >> (8 - byteshift)) << 11;
             Base += tile_addr + ((x << byteshift) % 256) + ((y % 8) << 8);
             save->crtc_offset_cntl = save->crtc_offset_cntl | (y % 16);
         }
    }
    else {
       int offset = y * info->CurrentLayout.displayWidth + x;
       switch (info->CurrentLayout.pixel_code) {
       case 15:
       case 16: offset *= 2; break;
       case 24: offset *= 3; break;
       case 32: offset *= 4; break;
       }
       Base += offset;
    }

    Base &= ~7;                 /* 3 lower bits are always 0 */


#ifdef XF86DRI
    if (info->directRenderingInited) {
	/* note cannot use pScrn->pScreen since this is unitialized when called from
	   RADEONScreenInit, and we need to call from there to get mergedfb + pageflip working */
        /*** NOTE: r3/4xx will need sarea and drm pageflip updates to handle the xytile regs for
	 *** pageflipping!
	 ***/
	pSAREAPriv = DRIGetSAREAPrivate(screenInfo.screens[pScrn->scrnIndex]);
	/* can't get at sarea in a semi-sane way? */
	pSAREA = (void *)((char*)pSAREAPriv - sizeof(XF86DRISAREARec));

	pSAREA->frame.x = (Base  / info->CurrentLayout.pixel_bytes)
	    % info->CurrentLayout.displayWidth;
	pSAREA->frame.y = (Base / info->CurrentLayout.pixel_bytes)
	    / info->CurrentLayout.displayWidth;
	pSAREA->frame.width = pScrn->frameX1 - x + 1;
	pSAREA->frame.height = pScrn->frameY1 - y + 1;

	if (pSAREAPriv->pfCurrentPage == 1) {
	    Base += info->backOffset - info->frontOffset;
	}
    }
#endif
    save->crtc_offset = Base;


    if (info->IsDellServer) {
	save->dac2_cntl = info->SavedReg.dac2_cntl;
	save->tv_dac_cntl = info->SavedReg.tv_dac_cntl;
	save->crtc2_gen_cntl = info->SavedReg.crtc2_gen_cntl;
	save->disp_hw_debug = info->SavedReg.disp_hw_debug;

	save->dac2_cntl &= ~RADEON_DAC2_DAC_CLK_SEL;
	save->dac2_cntl |= RADEON_DAC2_DAC2_CLK_SEL;

	/* For CRT on DAC2, don't turn it on if BIOS didn't
	   enable it, even it's detected.
	*/
	save->disp_hw_debug |= RADEON_CRT2_DISP1_SEL;
	save->tv_dac_cntl &= ~((1<<2) | (3<<8) | (7<<24) | (0xff<<16));
	save->tv_dac_cntl |= (0x03 | (2<<8) | (0x58<<16));
    }

    return TRUE;
}

/* Define CRTC2 registers for requested video mode */
static Bool
RADEONInitCrtc2Registers(xf86CrtcPtr crtc, RADEONSavePtr save,
			 DisplayModePtr mode, int x, int y)
{
    ScrnInfoPtr pScrn = crtc->scrn;
    RADEONInfoPtr  info       = RADEONPTR(pScrn);
    //RADEONEntPtr pRADEONEnt   = RADEONEntPriv(pScrn);
    unsigned char *RADEONMMIO = info->MMIO;
    int    format;
    int    hsync_start;
    int    hsync_wid;
    int    vsync_wid;
    int    Base;
#ifdef XF86DRI
    RADEONSAREAPrivPtr pSAREAPriv;
    XF86DRISAREAPtr pSAREA;
#endif

    switch (info->CurrentLayout.pixel_code) {
    case 4:  format = 1; break;
    case 8:  format = 2; break;
    case 15: format = 3; break;      /*  555 */
    case 16: format = 4; break;      /*  565 */
    case 24: format = 5; break;      /*  RGB */
    case 32: format = 6; break;      /* xRGB */
    default:
	xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
		   "Unsupported pixel depth (%d)\n",
		   info->CurrentLayout.bitsPerPixel);
	return FALSE;
    }

    save->crtc2_h_total_disp =
	((((mode->CrtcHTotal / 8) - 1) & 0x3ff)
	 | ((((mode->CrtcHDisplay / 8) - 1) & 0x1ff) << 16));

    hsync_wid = (mode->CrtcHSyncEnd - mode->CrtcHSyncStart) / 8;
    if (!hsync_wid)       hsync_wid = 1;
    if (hsync_wid > 0x3f) hsync_wid = 0x3f;
    hsync_start = mode->CrtcHSyncStart - 8;

    save->crtc2_h_sync_strt_wid = ((hsync_start & 0x1fff)
				   | (hsync_wid << 16)
				   | ((mode->Flags & V_NHSYNC)
				      ? RADEON_CRTC_H_SYNC_POL
				      : 0));

				/* This works for double scan mode. */
    save->crtc2_v_total_disp = (((mode->CrtcVTotal - 1) & 0xffff)
				| ((mode->CrtcVDisplay - 1) << 16));

    vsync_wid = mode->CrtcVSyncEnd - mode->CrtcVSyncStart;
    if (!vsync_wid)       vsync_wid = 1;
    if (vsync_wid > 0x1f) vsync_wid = 0x1f;

    save->crtc2_v_sync_strt_wid = (((mode->CrtcVSyncStart - 1) & 0xfff)
				   | (vsync_wid << 16)
				   | ((mode->Flags & V_NVSYNC)
				      ? RADEON_CRTC2_V_SYNC_POL
				      : 0));

    /* It seems all fancy options apart from pflip can be safely disabled
     */
    save->crtc2_offset      = pScrn->fbOffset;
#ifdef XF86DRI
    if (info->allowPageFlip)
	save->crtc2_offset_cntl = RADEON_CRTC_OFFSET_FLIP_CNTL;
#endif

    if (info->tilingEnabled) {
       if (IS_R300_VARIANT)
          save->crtc2_offset_cntl |= (R300_CRTC_X_Y_MODE_EN |
				      R300_CRTC_MICRO_TILE_BUFFER_DIS |
				      R300_CRTC_MACRO_TILE_EN);
       else
          save->crtc2_offset_cntl |= RADEON_CRTC_TILE_EN;
    }
    else {
       if (IS_R300_VARIANT)
          save->crtc2_offset_cntl &= ~(R300_CRTC_X_Y_MODE_EN |
				      R300_CRTC_MICRO_TILE_BUFFER_DIS |
				      R300_CRTC_MACRO_TILE_EN);
       else
          save->crtc2_offset_cntl &= ~RADEON_CRTC_TILE_EN;
    }

    save->crtc2_pitch  = ((info->CurrentLayout.displayWidth * pScrn->bitsPerPixel) +
			  ((pScrn->bitsPerPixel * 8) -1)) / (pScrn->bitsPerPixel * 8);
    save->crtc2_pitch |= save->crtc2_pitch << 16;

    save->crtc2_gen_cntl = (RADEON_CRTC2_EN
			    | (format << 8)
			    | RADEON_CRTC2_VSYNC_DIS
			    | RADEON_CRTC2_HSYNC_DIS
			    | RADEON_CRTC2_DISP_DIS
			    | ((mode->Flags & V_DBLSCAN)
			       ? RADEON_CRTC2_DBL_SCAN_EN
			       : 0)
			    | ((mode->Flags & V_CSYNC)
			       ? RADEON_CRTC2_CSYNC_EN
			       : 0)
			    | ((mode->Flags & V_INTERLACE)
			       ? RADEON_CRTC2_INTERLACE_EN
			       : 0));

    save->disp2_merge_cntl = info->SavedReg.disp2_merge_cntl;
    save->disp2_merge_cntl &= ~(RADEON_DISP2_RGB_OFFSET_EN);

    save->fp_h2_sync_strt_wid = save->crtc2_h_sync_strt_wid;
    save->fp_v2_sync_strt_wid = save->crtc2_v_sync_strt_wid;

    Base = pScrn->fbOffset;

    if (info->tilingEnabled) {
        if (IS_R300_VARIANT) {
	/* On r300/r400 when tiling is enabled crtc_offset is set to the address of
	 * the surface.  the x/y offsets are handled by the X_Y tile reg for each crtc
	 * Makes tiling MUCH easier.
	 */
             save->crtc2_tile_x0_y0 = x | (y << 16);
             Base &= ~0x7ff;
         } else {
	     /* note we cannot really simply use the info->ModeReg.crtc_offset_cntl value, since the
		drm might have set FLIP_CNTL since we wrote that. Unfortunately FLIP_CNTL causes
		flickering when scrolling vertically in a virtual screen, possibly because crtc will
		pick up the new offset value at the end of each scanline, but the new offset_cntl value
		only after a vsync. We'd probably need to wait (in drm) for vsync and only then update
		OFFSET and OFFSET_CNTL, if the y coord has changed. Seems hard to fix. */
	     save->crtc2_offset_cntl = INREG(RADEON_CRTC2_OFFSET_CNTL) & ~0xf;
#if 0
	     /* try to get rid of flickering when scrolling at least for 2d */
#ifdef XF86DRI
	     if (!info->have3DWindows)
#endif
		 save->crtc2_offset_cntl &= ~RADEON_CRTC_OFFSET_FLIP_CNTL;
#endif

             int byteshift = info->CurrentLayout.bitsPerPixel >> 4;
             /* crtc uses 256(bytes)x8 "half-tile" start addresses? */
             int tile_addr = (((y >> 3) * info->CurrentLayout.displayWidth + x) >> (8 - byteshift)) << 11;
             Base += tile_addr + ((x << byteshift) % 256) + ((y % 8) << 8);
             save->crtc2_offset_cntl = save->crtc_offset_cntl | (y % 16);
         }
    }
    else {
       int offset = y * info->CurrentLayout.displayWidth + x;
       switch (info->CurrentLayout.pixel_code) {
       case 15:
       case 16: offset *= 2; break;
       case 24: offset *= 3; break;
       case 32: offset *= 4; break;
       }
       Base += offset;
    }

    Base &= ~7;                 /* 3 lower bits are always 0 */

#ifdef XF86DRI
    if (info->directRenderingInited) {
	/* note cannot use pScrn->pScreen since this is unitialized when called from
	   RADEONScreenInit, and we need to call from there to get mergedfb + pageflip working */
        /*** NOTE: r3/4xx will need sarea and drm pageflip updates to handle the xytile regs for
	 *** pageflipping!
	 ***/
	pSAREAPriv = DRIGetSAREAPrivate(screenInfo.screens[pScrn->scrnIndex]);
	/* can't get at sarea in a semi-sane way? */
	pSAREA = (void *)((char*)pSAREAPriv - sizeof(XF86DRISAREARec));

	pSAREAPriv->crtc2_base = Base;

	if (pSAREAPriv->pfCurrentPage == 1) {
	    Base += info->backOffset - info->frontOffset;
	}
    }
#endif
    save->crtc2_offset = Base;

    /* We must set SURFACE_CNTL properly on the second screen too */
    save->surface_cntl = 0;
#if X_BYTE_ORDER == X_BIG_ENDIAN
    /* We must set both apertures as they can be both used to map the entire
     * video memory. -BenH.
     */
    switch (pScrn->bitsPerPixel) {
    case 16:
       save->surface_cntl |= RADEON_NONSURF_AP0_SWP_16BPP;
       save->surface_cntl |= RADEON_NONSURF_AP1_SWP_16BPP;
       break;

    case 32:
       save->surface_cntl |= RADEON_NONSURF_AP0_SWP_32BPP;
       save->surface_cntl |= RADEON_NONSURF_AP1_SWP_32BPP;
       break;
    }
#endif
 
    if (info->ChipFamily == CHIP_FAMILY_RS400) {
	save->rs480_unk_e30 = 0x105DC1CC; /* because I'm worth it */
	save->rs480_unk_e34 = 0x2749D000; /* AMD really should */
	save->rs480_unk_e38 = 0x29ca71dc; /* release docs */
	save->rs480_unk_e3c = 0x28FBC3AC; /* this is so a trade secret */
    }

    return TRUE;
}


/* Compute n/d with rounding */
static int RADEONDiv(int n, int d)
{
    return (n + (d / 2)) / d;
}

/* Define PLL registers for requested video mode */
static void
RADEONInitPLLRegisters(ScrnInfoPtr pScrn, RADEONInfoPtr info,
		       RADEONSavePtr save, RADEONPLLPtr pll,
		       double dot_clock)
{
    unsigned long  freq = dot_clock * 100;

    struct {
	int divider;
	int bitvalue;
    } *post_div, post_divs[]   = {
				/* From RAGE 128 VR/RAGE 128 GL Register
				 * Reference Manual (Technical Reference
				 * Manual P/N RRG-G04100-C Rev. 0.04), page
				 * 3-17 (PLL_DIV_[3:0]).
				 */
	{  1, 0 },              /* VCLK_SRC                 */
	{  2, 1 },              /* VCLK_SRC/2               */
	{  4, 2 },              /* VCLK_SRC/4               */
	{  8, 3 },              /* VCLK_SRC/8               */
	{  3, 4 },              /* VCLK_SRC/3               */
	{ 16, 5 },              /* VCLK_SRC/16              */
	{  6, 6 },              /* VCLK_SRC/6               */
	{ 12, 7 },              /* VCLK_SRC/12              */
	{  0, 0 }
    };

    if (info->UseBiosDividers) {
       save->ppll_ref_div = info->RefDivider;
       save->ppll_div_3   = info->FeedbackDivider | (info->PostDivider << 16);
       save->htotal_cntl  = 0;
       return;
    }

    if (freq > pll->max_pll_freq)      freq = pll->max_pll_freq;
    if (freq * 12 < pll->min_pll_freq) freq = pll->min_pll_freq / 12;

    for (post_div = &post_divs[0]; post_div->divider; ++post_div) {
	save->pll_output_freq = post_div->divider * freq;

	if (save->pll_output_freq >= pll->min_pll_freq
	    && save->pll_output_freq <= pll->max_pll_freq) break;
    }

    if (!post_div->divider) {
	save->pll_output_freq = freq;
	post_div = &post_divs[0];
    }

    save->dot_clock_freq = freq;
    save->feedback_div   = RADEONDiv(pll->reference_div
				     * save->pll_output_freq,
				     pll->reference_freq);
    save->post_div       = post_div->divider;

    xf86DrvMsgVerb(pScrn->scrnIndex, X_INFO, RADEON_LOGLEVEL_DEBUG,
		   "dc=%ld, of=%ld, fd=%d, pd=%d\n",
		   save->dot_clock_freq,
		   save->pll_output_freq,
		   save->feedback_div,
		   save->post_div);

    save->ppll_ref_div   = pll->reference_div;
    save->ppll_div_3     = (save->feedback_div | (post_div->bitvalue << 16));
    save->htotal_cntl    = 0;

    save->vclk_cntl = (info->SavedReg.vclk_cntl &
	    ~RADEON_VCLK_SRC_SEL_MASK) | RADEON_VCLK_SRC_SEL_PPLLCLK;

}

/* Define PLL2 registers for requested video mode */
static void
RADEONInitPLL2Registers(ScrnInfoPtr pScrn, RADEONSavePtr save,
			RADEONPLLPtr pll, double dot_clock,
			int no_odd_postdiv)
{
    RADEONInfoPtr  info      = RADEONPTR(pScrn);
    unsigned long  freq = dot_clock * 100;

    struct {
	int divider;
	int bitvalue;
    } *post_div, post_divs[]   = {
				/* From RAGE 128 VR/RAGE 128 GL Register
				 * Reference Manual (Technical Reference
				 * Manual P/N RRG-G04100-C Rev. 0.04), page
				 * 3-17 (PLL_DIV_[3:0]).
				 */
	{  1, 0 },              /* VCLK_SRC                 */
	{  2, 1 },              /* VCLK_SRC/2               */
	{  4, 2 },              /* VCLK_SRC/4               */
	{  8, 3 },              /* VCLK_SRC/8               */
	{  3, 4 },              /* VCLK_SRC/3               */
	{  6, 6 },              /* VCLK_SRC/6               */
	{ 12, 7 },              /* VCLK_SRC/12              */
	{  0, 0 }
    };

    if (freq > pll->max_pll_freq)      freq = pll->max_pll_freq;
    if (freq * 12 < pll->min_pll_freq) freq = pll->min_pll_freq / 12;

    for (post_div = &post_divs[0]; post_div->divider; ++post_div) {
       /* Odd post divider value don't work properly on the second digital
        * output
        */
       if (no_odd_postdiv && (post_div->divider & 1))
           continue;
	save->pll_output_freq_2 = post_div->divider * freq;
	if (save->pll_output_freq_2 >= pll->min_pll_freq
	    && save->pll_output_freq_2 <= pll->max_pll_freq) break;
    }

    if (!post_div->divider) {
	save->pll_output_freq_2 = freq;
	post_div = &post_divs[0];
    }

    save->dot_clock_freq_2 = freq;
    save->feedback_div_2   = RADEONDiv(pll->reference_div
				       * save->pll_output_freq_2,
				       pll->reference_freq);
    save->post_div_2       = post_div->divider;

    xf86DrvMsgVerb(pScrn->scrnIndex, X_INFO, RADEON_LOGLEVEL_DEBUG,
		   "dc=%ld, of=%ld, fd=%d, pd=%d\n",
		   save->dot_clock_freq_2,
		   save->pll_output_freq_2,
		   save->feedback_div_2,
		   save->post_div_2);

    save->p2pll_ref_div    = pll->reference_div;
    save->p2pll_div_0      = (save->feedback_div_2 |
			      (post_div->bitvalue << 16));
    save->htotal_cntl2     = 0;

    save->pixclks_cntl = ((info->SavedReg.pixclks_cntl &
			   ~(RADEON_PIX2CLK_SRC_SEL_MASK)) |
			  RADEON_PIX2CLK_SRC_SEL_P2PLLCLK);

}

static void
radeon_crtc_mode_set(xf86CrtcPtr crtc, DisplayModePtr mode,
		     DisplayModePtr adjusted_mode, int x, int y)
{
    ScrnInfoPtr pScrn = crtc->scrn;
    xf86CrtcConfigPtr   xf86_config = XF86_CRTC_CONFIG_PTR(pScrn);
    RADEONCrtcPrivatePtr radeon_crtc = crtc->driver_private;
    RADEONInfoPtr info = RADEONPTR(pScrn);
    RADEONMonitorType montype = MT_NONE;
    Bool           tilingOld   = info->tilingEnabled;
    int i = 0;
    double         dot_clock = 0;


    if (info->allowColorTiling) {
        info->tilingEnabled = (adjusted_mode->Flags & (V_DBLSCAN | V_INTERLACE)) ? FALSE : TRUE;
#ifdef XF86DRI	
	if (info->directRenderingEnabled && (info->tilingEnabled != tilingOld)) {
	    RADEONSAREAPrivPtr pSAREAPriv;
	  if (RADEONDRISetParam(pScrn, RADEON_SETPARAM_SWITCH_TILING, (info->tilingEnabled ? 1 : 0)) < 0)
  	      xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
			 "[drm] failed changing tiling status\n");
	    pSAREAPriv = DRIGetSAREAPrivate(pScrn->pScreen);
	    info->tilingEnabled = pSAREAPriv->tiling_enabled ? TRUE : FALSE;
	}
#endif
    }

    for (i = 0; i < xf86_config->num_output; i++) {
	xf86OutputPtr output = xf86_config->output[i];
	RADEONOutputPrivatePtr radeon_output = output->driver_private;

	if (output->crtc == crtc) {
	    montype = radeon_output->MonType;
	}
    }

    ErrorF("init memmap\n");
    RADEONInitMemMapRegisters(pScrn, &info->ModeReg, info);
    ErrorF("init common\n");
    RADEONInitCommonRegisters(&info->ModeReg, info);

    switch (radeon_crtc->crtc_id) {
    case 0:
	ErrorF("init crtc1\n");
	RADEONInitCrtcRegisters(crtc, &info->ModeReg, adjusted_mode, x, y);
        dot_clock = adjusted_mode->Clock / 1000.0;
        if (dot_clock) {
	    ErrorF("init pll1\n");
	    RADEONInitPLLRegisters(pScrn, info, &info->ModeReg, &info->pll, dot_clock);
        } else {
            info->ModeReg.ppll_ref_div = info->SavedReg.ppll_ref_div;
            info->ModeReg.ppll_div_3   = info->SavedReg.ppll_div_3;
            info->ModeReg.htotal_cntl  = info->SavedReg.htotal_cntl;
        }
	break;
    case 1:
	ErrorF("init crtc2\n");
        RADEONInitCrtc2Registers(crtc, &info->ModeReg, adjusted_mode, x, y);
        dot_clock = adjusted_mode->Clock / 1000.0;
        if (dot_clock) {
	    ErrorF("init pll2\n");
	    RADEONInitPLL2Registers(pScrn, &info->ModeReg, &info->pll, dot_clock, montype != MT_CRT);
        }
	break;
    }
    
    ErrorF("restore memmap\n");
    RADEONRestoreMemMapRegisters(pScrn, &info->ModeReg);
    ErrorF("restore common\n");
    RADEONRestoreCommonRegisters(pScrn, &info->ModeReg);    

    switch (radeon_crtc->crtc_id) {
    case 0:
	ErrorF("restore crtc1\n");
	RADEONRestoreCrtcRegisters(pScrn, &info->ModeReg);
	ErrorF("restore pll1\n");
	RADEONRestorePLLRegisters(pScrn, &info->ModeReg);
	break;
    case 1:
	ErrorF("restore crtc2\n");
	RADEONRestoreCrtc2Registers(pScrn, &info->ModeReg);
	ErrorF("restore pll2\n");
	RADEONRestorePLL2Registers(pScrn, &info->ModeReg);
	break;
    }

    if (info->DispPriority)
        RADEONInitDispBandwidth(pScrn);

    if (info->tilingEnabled != tilingOld) {
	/* need to redraw front buffer, I guess this can be considered a hack ? */
	xf86EnableDisableFBAccess(pScrn->scrnIndex, FALSE);
	RADEONChangeSurfaces(pScrn);
	xf86EnableDisableFBAccess(pScrn->scrnIndex, TRUE);
	/* xf86SetRootClip would do, but can't access that here */
    }

    /* reset ecp_div for Xv */
    info->ecp_div = -1;

}

static void
radeon_crtc_mode_commit(xf86CrtcPtr crtc)
{
    radeon_crtc_dpms(crtc, DPMSModeOn);
}

void radeon_crtc_load_lut(xf86CrtcPtr crtc)
{
    ScrnInfoPtr pScrn = crtc->scrn;
    RADEONCrtcPrivatePtr radeon_crtc = crtc->driver_private;
    RADEONInfoPtr info = RADEONPTR(pScrn);
    unsigned char *RADEONMMIO = info->MMIO;
    int i;

    if (!crtc->enabled)
	return;

    PAL_SELECT(radeon_crtc->crtc_id);

    for (i = 0; i < 256; i++) {
	OUTPAL(i, radeon_crtc->lut_r[i], radeon_crtc->lut_g[i], radeon_crtc->lut_b[i]);
    }
}


static void
radeon_crtc_gamma_set(xf86CrtcPtr crtc, CARD16 *red, CARD16 *green, 
		      CARD16 *blue, int size)
{
    RADEONCrtcPrivatePtr radeon_crtc = crtc->driver_private;
    int i;

    for (i = 0; i < 256; i++) {
	radeon_crtc->lut_r[i] = red[i] >> 8;
	radeon_crtc->lut_g[i] = green[i] >> 8;
	radeon_crtc->lut_b[i] = blue[i] >> 8;
    }

    radeon_crtc_load_lut(crtc);
}

static Bool
radeon_crtc_lock(xf86CrtcPtr crtc)
{
    ScrnInfoPtr		pScrn = crtc->scrn;
    RADEONInfoPtr  info = RADEONPTR(pScrn);

#ifdef XF86DRI
    if (info->CPStarted && pScrn->pScreen) {
	DRILock(pScrn->pScreen, 0);
	if (info->accelOn)
	    RADEON_SYNC(info, pScrn);
	return TRUE;
    }
#endif
    if (info->accelOn)
        RADEON_SYNC(info, pScrn);

    return FALSE;

}

static void
radeon_crtc_unlock(xf86CrtcPtr crtc)
{
    ScrnInfoPtr		pScrn = crtc->scrn;
    RADEONInfoPtr  info = RADEONPTR(pScrn);

#ifdef XF86DRI
	if (info->CPStarted && pScrn->pScreen) DRIUnlock(pScrn->pScreen);
#endif

    if (info->accelOn)
        RADEON_SYNC(info, pScrn);
}

#ifdef USE_XAA
/**
 * Allocates memory from the XF86 linear allocator, but also purges
 * memory if possible to cause the allocation to succeed.
 */
static FBLinearPtr
radeon_xf86AllocateOffscreenLinear(ScreenPtr pScreen, int length,
				 int granularity,
				 MoveLinearCallbackProcPtr moveCB,
				 RemoveLinearCallbackProcPtr removeCB,
				 pointer privData)
{
    FBLinearPtr linear;
    int max_size;

    linear = xf86AllocateOffscreenLinear(pScreen, length, granularity, moveCB,
					 removeCB, privData);
    if (linear != NULL)
	return linear;

    /* The above allocation didn't succeed, so purge unlocked stuff and try
     * again.
     */
    xf86QueryLargestOffscreenLinear(pScreen, &max_size, granularity,
				    PRIORITY_EXTREME);

    if (max_size < length)
	return NULL;

    xf86PurgeUnlockedOffscreenAreas(pScreen);

    linear = xf86AllocateOffscreenLinear(pScreen, length, granularity, moveCB,
					 removeCB, privData);

    return linear;
}
#endif

/**
 * Allocates memory for a locked-in-framebuffer shadow of the given
 * width and height for this CRTC's rotated shadow framebuffer.
 */
 
static void *
radeon_crtc_shadow_allocate (xf86CrtcPtr crtc, int width, int height)
{
    ScrnInfoPtr pScrn = crtc->scrn;
    ScreenPtr pScreen = pScrn->pScreen;
    RADEONInfoPtr  info = RADEONPTR(pScrn);
    RADEONCrtcPrivatePtr radeon_crtc = crtc->driver_private;
    unsigned long rotate_pitch;
    unsigned long rotate_offset;
    int align = 4096, size;
    int cpp = pScrn->bitsPerPixel / 8;

    rotate_pitch = pScrn->displayWidth * cpp;
    size = rotate_pitch * height;

#ifdef USE_EXA
    /* We could get close to what we want here by just creating a pixmap like
     * normal, but we have to lock it down in framebuffer, and there is no
     * setter for offscreen area locking in EXA currently.  So, we just
     * allocate offscreen memory and fake up a pixmap header for it.
     */
    if (info->useEXA) {
	assert(radeon_crtc->rotate_mem_exa == NULL);

	radeon_crtc->rotate_mem_exa = exaOffscreenAlloc(pScreen, size, align,
						       TRUE, NULL, NULL);
	if (radeon_crtc->rotate_mem_exa == NULL) {
	    xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
		       "Couldn't allocate shadow memory for rotated CRTC\n");
	    return NULL;
	}
	rotate_offset = radeon_crtc->rotate_mem_exa->offset;
    }
#endif /* USE_EXA */
#ifdef USE_XAA
    if (!info->useEXA) {
	/* The XFree86 linear allocator operates in units of screen pixels,
	 * sadly.
	 */
	size = (size + cpp - 1) / cpp;
	align = (align + cpp - 1) / cpp;

	assert(radeon_crtc->rotate_mem_xaa == NULL);

	radeon_crtc->rotate_mem_xaa =
	    radeon_xf86AllocateOffscreenLinear(pScreen, size, align,
					       NULL, NULL, NULL);
	if (radeon_crtc->rotate_mem_xaa == NULL) {
	    xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
		       "Couldn't allocate shadow memory for rotated CRTC\n");
	    return NULL;
	}
#ifdef XF86DRI
	rotate_offset = info->frontOffset +
	    radeon_crtc->rotate_mem_xaa->offset * cpp;
#endif
    }
#endif /* USE_XAA */

    return info->FB + rotate_offset;
}
    
/**
 * Creates a pixmap for this CRTC's rotated shadow framebuffer.
 */
static PixmapPtr
radeon_crtc_shadow_create(xf86CrtcPtr crtc, void *data, int width, int height)
{
    ScrnInfoPtr pScrn = crtc->scrn;
    unsigned long rotate_pitch;
    PixmapPtr rotate_pixmap;
    int cpp = pScrn->bitsPerPixel / 8;

    if (!data)
	data = radeon_crtc_shadow_allocate(crtc, width, height);
    
    rotate_pitch = pScrn->displayWidth * cpp;

    rotate_pixmap = GetScratchPixmapHeader(pScrn->pScreen,
					   width, height,
					   pScrn->depth,
					   pScrn->bitsPerPixel,
					   rotate_pitch,
					   data);

    if (rotate_pixmap == NULL) {
	xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
		   "Couldn't allocate shadow pixmap for rotated CRTC\n");
    }

    return rotate_pixmap;
}

static void
radeon_crtc_shadow_destroy(xf86CrtcPtr crtc, PixmapPtr rotate_pixmap, void *data)
{
    ScrnInfoPtr pScrn = crtc->scrn;
    RADEONInfoPtr  info = RADEONPTR(pScrn);
    RADEONCrtcPrivatePtr radeon_crtc = crtc->driver_private;

    if (rotate_pixmap)
	FreeScratchPixmapHeader(rotate_pixmap);
    
    if (data) {
#ifdef USE_EXA
	if (info->useEXA && radeon_crtc->rotate_mem_exa != NULL) {
	    exaOffscreenFree(pScrn->pScreen, radeon_crtc->rotate_mem_exa);
	    radeon_crtc->rotate_mem_exa = NULL;
	}
#endif /* USE_EXA */
#ifdef USE_XAA
	if (!info->useEXA) {
	    xf86FreeOffscreenLinear(radeon_crtc->rotate_mem_xaa);
	    radeon_crtc->rotate_mem_xaa = NULL;
	}
#endif /* USE_XAA */
    }
}

static const xf86CrtcFuncsRec radeon_crtc_funcs = {
    .dpms = radeon_crtc_dpms,
    .save = NULL, /* XXX */
    .restore = NULL, /* XXX */
    .mode_fixup = radeon_crtc_mode_fixup,
    .prepare = radeon_crtc_mode_prepare,
    .mode_set = radeon_crtc_mode_set,
    .commit = radeon_crtc_mode_commit,
    .gamma_set = radeon_crtc_gamma_set,
    .lock = radeon_crtc_lock,
    .unlock = radeon_crtc_unlock,
    .shadow_create = radeon_crtc_shadow_create,
    .shadow_allocate = radeon_crtc_shadow_allocate,
    .shadow_destroy = radeon_crtc_shadow_destroy,
    .set_cursor_colors = radeon_crtc_set_cursor_colors,
    .set_cursor_position = radeon_crtc_set_cursor_position,
    .show_cursor = radeon_crtc_show_cursor,
    .hide_cursor = radeon_crtc_hide_cursor,
    .load_cursor_argb = radeon_crtc_load_cursor_argb,
    .destroy = NULL, /* XXX */
};

Bool RADEONAllocateControllers(ScrnInfoPtr pScrn)
{
    RADEONEntPtr pRADEONEnt = RADEONEntPriv(pScrn);

    if (pRADEONEnt->Controller[0])
      return TRUE;

    pRADEONEnt->pCrtc[0] = xf86CrtcCreate(pScrn, &radeon_crtc_funcs);
    if (!pRADEONEnt->pCrtc[0])
      return FALSE;

    pRADEONEnt->Controller[0] = xnfcalloc(sizeof(RADEONCrtcPrivateRec), 1);
    if (!pRADEONEnt->Controller[0])
        return FALSE;

    pRADEONEnt->pCrtc[0]->driver_private = pRADEONEnt->Controller[0];
    pRADEONEnt->Controller[0]->crtc_id = 0;

    if (!pRADEONEnt->HasCRTC2)
	return TRUE;

    pRADEONEnt->pCrtc[1] = xf86CrtcCreate(pScrn, &radeon_crtc_funcs);
    if (!pRADEONEnt->pCrtc[1])
      return FALSE;

    pRADEONEnt->Controller[1] = xnfcalloc(sizeof(RADEONCrtcPrivateRec), 1);
    if (!pRADEONEnt->Controller[1])
    {
	xfree(pRADEONEnt->Controller[0]);
	return FALSE;
    }

    pRADEONEnt->pCrtc[1]->driver_private = pRADEONEnt->Controller[1];
    pRADEONEnt->Controller[1]->crtc_id = 1;
    return TRUE;
}

/**
 * In the current world order, there are lists of modes per output, which may
 * or may not include the mode that was asked to be set by XFree86's mode
 * selection.  Find the closest one, in the following preference order:
 *
 * - Equality
 * - Closer in size to the requested mode, but no larger
 * - Closer in refresh rate to the requested mode.
 */
DisplayModePtr
RADEONCrtcFindClosestMode(xf86CrtcPtr crtc, DisplayModePtr pMode)
{
    ScrnInfoPtr	pScrn = crtc->scrn;
    xf86CrtcConfigPtr   xf86_config = XF86_CRTC_CONFIG_PTR(pScrn);
    DisplayModePtr pBest = NULL, pScan = NULL;
    int i;

    /* Assume that there's only one output connected to the given CRTC. */
    for (i = 0; i < xf86_config->num_output; i++) 
    {
	xf86OutputPtr  output = xf86_config->output[i];
	if (output->crtc == crtc && output->probed_modes != NULL)
	{
	    pScan = output->probed_modes;
	    break;
	}
    }

    /* If the pipe doesn't have any detected modes, just let the system try to
     * spam the desired mode in.
     */
    if (pScan == NULL) {
	RADEONCrtcPrivatePtr  radeon_crtc = crtc->driver_private;
	xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
		   "No crtc mode list for crtc %d,"
		   "continuing with desired mode\n", radeon_crtc->crtc_id);
	return pMode;
    }

    for (; pScan != NULL; pScan = pScan->next) {
	assert(pScan->VRefresh != 0.0);

	/* If there's an exact match, we're done. */
	if (xf86ModesEqual(pScan, pMode)) {
	    pBest = pMode;
	    break;
	}

	/* Reject if it's larger than the desired mode. */
	if (pScan->HDisplay > pMode->HDisplay ||
	    pScan->VDisplay > pMode->VDisplay)
	{
	    continue;
	}

	if (pBest == NULL) {
	    pBest = pScan;
	    continue;
	}

	/* Find if it's closer to the right size than the current best
	 * option.
	 */
	if ((pScan->HDisplay > pBest->HDisplay &&
	     pScan->VDisplay >= pBest->VDisplay) ||
	    (pScan->HDisplay >= pBest->HDisplay &&
	     pScan->VDisplay > pBest->VDisplay))
	{
	    pBest = pScan;
	    continue;
	}

	/* Find if it's still closer to the right refresh than the current
	 * best resolution.
	 */
	if (pScan->HDisplay == pBest->HDisplay &&
	    pScan->VDisplay == pBest->VDisplay &&
	    (fabs(pScan->VRefresh - pMode->VRefresh) <
	     fabs(pBest->VRefresh - pMode->VRefresh))) {
	    pBest = pScan;
	}
    }

    if (pBest == NULL) {
	xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
		   "No suitable mode found to program for the pipe.\n"
		   "	continuing with desired mode %dx%d@%.1f\n",
		   pMode->HDisplay, pMode->VDisplay, pMode->VRefresh);
    } else if (!xf86ModesEqual(pBest, pMode)) {
      RADEONCrtcPrivatePtr  radeon_crtc = crtc->driver_private;
      int		    crtc = radeon_crtc->crtc_id;
      xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
		   "Choosing pipe %d's mode %dx%d@%.1f instead of xf86 "
		   "mode %dx%d@%.1f\n", crtc,
		   pBest->HDisplay, pBest->VDisplay, pBest->VRefresh,
		   pMode->HDisplay, pMode->VDisplay, pMode->VRefresh);
	pMode = pBest;
    }
    return pMode;
}

void
RADEONChooseOverlayCRTC(ScrnInfoPtr pScrn, BoxPtr dstBox)
{
    xf86CrtcConfigPtr xf86_config = XF86_CRTC_CONFIG_PTR(pScrn);
    RADEONInfoPtr  info       = RADEONPTR(pScrn);
    int c;
    int crtc_num = 0;

    for (c = 0; c < xf86_config->num_crtc; c++)
    {
        xf86CrtcPtr crtc = xf86_config->crtc[c];

	if (!crtc->enabled)
	    continue;

	if ((dstBox->x1 >= crtc->x) && (dstBox->y1 >= crtc->y))
	    crtc_num = c;
    }

    if (crtc_num == 1)
        info->OverlayOnCRTC2 = TRUE;
    else
        info->OverlayOnCRTC2 = FALSE;
}
