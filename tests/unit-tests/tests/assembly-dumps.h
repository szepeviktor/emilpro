#pragma once

#include <stdint.h>

// IA32 code (from coincident unit test)
static const uint8_t ia32_dump[] =
{
		0x76, 0x03,                         //     0 jbe    1f
		0x89, 0x1c, 0x24,                   //     2 mov    %ebx,(%esp)
		0x83, 0xe0, 0x0f,                   // 1:  5 and    $0xf,%eax
		0x8d, 0x50, 0x01,                   //     8 lea    0x1(%eax),%edx
		0xd1, 0xea,                         //    11 shr    %edx
		0x88, 0x55, 0xcc,                   //    13 mov    %dl,-0x34(%ebp)
		0xe8, 0x07, 0x00, 0x00, 0x00,       //    16 call   28
		0x8b, 0x83, 0x58, 0x02, 0x00, 0x00, //    21 mov    0x258(%ebx),%eax
		0xf4,                               //    27 hlt
		0xcc,                               //    28 int3
		0xa1, 0x80, 0x00, 0x00, 0x00,       //    29 mov    0x80,%eax
};

// From sample-source.c
static const uint8_t ppc32_dump[] =
{
		0x2f, 0x83, 0x00, 0x05, // cmpwi   cr7,r3,5
		0x7c, 0x84, 0x1a, 0x14, // add     r4,r4,r3
		0x41, 0x9d, 0x00, 0x08, // bgt-    cr7,10 <fn+0x10>
		0x7c, 0x64, 0x1b, 0x78, // mr      r4,r3
		0x7c, 0x83, 0x23, 0x78, // mr      r3,r4
		0x4e, 0x80, 0x00, 0x20, // blr

		0x38, 0x80, 0x00, 0x03, // li      r4,3
		0x48, 0x00, 0x00, 0x00, // b       1c <second+0x4>
};

static const uint8_t arm_dump[] =
{
		0x04, 0xe0, 0x2d, 0xe5, // push    {lr}        ; (str lr, [sp, #-4]!)
		0x01, 0x00, 0x50, 0xe1, // cmp     r0, r1
		0x04, 0xd0, 0x4d, 0xe2, // sub     sp, sp, #4
		0x00, 0x00, 0xa0, 0xd1, // movle   r0, r0
		0x00, 0x00, 0x00, 0xda, // ble     18 <fn+0x18>
		0xfe, 0xff, 0xff, 0xeb, // bl      0 <__aeabi_idiv>
		0x04, 0xd0, 0x8d, 0xe2, // add     sp, sp, #4
		0x00, 0x80, 0xbd, 0xe8, // pop     {pc}

		0x03, 0x10, 0xa0, 0xe3, // mov     r1, #3
		0xfe, 0xff, 0xff, 0xea, // b       0 <fn>
};

static const uint8_t mips_dump[] =
{
		0x00, 0xa4, 0x10, 0x2a, // slt    v0,a1,a0
		0x10, 0x40, 0x00, 0x05, // beqz    v0,1c <fn+0x1c>
		0x00, 0x00, 0x00, 0x00, // nop
		0x00, 0x85, 0x00, 0x1a, // div    zero,a0,a1
		0x00, 0x00, 0x10, 0x12, // mflo    v0
		0x03, 0xe0, 0x00, 0x08, // jr    ra
		0x00, 0x00, 0x00, 0x00, // nop
		0x03, 0xe0, 0x00, 0x08, // jr    ra
		0x00, 0x80, 0x10, 0x21, // move    v0,a0

		0x27, 0xbd, 0xff, 0xe8, // addiu    sp,sp,-24
		0xaf, 0xbf, 0x00, 0x14, // sw    ra,20(sp)
		0x0c, 0x00, 0x00, 0x00, // jal    0 <fn>
		0x24, 0x05, 0x00, 0x03, // li    a1,3
		0x8f, 0xbf, 0x00, 0x14, // lw    ra,20(sp)
		0x00, 0x00, 0x00, 0x00, // nop
		0x03, 0xe0, 0x00, 0x08, // jr    ra
		0x27, 0xbd, 0x00, 0x18, // addiu    sp,sp,24
};

