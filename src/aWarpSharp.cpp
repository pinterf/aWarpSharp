// aWarpSharp package 2016.06.23 for Avisynth+ and Avisynth 2.6
// based on Firesledge's 2015.12.30 for Avisynth 2.5
// aWarpSharp package 2012.03.28 for Avisynth 2.5
// Copyright (C) 2003 MarcFD, 2012 Skakov Pavel
// 2015 Firesledge
// 2016 pinterf
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "avisynth.h"
#include "avs\config.h"
#include <cstring>
#include <cassert>
#include <algorithm>

__declspec(align(16)) static const unsigned char dq0toF[0x10] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 };

int g_cpuid;

#pragma warning(disable: 4731) // frame pointer register 'ebp' modified by inline assembly code
#pragma warning(disable: 4799) // function has no EMMS instruction

#pragma optimize("t", on)

#if !defined(X86_32) 
#define	QAX		rax
#define QBX		rbx
#define QCX		rcx
#define QDX		rdx
#define QBP		rbp
#define QSP		rsp
#define QSI		rsi
#define QDI		rdi
#define movsx_int movsxd
#define SEXT(a, b) movsxd	a, b
#else
#define	QAX		eax
#define QBX		ebx
#define QCX		ecx
#define QDX		edx
#define QBP		ebp
#define QSP		esp
#define QSI		esi
#define QDI		edi
#define movsx_int mov
#define SEXT(a, b)
#endif


#define SMAGL 2
#include "aWarp.h"
#define SMAGL 0
#include "aWarp.h"

// WxH min: 4x1, mul: 4x1
// thresh: 0..FFh
// dummy first and last result columns
void Sobel(PVideoFrame &src, PVideoFrame &dst, int plane, int thresh, const VideoInfo &src_vi, const VideoInfo &dst_vi)
{
  const int src_pitch = src->GetPitch(plane);
  const int dst_pitch = dst->GetPitch(plane);
  const unsigned char *psrc = src->GetReadPtr(plane);
  unsigned char *pdst = dst->GetWritePtr(plane);
  const int height = src->GetHeight() >> src_vi.GetPlaneHeightSubsampling(plane);
  const int dst_row_size = dst->GetRowSize() >> dst_vi.GetPlaneWidthSubsampling(plane);
  const int padded_div4_rowsize = (dst_row_size + 3) >> 2;
  const int i = padded_div4_rowsize; // for asm

  if (g_cpuid & CPUF_SSE2)
    // SSE2 version
    for (int y = 0; y < height; y++)
    {
#if 1 // def _M_X64
      // 0: prev=0 curr = 0 next = 1
      // y-1: prev = y-2, curr = y-1, next = y-1
      // else between: prev = y-1, curr = y, next = y+1
        const uint8_t *psrc_qsi = psrc; //  mov	QSI, psrc
        uint8_t *pdst_qdi = pdst; // mov	QDI, pdst
        int qdx;
        int qax_diffpitch_to_prev;

        // last row: "next" is pitch
        if (y < height - 1) //  cmp	ecx, height     // 32 bit O.K.
          qdx = src_pitch * 2; // last row: "next" is only pitches away  // prev-next diff
        else
          qdx = src_pitch; // not last row: "next" is two pitches away

        /*   y        prev            current               next         qdx_next_prev_diff   qax_curr_prev_diff
             0        psrc            psrc              psrc+pitch       pitch*2               0
             ..       psrc-pitch      psrc              psrc+pitch       pitch*2               pitch
          height-1    psrc-pitch      psrc              psrc             pitch                 pitch
        */

        if (y != 0) {// test	QCX, QCX
          qax_diffpitch_to_prev = src_pitch;
          psrc_qsi -= src_pitch; // base is the prev line
        }
        else {
          qax_diffpitch_to_prev = 0; // first row: center is same
          // psrc_qsi -= 0; // base is the current line (not previous to the y=0 line)
          qdx = src_pitch;
        }
        __m128i thresh128 = _mm_set1_epi8(thresh);
        // PF 170926 todo comment: we have to separate first 16, last 16 and between
        for (int qcx = padded_div4_rowsize; qcx > 0; qcx -= 4, psrc_qsi +=16, pdst_qdi += 16) {
          __m128i prev_left_xmm2 = _mm_loadu_si128(reinterpret_cast<const __m128i *>(psrc_qsi - 1)); // PF 170926 read before the very first byte! Dangerous. Todo eliminate.
          __m128i prev_cent_xmm3 = _mm_loadu_si128(reinterpret_cast<const __m128i *>(psrc_qsi + 0));
          __m128i prev_righ_xmm4 = _mm_loadu_si128(reinterpret_cast<const __m128i *>(psrc_qsi + 1));
          __m128i next_left_xmm5 = _mm_loadu_si128(reinterpret_cast<const __m128i *>(psrc_qsi + qdx - 1));
          __m128i next_cent_xmm6 = _mm_loadu_si128(reinterpret_cast<const __m128i *>(psrc_qsi + qdx + 0));
          __m128i next_righ_xmm7 = _mm_loadu_si128(reinterpret_cast<const __m128i *>(psrc_qsi + qdx + 1)); // PF 170926 read after the last byte! Dangerous. Todo eliminate.

          __m128i avg_top_xmm3 = _mm_avg_epu8(_mm_avg_epu8(prev_left_xmm2, prev_righ_xmm4), prev_cent_xmm3);
          __m128i avg_bottom_xmm6 = _mm_avg_epu8(_mm_avg_epu8(next_left_xmm5, next_righ_xmm7), next_cent_xmm6);

          __m128i absdiff_topbottom_xmm6 = _mm_or_si128(_mm_subs_epu8(avg_top_xmm3, avg_bottom_xmm6), _mm_subs_epu8(avg_bottom_xmm6, avg_top_xmm3));

          __m128i curr_left_xmm1 = _mm_loadu_si128(reinterpret_cast<const __m128i *>(psrc_qsi + qax_diffpitch_to_prev - 1));
          __m128i curr_righ_xmm3 = _mm_loadu_si128(reinterpret_cast<const __m128i *>(psrc_qsi + qax_diffpitch_to_prev + 1));

          __m128i avg_left_xmm1 = _mm_avg_epu8(_mm_avg_epu8(next_left_xmm5, prev_left_xmm2), curr_left_xmm1);// pavgb	xmm5, xmm2
          __m128i avg_right_xmm3 = _mm_avg_epu8(_mm_avg_epu8(next_righ_xmm7, prev_righ_xmm4), curr_righ_xmm3); // pavgb	xmm7, xmm4

          __m128i absdiff_leftright_xmm1 = _mm_or_si128(_mm_subs_epu8(avg_left_xmm1, avg_right_xmm3), _mm_subs_epu8(avg_right_xmm3, avg_left_xmm1));

          __m128i xmm2 = _mm_adds_epu8(absdiff_topbottom_xmm6, absdiff_leftright_xmm1);
          __m128i xmm1 = _mm_max_epu8(absdiff_topbottom_xmm6, absdiff_leftright_xmm1);
          xmm2 = _mm_adds_epu8(xmm2, xmm1);
          
          xmm2 = _mm_adds_epu8(_mm_adds_epu8(xmm2, xmm2), xmm2); // *3
          xmm2 = _mm_adds_epu8(xmm2, xmm2); // *6
          xmm2 = _mm_min_epu8(xmm2, thresh128);  //          pminub	xmm2, xmm0 // thresh
          // qcx: -=4 in cycle, originally: (row_size + 3) / 4
          // number of 4 byte blocks
          // row_size     qcx 
          // 1..4        1     
          // 5..8        2
          // 6..12.......3
          // 13..16      4
          if (qcx >= 4) {
            // full 16 byte
            _mm_storeu_si128(reinterpret_cast<__m128i *>(pdst_qdi), xmm2);
          }
          else if (qcx == 1) {
            // 4 byte
            *(uint32_t *)(pdst_qdi) = _mm_cvtsi128_si32(xmm2);
          }
          else if (qcx == 2) {
            // 8 byte
            _mm_storel_epi64(reinterpret_cast<__m128i *>(pdst_qdi), xmm2);
          }
          else if (qcx == 3) {
            // 8+4 byte
            _mm_storel_epi64(reinterpret_cast<__m128i *>(pdst_qdi), xmm2);
            *(uint32_t *)(pdst_qdi + 8) = _mm_cvtsi128_si32(_mm_srli_si128(xmm2, 8));
          }
        }
#else
      __asm {
        mov	QSI, psrc
        mov	QDI, pdst
        movsx_int	QDX, src_pitch
        xor	QAX, QAX
        movsx_int	QCX, y
        test	QCX, QCX
        cmovnz	QAX, QDX
        inc	QCX
        add	QDX, QAX
        cmp	ecx, height     // 32 bit O.K.
        cmovz	QDX, QAX
        sub	QSI, QAX
        movsx_int	QCX, i
        sub	QDI, 10h
        sub	QDI, QSI
        movd	xmm0, thresh
        pshufd	xmm0, xmm0, 0
        packssdw	xmm0, xmm0
        packuswb	xmm0, xmm0
        align	10h
        l :
        movdqu	xmm2, [QSI - 1]
          movdqu	xmm3, [QSI]
          movdqu	xmm4, [QSI + 1]
          movdqu	xmm5, [QSI + QDX - 1]
          movdqu	xmm6, [QSI + QDX]
          movdqu	xmm7, [QSI + QDX + 1]

          movdqa	xmm1, xmm2
          pavgb	xmm1, xmm4
          pavgb	xmm3, xmm1

          movdqa	xmm1, xmm5
          pavgb	xmm1, xmm7
          pavgb	xmm6, xmm1

          movdqa	xmm1, xmm3
          psubusb	xmm3, xmm6
          psubusb	xmm6, xmm1
          por	xmm6, xmm3

          movdqu	xmm1, [QSI + QAX - 1]
          movdqu	xmm3, [QSI + QAX + 1]
          pavgb	xmm5, xmm2
          pavgb	xmm7, xmm4
          pavgb	xmm1, xmm5
          pavgb	xmm3, xmm7
          movdqa	xmm5, xmm1
          psubusb	xmm1, xmm3
          psubusb	xmm3, xmm5
          por	xmm1, xmm3

          movdqa	xmm2, xmm6
          paddusb	xmm2, xmm1
          pmaxub	xmm1, xmm6
          paddusb	xmm2, xmm1

          movdqa	xmm3, xmm2
          paddusb	xmm2, xmm2
          paddusb	xmm2, xmm3
          paddusb	xmm2, xmm2
          pminub	xmm2, xmm0 // thresh
          add	QSI, 10h
          sub	QCX, 4
          jb	le1
          movntdq[QSI + QDI], xmm2
          jnz	l
          jmp	lx
          le1 :
        test	QCX, 2
          jz	le2
          movq	qword ptr[QSI + QDI], xmm2
          test	QCX, 1
          jz	lx
          add	QSI, 8
          psrldq	xmm2, 8
          le2:
        movd[QSI + QDI], xmm2
          lx :
      }
#endif
      pdst[0] = pdst[1];
      pdst[dst_row_size - 1] = pdst[dst_row_size - 2];
      psrc += src_pitch;
      pdst += dst_pitch;
    }
  __asm sfence;
}

