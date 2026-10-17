endbr64
push rbp
mov rbp,rsp
push r15
push r14
push r13
push r12
push rbx
and rsp,0xffffffffffffffe0
add rsp,0xffffffffffffff80
mov r12,QWORD PTR [rdi+0x8]
mov r9,QWORD PTR [r12+0x38]
mov rcx,QWORD PTR [r12+0x30]
cmp r9,rcx
je self+0x190
mov r10,QWORD PTR [r12+0x48]
mov r14,QWORD PTR [r12+0x78]
mov r13,rdx
mov rdx,rsi
mov rbx,rsi
mov rax,QWORD PTR [r10+0x8]
cmp r14,rsi
mov rsi,QWORD PTR [r10]
cmovbe rdx,r14
cmp r14,rax
cmovbe rax,r14
cmp rax,rdx
jb self+0x1b8
test rdx,rdx
jne self+0x450
test rax,rax
jne self+0x1e4
cmp rbx,r14
jb self+0x878
mov rax,rbx
sub rax,r14
je self+0x508
movzx edx,BYTE PTR [r13+r14*1+0x0]
mov rsi,rdx
shl rsi,0x8
cmp rax,0x1
je self+0x4fb
movzx edx,BYTE PTR [r14+r13*1+0x1]
or rdx,rsi
shl rdx,0x8
cmp rax,0x2
je self+0x870
movzx esi,BYTE PTR [r14+r13*1+0x2]
or rsi,rdx
shl rsi,0x8
cmp rax,0x3
je self+0x50f
movzx edx,BYTE PTR [r14+r13*1+0x3]
or rdx,rsi
shl rdx,0x8
cmp rax,0x4
je self+0x516
movzx esi,BYTE PTR [r14+r13*1+0x4]
or rsi,rdx
shl rsi,0x8
cmp rax,0x5
je self+0x51d
movzx edx,BYTE PTR [r14+r13*1+0x5]
or rdx,rsi
shl rdx,0x8
cmp rax,0x6
je self+0x524
movzx esi,BYTE PTR [r14+r13*1+0x6]
or rdx,rsi
mov rdi,rdx
shl rdi,0x8
mov QWORD PTR [rsp+0x38],rdi
cmp rax,0x7
je self+0x13b
movzx eax,BYTE PTR [r14+r13*1+0x7]
or rdi,rax
mov QWORD PTR [rsp+0x38],rdi
mov r11,QWORD PTR [r12+0x60]
mov r9,QWORD PTR [r12+0x68]
sub r9,r11
sar r9,0x3
je self+0x1e4
xor r14d,r14d
test rbx,rbx
jne self+0x4de
jmp self+0x4b2
nop DWORD PTR [rax+rax*1+0x0]
mov rdx,r8
mov rdi,r13
mov QWORD PTR [rsp+0x30],rcx
mov QWORD PTR [rsp+0x38],r8
call absolute:46e0
mov r8,QWORD PTR [rsp+0x38]
mov rcx,QWORD PTR [rsp+0x30]
test eax,eax
je self+0x1fa
nop DWORD PTR [rax+rax*1+0x0]
xor eax,eax
mov QWORD PTR [rsp+0x70],r15
mov BYTE PTR [rsp+0x78],al
mov rax,QWORD PTR [rsp+0x70]
mov rdx,QWORD PTR [rsp+0x78]
lea rsp,[rbp-0x28]
pop rbx
pop r12
pop r13
pop r14
pop r15
pop rbp
ret
nop DWORD PTR [rax+0x0]
test rax,rax
je self+0x1e0
mov rdx,rax
mov rdi,r13
mov QWORD PTR [rsp+0x38],r9
mov QWORD PTR [rsp+0x30],rcx
call absolute:46e0
mov r9,QWORD PTR [rsp+0x38]
test eax,eax
jne self+0x538
nop
lea rcx,[r9-0x40]
mov r8,QWORD PTR [rcx+0x10]
cmp rbx,r8
jb self+0x190
mov rsi,QWORD PTR [rcx+0x8]
test r8,r8
jne self+0x168
lea rax,[r13+r8*1+0x0]
mov r14,rbx
mov QWORD PTR [rsp+0x30],rax
sub r14,r8
jne self+0x608
xor eax,eax
shl rax,0x9
mov rsi,rax
mov rax,rsi
shl rax,0x9
shl rax,0x9
mov rsi,rax
mov rax,rsi
shl rax,0x9
shl rax,0x9
mov r9,rax
mov r11d,DWORD PTR [rcx+0x4]
mov r8,QWORD PTR [rcx+0x30]
lea eax,[r11-0x1]
mov DWORD PTR [rsp+0x28],eax
cmp QWORD PTR [r8-0x28],r9
jb self+0x7e0
movabs rax,0xaaaaaaaaaaaaaaab
mov r10,QWORD PTR [rcx+0x28]
xor edi,edi
sub r8,r10
sar r8,0x4
imul r8,rax
nop WORD PTR [rax+rax*1+0x0]
cmp rdi,r8
jae self+0x29d
mov rax,r8
sub rax,rdi
shr rax,1
add rax,rdi
lea rsi,[rax+rax*2]
shl rsi,0x4
cmp QWORD PTR [r10+rsi*1+0x8],r9
jae self+0x7b0
lea rdi,[rax+0x1]
cmp rdi,r8
jb self+0x275
lea rax,[rdi+rdi*2]
vxorps xmm1,xmm1,xmm1
shl rax,0x4
add rax,r10
mov rsi,QWORD PTR [rax]
cmp r9,rsi
jb self+0x7e9
sub r9,rsi
js self+0x81b
vcvtsi2sd xmm0,xmm1,r9
mov esi,DWORD PTR [rax+0x18]
mov edi,DWORD PTR [rax+0x1c]
vcvtsi2sd xmm2,xmm1,rsi
vfmadd132sd xmm0,xmm2,QWORD PTR [rax+0x10]
vcvtsi2sd xmm2,xmm1,rdi
vcomisd xmm0,xmm2
jbe self+0x2fd
mov edi,DWORD PTR [rax+0x20]
vcvtsi2sd xmm1,xmm1,rdi
vcomisd xmm0,xmm1
jae self+0x2fd
vroundsd xmm0,xmm0,xmm0,0x9
vcvttsd2si rdi,xmm0
mov edi,edi
movsxd rsi,DWORD PTR [rax+0x24]
movsxd rax,DWORD PTR [rax+0x28]
mov r8d,0x0
add rsi,rdi
cmovs rsi,r8
add rax,rdi
mov edi,DWORD PTR [rsp+0x28]
cmp rax,rdi
cmovg rax,rdi
mov DWORD PTR [rsp+0x20],eax
cmp eax,esi
jb self+0x893
mov DWORD PTR [rsp+0x28],esi
cmp DWORD PTR [rsp+0x20],r11d
jae self+0x893
cmp r14,0x7
jbe self+0x800
lea r9,[r14-0x8]
mov rbx,QWORD PTR [rsp+0x30]
mov rsi,r9
and rsi,0xfffffffffffffff8
mov rax,rbx
lea r8,[rbx+rsi*1+0x8]
mov esi,0x9e3779b9
nop WORD PTR [rax+rax*1+0x0]
mov edi,esi
add rax,0x8
crc32 rdi,QWORD PTR [rax-0x8]
mov esi,edi
cmp r8,rax
jne self+0x368
mov rax,QWORD PTR [rsp+0x30]
and r9,0xfffffffffffffff8
mov rbx,r14
and ebx,0x7
lea rax,[rax+r9*1+0x8]
add rbx,rax
cmp rbx,rax
je self+0x84d
nop DWORD PTR [rax+0x0]
crc32 esi,BYTE PTR [rax]
add rax,0x1
cmp rbx,rax
jne self+0x3a0
mov eax,DWORD PTR [rcx]
vmovd xmm5,esi
vpbroadcastb xmm5,xmm5
mov DWORD PTR [rsp+0x18],eax
mov rax,QWORD PTR [r12+0x18]
vmovdqa XMMWORD PTR [rsp],xmm5
mov QWORD PTR [rsp+0x10],rax
test r14,r14
jne self+0x713
mov eax,DWORD PTR [rsp+0x18]
mov ecx,DWORD PTR [rsp+0x28]
vmovdqa xmm3,XMMWORD PTR [rsp]
add eax,ecx
mov eax,eax
lea r9,[rax+rax*8]
mov rax,QWORD PTR [rsp+0x10]
shl r9,0x5
lea r13,[rax+r9*1]
vpcmpeqb xmm0,xmm3,XMMWORD PTR [r13+0x0]
mov edx,DWORD PTR [r13+0x10]
vpmovmskb eax,xmm0
bzhi eax,eax,edx
nop WORD PTR [rax+rax*1+0x0]
test eax,eax
je self+0x7c0
xor ebx,ebx
tzcnt ebx,eax
blsr eax,eax
mov rdx,rbx
shl rdx,0x4
mov edx,DWORD PTR [r13+rdx*1+0x1c]
test edx,edx
jne self+0x410
shl rbx,0x4
mov eax,0x1
mov r15,QWORD PTR [r13+rbx*1+0x20]
jmp self+0x192
cs nop WORD PTR [rax+rax*1+0x0]
mov rdi,r13
mov QWORD PTR [rsp+0x30],rcx
mov QWORD PTR [rsp+0x38],r9
mov QWORD PTR [rsp+0x18],rax
mov QWORD PTR [rsp+0x20],r10
mov QWORD PTR [rsp+0x28],rdx
call absolute:46e0
mov r9,QWORD PTR [rsp+0x38]
mov rcx,QWORD PTR [rsp+0x30]
test eax,eax
jne self+0x53d
mov rdx,QWORD PTR [rsp+0x28]
mov r8,QWORD PTR [rsp+0x18]
mov r10,QWORD PTR [rsp+0x20]
cmp r8,rdx
je self+0x73
jmp self+0x1e4
je self+0x570
mov r9,rax
cmp r14,r9
jae self+0x550
mov rax,r9
mov rdi,QWORD PTR [rsp+0x38]
sub rax,r14
shr rax,1
add rax,r14
cmp QWORD PTR [r11+rax*8],rdi
jae self+0x4a0
lea r14,[rax+0x1]
jmp self+0x4a9
nop
je self+0x590
mov r9,r8
cmp r14,r9
jae self+0x550
mov r8,r9
mov rax,QWORD PTR [rsp+0x38]
sub r8,r14
shr r8,1
add r8,r14
cmp QWORD PTR [r11+r8*8],rax
jae self+0x4d0
lea r14,[r8+0x1]
jmp self+0x4d9
mov rax,rdx
shl rax,0x10
nop WORD PTR [rax+rax*1+0x0]
shl rax,0x8
mov rsi,rax
shl rsi,0x8
mov rdx,rsi
mov rsi,rdx
shl rsi,0x8
shl rsi,0x8
mov rdx,rsi
shl rdx,0x8
mov QWORD PTR [rsp+0x38],rdx
jmp self+0x13b
nop WORD PTR [rax+rax*1+0x0]
mov rcx,QWORD PTR [rsp+0x30]
test eax,eax
jns self+0x1e0
jmp self+0x1e4
nop WORD PTR [rax+rax*1+0x0]
test r14,r14
je self+0x1e4
sub r14,0x1
shl r14,0x6
add rcx,r14
jmp self+0x1e4
nop DWORD PTR [rax+0x0]
mov rdx,rax
shl rdx,0x5
cmp QWORD PTR [r10+rdx*1+0x8],0x0
je self+0x4c9
jmp self+0x4a6
nop DWORD PTR [rax+rax*1+0x0]
mov rax,r8
shl rax,0x5
add rax,r10
mov rdx,QWORD PTR [rax+0x8]
mov rdi,QWORD PTR [rax]
cmp rbx,rdx
jb self+0x6b0
test rdx,rdx
je self+0x4f5
mov rsi,r13
mov QWORD PTR [rsp+0x10],r9
mov QWORD PTR [rsp+0x18],rcx
mov QWORD PTR [rsp+0x20],r8
mov QWORD PTR [rsp+0x28],r11
mov QWORD PTR [rsp+0x30],r10
call absolute:46e0
mov r10,QWORD PTR [rsp+0x30]
mov r11,QWORD PTR [rsp+0x28]
test eax,eax
mov r8,QWORD PTR [rsp+0x20]
mov rcx,QWORD PTR [rsp+0x18]
mov r9,QWORD PTR [rsp+0x10]
je self+0x4f5
test eax,eax
jg self+0x4d6
jmp self+0x4f5
nop WORD PTR [rax+rax*1+0x0]
movzx eax,BYTE PTR [rax]
add rax,0x1
shl rax,0x9
cmp r14,0x1
je self+0x212
mov rdi,QWORD PTR [rsp+0x30]
movzx esi,BYTE PTR [rdi+0x1]
add rsi,0x1
or rsi,rax
shl rsi,0x9
cmp r14,0x2
je self+0x219
movzx eax,BYTE PTR [rdi+0x2]
add rax,0x1
or rax,rsi
shl rax,0x9
cmp r14,0x3
je self+0x220
movzx esi,BYTE PTR [rdi+0x3]
add rsi,0x1
or rsi,rax
shl rsi,0x9
cmp r14,0x4
je self+0x227
movzx eax,BYTE PTR [rdi+0x4]
add rax,0x1
or rax,rsi
shl rax,0x9
cmp r14,0x5
je self+0x22e
movzx r9d,BYTE PTR [rdi+0x5]
add r9,0x1
or r9,rax
shl r9,0x9
cmp r14,0x6
je self+0x235
movzx eax,BYTE PTR [rdi+0x6]
add rax,0x1
or r9,rax
jmp self+0x235
mov rdx,rbx
mov rsi,r13
mov QWORD PTR [rsp+0x18],rcx
mov QWORD PTR [rsp+0x20],r8
mov QWORD PTR [rsp+0x28],r11
mov QWORD PTR [rsp+0x30],r10
mov QWORD PTR [rsp+0x10],r9
call absolute:46e0
mov r10,QWORD PTR [rsp+0x30]
mov r11,QWORD PTR [rsp+0x28]
test eax,eax
mov r8,QWORD PTR [rsp+0x20]
mov rcx,QWORD PTR [rsp+0x18]
je self+0x4d6
mov r9,QWORD PTR [rsp+0x10]
jmp self+0x5f5
nop WORD PTR [rax+rax*1+0x0]
add DWORD PTR [rsp+0x28],0x1
mov eax,DWORD PTR [rsp+0x28]
cmp DWORD PTR [rsp+0x20],eax
jb self+0x190
mov eax,DWORD PTR [rsp+0x18]
mov ecx,DWORD PTR [rsp+0x28]
vmovdqa xmm4,XMMWORD PTR [rsp]
add eax,ecx
mov eax,eax
lea r9,[rax+rax*8]
mov rax,QWORD PTR [rsp+0x10]
shl r9,0x5
lea r13,[rax+r9*1]
vpcmpeqb xmm0,xmm4,XMMWORD PTR [r13+0x0]
mov eax,DWORD PTR [r13+0x10]
vpmovmskb r8d,xmm0
bzhi r8d,r8d,eax
nop DWORD PTR [rax+rax*1+0x0]
test r8d,r8d
je self+0x700
xor ebx,ebx
tzcnt ebx,r8d
blsr r8d,r8d
mov rax,rbx
shl rax,0x4
add rax,r13
mov esi,DWORD PTR [rax+0x1c]
cmp rsi,r14
jne self+0x750
mov rsi,QWORD PTR [r12]
lea rdi,[absolute:30498]
cmp QWORD PTR [r12+0x8],rsi
je self+0x78b
mov edi,DWORD PTR [rax+0x18]
add rdi,rsi
mov rsi,QWORD PTR [rsp+0x30]
mov rdx,r14
mov DWORD PTR [rsp+0x38],r8d
call absolute:46e0
mov r8d,DWORD PTR [rsp+0x38]
test eax,eax
jne self+0x750
jmp self+0x433
nop DWORD PTR [rax+rax*1+0x0]
mov r8,rax
jmp self+0x270
nop DWORD PTR [rax+rax*1+0x0]
add DWORD PTR [rsp+0x28],0x1
mov eax,DWORD PTR [rsp+0x28]
cmp DWORD PTR [rsp+0x20],eax
jae self+0x3d5
jmp self+0x190
nop DWORD PTR [rax+rax*1+0x0]
mov DWORD PTR [rsp+0x20],eax
jmp self+0x330
sub rsi,r9
js self+0x836
vcvtsi2sd xmm0,xmm1,rsi
vxorpd xmm0,xmm0,XMMWORD PTR [absolute:31cd0]
jmp self+0x2c6
mov rax,QWORD PTR [rsp+0x30]
add rbx,r13
mov esi,0x9e3779b9
cmp rbx,rax
jne self+0x3a0
jmp self+0x3ae
mov rsi,r9
and r9d,0x1
shr rsi,1
or rsi,r9
vcvtsi2sd xmm0,xmm1,rsi
vaddsd xmm0,xmm0,xmm0
jmp self+0x2c6
mov rdi,rsi
and esi,0x1
shr rdi,1
or rdi,rsi
vcvtsi2sd xmm0,xmm1,rdi
vaddsd xmm0,xmm0,xmm0
jmp self+0x7f3
mov eax,DWORD PTR [rcx]
vmovd xmm6,edi
vpbroadcastb xmm6,xmm6
mov DWORD PTR [rsp+0x18],eax
mov rax,QWORD PTR [r12+0x18]
vmovdqa XMMWORD PTR [rsp],xmm6
mov QWORD PTR [rsp+0x10],rax
jmp self+0x713
mov rax,rdx
jmp self+0x508
mov rcx,rbx
mov rdx,r14
lea rsi,[absolute:301ac]
xor eax,eax
lea rdi,[absolute:30ca0]
call absolute:49c0
mov edi,0x10
call absolute:4700
lea rsi,[absolute:301c6]
mov rdi,rax
mov rbx,rax
call absolute:4990
mov rdx,QWORD PTR [absolute:37fd0]
lea rsi,[absolute:37880]
mov rdi,rbx
call absolute:4a50
endbr64
mov r12,rax
mov rdi,rbx
vzeroupper
call absolute:47b0
mov rdi,r12
call absolute:4a90
