// aWarpSharp package 2012.03.28 for Avisynth 2.5
// Copyright (C) 2003 MarcFD, 2012 Skakov Pavel
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

// note: when included SMAGL must be defined to log2 of src upscaling level
#define SMAG (1<<SMAGL)
#define MERGE2(a, b) a##b
#define MERGE(a, b) MERGE2(a, b)

// Warp0 and Warp2
/*
#define SMAGL 2
#include "aWarp.h"
#define SMAGL 0
#include "aWarp.h"
*/
// WxH min: 4x2, mul: 4x1
// depth: 0..7FFFh

/*
; parameter 1(src) : 8 + ebp
; parameter 2(edg) : 12 + ebp
; parameter 3(dst) : 16 + ebp
; parameter 4(plane) : 20 + ebp
; parameter 5(plane_edg) : 24 + ebp
; parameter 6(depth) : 28 + ebp
; parameter 7(dst_vi) : 32 + ebp
*/
void MERGE(Warp, SMAGL)(PVideoFrame &src, PVideoFrame &edg, PVideoFrame &dst, int plane, int plane_edg, int depth, const VideoInfo &dst_vi)
{
	const int src_pitch = src->GetPitch(plane);
	const int edg_pitch = edg->GetPitch(plane_edg);
	const int dst_pitch = dst->GetPitch(plane);
	const int row_size = dst->GetRowSize() >> dst_vi.GetPlaneWidthSubsampling (plane);
	const int i = -(row_size + 3 & ~3);
	const int c = row_size + i - 1;
	const int height = dst->GetHeight() >> dst_vi.GetPlaneHeightSubsampling (plane);
	const unsigned char *psrc = src->GetReadPtr(plane) - i*SMAG;
	const unsigned char *pedg = edg->GetReadPtr(plane_edg) - i;
	unsigned char *pdst = dst->GetWritePtr(plane) - i;

	depth <<= 8;

	const short x_limit_min[8] = {i*SMAG, (i-1)*SMAG, (i-2)*SMAG, (i-3)*SMAG, (i-4)*SMAG, (i-5)*SMAG, (i-6)*SMAG, (i-7)*SMAG};
	const short x_limit_max[8] = {c*SMAG, (c-1)*SMAG, (c-2)*SMAG, (c-3)*SMAG, (c-4)*SMAG, (c-5)*SMAG, (c-6)*SMAG, (c-7)*SMAG};

  if (g_cpuid & CPUF_SSE2)
  {
    // SSE2 and SSSE3 versions
    for (int y = 0; y < height; y++)
    {
      int y_limit_min = -y * 0x80;
      int y_limit_max = (height - y) * 0x80 - 0x81;
      int edg_pitchp = -(y ? edg_pitch : 0);
      int edg_pitchn = y != height - 1 ? edg_pitch : 0;

      __asm {
        mov	QSI, psrc
        mov	QCX, pedg
        mov	QAX, pdst
        movsx_int	QDX, src_pitch
        movsx_int	QBX, edg_pitchp
        movsx_int	QDI, i          // signed!
        sub	QAX, 8
        add	QDX, QSI
        add	QBX, QCX
        movd	xmm1, y_limit_min
        movd	xmm2, y_limit_max
        movdqu	xmm3, x_limit_min
        movdqu	xmm4, x_limit_max
        movd	xmm6, depth
        movd	xmm0, src_pitch
        pcmpeqw	xmm7, xmm7
        psrlw	xmm7, 0Fh
        punpcklwd	xmm0, xmm7
        pshufd	xmm1, xmm1, 0
        pshufd	xmm2, xmm2, 0
        pshufd	xmm6, xmm6, 0
        pshufd	xmm0, xmm0, 0
        packssdw	xmm1, xmm1
        packssdw	xmm2, xmm2
        packssdw	xmm6, xmm6
        pcmpeqw	xmm5, xmm5
        psllw	xmm5, 0Fh
        push	QBP            // save ebp. 4 bytes on stack also for x64!
#if defined(X86_32)
        // make room for local variables
        push	edg_pitchn
        
        lea	ebp, [esp - 5Ch]
        and ebp, ~0Fh
        xchg	esp, ebp
        
        push	eax
        push	ebp
        mov	ebp, [ebp]
        add	ebp, ecx

        movdqa	xmm7, [QDI + QCX]
        movdqa[esp + 8h], xmm0
        movdqa[esp + 18h], xmm6
        movdqa[esp + 28h], xmm1
        movdqa[esp + 38h], xmm2
        movdqa[esp + 48h], xmm3
        movdqa[esp + 58h], xmm4
#else
        movsx_int	rbp, edg_pitchn
        mov	r8, rax                     // save to R8
        add	rbp, rcx
        movdqa	xmm7, [rdi + rcx]       
        movdqa	xmm8, xmm0	// [rsp+ 8h]
        movdqa	xmm9, xmm6	// [rsp+18h]
        movdqa	xmm10, xmm1	// [rsp+28h]
        movdqa	xmm11, xmm2	// [rsp+38h]
        movdqa	xmm12, xmm3	// [rsp+48h]
        movdqa	xmm13, xmm4	// [rsp+58h]
#endif
        movdqa	xmm1, xmm7
        pslldq	xmm7, 7
        punpcklqdq	xmm7, xmm1
        psrldq	xmm1, 1
        psrldq	xmm7, 7
        test	g_cpuid, CPUF_SSSE3
        jnz	l3
        psrlw	xmm5, 8
        align	10h
        // SSE2 version
        l2 :
        movq	xmm4, qword ptr[QDI + QBX]
          movq	xmm2, qword ptr[QDI + QBP]
          pxor	xmm3, xmm3
          punpcklbw	xmm7, xmm3
          punpcklbw	xmm1, xmm3
          punpcklbw	xmm4, xmm3
          punpcklbw	xmm2, xmm3
          psubw	xmm7, xmm1
          psubw	xmm4, xmm2
          psllw	xmm7, 7
          psllw	xmm4, 7
          pmulhw	xmm7, xmm6 // depth
          pmulhw	xmm4, xmm6 // depth
#if defined(X86_32)
          movdqa	xmm6, [esp + 8h] // preload 1 src_pitch
          pmaxsw	xmm4, [esp + 28h] // y_limit_min
          pminsw	xmm4, [esp + 38h] // y_limit_max
#else
          movdqa	xmm6, xmm8	// preload 1 src_pitch
          pmaxsw	xmm4, xmm10	// y_limit_min
          pminsw	xmm4, xmm11	// y_limit_max
#endif
          pcmpeqw	xmm3, xmm3
          psrlw	xmm3, 9
          movdqa	xmm1, xmm7
          movdqa	xmm2, xmm4
#if SMAGL
          psllw	xmm4, SMAGL
          psllw	xmm7, SMAGL
#endif
          pand	xmm7, xmm3 // 007F
          pand	xmm4, xmm3 // 007F
          psraw	xmm1, 7 - SMAGL
          psraw	xmm2, 7 - SMAGL

          movd	xmm3, edi
          pshufd	xmm3, xmm3, 0
#if SMAGL
          pslld	xmm3, SMAGL
#endif
          packssdw	xmm3, xmm3
          paddsw	xmm1, xmm3

#if defined(X86_32)
          movdqa	xmm3, [esp + 58h] // x_limit_max
          movdqa	xmm0, [esp + 48h] // x_limit_min
#else
          movdqa	xmm3, xmm13	// x_limit_max
          movdqa	xmm0, xmm12	// x_limit_min
#endif
          pcmpgtw	xmm3, xmm1
          pcmpgtw	xmm0, xmm1
#if defined(X86_32)
          pminsw	xmm1, [esp + 58h] // x_limit_max
          pmaxsw	xmm1, [esp + 48h] // x_limit_min
#else
          movdqa	xmm1, xmm13	// x_limit_max
          movdqa	xmm1, xmm12	// x_limit_min
#endif
          pand	xmm7, xmm3
          pandn	xmm0, xmm7

          movdqa	xmm7, xmm2
          punpcklwd	xmm2, xmm1
          punpckhwd	xmm7, xmm1
          pmaddwd	xmm2, xmm6 // 1 src_pitch
          pmaddwd	xmm7, xmm6 // 1 src_pitch

          movd	eax, xmm2
          psrldq	xmm2, 4
          pinsrw	xmm3, [QAX + QSI], 0
          pinsrw	xmm1, [QAX + QDX], 0
          movd	eax, xmm2
          psrldq	xmm2, 4
          pinsrw	xmm3, [QAX + QSI + 1 * SMAG], 1
          pinsrw	xmm1, [QAX + QDX + 1 * SMAG], 1
          movd	eax, xmm2
          psrldq	xmm2, 4
          pinsrw	xmm3, [QAX + QSI + 2 * SMAG], 2
          pinsrw	xmm1, [QAX + QDX + 2 * SMAG], 2
          movd	eax, xmm2
          pinsrw	xmm3, [QAX + QSI + 3 * SMAG], 3
          pinsrw	xmm1, [QAX + QDX + 3 * SMAG], 3
          movd	eax, xmm7
          psrldq	xmm7, 4
          pinsrw	xmm3, [QAX + QSI + 4 * SMAG], 4
          pinsrw	xmm1, [QAX + QDX + 4 * SMAG], 4
          movd	eax, xmm7
          psrldq	xmm7, 4
          pinsrw	xmm3, [QAX + QSI + 5 * SMAG], 5
          pinsrw	xmm1, [QAX + QDX + 5 * SMAG], 5
          movd	eax, xmm7
          psrldq	xmm7, 4
          pinsrw	xmm3, [QAX + QSI + 6 * SMAG], 6
          pinsrw	xmm1, [QAX + QDX + 6 * SMAG], 6
          movd	eax, xmm7
          pinsrw	xmm3, [QAX + QSI + 7 * SMAG], 7
          pinsrw	xmm1, [QAX + QDX + 7 * SMAG], 7
#if defined(X86_32)
          mov	eax, [esp + 4]
#else
          mov	rax, r8    // restore rax
#endif

          pcmpeqw	xmm6, xmm6
          movdqa	xmm2, xmm3
          psrlw	xmm6, 8
          movdqa	xmm7, xmm1
          pand	xmm3, xmm6 // 00FF
          pand	xmm1, xmm6 // 00FF
          movdqa	xmm6, xmm5
          psubw	xmm6, xmm0
          pmullw	xmm3, xmm6
          pmullw	xmm1, xmm6
          movdqa	xmm6, xmm5 // 0080
          psrlw	xmm5, 1
          psrlw	xmm2, 8
          psrlw	xmm7, 8
          pmullw	xmm2, xmm0
          pmullw	xmm7, xmm0
          paddw	xmm3, xmm2
          paddw	xmm1, xmm7
          paddw	xmm3, xmm5 // 0040
          paddw	xmm1, xmm5 // 0040
          psraw	xmm3, 7
          psraw	xmm1, 7

          psubw	xmm6, xmm4
          movdqu	xmm7, [QDI + QCX + 7]
          pmullw	xmm1, xmm4
          pmullw	xmm3, xmm6
          paddw	xmm3, xmm1
          movdqa	xmm1, xmm7
#if defined(X86_32)
            movdqa	xmm6, [esp + 18h] // preload depth
#else
            movdqa	xmm6, xmm9	// preload depth
#endif

          paddw	xmm3, xmm5 // 0040
          psrldq	xmm1, 2
          psraw	xmm3, 7
          paddw	xmm5, xmm5
          packuswb	xmm3, xmm3
          add	QDI, 8
          jg	ls
          movq	qword ptr[QDI + QAX], xmm3
          jnz	l2
          jmp	lx
          align	10h
          // SSSE3 version (pmaddubsw, psignw, palignr)
          l3 :
        movq	xmm4, qword ptr[QDI + QBX]
          movq	xmm2, qword ptr[QDI + QBP]
          pxor	xmm0, xmm0
          punpcklbw	xmm7, xmm0
          punpcklbw	xmm1, xmm0
          punpcklbw	xmm4, xmm0
          punpcklbw	xmm2, xmm0
          psubw	xmm7, xmm1
          psubw	xmm4, xmm2
          psllw	xmm7, 7
          psllw	xmm4, 7
          pmulhw	xmm7, xmm6 // depth
          pmulhw	xmm4, xmm6 // depth
          movd	xmm6, edi // preload
#if defined(X86_32)
          pmaxsw	xmm4, [esp + 28h] // y_limit_min
          pminsw	xmm4, [esp + 38h] // y_limit_max
#else
          pmaxsw	xmm4, xmm10	// y_limit_min
          pminsw	xmm4, xmm11	// y_limit_max
#endif

          pshufd	xmm6, xmm6, 0
#if SMAGL
          pslld	xmm6, SMAGL
#endif

          pcmpeqw	xmm0, xmm0
          psrlw	xmm0, 9
          movdqa	xmm2, xmm4
          movdqa	xmm1, xmm7
#if SMAGL
          psllw	xmm4, SMAGL
          psllw	xmm7, SMAGL
#endif
          pand	xmm4, xmm0 // 007F
          pand	xmm7, xmm0 // 007F

          psraw	xmm1, 7 - SMAGL
          packssdw	xmm6, xmm6
          paddsw	xmm1, xmm6
#if defined(X86_32)
          movdqa	xmm6, [esp + 8h] // preload 1 src_pitch

          movdqa	xmm0, [esp + 58h] // x_limit_max
          movdqa	xmm3, [esp + 48h] // x_limit_min
#else
          movdqa	xmm6, xmm8	// preload 1 src_pitch

          movdqa	xmm0, xmm13	// x_limit_max
          movdqa	xmm3, xmm12	// x_limit_min
#endif

          pcmpgtw	xmm0, xmm1
          pcmpgtw	xmm3, xmm1
#if defined(X86_32)
          pminsw	xmm1, [esp + 58h] // x_limit_max
          pmaxsw	xmm1, [esp + 48h] // x_limit_min
#else
          pminsw	xmm1, xmm13	// x_limit_max
          pmaxsw	xmm1, xmm12	// x_limit_min
#endif
          pand	xmm7, xmm0
          pandn	xmm3, xmm7

          psraw	xmm2, 7 - SMAGL
          movdqa	xmm7, xmm2
          punpcklwd	xmm2, xmm1
          punpckhwd	xmm7, xmm1
          pmaddwd	xmm2, xmm6 // 1 src_pitch
          pmaddwd	xmm7, xmm6 // 1 src_pitch
#if defined(X86_32)
          movdqa	xmm6, [esp + 18h] // preload depth
#else
          movdqa	xmm6, xmm9	// preload depth
#endif

          psignw	xmm3, xmm5 // 8000
          psignw	xmm4, xmm5 // 8000
          packsswb	xmm5, xmm5
          packsswb	xmm3, xmm3
          packsswb	xmm4, xmm4

          movdqa	xmm0, xmm5 // 80
          movdqa	xmm1, xmm5 // 80
          psubb	xmm0, xmm3
          psubb	xmm1, xmm4
          psrlw	xmm5, 9
          punpcklbw	xmm0, xmm3
          punpcklbw	xmm1, xmm4

          movd	eax, xmm2
          SEXT(rax, eax)
          pinsrw	xmm3, [QAX + QSI], 0
          pinsrw	xmm4, [QAX + QDX], 0
          psrldq	xmm2, 4
          movd	eax, xmm2
          SEXT(rax, eax)
          pinsrw	xmm3, [QAX + QSI + 1 * SMAG], 1
          pinsrw	xmm4, [QAX + QDX + 1 * SMAG], 1
          psrldq	xmm2, 4
          movd	eax, xmm2
          SEXT(rax, eax)
          pinsrw	xmm3, [QAX + QSI + 2 * SMAG], 2
          pinsrw	xmm4, [QAX + QDX + 2 * SMAG], 2
          psrldq	xmm2, 4
          movd	eax, xmm2
          SEXT(rax, eax)
          pinsrw	xmm3, [QAX + QSI + 3 * SMAG], 3
          pinsrw	xmm4, [QAX + QDX + 3 * SMAG], 3
          movd	eax, xmm7
          SEXT(rax, eax)
          pinsrw	xmm3, [QAX + QSI + 4 * SMAG], 4
          pinsrw	xmm4, [QAX + QDX + 4 * SMAG], 4
          psrldq	xmm7, 4
          movd	eax, xmm7
          SEXT(rax, eax)
          pinsrw	xmm3, [QAX + QSI + 5 * SMAG], 5
          pinsrw	xmm4, [QAX + QDX + 5 * SMAG], 5
          psrldq	xmm7, 4
          movd	eax, xmm7
          SEXT(rax, eax)
          pinsrw	xmm3, [QAX + QSI + 6 * SMAG], 6
          pinsrw	xmm4, [QAX + QDX + 6 * SMAG], 6
          psrldq	xmm7, 4
          movd	eax, xmm7
          SEXT(rax, eax)
          pinsrw	xmm3, [QAX + QSI + 7 * SMAG], 7
          pinsrw	xmm4, [QAX + QDX + 7 * SMAG], 7

          pcmpeqw	xmm2, xmm2
          movdqu	xmm7, [QDI + QCX + 7]
          pmaddubsw	xmm3, xmm0
          pmaddubsw	xmm4, xmm0
#if defined(X86_32)
          mov	eax, [esp + 4]
#else
          mov	rax, r8
#endif
          psignw	xmm3, xmm2
          psignw	xmm4, xmm2
          paddw	xmm3, xmm5 // 0040
          paddw	xmm4, xmm5 // 0040
          psraw	xmm3, 7
          psraw	xmm4, 7
          packuswb	xmm3, xmm3
          packuswb	xmm4, xmm4
          punpcklbw	xmm3, xmm4
          pmaddubsw	xmm3, xmm1
          palignr	xmm1, xmm7, 2
          psignw	xmm3, xmm2

          paddw	xmm3, xmm5 // 0040
          psraw	xmm3, 7
          psllw	xmm5, 9
          packuswb	xmm3, xmm3
          add	QDI, 8
          jg	ls
          movq	qword ptr[QDI + QAX], xmm3
          jnz	l3
          jmp	lx
        ls :
          movd[QDI + QAX], xmm3
        lx :
#if defined(X86_32)
          pop	QSP
          pop	QCX
#endif
          pop	QBP
      }
      psrc += src_pitch*SMAG;
      pedg += edg_pitch;
      pdst += dst_pitch;
    } // for y
  }
#ifdef X86_32
  // PF personal opinion: it's 2016 now, MMX parts should die.
  else
  {
    // MMXExt version
    for (int y = 0; y < height; y++)
    {
      int y_limit_min = -y * 0x80;
      int y_limit_max = (height - y) * 0x80 - 0x81;
      int edg_pitchp = -(y ? edg_pitch : 0);
      int edg_pitchn = y != height - 1 ? edg_pitch : 0;

      __asm {
        mov	esi, psrc
        mov	ecx, pedg
        mov	eax, pdst
        mov	edx, src_pitch
        mov	ebx, edg_pitchp
        mov	edi, i
        add	edx, esi
        add	ebx, ecx
        movd	mm1, y_limit_min
        movd	mm2, y_limit_max
        movq	mm3, x_limit_min
        movq	mm4, x_limit_max
        movd	mm6, depth
        movd	mm0, src_pitch
        pcmpeqw	mm7, mm7
        psrlw	mm7, 0Fh
        punpcklwd	mm0, mm7
        punpckldq	mm1, mm1
        punpckldq	mm2, mm2
        punpckldq	mm6, mm6
        punpckldq	mm0, mm0
        packssdw	mm1, mm1
        packssdw	mm2, mm2
        packssdw	mm6, mm6
        pcmpeqw	mm5, mm5
        psllw	mm5, 0Fh
        push	ebp
        push	edg_pitchn
        lea	ebp, [esp - 2Ch]
        and ebp, ~07h
        xchg	esp, ebp
        push	eax
        push	ebp
        mov	ebp, [ebp]
        add	ebp, ecx
        movq	mm7, [edi + ecx]
        movq[esp + 8h], mm0
        movq[esp + 10h], mm6
        movq[esp + 18h], mm1
        movq[esp + 20h], mm2
        movq[esp + 28h], mm3
        movq[esp + 30h], mm4
        movq	mm1, mm7
        psllq	mm7, 18h
        punpckldq	mm7, mm1
        psrlq	mm1, 8
        psrlq	mm7, 18h
        psrlw	mm5, 8
        align	10h
        lm :
        movd	mm4, [edi + ebx]
          movd	mm2, [edi + ebp]
          pxor	mm3, mm3
          punpcklbw	mm7, mm3
          punpcklbw	mm1, mm3
          punpcklbw	mm4, mm3
          punpcklbw	mm2, mm3
          psubw	mm7, mm1
          psubw	mm4, mm2
          psllw	mm7, 7
          psllw	mm4, 7
          pmulhw	mm7, mm6 // depth
          pmulhw	mm4, mm6 // depth
          movq	mm6, [esp + 8h] // preload 1 src_pitch
          pmaxsw	mm4, [esp + 18h] // y_limit_min
          pminsw	mm4, [esp + 20h] // y_limit_max

          pcmpeqw	mm3, mm3
          psrlw	mm3, 9
          movq	mm1, mm7
          movq	mm2, mm4
#if SMAGL
          psllw	mm4, SMAGL
          psllw	mm7, SMAGL
#endif
          pand	mm7, mm3 // 007F
          pand	mm4, mm3 // 007F
          psraw	mm1, 7 - SMAGL
          psraw	mm2, 7 - SMAGL

          movd	mm3, edi
          punpckldq	mm3, mm3
#if SMAGL
          pslld	mm3, SMAGL
#endif
          packssdw	mm3, mm3
          paddsw	mm1, mm3

          movq	mm3, [esp + 30h] // x_limit_max
          movq	mm0, [esp + 28h] // x_limit_min
          pcmpgtw	mm3, mm1
          pcmpgtw	mm0, mm1
          pminsw	mm1, [esp + 30h] // x_limit_max
          pmaxsw	mm1, [esp + 28h] // x_limit_min
          pand	mm7, mm3
          pandn	mm0, mm7

          movq	mm7, mm2
          punpcklwd	mm2, mm1
          punpckhwd	mm7, mm1
          pmaddwd	mm2, mm6 // 1 src_pitch
          pmaddwd	mm7, mm6 // 1 src_pitch

          movd	eax, mm2
          punpckhdq	mm2, mm2
          pinsrw	mm3, [eax + esi], 0
          pinsrw	mm1, [eax + edx], 0
          movd	eax, mm2
          pinsrw	mm3, [eax + esi + 1 * SMAG], 1
          pinsrw	mm1, [eax + edx + 1 * SMAG], 1
          movd	eax, mm7
          punpckhdq	mm7, mm7
          pinsrw	mm3, [eax + esi + 2 * SMAG], 2
          pinsrw	mm1, [eax + edx + 2 * SMAG], 2
          movd	eax, mm7
          pinsrw	mm3, [eax + esi + 3 * SMAG], 3
          pinsrw	mm1, [eax + edx + 3 * SMAG], 3
          mov	eax, [esp + 4]

          pcmpeqw	mm6, mm6
          movq	mm2, mm3
          psrlw	mm6, 8
          movq	mm7, mm1
          pand	mm3, mm6 // 00FF
          pand	mm1, mm6 // 00FF
          movq	mm6, mm5
          psubw	mm6, mm0
          pmullw	mm3, mm6
          pmullw	mm1, mm6
          movq	mm6, mm5 // 0080
          psrlw	mm5, 1
          psrlw	mm2, 8
          psrlw	mm7, 8
          pmullw	mm2, mm0
          pmullw	mm7, mm0
          paddw	mm3, mm2
          paddw	mm1, mm7
          paddw	mm3, mm5 // 0040
          paddw	mm1, mm5 // 0040
          psraw	mm3, 7
          psraw	mm1, 7

          psubw	mm6, mm4
          movq	mm7, [edi + ecx + 3]
          pmullw	mm1, mm4
          pmullw	mm3, mm6
          paddw	mm3, mm1
          movq	mm1, mm7
          movq	mm6, [esp + 10h] // preload depth

          paddw	mm3, mm5 // 0040
          psrlq	mm1, 10h
          psraw	mm3, 7
          paddw	mm5, mm5
          packuswb	mm3, mm3
          movd[edi + eax], mm3
          add	edi, 4
          jnz	lm
          pop	esp
          pop	ecx
          pop	ebp
      }
      psrc += src_pitch*SMAG;
      pedg += edg_pitch;
      pdst += dst_pitch;
    }
  }
#endif
}
#undef MERGE
#undef MERGE2
#undef SMAG
#undef SMAGL