// WxH min: 1x12, mul: 1x1 (write 16x1)
// (6+5+4+3)/16 + (2+1)*3/16 + (0)*6/16
void BlurR6(PVideoFrame &src, PVideoFrame &tmp, int plane, const VideoInfo &src_vi)
{
  const int src_pitch = src->GetPitch(plane);
  const int tmp_pitch = tmp->GetPitch(plane);
  unsigned char *const psrc = src->GetWritePtr(plane);
  unsigned char *const ptmp = tmp->GetWritePtr(plane);
  const int height = src->GetHeight() >> src_vi.GetPlaneHeightSubsampling(plane);
  const int i = src->GetRowSize() >> src_vi.GetPlaneWidthSubsampling(plane);
  const int ia = (i + 0xF) >> 4; // aligned
  unsigned char *psrc2, *ptmp2;

  psrc2 = psrc;
  ptmp2 = ptmp;
  // Horizontal Blur
  // WxH min: 1x1, mul: 1x1 (write 16x1)
  if (g_cpuid & CPUF_SSSE3) // SSSE?
    // SSSE3 version (palignr, pshufb)
    for (int y = 0; y < height; y++)
    {
      __asm {
        mov	QSI, psrc2
        mov	QDI, ptmp2
        movsx_int	QCX, i
        add	QSI, 10h
        sub	QDI, QSI
        movdqa	xmm6, [QSI - 10h]
        movdqa	xmm5, xmm6
        movdqa	xmm7, xmm6
        pxor	xmm0, xmm0
        pshufb	xmm5, xmm0
        sub	QCX, 10h
        jna	l0e
        align	10h
        l0 :
        movdqa	xmm7, [QSI]
          movdqa	xmm0, xmm6
          movdqa	xmm2, xmm7
          palignr	xmm0, xmm5, 10
          palignr	xmm2, xmm6, 6
          pavgb	xmm0, xmm2
          movdqa	xmm3, xmm6
          movdqa	xmm4, xmm7
          palignr	xmm3, xmm5, 11
          palignr	xmm4, xmm6, 5
          pavgb	xmm3, xmm4
          pavgb	xmm0, xmm3
          movdqa	xmm1, xmm6
          movdqa	xmm2, xmm7
          palignr	xmm1, xmm5, 12
          palignr	xmm2, xmm6, 4
          pavgb	xmm1, xmm2
          movdqa	xmm3, xmm6
          movdqa	xmm4, xmm7
          palignr	xmm3, xmm5, 13
          palignr	xmm4, xmm6, 3
          pavgb	xmm3, xmm4
          pavgb	xmm1, xmm3
          pavgb	xmm0, xmm1
          movdqa	xmm1, xmm6
          movdqa	xmm2, xmm7
          palignr	xmm1, xmm5, 14
          palignr	xmm2, xmm6, 2
          pavgb	xmm1, xmm2
          movdqa	xmm3, xmm6
          movdqa	xmm4, xmm7
          palignr	xmm3, xmm5, 15
          palignr	xmm4, xmm6, 1
          pavgb	xmm3, xmm4
          pavgb	xmm1, xmm3
          pavgb	xmm1, xmm6
          movdqa	xmm5, xmm6
          movdqa	xmm6, xmm7
          pavgb	xmm0, xmm1
          pavgb	xmm0, xmm1
          movntdq[QSI + QDI], xmm0
          add	QSI, 10h
          sub	QCX, 10h
          ja	l0
          l0e :
        add	QCX, 0Fh
          pxor	xmm0, xmm0
          movd	xmm1, ecx
          pshufb	xmm1, xmm0
          pminub	xmm1, dq0toF // 0x0F0E..00
          pshufb	xmm6, xmm1
          psrldq	xmm7, 15
          pshufb	xmm7, xmm0
          movdqa	xmm0, xmm6
          movdqa	xmm2, xmm7
          palignr	xmm0, xmm5, 10
          palignr	xmm2, xmm6, 6
          pavgb	xmm0, xmm2
          movdqa	xmm3, xmm6
          movdqa	xmm4, xmm7
          palignr	xmm3, xmm5, 11
          palignr	xmm4, xmm6, 5
          pavgb	xmm3, xmm4
          pavgb	xmm0, xmm3
          movdqa	xmm1, xmm6
          movdqa	xmm2, xmm7
          palignr	xmm1, xmm5, 12
          palignr	xmm2, xmm6, 4
          pavgb	xmm1, xmm2
          movdqa	xmm3, xmm6
          movdqa	xmm4, xmm7
          palignr	xmm3, xmm5, 13
          palignr	xmm4, xmm6, 3
          pavgb	xmm3, xmm4
          pavgb	xmm1, xmm3
          pavgb	xmm0, xmm1
          movdqa	xmm1, xmm6
          movdqa	xmm2, xmm7
          palignr	xmm1, xmm5, 14
          palignr	xmm2, xmm6, 2
          pavgb	xmm1, xmm2
          movdqa	xmm3, xmm6
          movdqa	xmm4, xmm7
          palignr	xmm3, xmm5, 15
          palignr	xmm4, xmm6, 1
          pavgb	xmm3, xmm4
          pavgb	xmm1, xmm3
          pavgb	xmm1, xmm6
          movdqa	xmm5, xmm6
          movdqa	xmm6, xmm7
          pavgb	xmm0, xmm1
          pavgb	xmm0, xmm1
          movntdq[QSI + QDI], xmm0
      }
      psrc2 += src_pitch;
      ptmp2 += tmp_pitch;
    }
  else if (g_cpuid & CPUF_SSE2)
    // SSE2 version
    // 6 left and right pixels are wrong
    for (int y = 0; y < height; y++)
    {
      __asm {
        mov	QSI, psrc2
        mov	QDI, ptmp2
        movsx_int	QCX, ia
        sub	QDI, QSI
        align	10h
        ls0 :
        movdqu	xmm6, [QSI - 6]
          movdqu	xmm0, [QSI + 6]
          pavgb	xmm6, xmm0
          movdqu	xmm5, [QSI - 5]
          movdqu	xmm7, [QSI + 5]
          pavgb	xmm5, xmm7
          movdqu	xmm4, [QSI - 4]
          movdqu	xmm0, [QSI + 4]
          pavgb	xmm4, xmm0
          movdqu	xmm3, [QSI - 3]
          movdqu	xmm7, [QSI + 3]
          pavgb	xmm3, xmm7
          movdqu	xmm2, [QSI - 2]
          movdqu	xmm0, [QSI + 2]
          pavgb	xmm2, xmm0
          movdqu	xmm1, [QSI - 1]
          movdqu	xmm7, [QSI + 1]
          pavgb	xmm1, xmm7
          movdqa	xmm0, [QSI]
          pavgb	xmm6, xmm5
          pavgb	xmm4, xmm3
          pavgb	xmm2, xmm1
          pavgb	xmm6, xmm4
          pavgb	xmm2, xmm0
          pavgb	xmm6, xmm2
          pavgb	xmm6, xmm2
          movntdq[QSI + QDI], xmm6
          add	QSI, 10h
          dec	QCX
          jnz	ls0
      }
      psrc2 += src_pitch;
      ptmp2 += tmp_pitch;
    }
  __asm sfence;

  // Vertical Blur
  // WxH min: 1x12, mul: 1x1 (write 16x1)
  if (g_cpuid & CPUF_SSE2)
  {
    // SSE2 version
    int y;
    psrc2 = psrc;
    ptmp2 = ptmp;
    for (y = 0; y < 6; y++)
    {
      __asm {
        movsx_int	QAX, tmp_pitch
        mov	QSI, ptmp2
        mov	QDI, psrc2
        movsx_int QCX, ia
        lea	QBX, [QAX + QAX * 2] // pitch*3
        lea	QDX, [QBX + QAX * 2] // pitch*5
        add	QDX, QSI
        sub	QDI, QSI
        align	10h
        l1 :
        movdqa	xmm0, [QSI]
          movdqa	xmm1, [QSI + QAX * 1]
          movdqa	xmm2, [QSI + QAX * 2]
          movdqa	xmm3, [QSI + QBX * 1] // pitch*3, fixed 20170928
          movdqa	xmm4, [QSI + QAX * 4]
          movdqa	xmm5, [QDX]           // pitch*5
          movdqa	xmm6, [QDX + QAX * 1] // pitch*6
          pavgb	xmm6, xmm5
          pavgb	xmm4, xmm3
          pavgb	xmm2, xmm1
          pavgb	xmm6, xmm4
          pavgb	xmm2, xmm0
          pavgb	xmm6, xmm2
          pavgb	xmm6, xmm2
          movntdq[QSI + QDI], xmm6
          add	QSI, 10h
          add	QDX, 10h
          dec	QCX
          jnz	l1
      }
      psrc2 += src_pitch;
      ptmp2 += tmp_pitch;
    }
    ptmp2 = ptmp;
    for (; y < height - 6; y++)
    {
      __asm {
        movsx_int	QAX, tmp_pitch
        mov	QSI, ptmp2
        mov	QDI, psrc2
        movsx_int	QCX, ia
        push	QBP
        lea	QBP, [QAX + QAX * 2]
        lea	QDX, [QBP + QAX * 2]
        lea	QBX, [QSI + QDX * 2]
        add	QDX, QSI
        sub	QDI, QSI
        align	10h
        l2 :
        movdqa	xmm6, [QSI]
          pavgb	xmm6, [QBX + QAX * 2]
          movdqa	xmm5, [QSI + QAX * 1]
          pavgb	xmm5, [QBX + QAX * 1]
          movdqa	xmm4, [QSI + QAX * 2]
          pavgb	xmm4, [QBX]
          movdqa	xmm3, [QSI + QBP * 1]
          pavgb	xmm3, [QDX + QAX * 4]
          movdqa	xmm2, [QSI + QAX * 4]
          pavgb	xmm2, [QSI + QAX * 8]
          movdqa	xmm1, [QDX]
          pavgb	xmm1, [QDX + QAX * 2]
          movdqa	xmm0, [QDX + QAX * 1]
          pavgb	xmm6, xmm5
          pavgb	xmm4, xmm3
          pavgb	xmm2, xmm1
          pavgb	xmm6, xmm4
          pavgb	xmm2, xmm0
          pavgb	xmm6, xmm2
          pavgb	xmm6, xmm2
          movntdq[QSI + QDI], xmm6
          add	QSI, 10h
          add	QDX, 10h
          add	QBX, 10h
          dec	QCX
          jnz	l2
          pop	QBP
      }
      psrc2 += src_pitch;
      ptmp2 += tmp_pitch;
    }
    for (; y < height; y++)
    {
      __asm {
        movsx_int	QAX, tmp_pitch
        mov	QSI, ptmp2
        mov	QDI, psrc2
        movsx_int	QCX, ia
        lea	QBX, [QAX + QAX * 2] // pitch*3
        lea	QDX, [QBX + QAX * 2] // pitch*5
        add	QDX, QSI
        sub	QDI, QSI
        align	10h
        l3 :
        movdqa	xmm6, [QSI]
          movdqa	xmm5, [QSI + QAX * 1]
          movdqa	xmm4, [QSI + QAX * 2]
          movdqa	xmm3, [QSI + QBX * 1] // pitch*3
          movdqa	xmm2, [QSI + QAX * 4]
          movdqa	xmm1, [QDX]           // pitch*5
          movdqa	xmm0, [QDX + QAX * 1] // pitch*6
          pavgb	xmm6, xmm5
          pavgb	xmm4, xmm3
          pavgb	xmm2, xmm1
          pavgb	xmm6, xmm4
          pavgb	xmm2, xmm0
          pavgb	xmm6, xmm2
          pavgb	xmm6, xmm2
          movntdq[QSI + QDI], xmm6
          add	QSI, 10h
          add	QDX, 10h
          dec	QCX
          jnz	l3
      }
      psrc2 += src_pitch;
      ptmp2 += tmp_pitch;
    }
  }
  __asm sfence;
}

