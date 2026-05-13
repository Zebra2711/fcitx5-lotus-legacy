
.intel_syntax noprefix
.text

.global compare_split_avx512
.type compare_split_avx512, @function

// SysV ABI:
// rdi = A
// rsi = B
// rdx = lenA
// rcx = lenB
// r8  = deletedPart*
/*
NOTE: we DO NOT touch std::string in ASM (ABI unsafe).
We only return split index in rax.
*/

compare_split_avx512:
    xor rax, rax

.Lloop:
    mov r10, rax
    add r10, 64
    cmp r10, rdx
    ja .Ltail

    vmovdqu64 zmm0, ZMMWORD PTR [rdi + rax]
    vmovdqu64 zmm1, ZMMWORD PTR [rsi + rax]

    vpcmpeqb k1, zmm0, zmm1
    kortestq k1, k1
    jnz .Ldiff

    add rax, 64
    jmp .Lloop

.Ldiff:
    knotq k1, k1
    tzcnt r11, k1
    add rax, r11
    ret

.Ltail:
    cmp rax, rdx
    jae .Ldone

.Lt:
    mov r10b, BYTE PTR [rdi + rax]
    cmp r10b, BYTE PTR [rsi + rax]
    jne .Ldone
    inc rax
    cmp rax, rdx
    jb .Lt

.Ldone:
    ret