// WxH min: 1x1, mul: 1x1 (write 16x1)
// (2)/8 + (1)*4/8 + (0)*3/8
void BlurR2(PVideoFrame &src, PVideoFrame &tmp, int plane, const VideoInfo &src_vi)
{
  const int src_pitch = src->GetPitch(plane);
  const int tmp_pitch = tmp->GetPitch(plane);
  unsigned char *psrc = src->GetWritePtr(plane);
  unsigned char *ptmp = tmp->GetWritePtr(plane);
  const int height = src->GetHeight() >> src_vi.GetPlaneHeightSubsampling(plane);
  const int i = src->GetRowSize() >> src_vi.GetPlaneWidthSubsampling(plane);
  const int ia = i + 0xF & ~0xF;
  unsigned char *psrc2, *ptmp2;

  psrc2 = psrc;
  ptmp2 = ptmp;
  // Horizontal Blur
  // WxH min: 1x1, mul: 1x1 (write 16x1)
  if (g_cpuid & CPUF_SSSE3)
    // SSSE3 version (palignr, pshufb)
    for (int y = 0; y < height; y++)
    {
      __asm {
        mov	QSI, psrc2
        mov	QDI, ptmp2
        movsx_int	QCX, i
        add	QSI, 10h
        sub	QDI, QSI
        movdqa	xmm4, dq0toF
        movdqa	xmm6, [QSI - 10h]
        movdqa	xmm5, xmm6
        movdqa	xmm7, xmm6
        pxor	xmm0, xmm0
        pshufb	xmm5, xmm0
        sub	QCX, 10h
        jna	l1e
        align	10h
        l1 :
        movdqa	xmm7, [QSI]
          movdqa	xmm0, xmm6
          movdqa	xmm2, xmm7
          palignr	xmm0, xmm5, 14
          palignr	xmm2, xmm6, 2
          pavgb	xmm0, xmm2
          movdqa	xmm1, xmm6
          movdqa	xmm3, xmm7
          palignr	xmm1, xmm5, 15
          palignr	xmm3, xmm6, 1
          pavgb	xmm0, xmm6
          pavgb	xmm1, xmm3
          pavgb	xmm0, xmm6
          movdqa	xmm5, xmm6
          movdqa	xmm6, xmm7
          pavgb	xmm0, xmm1
          movntdq[QSI + QDI], xmm0
          add	QSI, 10h
          sub	QCX, 10h
          ja	l1
          l1e :
        add	QCX, 0Fh
          pxor	xmm0, xmm0
          movd	xmm1, ecx
          pshufb	xmm1, xmm0
          pminub	xmm1, xmm4 // 0x0F0E..00
          pshufb	xmm6, xmm1
          psrldq	xmm7, 15
          pshufb	xmm7, xmm0
          movdqa	xmm0, xmm6
          movdqa	xmm2, xmm7
          palignr	xmm0, xmm5, 14
          palignr	xmm2, xmm6, 2
          pavgb	xmm0, xmm2
          movdqa	xmm1, xmm6
          movdqa	xmm3, xmm7
          palignr	xmm1, xmm5, 15
          palignr	xmm3, xmm6, 1
          pavgb	xmm0, xmm6
          pavgb	xmm1, xmm3
          pavgb	xmm0, xmm6
          movdqa	xmm5, xmm6
          movdqa	xmm6, xmm7
          pavgb	xmm0, xmm1
          movntdq[QSI + QDI], xmm0
      }
      psrc2 += src_pitch;
      ptmp2 += tmp_pitch;
    }
  else if (g_cpuid & CPUF_SSE2)
    // SSE2 version
    // 2 left and right pixels are wrong
    for (int y = 0; y < height; y++)
    {
      __asm {
        mov	QSI, psrc2
        mov	QDI, ptmp2
        movsx_int	QCX, ia
        add	QSI, QCX
        add	QDI, QCX
        neg	QCX
        align	10h
        ls1 :
        movdqu	xmm0, [QCX + QSI - 2]
          movdqu	xmm4, [QCX + QSI + 2]
          pavgb	xmm0, xmm4
          movdqu	xmm1, [QCX + QSI - 1]
          movdqu	xmm3, [QCX + QSI + 1]
          pavgb	xmm1, xmm3
          movdqa	xmm2, [QCX + QSI]
          pavgb	xmm0, xmm2
          pavgb	xmm0, xmm2
          pavgb	xmm0, xmm1
          movntdq[QCX + QDI], xmm0
          add	QCX, 10h
          jnz	ls1
      }
      psrc2 += src_pitch;
      ptmp2 += tmp_pitch;
    }
  __asm sfence;

  psrc2 = psrc;
  ptmp2 = ptmp;
  // Vertical Blur
  // WxH min: 1x1, mul: 1x1 (write 16x1)
  if (g_cpuid & CPUF_SSE2)
    // SSE2 version
    for (int y = 0; y < height; y++)
    {
      int tmp_pitchp1 = y ? -tmp_pitch : 0;
      int tmp_pitchp2 = y > 1 ? tmp_pitchp1 * 2 : tmp_pitchp1;
      int tmp_pitchn1 = y < height - 1 ? tmp_pitch : 0;
      int tmp_pitchn2 = y < height - 2 ? tmp_pitchn1 * 2 : tmp_pitchn1;
      __asm {
        push	QBP
        mov	QSI, ptmp2
        mov	QDI, psrc2
        movsx_int	QCX, ia
        movsx_int	QAX, tmp_pitchp2
        movsx_int	QDX, tmp_pitchn2
        movsx_int	QBX, tmp_pitchp1
        movsx_int	QBP, tmp_pitchn1
        sub	QDI, QSI
        align	10h
        l2 :
        movdqa	xmm0, [QSI + QAX]
          pavgb	xmm0, [QSI + QDX]
          movdqa	xmm1, [QSI + QBX]
          pavgb	xmm1, [QSI + QBP]
          movdqa	xmm2, [QSI]
          pavgb	xmm0, xmm2
          pavgb	xmm0, xmm2
          pavgb	xmm0, xmm1
          movntdq[QSI + QDI], xmm0
          add	QSI, 10h
          sub	QCX, 10h
          jnz	l2
          pop	QBP
      }
      psrc2 += src_pitch;
      ptmp2 += tmp_pitch;
    }
  __asm sfence;
}

void GuideChroma(PVideoFrame &src, PVideoFrame &dst, const VideoInfo &src_vi, const VideoInfo &dst_vi, bool cplace_mpeg2_flag)
{
  const int	subspl_dst_h_l2 = dst_vi.GetPlaneWidthSubsampling(PLANAR_U);
  const int	subspl_dst_v_l2 = dst_vi.GetPlaneHeightSubsampling(PLANAR_U);
  const unsigned char *py = src->GetReadPtr(PLANAR_Y);
  unsigned char *pu = dst->GetWritePtr(PLANAR_U);
  const int pitch_y = src->GetPitch();
  const int pitch_uv = dst->GetPitch(PLANAR_U);
  const int height = dst->GetHeight() >> subspl_dst_v_l2;
  const int width = dst->GetRowSize() >> subspl_dst_h_l2;
  const int i = -((width + 7) & ~7);

  // 4:2:0
  if (subspl_dst_h_l2 == 1 && subspl_dst_v_l2 == 1)
  {
    // MPEG-2 chroma placement
    if (cplace_mpeg2_flag)
    {
      for (int y = 0; y < height; ++y)
      {
        int            c2 = py[0] + py[pitch_y];
        for (int x = 0; x < width; ++x)
        {
          const int      c0 = c2;
          const int      c1 = py[x * 2] + py[pitch_y + x * 2];
          c2 = py[x * 2 + 1] + py[pitch_y + x * 2 + 1];
          pu[x] = static_cast <unsigned char> ((c0 + 2 * c1 + c2 + 4) >> 3);
        }
        py += pitch_y * 2;
        pu += pitch_uv;
      }
    }

    // MPEG-1
    else
    {
      if (g_cpuid & CPUF_SSE2)
      {
        // SSE2 version
        for (int y = 0; y < height; y++)
        {
          __asm {
            movsx_int	QCX, i
            mov	QSI, py
            movsx_int	QAX, pitch_y
            mov	QDX, pu
            sub	QSI, QCX
            sub	QSI, QCX
            add	QAX, QSI
            sub	QDX, QCX
            sub	QDX, 10h
            pcmpeqw	xmm7, xmm7
            psrlw	xmm7, 8
            align	10h
            l :
            movdqa	xmm0, [QSI + QCX * 2]
              movdqa	xmm2, [QSI + QCX * 2 + 10h]
              movdqa	xmm1, xmm0
              movdqa	xmm3, xmm2
              pand	xmm0, xmm7
              pand	xmm2, xmm7
              packuswb	xmm0, xmm2
              psrlw	xmm1, 8
              psrlw	xmm3, 8
              packuswb	xmm1, xmm3
              pavgb	xmm0, xmm1
              movdqa	xmm1, [QAX + QCX * 2]
              movdqa	xmm3, [QAX + QCX * 2 + 10h]
              movdqa	xmm2, xmm1
              movdqa	xmm4, xmm3
              pand	xmm1, xmm7
              pand	xmm3, xmm7
              packuswb	xmm1, xmm3
              psrlw	xmm2, 8
              psrlw	xmm4, 8
              packuswb	xmm2, xmm4
              pavgb	xmm1, xmm2
              pavgb	xmm0, xmm1
              add	QCX, 10h
              jg	ls
              movntdq[QCX + QDX], xmm0
              jnz	l
              jmp	lx
              ls :
            movq	qword ptr[QCX + QDX], xmm0
              lx :
          }
          py += pitch_y * 2;
          pu += pitch_uv;
        }
      }
    }	// MPEG-1
  }

  // 4:2:2
  else if (subspl_dst_h_l2 == 1 && subspl_dst_v_l2 == 0)
  {
    // MPEG-2 chroma placement
    if (cplace_mpeg2_flag)
    {
      for (int y = 0; y < height; ++y)
      {
        int            c2 = py[0];
        for (int x = 0; x < width; ++x)
        {
          const int      c0 = c2;
          const int      c1 = py[x * 2];
          c2 = py[x * 2 + 1];
          pu[x] = static_cast <unsigned char> ((c0 + 2 * c1 + c2 + 2) >> 2);
        }
        py += pitch_y;
        pu += pitch_uv;
      }
    }

    // MPEG-1
    else
    {
      if (g_cpuid & CPUF_SSE2)
      {
        // SSE2 version
        for (int y = 0; y < height; y++)
        {
          __asm {
            movsx_int	QCX, i 
            mov	QSI, py
            movsx_int	QAX, pitch_y 
            mov	QDX, pu
            sub	QSI, QCX
            sub	QSI, QCX
            sub	QDX, QCX
            sub	QDX, 10h
            pcmpeqw	xmm7, xmm7
            psrlw	xmm7, 8
            align	10h
            l422 :
            movdqa	xmm0, [QSI + QCX * 2]
              movdqa	xmm2, [QSI + QCX * 2 + 10h]
              movdqa	xmm1, xmm0
              movdqa	xmm3, xmm2
              pand	xmm0, xmm7
              pand	xmm2, xmm7
              packuswb	xmm0, xmm2
              psrlw	xmm1, 8
              psrlw	xmm3, 8
              packuswb	xmm1, xmm3
              pavgb	xmm0, xmm1
              add	QCX, 10h
              jg	ls422
              movntdq[QCX + QDX], xmm0
              jnz	l422
              jmp	lx422
              ls422 :
            movq	qword ptr[QCX + QDX], xmm0
              lx422 :
          }
          py += pitch_y;
          pu += pitch_uv;
        }
      }
    }	// MPEG-1
  }

  // 4:4:4
  else if (subspl_dst_h_l2 == 0 && subspl_dst_v_l2 == 0)
  {
    for (int y = 0; y < height; ++y)
    {
      memcpy(pu + y * pitch_uv, py + y * pitch_y, width);
    }
  }

  else
  {
    assert(false);
    throw "aWarpSharp2: Unsupported colorspace.";
  }
  __asm sfence;
}

#pragma optimize("", on)

void SetPlane(PVideoFrame &dst, int plane, int value, const VideoInfo &dst_vi)
{
  const int dst_pitch = dst->GetPitch(plane);
  unsigned char *pdst = dst->GetWritePtr(plane);
  const int dst_row_size = dst->GetRowSize() >> dst_vi.GetPlaneWidthSubsampling(plane);
  int height = dst->GetHeight() >> dst_vi.GetPlaneHeightSubsampling(plane);
  if (dst_pitch == dst_row_size)
    memset(pdst, value, dst_pitch*height);
  else
    for (; height--; pdst += dst_pitch)
      memset(pdst, value, dst_row_size);
}

void SetPlane_uint16(PVideoFrame &dst, int plane, int value, const VideoInfo &dst_vi)
{
  const int dst_pitch = dst->GetPitch(plane) / sizeof(uint16_t);
  uint16_t *pdst = reinterpret_cast<uint16_t *>(dst->GetWritePtr(plane));
  const int dst_row_size = (dst->GetRowSize() >> dst_vi.GetPlaneWidthSubsampling(plane)) / sizeof(uint16_t);
  int height = dst->GetHeight() >> dst_vi.GetPlaneHeightSubsampling(plane);
  if (dst_pitch == dst_row_size)
    std::fill_n((uint16_t *)pdst, dst_pitch*height, (uint16_t)value);
  else
    for (; height--; pdst += dst_pitch)
      std::fill_n((uint16_t *)pdst, dst_row_size, (uint16_t)value);
}

void SetPlane_float(PVideoFrame &dst, int plane, float value, const VideoInfo &dst_vi)
{
  const int dst_pitch = dst->GetPitch(plane) / sizeof(float);
  float *pdst = reinterpret_cast<float *>(dst->GetWritePtr(plane));
  const int dst_row_size = (dst->GetRowSize() >> dst_vi.GetPlaneWidthSubsampling(plane)) / sizeof(float);
  int height = dst->GetHeight() >> dst_vi.GetPlaneHeightSubsampling(plane);
  if (dst_pitch == dst_row_size)
    std::fill_n((float *)pdst, dst_pitch*height, value);
  else
    for (; height--; pdst += dst_pitch)
      std::fill_n((float *)pdst, dst_row_size, value);
}

void CopyPlane(PVideoFrame &src, PVideoFrame &dst, int plane, const VideoInfo &dst_vi)
{
  const int src_pitch = src->GetPitch(plane);
  const int dst_pitch = dst->GetPitch(plane);
  const unsigned char *psrc = src->GetReadPtr(plane);
  unsigned char *pdst = dst->GetWritePtr(plane);
  const int dst_row_size = dst->GetRowSize() >> dst_vi.GetPlaneWidthSubsampling(plane);
  int height = dst->GetHeight() >> dst_vi.GetPlaneHeightSubsampling(plane);
  if (dst_pitch == src_pitch && dst_pitch == dst_row_size)
    memcpy(pdst, psrc, dst_pitch*height);
  else
    for (; height--; psrc += src_pitch, pdst += dst_pitch)
      memcpy(pdst, psrc, dst_row_size);
}

#define BlurRX(src, tmp, plane, blur_type, vi) (blur_type ? BlurR2 : BlurR6)(src, tmp, plane, vi)

void CheckParams(IScriptEnvironment *env, const char *name, bool yuvPlanar, int thresh, int blur_type, int depth, int depthC, int chroma)
{
  if (!(g_cpuid & CPUF_SSE2))
    env->ThrowError("%s: SSE2 capable CPU is required", name);
  if (!yuvPlanar)
    env->ThrowError("%s: Planar YUV input is required", name);
  if (thresh < 0 || thresh > 255)
    env->ThrowError("%s: 'thresh' must be 0..255", name);
  if (blur_type < 0 || blur_type > 1)
    env->ThrowError("%s: 'type' must be 0..1", name);
  if (depth < -128 || depth > 127)
    env->ThrowError("%s: 'depth' must be -128..127", name);
  if (depthC < -128 || depthC > 127)
    env->ThrowError("%s: 'depthC' must be -128..127", name);
  if (chroma < 0 || chroma > 6)
    env->ThrowError("%s: 'chroma' must be 0..6", name);
}

class aWarpSharp : public GenericVideoFilter
{
  int thresh;
  int blur_level;
  int depth;
  int depthC;
  int chroma;
  int blur_type;
  bool cplace_mpeg2_flag = false;
  
  int pixelsize;
  int bits_per_pixel;

public:
  aWarpSharp(PClip _child, int _thresh, int _blur_level, int _blur_type, int _depth, int _chroma, int _depthC, bool _cplace_mpeg2_flag, IScriptEnvironment *env) :
    GenericVideoFilter(_child), thresh(_thresh), blur_level(_blur_level), blur_type(_blur_type), depth(_depth), chroma(_chroma), depthC(_depthC), cplace_mpeg2_flag(_cplace_mpeg2_flag)
  {
    pixelsize = vi.ComponentSize();
    bits_per_pixel = vi.BitsPerComponent();
    if (depthC == 0)
      depthC = vi.Is444() ? depth : depth / 2;
    if (vi.IsY())
      chroma = 1;
    CheckParams(env, "aWarpSharp2", vi.IsYUV() && vi.IsPlanar(), thresh, blur_type, depth, depthC, chroma);
  }
  ~aWarpSharp() {}
  PVideoFrame __stdcall GetFrame(int n, IScriptEnvironment *env);
};

PVideoFrame __stdcall aWarpSharp::GetFrame(int n, IScriptEnvironment *env)
{
  PVideoFrame src = child->GetFrame(n, env);
  PVideoFrame tmp = env->NewVideoFrame(vi);
  PVideoFrame dst = env->NewVideoFrame(vi);

  // plane Y
  if (chroma != 5)
  {
    Sobel(src, tmp, PLANAR_Y, thresh, vi, vi);
    for (int i = blur_level; i; i--)
      BlurRX(tmp, dst, PLANAR_Y, blur_type, vi);
    if (chroma != 6)
      Warp0(src, tmp, dst, PLANAR_Y, PLANAR_Y, depth, vi);
    else
      CopyPlane(src, dst, PLANAR_Y, vi);
  }
  else
    CopyPlane(src, dst, PLANAR_Y, vi);

  // plane U and V
  switch (chroma)
  {
  case 0:
    switch (pixelsize) {
    case 1:
      SetPlane(dst, PLANAR_U, 0x80, vi);
      SetPlane(dst, PLANAR_V, 0x80, vi);
      break;
    case 2: 
      SetPlane_uint16(dst, PLANAR_U, 1 << (bits_per_pixel - 1), vi);
      SetPlane_uint16(dst, PLANAR_V, 1 << (bits_per_pixel - 1), vi);
      break;
    case 4:
      SetPlane_float(dst, PLANAR_U, 0.5f, vi);
      SetPlane_float(dst, PLANAR_V, 0.5f, vi);
      break;
    }
    break;
  case 2:
    CopyPlane(src, dst, PLANAR_U, vi);
    CopyPlane(src, dst, PLANAR_V, vi);
    break;
  case 3:
  case 5:
    Sobel(src, tmp, PLANAR_U, thresh, vi, vi);
    for (int i = (blur_level + 1) / 2; i; i--)
      BlurRX(tmp, dst, PLANAR_U, blur_type, vi);
    Warp0(src, tmp, dst, PLANAR_U, PLANAR_U, depthC, vi);
    Sobel(src, tmp, PLANAR_V, thresh, vi, vi);
    for (int i = (blur_level + 1) / 2; i; i--)
      BlurRX(tmp, dst, PLANAR_V, blur_type, vi);
    Warp0(src, tmp, dst, PLANAR_V, PLANAR_V, depthC, vi);
    break;
  case 4:
  case 6:
    if (!vi.Is444())
    {
      GuideChroma(tmp, tmp, vi, vi, cplace_mpeg2_flag);
      Warp0(src, tmp, dst, PLANAR_U, PLANAR_U, depthC, vi);
      Warp0(src, tmp, dst, PLANAR_V, PLANAR_U, depthC, vi);
    }
    else
    {
      Warp0(src, tmp, dst, PLANAR_U, PLANAR_Y, depthC, vi);
      Warp0(src, tmp, dst, PLANAR_V, PLANAR_Y, depthC, vi);
    }
  }

  if (!(g_cpuid & CPUF_SSE2))
    __asm emms;

  return dst;
}

class aSobel : public GenericVideoFilter
{
  int thresh;
  int chroma;

  int pixelsize;
  int bits_per_pixel;
public:
  aSobel(PClip _child, int _thresh, int _chroma, IScriptEnvironment *env) :
    GenericVideoFilter(_child), thresh(_thresh), chroma(_chroma)
  {
    pixelsize = vi.ComponentSize();
    bits_per_pixel = vi.BitsPerComponent();
    if (vi.IsY())
      chroma = 1;
    CheckParams(env, "aSobel", vi.IsYUV() && vi.IsPlanar(), thresh, 0, 0, 0, chroma);
  }
  ~aSobel() {}
  PVideoFrame __stdcall GetFrame(int n, IScriptEnvironment *env);
};

PVideoFrame __stdcall aSobel::GetFrame(int n, IScriptEnvironment *env)
{
  PVideoFrame src = child->GetFrame(n, env);
  PVideoFrame dst = env->NewVideoFrame(vi);

  // plane Y
  if (chroma < 5)
    Sobel(src, dst, PLANAR_Y, thresh, vi, vi);
  else
    CopyPlane(src, dst, PLANAR_Y, vi);

  // plane U and V
  switch (chroma)
  {
  case 0:
    switch (pixelsize) {
    case 1:
      SetPlane(dst, PLANAR_U, 0x80, vi);
      SetPlane(dst, PLANAR_V, 0x80, vi);
      break;
    case 2:
      SetPlane_uint16(dst, PLANAR_U, 1 << (bits_per_pixel - 1), vi);
      SetPlane_uint16(dst, PLANAR_V, 1 << (bits_per_pixel - 1), vi);
      break;
    case 4:
      SetPlane_float(dst, PLANAR_U, 0.5f, vi);
      SetPlane_float(dst, PLANAR_V, 0.5f, vi);
      break;
    }
    break;
  case 1:
    break;
  case 2:
    CopyPlane(src, dst, PLANAR_U, vi);
    CopyPlane(src, dst, PLANAR_V, vi);
    break;
  default:
    Sobel(src, dst, PLANAR_U, thresh, vi, vi);
    Sobel(src, dst, PLANAR_V, thresh, vi, vi);
    break;
  }

  if (!(g_cpuid & CPUF_SSE2))
    __asm emms;

  return dst;
}

class aBlur : public GenericVideoFilter
{
  int blur_level;
  int blur_type;
  int chroma;

  int pixelsize;
  int bits_per_pixel;
public:
  aBlur(PClip _child, int _blur_level, int _blur_type, int _chroma, IScriptEnvironment *env) :
    GenericVideoFilter(_child), blur_level(_blur_level), blur_type(_blur_type), chroma(_chroma)
  {
    pixelsize = vi.ComponentSize();
    bits_per_pixel = vi.BitsPerComponent();
    if (vi.IsY())
      chroma = 1;
    CheckParams(env, "aBlur", vi.IsYUV() && vi.IsPlanar(), 0, blur_type, 0, 0, chroma);
  }
  ~aBlur() {}
  PVideoFrame __stdcall GetFrame(int n, IScriptEnvironment *env);
};

PVideoFrame __stdcall aBlur::GetFrame(int n, IScriptEnvironment *env)
{
  PVideoFrame src = child->GetFrame(n, env);
  PVideoFrame tmp = env->NewVideoFrame(vi);
  env->MakeWritable(&src);

  if (chroma < 5)
    for (int i = blur_level; i; i--)
      BlurRX(src, tmp, PLANAR_Y, blur_type, vi);
  switch (chroma)
  {
  case 0:
    switch (pixelsize) {
    case 1:
      SetPlane(src, PLANAR_U, 0x80, vi);
      SetPlane(src, PLANAR_V, 0x80, vi);
      break;
    case 2:
      SetPlane_uint16(src, PLANAR_U, 1 << (bits_per_pixel - 1), vi);
      SetPlane_uint16(src, PLANAR_V, 1 << (bits_per_pixel - 1), vi);
      break;
    case 4:
      SetPlane_float(src, PLANAR_U, 0.5f, vi);
      SetPlane_float(src, PLANAR_V, 0.5f, vi);
      break;
    }
    break;
  case 1:
  case 2:
    break;
  default:
    for (int i = (blur_level + 1) / 2; i; i--)
      BlurRX(src, tmp, PLANAR_U, blur_type, vi);
    for (int i = (blur_level + 1) / 2; i; i--)
      BlurRX(src, tmp, PLANAR_V, blur_type, vi);
    break;
  }

  if (!(g_cpuid & CPUF_SSE2))
    __asm emms;

  return src;
}

class aWarp : public GenericVideoFilter
{
  PClip edges;
  int depth;
  int depthC;
  int chroma;
  bool cplace_mpeg2_flag = false;

  int pixelsize;
  int bits_per_pixel;
public:
  aWarp(PClip _child, PClip _edges, int _depth, int _chroma, int _depthC, bool _cplace_mpeg2_flag, IScriptEnvironment *env) :
    GenericVideoFilter(_child), edges(_edges), depth(_depth), chroma(_chroma), depthC(_depthC), cplace_mpeg2_flag(_cplace_mpeg2_flag)
  {
    pixelsize = vi.ComponentSize();
    bits_per_pixel = vi.BitsPerComponent();

    const VideoInfo &vi2 = edges->GetVideoInfo();
    if (depthC == NULL)
      depthC = vi.Is444() ? depth : depth / 2;
    if (vi.IsY())
      chroma = 1;
    CheckParams(env, "aWarp", vi.IsYUV() && vi.IsPlanar() && vi2.IsYUV() && vi2.IsPlanar(), 0, 0, depth, depthC, chroma);
    if (vi.width != vi2.width || vi.height != vi2.height)
      env->ThrowError("%s: both sources must have the same width and height", "aWarp");
    if (vi.pixel_type != vi2.pixel_type)
    {
      env->ThrowError("%s: both sources must have the colorspace", "aWarp");
    }
  }
  ~aWarp() {}
  PVideoFrame __stdcall GetFrame(int n, IScriptEnvironment *env);
};

PVideoFrame __stdcall aWarp::GetFrame(int n, IScriptEnvironment *env)
{
  PVideoFrame src = child->GetFrame(n, env);
  PVideoFrame edg = edges->GetFrame(n, env);
  PVideoFrame dst = env->NewVideoFrame(vi);

  if (chroma < 5)
    Warp0(src, edg, dst, PLANAR_Y, PLANAR_Y, depth, vi);
  else
    CopyPlane(src, dst, PLANAR_Y, vi);
  switch (chroma)
  {
  case 0:
    switch (pixelsize) {
    case 1:
      SetPlane(dst, PLANAR_U, 0x80, vi);
      SetPlane(dst, PLANAR_V, 0x80, vi);
      break;
    case 2:
      SetPlane_uint16(dst, PLANAR_U, 1 << (bits_per_pixel - 1), vi);
      SetPlane_uint16(dst, PLANAR_V, 1 << (bits_per_pixel - 1), vi);
      break;
    case 4:
      SetPlane_float(dst, PLANAR_U, 0.5f, vi);
      SetPlane_float(dst, PLANAR_V, 0.5f, vi);
      break;
    }
    break;
  case 2:
    CopyPlane(src, dst, PLANAR_U, vi);
    CopyPlane(src, dst, PLANAR_V, vi);
    break;
  case 3:
  case 5:
    Warp0(src, edg, dst, PLANAR_V, PLANAR_V, depthC, vi);
    Warp0(src, edg, dst, PLANAR_U, PLANAR_U, depthC, vi);
    break;
  case 4:
  case 6:
    if (!vi.Is444())
    {
      if (edg->IsWritable())
        GuideChroma(edg, edg, vi, vi, cplace_mpeg2_flag);
      else
      {
        PVideoFrame tmp = env->NewVideoFrame(vi);
        GuideChroma(edg, tmp, vi, vi, cplace_mpeg2_flag);
        edg = tmp;
      }
      Warp0(src, edg, dst, PLANAR_U, PLANAR_U, depthC, vi);
      Warp0(src, edg, dst, PLANAR_V, PLANAR_U, depthC, vi);
    }
    else
    {
      Warp0(src, edg, dst, PLANAR_U, PLANAR_Y, depthC, vi);
      Warp0(src, edg, dst, PLANAR_V, PLANAR_Y, depthC, vi);
    }
    break;
  }

  if (!(g_cpuid & CPUF_SSE2))
    __asm emms;

  return dst;
}

class aWarp4 : public GenericVideoFilter
{
  PClip edges;
  int depth;
  int depthC;
  int chroma;
  bool cplace_mpeg2_flag = false;

  int pixelsize;
  int bits_per_pixel;
public:
  aWarp4(PClip _child, PClip _edges, int _depth, int _chroma, int _depthC, bool _cplace_mpeg2_flag, IScriptEnvironment *env) :
    GenericVideoFilter(_child), edges(_edges), depth(_depth), chroma(_chroma), depthC(_depthC), cplace_mpeg2_flag(_cplace_mpeg2_flag)
  {
    pixelsize = vi.ComponentSize();
    bits_per_pixel = vi.BitsPerComponent();

    const VideoInfo &vi2 = edges->GetVideoInfo();
    if (depthC == NULL)
      depthC = vi.Is444() ? depth : depth / 2;
    if (vi.IsY())
      chroma = 1;
    CheckParams(env, "aWarp", vi.IsYUV() && vi.IsPlanar() && vi2.IsYUV() && vi2.IsPlanar(), 0, 0, depth, depthC, chroma);
    if (vi.width != vi2.width * 4 || vi.height != vi2.height * 4)
      env->ThrowError("%s: first source must be excatly 4 times width and height of second source", "aWarp4");
    if (vi.pixel_type != vi2.pixel_type)
    {
      env->ThrowError("%s: both sources must have the colorspace", "aWarp4");
    }
    vi = vi2;
  }
  ~aWarp4() {}
  PVideoFrame __stdcall GetFrame(int n, IScriptEnvironment *env);
};

PVideoFrame __stdcall aWarp4::GetFrame(int n, IScriptEnvironment *env)
{
  PVideoFrame src = child->GetFrame(n, env);
  PVideoFrame edg = edges->GetFrame(n, env);
  PVideoFrame dst = env->NewVideoFrame(vi);

  if (chroma < 5)
    Warp2(src, edg, dst, PLANAR_Y, PLANAR_Y, depth, vi);
  else
    CopyPlane(src, dst, PLANAR_Y, vi);
  switch (chroma)
  {
  case 0:
    switch (pixelsize) {
    case 1:
      SetPlane(dst, PLANAR_U, 0x80, vi);
      SetPlane(dst, PLANAR_V, 0x80, vi);
      break;
    case 2:
      SetPlane_uint16(dst, PLANAR_U, 1 << (bits_per_pixel - 1), vi);
      SetPlane_uint16(dst, PLANAR_V, 1 << (bits_per_pixel - 1), vi);
      break;
    case 4:
      SetPlane_float(dst, PLANAR_U, 0.5f, vi);
      SetPlane_float(dst, PLANAR_V, 0.5f, vi);
      break;
    }
    break;
  case 2:
    CopyPlane(src, dst, PLANAR_U, vi);
    CopyPlane(src, dst, PLANAR_V, vi);
    break;
  case 3:
  case 5:
    Warp2(src, edg, dst, PLANAR_V, PLANAR_V, depthC, vi);
    Warp2(src, edg, dst, PLANAR_U, PLANAR_U, depthC, vi);
    break;
  case 4:
  case 6:
    if (!vi.Is444())
    {
      if (edg->IsWritable())
        GuideChroma(edg, edg, vi, vi, cplace_mpeg2_flag);
      else
      {
        PVideoFrame tmp = env->NewVideoFrame(vi);
        GuideChroma(edg, tmp, vi, vi, cplace_mpeg2_flag);
        edg = tmp;
      }
      Warp2(src, edg, dst, PLANAR_U, PLANAR_U, depthC, vi);
      Warp2(src, edg, dst, PLANAR_V, PLANAR_U, depthC, vi);
    }
    else
    {
      Warp2(src, edg, dst, PLANAR_U, PLANAR_Y, depthC, vi);
      Warp2(src, edg, dst, PLANAR_V, PLANAR_Y, depthC, vi);
    }
    break;
  }

  if (!(g_cpuid & CPUF_SSE2))
    __asm emms;

  return dst;
}

static bool is_cplace_mpeg2(const AVSValue &args, int pos)
{
  const char *   cplace_0 = args[pos].AsString("");
  const bool     cplace_mpeg2_flag = (_stricmp(cplace_0, "MPEG2") == 0);
  return (cplace_mpeg2_flag);
}

AVSValue __cdecl Create_aWarpSharp(AVSValue args, void *user_data, IScriptEnvironment *env)
{
  switch ((intptr_t)user_data)
  {
  case 0:
    return new aWarpSharp(args[0].AsClip(), args[1].AsInt(0x80), args[2].AsInt(args[3].AsInt(0) ? 3 : 2), args[3].AsInt(0), args[4].AsInt(16), args[5].AsInt(4), args[6].AsInt(NULL), is_cplace_mpeg2(args, 7), env);
  case 1:
  {
    const int type = args[5].AsInt(2) != 2;
    const int blurlevel = args[2].AsInt(2);
    const unsigned int cm = args[4].AsInt(1);
    static const char map[3] = { 2, 4, 3 };
    return new aWarpSharp(args[0].AsClip(), int(args[3].AsFloat(0.5)*256.0), type ? blurlevel * 3 : blurlevel, type, int(args[1].AsFloat(16.0)*blurlevel*0.5), cm < 3 ? map[cm] : -1, -1, false, env);
  }
  case 2:
    return new aSobel(args[0].AsClip(), args[1].AsInt(0x80), args[2].AsInt(1), env);
  case 3:
    return new aBlur(args[0].AsClip(), args[1].AsInt(args[2].AsInt(1) ? 3 : 2), args[2].AsInt(1), args[3].AsInt(1), env);
  case 4:
    return new aWarp(args[0].AsClip(), args[1].AsClip(), args[2].AsInt(3), args[3].AsInt(4), args[4].AsInt(NULL), is_cplace_mpeg2(args, 5), env);
  case 5:
    return new aWarp4(args[0].AsClip(), args[1].AsClip(), args[2].AsInt(3), args[3].AsInt(4), args[4].AsInt(NULL), is_cplace_mpeg2(args, 5), env);
  }
  return NULL;
}

// thresh: 0..255
// blur:   0..?
// type:   0..1
// depth:  -128..127
// chroma modes:
// 0 - zero
// 1 - don't care
// 2 - copy
// 3 - process
// 4 - guide by luma - warp only
// remap from MarcFD's aWarpSharp: thresh=_thresh*256, blur=_blurlevel, type= (bm=0)->1, (bm=2)->0, depth=_depth*_blurlevel/2, chroma= 0->2, 1->4, 2->3

/* New 2.6 requirement!!! */
// Declare and initialise server pointers static storage.
const AVS_Linkage *AVS_linkage = 0;

/* New 2.6 requirement!!! */
// DLL entry point called from LoadPlugin() to setup a user plugin.
extern "C" __declspec(dllexport) const char* __stdcall
AvisynthPluginInit3(IScriptEnvironment* env, const AVS_Linkage* const vectors) {

  /* New 2.6 requirment!!! */
  // Save the server pointers.
  AVS_linkage = vectors;
  /*if (!g_cpuid)
    g_cpuid = GetCPUID();
  */

  g_cpuid = env->GetCPUFlags(); // PF

  env->AddFunction("aWarpSharp2", "c[thresh]i[blur]i[type]i[depth]i[chroma]i[depthC]i[cplace]s", Create_aWarpSharp, (void*)0);
  env->AddFunction("aWarpSharp", "c[depth]f[blurlevel]i[thresh]f[cm]i[bm]i[show]b", Create_aWarpSharp, (void*)1);
  env->AddFunction("aSobel", "c[thresh]i[chroma]i", Create_aWarpSharp, (void*)2);
  env->AddFunction("aBlur", "c[blur]i[type]i[chroma]i", Create_aWarpSharp, (void*)3);
  env->AddFunction("aWarp", "cc[depth]i[chroma]i[depthC]i[cplace]s", Create_aWarpSharp, (void*)4);
  env->AddFunction("aWarp4", "cc[depth]i[chroma]i[depthC]i[cplace]s", Create_aWarpSharp, (void*)5);

  //return "aWarpSharp package 2012.03.28";
  return "aWarpSharp package 2016.06.23";
}
