
/mnt/d/StringLearnedIndex_Workspace/07_Eval_Integration/results/static_interface_20261012c/build/bench:     file format elf64-x86-64


Disassembly of section .text:

0000000000025d60 <_ZNK12_GLOBAL__N_17AdapterILN15static_physical6LookupE0EE4findESt17basic_string_viewIcSt11char_traitsIcEE>:
   25d60:	endbr64
   25d64:	push   rbp
   25d65:	mov    rbp,rsp
   25d68:	push   r15
   25d6a:	push   r14
   25d6c:	push   r13
   25d6e:	push   r12
   25d70:	push   rbx
   25d71:	and    rsp,0xffffffffffffffe0
   25d75:	add    rsp,0xffffffffffffff80
   25d79:	mov    rbx,QWORD PTR [rdi+0x8]
   25d7d:	test   rbx,rbx
   25d80:	je     a86e <_ZNK12_GLOBAL__N_17AdapterILN15static_physical6LookupE0EE4findESt17basic_string_viewIcSt11char_traitsIcEE.cold+0x32>
   25d86:	mov    r9,QWORD PTR [rbx+0x38]
   25d8a:	mov    rcx,QWORD PTR [rbx+0x30]
   25d8e:	cmp    r9,rcx
   25d91:	je     25ee8 <_ZNK12_GLOBAL__N_17AdapterILN15static_physical6LookupE0EE4findESt17basic_string_viewIcSt11char_traitsIcEE+0x188>
   25d97:	mov    r10,QWORD PTR [rbx+0x48]
   25d9b:	mov    r14,QWORD PTR [rbx+0x78]
   25d9f:	mov    r13,rdx
   25da2:	mov    rdx,rsi
   25da5:	mov    r12,rsi
   25da8:	mov    rax,QWORD PTR [r10+0x8]
   25dac:	cmp    r14,rsi
   25daf:	mov    rsi,QWORD PTR [r10]
   25db2:	cmovbe rdx,r14
   25db6:	cmp    r14,rax
   25db9:	cmovbe rax,r14
   25dbd:	cmp    rax,rdx
   25dc0:	jb     25f10 <_ZNK12_GLOBAL__N_17AdapterILN15static_physical6LookupE0EE4findESt17basic_string_viewIcSt11char_traitsIcEE+0x1b0>
   25dc6:	test   rdx,rdx
   25dc9:	jne    261b0 <_ZNK12_GLOBAL__N_17AdapterILN15static_physical6LookupE0EE4findESt17basic_string_viewIcSt11char_traitsIcEE+0x450>
   25dcf:	test   rax,rax
   25dd2:	jne    25f44 <_ZNK12_GLOBAL__N_17AdapterILN15static_physical6LookupE0EE4findESt17basic_string_viewIcSt11char_traitsIcEE+0x1e4>
   25dd8:	cmp    r12,r14
   25ddb:	jb     265e7 <_ZNK12_GLOBAL__N_17AdapterILN15static_physical6LookupE0EE4findESt17basic_string_viewIcSt11char_traitsIcEE+0x887>
   25de1:	mov    rax,r12
   25de4:	sub    rax,r14
   25de7:	je     26270 <_ZNK12_GLOBAL__N_17AdapterILN15static_physical6LookupE0EE4findESt17basic_string_viewIcSt11char_traitsIcEE+0x510>
   25ded:	movzx  edx,BYTE PTR [r13+r14*1+0x0]
   25df3:	mov    rsi,rdx
   25df6:	shl    rsi,0x8
   25dfa:	cmp    rax,0x1
   25dfe:	je     26269 <_ZNK12_GLOBAL__N_17AdapterILN15static_physical6LookupE0EE4findESt17basic_string_viewIcSt11char_traitsIcEE+0x509>
   25e04:	movzx  edx,BYTE PTR [r14+r13*1+0x1]
   25e0a:	or     rdx,rsi
   25e0d:	shl    rdx,0x8
   25e11:	cmp    rax,0x2
   25e15:	je     265df <_ZNK12_GLOBAL__N_17AdapterILN15static_physical6LookupE0EE4findESt17basic_string_viewIcSt11char_traitsIcEE+0x87f>
   25e1b:	movzx  esi,BYTE PTR [r14+r13*1+0x2]
   25e21:	or     rsi,rdx
   25e24:	shl    rsi,0x8
   25e28:	cmp    rax,0x3
   25e2c:	je     26277 <_ZNK12_GLOBAL__N_17AdapterILN15static_physical6LookupE0EE4findESt17basic_string_viewIcSt11char_traitsIcEE+0x517>
   25e32:	movzx  edx,BYTE PTR [r14+r13*1+0x3]
   25e38:	or     rdx,rsi
   25e3b:	shl    rdx,0x8
   25e3f:	cmp    rax,0x4
   25e43:	je     2627e <_ZNK12_GLOBAL__N_17AdapterILN15static_physical6LookupE0EE4findESt17basic_string_viewIcSt11char_traitsIcEE+0x51e>
   25e49:	movzx  esi,BYTE PTR [r14+r13*1+0x4]
   25e4f:	or     rsi,rdx
   25e52:	shl    rsi,0x8
   25e56:	cmp    rax,0x5
   25e5a:	je     26285 <_ZNK12_GLOBAL__N_17AdapterILN15static_physical6LookupE0EE4findESt17basic_string_viewIcSt11char_traitsIcEE+0x525>
   25e60:	movzx  edx,BYTE PTR [r14+r13*1+0x5]
   25e66:	or     rdx,rsi
   25e69:	shl    rdx,0x8
   25e6d:	cmp    rax,0x6
   25e71:	je     2628c <_ZNK12_GLOBAL__N_17AdapterILN15static_physical6LookupE0EE4findESt17basic_string_viewIcSt11char_traitsIcEE+0x52c>
   25e77:	movzx  r11d,BYTE PTR [r14+r13*1+0x6]
   25e7d:	or     r11,rdx
   25e80:	shl    r11,0x8
   25e84:	cmp    rax,0x7
   25e88:	je     25e93 <_ZNK12_GLOBAL__N_17AdapterILN15static_physical6LookupE0EE4findESt17basic_string_viewIcSt11char_traitsIcEE+0x133>
   25e8a:	movzx  eax,BYTE PTR [r14+r13*1+0x7]
   25e90:	or     r11,rax
   25e93:	mov    rax,QWORD PTR [rbx+0x60]
   25e97:	mov    r9,QWORD PTR [rbx+0x68]
   25e9b:	sub    r9,rax
   25e9e:	mov    QWORD PTR [rsp+0x38],rax
   25ea3:	sar    r9,0x3
   25ea7:	je     25f44 <_ZNK12_GLOBAL__N_17AdapterILN15static_physical6LookupE0EE4findESt17basic_string_viewIcSt11char_traitsIcEE+0x1e4>
   25ead:	xor    r14d,r14d
   25eb0:	test   r12,r12
   25eb3:	jne    26249 <_ZNK12_GLOBAL__N_17AdapterILN15static_physical6LookupE0EE4findESt17basic_string_viewIcSt11char_traitsIcEE+0x4e9>
   25eb9:	jmp    26215 <_ZNK12_GLOBAL__N_17AdapterILN15static_physical6LookupE0EE4findESt17basic_string_viewIcSt11char_traitsIcEE+0x4b5>
   25ebe:	xchg   ax,ax
   25ec0:	mov    rdx,r8
   25ec3:	mov    rdi,r13
   25ec6:	mov    QWORD PTR [rsp+0x30],rcx
   25ecb:	mov    QWORD PTR [rsp+0x38],r8
   25ed0:	call   47e0 <memcmp@plt>
   25ed5:	mov    r8,QWORD PTR [rsp+0x38]
   25eda:	mov    rcx,QWORD PTR [rsp+0x30]
   25edf:	test   eax,eax
   25ee1:	je     25f5a <_ZNK12_GLOBAL__N_17AdapterILN15static_physical6LookupE0EE4findESt17basic_string_viewIcSt11char_traitsIcEE+0x1fa>
   25ee3:	nop    DWORD PTR [rax+rax*1+0x0]
   25ee8:	xor    eax,eax
   25eea:	mov    QWORD PTR [rsp+0x70],r15
   25eef:	mov    BYTE PTR [rsp+0x78],al
   25ef3:	mov    rax,QWORD PTR [rsp+0x70]
   25ef8:	mov    rdx,QWORD PTR [rsp+0x78]
   25efd:	lea    rsp,[rbp-0x28]
   25f01:	pop    rbx
   25f02:	pop    r12
   25f04:	pop    r13
   25f06:	pop    r14
   25f08:	pop    r15
   25f0a:	pop    rbp
   25f0b:	ret
   25f0c:	nop    DWORD PTR [rax+0x0]
   25f10:	test   rax,rax
   25f13:	je     25f40 <_ZNK12_GLOBAL__N_17AdapterILN15static_physical6LookupE0EE4findESt17basic_string_viewIcSt11char_traitsIcEE+0x1e0>
   25f15:	mov    rdx,rax
   25f18:	mov    rdi,r13
   25f1b:	mov    QWORD PTR [rsp+0x38],r9
   25f20:	mov    QWORD PTR [rsp+0x30],rcx
   25f25:	call   47e0 <memcmp@plt>
   25f2a:	mov    r9,QWORD PTR [rsp+0x38]
   25f2f:	test   eax,eax
   25f31:	jne    262a0 <_ZNK12_GLOBAL__N_17AdapterILN15static_physical6LookupE0EE4findESt17basic_string_viewIcSt11char_traitsIcEE+0x540>
   25f37:	nop    WORD PTR [rax+rax*1+0x0]
   25f40:	lea    rcx,[r9-0x40]
   25f44:	mov    r8,QWORD PTR [rcx+0x10]
   25f48:	cmp    r12,r8
   25f4b:	jb     25ee8 <_ZNK12_GLOBAL__N_17AdapterILN15static_physical6LookupE0EE4findESt17basic_string_viewIcSt11char_traitsIcEE+0x188>
   25f4d:	mov    rsi,QWORD PTR [rcx+0x8]
   25f51:	test   r8,r8
   25f54:	jne    25ec0 <_ZNK12_GLOBAL__N_17AdapterILN15static_physical6LookupE0EE4findESt17basic_string_viewIcSt11char_traitsIcEE+0x160>
   25f5a:	lea    rax,[r13+r8*1+0x0]
   25f5f:	mov    r14,r12
   25f62:	mov    QWORD PTR [rsp+0x30],rax
   25f67:	sub    r14,r8
   25f6a:	jne    26370 <_ZNK12_GLOBAL__N_17AdapterILN15static_physical6LookupE0EE4findESt17basic_string_viewIcSt11char_traitsIcEE+0x610>
   25f70:	xor    eax,eax
   25f72:	shl    rax,0x9
   25f76:	mov    rsi,rax
   25f79:	mov    rax,rsi
   25f7c:	shl    rax,0x9
   25f80:	shl    rax,0x9
   25f84:	mov    rsi,rax
   25f87:	mov    rax,rsi
   25f8a:	shl    rax,0x9
   25f8e:	shl    rax,0x9
   25f92:	mov    r9,rax
   25f95:	mov    r11d,DWORD PTR [rcx+0x4]
   25f99:	mov    r8,QWORD PTR [rcx+0x30]
   25f9d:	lea    eax,[r11-0x1]
   25fa1:	mov    DWORD PTR [rsp+0x28],eax
   25fa5:	cmp    QWORD PTR [r8-0x28],r9
   25fa9:	jb     26550 <_ZNK12_GLOBAL__N_17AdapterILN15static_physical6LookupE0EE4findESt17basic_string_viewIcSt11char_traitsIcEE+0x7f0>
   25faf:	movabs rax,0xaaaaaaaaaaaaaaab
   25fb9:	mov    r10,QWORD PTR [rcx+0x28]
   25fbd:	xor    edi,edi
   25fbf:	sub    r8,r10
   25fc2:	sar    r8,0x4
   25fc6:	imul   r8,rax
   25fca:	nop    WORD PTR [rax+rax*1+0x0]
   25fd0:	cmp    rdi,r8
   25fd3:	jae    25ffd <_ZNK12_GLOBAL__N_17AdapterILN15static_physical6LookupE0EE4findESt17basic_string_viewIcSt11char_traitsIcEE+0x29d>
   25fd5:	mov    rax,r8
   25fd8:	sub    rax,rdi
   25fdb:	shr    rax,1
   25fde:	add    rax,rdi
   25fe1:	lea    rsi,[rax+rax*2]
   25fe5:	shl    rsi,0x4
   25fe9:	cmp    QWORD PTR [r10+rsi*1+0x8],r9
   25fee:	jae    26520 <_ZNK12_GLOBAL__N_17AdapterILN15static_physical6LookupE0EE4findESt17basic_string_viewIcSt11char_traitsIcEE+0x7c0>
   25ff4:	lea    rdi,[rax+0x1]
   25ff8:	cmp    rdi,r8
   25ffb:	jb     25fd5 <_ZNK12_GLOBAL__N_17AdapterILN15static_physical6LookupE0EE4findESt17basic_string_viewIcSt11char_traitsIcEE+0x275>
   25ffd:	lea    rax,[rdi+rdi*2]
   26001:	vxorps xmm1,xmm1,xmm1
   26005:	shl    rax,0x4
   26009:	add    rax,r10
   2600c:	mov    rsi,QWORD PTR [rax]
   2600f:	cmp    r9,rsi
   26012:	jb     26559 <_ZNK12_GLOBAL__N_17AdapterILN15static_physical6LookupE0EE4findESt17basic_string_viewIcSt11char_traitsIcEE+0x7f9>
   26018:	sub    r9,rsi
   2601b:	js     2658b <_ZNK12_GLOBAL__N_17AdapterILN15static_physical6LookupE0EE4findESt17basic_string_viewIcSt11char_traitsIcEE+0x82b>
   26021:	vcvtsi2sd xmm0,xmm1,r9
   26026:	mov    esi,DWORD PTR [rax+0x18]
   26029:	mov    edi,DWORD PTR [rax+0x1c]
   2602c:	vcvtsi2sd xmm2,xmm1,rsi
   26031:	vfmadd132sd xmm0,xmm2,QWORD PTR [rax+0x10]
   26037:	vcvtsi2sd xmm2,xmm1,rdi
   2603c:	vcomisd xmm0,xmm2
   26040:	jbe    2605d <_ZNK12_GLOBAL__N_17AdapterILN15static_physical6LookupE0EE4findESt17basic_string_viewIcSt11char_traitsIcEE+0x2fd>
   26042:	mov    edi,DWORD PTR [rax+0x20]
   26045:	vcvtsi2sd xmm1,xmm1,rdi
   2604a:	vcomisd xmm0,xmm1
   2604e:	jae    2605d <_ZNK12_GLOBAL__N_17AdapterILN15static_physical6LookupE0EE4findESt17basic_string_viewIcSt11char_traitsIcEE+0x2fd>
   26050:	vroundsd xmm0,xmm0,xmm0,0x9
   26056:	vcvttsd2si rdi,xmm0
   2605b:	mov    edi,edi
   2605d:	movsxd rsi,DWORD PTR [rax+0x24]
   26061:	movsxd rax,DWORD PTR [rax+0x28]
   26065:	mov    r8d,0x0
   2606b:	add    rsi,rdi
   2606e:	cmovs  rsi,r8
   26072:	add    rax,rdi
   26075:	mov    edi,DWORD PTR [rsp+0x28]
   26079:	cmp    rax,rdi
   2607c:	cmovg  rax,rdi
   26080:	mov    DWORD PTR [rsp+0x20],eax
   26084:	cmp    eax,esi
   26086:	jb     a83c <_ZNK12_GLOBAL__N_17AdapterILN15static_physical6LookupE0EE4findESt17basic_string_viewIcSt11char_traitsIcEE.cold>
   2608c:	mov    DWORD PTR [rsp+0x28],esi
   26090:	cmp    DWORD PTR [rsp+0x20],r11d
   26095:	jae    a83c <_ZNK12_GLOBAL__N_17AdapterILN15static_physical6LookupE0EE4findESt17basic_string_viewIcSt11char_traitsIcEE.cold>
   2609b:	cmp    r14,0x7
   2609f:	jbe    26570 <_ZNK12_GLOBAL__N_17AdapterILN15static_physical6LookupE0EE4findESt17basic_string_viewIcSt11char_traitsIcEE+0x810>
   260a5:	lea    r9,[r14-0x8]
   260a9:	mov    rdi,QWORD PTR [rsp+0x30]
   260ae:	mov    rsi,r9
   260b1:	and    rsi,0xfffffffffffffff8
   260b5:	mov    rax,rdi
   260b8:	lea    r8,[rdi+rsi*1+0x8]
   260bd:	mov    esi,0x9e3779b9
   260c2:	nop    WORD PTR [rax+rax*1+0x0]
   260c8:	mov    edi,esi
   260ca:	add    rax,0x8
   260ce:	crc32  rdi,QWORD PTR [rax-0x8]
   260d5:	mov    esi,edi
   260d7:	cmp    r8,rax
   260da:	jne    260c8 <_ZNK12_GLOBAL__N_17AdapterILN15static_physical6LookupE0EE4findESt17basic_string_viewIcSt11char_traitsIcEE+0x368>
   260dc:	mov    rax,QWORD PTR [rsp+0x30]
   260e1:	and    r9,0xfffffffffffffff8
   260e5:	mov    r12,r14
   260e8:	and    r12d,0x7
   260ec:	lea    rax,[rax+r9*1+0x8]
   260f1:	add    r12,rax
   260f4:	cmp    r12,rax
   260f7:	je     265bd <_ZNK12_GLOBAL__N_17AdapterILN15static_physical6LookupE0EE4findESt17basic_string_viewIcSt11char_traitsIcEE+0x85d>
   260fd:	nop    DWORD PTR [rax]
   26100:	crc32  esi,BYTE PTR [rax]
   26105:	add    rax,0x1
   26109:	cmp    r12,rax
   2610c:	jne    26100 <_ZNK12_GLOBAL__N_17AdapterILN15static_physical6LookupE0EE4findESt17basic_string_viewIcSt11char_traitsIcEE+0x3a0>
   2610e:	mov    eax,DWORD PTR [rcx]
   26110:	vmovd  xmm5,esi
   26114:	vpbroadcastb xmm5,xmm5
   26119:	mov    DWORD PTR [rsp+0x18],eax
   2611d:	mov    rax,QWORD PTR [rbx+0x18]
   26121:	vmovdqa XMMWORD PTR [rsp],xmm5
   26126:	mov    QWORD PTR [rsp+0x10],rax
   2612b:	test   r14,r14
   2612e:	jne    26483 <_ZNK12_GLOBAL__N_17AdapterILN15static_physical6LookupE0EE4findESt17basic_string_viewIcSt11char_traitsIcEE+0x723>
   26134:	mov    eax,DWORD PTR [rsp+0x18]
   26138:	mov    ebx,DWORD PTR [rsp+0x28]
   2613c:	vmovdqa xmm3,XMMWORD PTR [rsp]
   26141:	add    eax,ebx
   26143:	mov    eax,eax
   26145:	lea    r9,[rax+rax*8]
   26149:	mov    rax,QWORD PTR [rsp+0x10]
   2614e:	shl    r9,0x5
   26152:	lea    r13,[rax+r9*1]
   26156:	vpcmpeqb xmm0,xmm3,XMMWORD PTR [r13+0x0]
   2615c:	mov    edx,DWORD PTR [r13+0x10]
   26160:	vpmovmskb eax,xmm0
   26164:	bzhi   eax,eax,edx
   26169:	nop    DWORD PTR [rax+0x0]
   26170:	test   eax,eax
   26172:	je     26530 <_ZNK12_GLOBAL__N_17AdapterILN15static_physical6LookupE0EE4findESt17basic_string_viewIcSt11char_traitsIcEE+0x7d0>
   26178:	xor    r12d,r12d
   2617b:	tzcnt  r12d,eax
   26180:	blsr   eax,eax
   26185:	mov    rdx,r12
   26188:	shl    rdx,0x4
   2618c:	mov    edx,DWORD PTR [r13+rdx*1+0x1c]
   26191:	test   edx,edx
   26193:	jne    26170 <_ZNK12_GLOBAL__N_17AdapterILN15static_physical6LookupE0EE4findESt17basic_string_viewIcSt11char_traitsIcEE+0x410>
   26195:	mov    rcx,r12
   26198:	mov    eax,0x1
   2619d:	shl    rcx,0x4
   261a1:	mov    r15,QWORD PTR [r13+rcx*1+0x20]
   261a6:	jmp    25eea <_ZNK12_GLOBAL__N_17AdapterILN15static_physical6LookupE0EE4findESt17basic_string_viewIcSt11char_traitsIcEE+0x18a>
   261ab:	nop    DWORD PTR [rax+rax*1+0x0]
   261b0:	mov    rdi,r13
   261b3:	mov    QWORD PTR [rsp+0x30],rcx
   261b8:	mov    QWORD PTR [rsp+0x38],r9
   261bd:	mov    QWORD PTR [rsp+0x18],rax
   261c2:	mov    QWORD PTR [rsp+0x20],r10
   261c7:	mov    QWORD PTR [rsp+0x28],rdx
   261cc:	call   47e0 <memcmp@plt>
   261d1:	mov    r9,QWORD PTR [rsp+0x38]
   261d6:	mov    rcx,QWORD PTR [rsp+0x30]
   261db:	test   eax,eax
   261dd:	jne    262a5 <_ZNK12_GLOBAL__N_17AdapterILN15static_physical6LookupE0EE4findESt17basic_string_viewIcSt11char_traitsIcEE+0x545>
   261e3:	mov    rdx,QWORD PTR [rsp+0x28]
   261e8:	mov    r8,QWORD PTR [rsp+0x18]
   261ed:	mov    r10,QWORD PTR [rsp+0x20]
   261f2:	cmp    r8,rdx
   261f5:	je     25dd8 <_ZNK12_GLOBAL__N_17AdapterILN15static_physical6LookupE0EE4findESt17basic_string_viewIcSt11char_traitsIcEE+0x78>
   261fb:	jmp    25f44 <_ZNK12_GLOBAL__N_17AdapterILN15static_physical6LookupE0EE4findESt17basic_string_viewIcSt11char_traitsIcEE+0x1e4>
   26200:	cmp    r11,rdx
   26203:	je     26350 <_ZNK12_GLOBAL__N_17AdapterILN15static_physical6LookupE0EE4findESt17basic_string_viewIcSt11char_traitsIcEE+0x5f0>
   26209:	mov    r9,rax
   2620c:	cmp    r14,r9
   2620f:	jae    262b8 <_ZNK12_GLOBAL__N_17AdapterILN15static_physical6LookupE0EE4findESt17basic_string_viewIcSt11char_traitsIcEE+0x558>
   26215:	mov    rax,r9
   26218:	mov    rdi,QWORD PTR [rsp+0x38]
   2621d:	sub    rax,r14
   26220:	shr    rax,1
   26223:	add    rax,r14
   26226:	mov    rdx,QWORD PTR [rdi+rax*8]
   2622a:	cmp    rdx,r11
   2622d:	jae    26200 <_ZNK12_GLOBAL__N_17AdapterILN15static_physical6LookupE0EE4findESt17basic_string_viewIcSt11char_traitsIcEE+0x4a0>
   2622f:	lea    r14,[rax+0x1]
   26233:	jmp    2620c <_ZNK12_GLOBAL__N_17AdapterILN15static_physical6LookupE0EE4findESt17basic_string_viewIcSt11char_traitsIcEE+0x4ac>
   26235:	nop    DWORD PTR [rax]
   26238:	cmp    r11,rax
   2623b:	je     262d8 <_ZNK12_GLOBAL__N_17AdapterILN15static_physical6LookupE0EE4findESt17basic_string_viewIcSt11char_traitsIcEE+0x578>
   26241:	mov    r9,r8
   26244:	cmp    r14,r9
   26247:	jae    262b8 <_ZNK12_GLOBAL__N_17AdapterILN15static_physical6LookupE0EE4findESt17basic_string_viewIcSt11char_traitsIcEE+0x558>
   26249:	mov    r8,r9
   2624c:	mov    rax,QWORD PTR [rsp+0x38]
   26251:	sub    r8,r14
   26254:	shr    r8,1
   26257:	add    r8,r14
   2625a:	mov    rax,QWORD PTR [rax+r8*8]
   2625e:	cmp    rax,r11
   26261:	jae    26238 <_ZNK12_GLOBAL__N_17AdapterILN15static_physical6LookupE0EE4findESt17basic_string_viewIcSt11char_traitsIcEE+0x4d8>
   26263:	lea    r14,[r8+0x1]
   26267:	jmp    26244 <_ZNK12_GLOBAL__N_17AdapterILN15static_physical6LookupE0EE4findESt17basic_string_viewIcSt11char_traitsIcEE+0x4e4>
   26269:	mov    rax,rdx
   2626c:	shl    rax,0x10
   26270:	shl    rax,0x8
   26274:	mov    rsi,rax
   26277:	shl    rsi,0x8
   2627b:	mov    rdx,rsi
   2627e:	mov    rsi,rdx
   26281:	shl    rsi,0x8
   26285:	shl    rsi,0x8
   26289:	mov    rdx,rsi
   2628c:	shl    rdx,0x8
   26290:	mov    r11,rdx
   26293:	jmp    25e93 <_ZNK12_GLOBAL__N_17AdapterILN15static_physical6LookupE0EE4findESt17basic_string_viewIcSt11char_traitsIcEE+0x133>
   26298:	nop    DWORD PTR [rax+rax*1+0x0]
   262a0:	mov    rcx,QWORD PTR [rsp+0x30]
   262a5:	test   eax,eax
   262a7:	jns    25f40 <_ZNK12_GLOBAL__N_17AdapterILN15static_physical6LookupE0EE4findESt17basic_string_viewIcSt11char_traitsIcEE+0x1e0>
   262ad:	jmp    25f44 <_ZNK12_GLOBAL__N_17AdapterILN15static_physical6LookupE0EE4findESt17basic_string_viewIcSt11char_traitsIcEE+0x1e4>
   262b2:	nop    WORD PTR [rax+rax*1+0x0]
   262b8:	test   r14,r14
   262bb:	je     25f44 <_ZNK12_GLOBAL__N_17AdapterILN15static_physical6LookupE0EE4findESt17basic_string_viewIcSt11char_traitsIcEE+0x1e4>
   262c1:	sub    r14,0x1
   262c5:	shl    r14,0x6
   262c9:	add    rcx,r14
   262cc:	jmp    25f44 <_ZNK12_GLOBAL__N_17AdapterILN15static_physical6LookupE0EE4findESt17basic_string_viewIcSt11char_traitsIcEE+0x1e4>
   262d1:	nop    DWORD PTR [rax+0x0]
   262d8:	mov    rax,r8
   262db:	shl    rax,0x5
   262df:	add    rax,r10
   262e2:	mov    rdx,QWORD PTR [rax+0x8]
   262e6:	mov    rdi,QWORD PTR [rax]
   262e9:	cmp    r12,rdx
   262ec:	jb     26420 <_ZNK12_GLOBAL__N_17AdapterILN15static_physical6LookupE0EE4findESt17basic_string_viewIcSt11char_traitsIcEE+0x6c0>
   262f2:	test   rdx,rdx
   262f5:	je     26263 <_ZNK12_GLOBAL__N_17AdapterILN15static_physical6LookupE0EE4findESt17basic_string_viewIcSt11char_traitsIcEE+0x503>
   262fb:	mov    rsi,r13
   262fe:	mov    QWORD PTR [rsp+0x10],r9
   26303:	mov    QWORD PTR [rsp+0x18],rcx
   26308:	mov    QWORD PTR [rsp+0x20],r8
   2630d:	mov    QWORD PTR [rsp+0x28],r10
   26312:	mov    QWORD PTR [rsp+0x30],r11
   26317:	call   47e0 <memcmp@plt>
   2631c:	mov    r11,QWORD PTR [rsp+0x30]
   26321:	mov    r10,QWORD PTR [rsp+0x28]
   26326:	test   eax,eax
   26328:	mov    r8,QWORD PTR [rsp+0x20]
   2632d:	mov    rcx,QWORD PTR [rsp+0x18]
   26332:	mov    r9,QWORD PTR [rsp+0x10]
   26337:	je     26263 <_ZNK12_GLOBAL__N_17AdapterILN15static_physical6LookupE0EE4findESt17basic_string_viewIcSt11char_traitsIcEE+0x503>
   2633d:	test   eax,eax
   2633f:	jg     26241 <_ZNK12_GLOBAL__N_17AdapterILN15static_physical6LookupE0EE4findESt17basic_string_viewIcSt11char_traitsIcEE+0x4e1>
   26345:	jmp    26263 <_ZNK12_GLOBAL__N_17AdapterILN15static_physical6LookupE0EE4findESt17basic_string_viewIcSt11char_traitsIcEE+0x503>
   2634a:	nop    WORD PTR [rax+rax*1+0x0]
   26350:	mov    rdx,rax
   26353:	shl    rdx,0x5
   26357:	cmp    QWORD PTR [r10+rdx*1+0x8],0x0
   2635d:	je     2622f <_ZNK12_GLOBAL__N_17AdapterILN15static_physical6LookupE0EE4findESt17basic_string_viewIcSt11char_traitsIcEE+0x4cf>
   26363:	jmp    26209 <_ZNK12_GLOBAL__N_17AdapterILN15static_physical6LookupE0EE4findESt17basic_string_viewIcSt11char_traitsIcEE+0x4a9>
   26368:	nop    DWORD PTR [rax+rax*1+0x0]
   26370:	movzx  eax,BYTE PTR [rax]
   26373:	add    rax,0x1
   26377:	shl    rax,0x9
   2637b:	cmp    r14,0x1
   2637f:	je     25f72 <_ZNK12_GLOBAL__N_17AdapterILN15static_physical6LookupE0EE4findESt17basic_string_viewIcSt11char_traitsIcEE+0x212>
   26385:	mov    rdi,QWORD PTR [rsp+0x30]
   2638a:	movzx  esi,BYTE PTR [rdi+0x1]
   2638e:	add    rsi,0x1
   26392:	or     rsi,rax
   26395:	shl    rsi,0x9
   26399:	cmp    r14,0x2
   2639d:	je     25f79 <_ZNK12_GLOBAL__N_17AdapterILN15static_physical6LookupE0EE4findESt17basic_string_viewIcSt11char_traitsIcEE+0x219>
   263a3:	movzx  eax,BYTE PTR [rdi+0x2]
   263a7:	add    rax,0x1
   263ab:	or     rax,rsi
   263ae:	shl    rax,0x9
   263b2:	cmp    r14,0x3
   263b6:	je     25f80 <_ZNK12_GLOBAL__N_17AdapterILN15static_physical6LookupE0EE4findESt17basic_string_viewIcSt11char_traitsIcEE+0x220>
   263bc:	movzx  esi,BYTE PTR [rdi+0x3]
   263c0:	add    rsi,0x1
   263c4:	or     rsi,rax
   263c7:	shl    rsi,0x9
   263cb:	cmp    r14,0x4
   263cf:	je     25f87 <_ZNK12_GLOBAL__N_17AdapterILN15static_physical6LookupE0EE4findESt17basic_string_viewIcSt11char_traitsIcEE+0x227>
   263d5:	movzx  eax,BYTE PTR [rdi+0x4]
   263d9:	add    rax,0x1
   263dd:	or     rax,rsi
   263e0:	shl    rax,0x9
   263e4:	cmp    r14,0x5
   263e8:	je     25f8e <_ZNK12_GLOBAL__N_17AdapterILN15static_physical6LookupE0EE4findESt17basic_string_viewIcSt11char_traitsIcEE+0x22e>
   263ee:	movzx  r9d,BYTE PTR [rdi+0x5]
   263f3:	add    r9,0x1
   263f7:	or     r9,rax
   263fa:	shl    r9,0x9
   263fe:	cmp    r14,0x6
   26402:	je     25f95 <_ZNK12_GLOBAL__N_17AdapterILN15static_physical6LookupE0EE4findESt17basic_string_viewIcSt11char_traitsIcEE+0x235>
   26408:	movzx  eax,BYTE PTR [rdi+0x6]
   2640c:	add    rax,0x1
   26410:	or     r9,rax
   26413:	jmp    25f95 <_ZNK12_GLOBAL__N_17AdapterILN15static_physical6LookupE0EE4findESt17basic_string_viewIcSt11char_traitsIcEE+0x235>
   26418:	nop    DWORD PTR [rax+rax*1+0x0]
   26420:	mov    rdx,r12
   26423:	mov    rsi,r13
   26426:	mov    QWORD PTR [rsp+0x18],rcx
   2642b:	mov    QWORD PTR [rsp+0x20],r8
   26430:	mov    QWORD PTR [rsp+0x28],r10
   26435:	mov    QWORD PTR [rsp+0x30],r11
   2643a:	mov    QWORD PTR [rsp+0x10],r9
   2643f:	call   47e0 <memcmp@plt>
   26444:	mov    r11,QWORD PTR [rsp+0x30]
   26449:	mov    r10,QWORD PTR [rsp+0x28]
   2644e:	test   eax,eax
   26450:	mov    r8,QWORD PTR [rsp+0x20]
   26455:	mov    rcx,QWORD PTR [rsp+0x18]
   2645a:	je     26241 <_ZNK12_GLOBAL__N_17AdapterILN15static_physical6LookupE0EE4findESt17basic_string_viewIcSt11char_traitsIcEE+0x4e1>
   26460:	mov    r9,QWORD PTR [rsp+0x10]
   26465:	jmp    2633d <_ZNK12_GLOBAL__N_17AdapterILN15static_physical6LookupE0EE4findESt17basic_string_viewIcSt11char_traitsIcEE+0x5dd>
   2646a:	nop    WORD PTR [rax+rax*1+0x0]
   26470:	add    DWORD PTR [rsp+0x28],0x1
   26475:	mov    eax,DWORD PTR [rsp+0x28]
   26479:	cmp    DWORD PTR [rsp+0x20],eax
   2647d:	jb     25ee8 <_ZNK12_GLOBAL__N_17AdapterILN15static_physical6LookupE0EE4findESt17basic_string_viewIcSt11char_traitsIcEE+0x188>
   26483:	mov    eax,DWORD PTR [rsp+0x18]
   26487:	mov    ecx,DWORD PTR [rsp+0x28]
   2648b:	vmovdqa xmm4,XMMWORD PTR [rsp]
   26490:	add    eax,ecx
   26492:	mov    eax,eax
   26494:	lea    r9,[rax+rax*8]
   26498:	mov    rax,QWORD PTR [rsp+0x10]
   2649d:	shl    r9,0x5
   264a1:	lea    r13,[rax+r9*1]
   264a5:	vpcmpeqb xmm0,xmm4,XMMWORD PTR [r13+0x0]
   264ab:	mov    eax,DWORD PTR [r13+0x10]
   264af:	vpmovmskb r8d,xmm0
   264b3:	bzhi   r8d,r8d,eax
   264b8:	nop    DWORD PTR [rax+rax*1+0x0]
   264c0:	test   r8d,r8d
   264c3:	je     26470 <_ZNK12_GLOBAL__N_17AdapterILN15static_physical6LookupE0EE4findESt17basic_string_viewIcSt11char_traitsIcEE+0x710>
   264c5:	xor    r12d,r12d
   264c8:	tzcnt  r12d,r8d
   264cd:	blsr   r8d,r8d
   264d2:	mov    rax,r12
   264d5:	shl    rax,0x4
   264d9:	add    rax,r13
   264dc:	mov    esi,DWORD PTR [rax+0x1c]
   264df:	cmp    rsi,r14
   264e2:	jne    264c0 <_ZNK12_GLOBAL__N_17AdapterILN15static_physical6LookupE0EE4findESt17basic_string_viewIcSt11char_traitsIcEE+0x760>
   264e4:	mov    rsi,QWORD PTR [rbx]
   264e7:	lea    rdi,[rip+0x1e4e8b]        # 20b379 <_IO_stdin_used+0x379>
   264ee:	cmp    QWORD PTR [rbx+0x8],rsi
   264f2:	je     264fa <_ZNK12_GLOBAL__N_17AdapterILN15static_physical6LookupE0EE4findESt17basic_string_viewIcSt11char_traitsIcEE+0x79a>
   264f4:	mov    edi,DWORD PTR [rax+0x18]
   264f7:	add    rdi,rsi
   264fa:	mov    rsi,QWORD PTR [rsp+0x30]
   264ff:	mov    rdx,r14
   26502:	mov    DWORD PTR [rsp+0x38],r8d
   26507:	call   47e0 <memcmp@plt>
   2650c:	mov    r8d,DWORD PTR [rsp+0x38]
   26511:	test   eax,eax
   26513:	jne    264c0 <_ZNK12_GLOBAL__N_17AdapterILN15static_physical6LookupE0EE4findESt17basic_string_viewIcSt11char_traitsIcEE+0x760>
   26515:	jmp    26195 <_ZNK12_GLOBAL__N_17AdapterILN15static_physical6LookupE0EE4findESt17basic_string_viewIcSt11char_traitsIcEE+0x435>
   2651a:	nop    WORD PTR [rax+rax*1+0x0]
   26520:	mov    r8,rax
   26523:	jmp    25fd0 <_ZNK12_GLOBAL__N_17AdapterILN15static_physical6LookupE0EE4findESt17basic_string_viewIcSt11char_traitsIcEE+0x270>
   26528:	nop    DWORD PTR [rax+rax*1+0x0]
   26530:	add    DWORD PTR [rsp+0x28],0x1
   26535:	mov    eax,DWORD PTR [rsp+0x28]
   26539:	cmp    DWORD PTR [rsp+0x20],eax
   2653d:	jae    26134 <_ZNK12_GLOBAL__N_17AdapterILN15static_physical6LookupE0EE4findESt17basic_string_viewIcSt11char_traitsIcEE+0x3d4>
   26543:	jmp    25ee8 <_ZNK12_GLOBAL__N_17AdapterILN15static_physical6LookupE0EE4findESt17basic_string_viewIcSt11char_traitsIcEE+0x188>
   26548:	nop    DWORD PTR [rax+rax*1+0x0]
   26550:	mov    DWORD PTR [rsp+0x20],eax
   26554:	jmp    26090 <_ZNK12_GLOBAL__N_17AdapterILN15static_physical6LookupE0EE4findESt17basic_string_viewIcSt11char_traitsIcEE+0x330>
   26559:	sub    rsi,r9
   2655c:	js     265a6 <_ZNK12_GLOBAL__N_17AdapterILN15static_physical6LookupE0EE4findESt17basic_string_viewIcSt11char_traitsIcEE+0x846>
   2655e:	vcvtsi2sd xmm0,xmm1,rsi
   26563:	vxorpd xmm0,xmm0,XMMWORD PTR [rip+0x29a825]        # 2c0d90 <_ZZNSt19_Sp_make_shared_tag5_S_tiEvE5__tag+0x18>
   2656b:	jmp    26026 <_ZNK12_GLOBAL__N_17AdapterILN15static_physical6LookupE0EE4findESt17basic_string_viewIcSt11char_traitsIcEE+0x2c6>
   26570:	mov    rax,QWORD PTR [rsp+0x30]
   26575:	add    r12,r13
   26578:	mov    esi,0x9e3779b9
   2657d:	cmp    r12,rax
   26580:	jne    26100 <_ZNK12_GLOBAL__N_17AdapterILN15static_physical6LookupE0EE4findESt17basic_string_viewIcSt11char_traitsIcEE+0x3a0>
   26586:	jmp    2610e <_ZNK12_GLOBAL__N_17AdapterILN15static_physical6LookupE0EE4findESt17basic_string_viewIcSt11char_traitsIcEE+0x3ae>
   2658b:	mov    rsi,r9
   2658e:	and    r9d,0x1
   26592:	shr    rsi,1
   26595:	or     rsi,r9
   26598:	vcvtsi2sd xmm0,xmm1,rsi
   2659d:	vaddsd xmm0,xmm0,xmm0
   265a1:	jmp    26026 <_ZNK12_GLOBAL__N_17AdapterILN15static_physical6LookupE0EE4findESt17basic_string_viewIcSt11char_traitsIcEE+0x2c6>
   265a6:	mov    rdi,rsi
   265a9:	and    esi,0x1
   265ac:	shr    rdi,1
   265af:	or     rdi,rsi
   265b2:	vcvtsi2sd xmm0,xmm1,rdi
   265b7:	vaddsd xmm0,xmm0,xmm0
   265bb:	jmp    26563 <_ZNK12_GLOBAL__N_17AdapterILN15static_physical6LookupE0EE4findESt17basic_string_viewIcSt11char_traitsIcEE+0x803>
   265bd:	mov    eax,DWORD PTR [rcx]
   265bf:	vmovd  xmm6,edi
   265c3:	vpbroadcastb xmm6,xmm6
   265c8:	mov    DWORD PTR [rsp+0x18],eax
   265cc:	mov    rax,QWORD PTR [rbx+0x18]
   265d0:	vmovdqa XMMWORD PTR [rsp],xmm6
   265d5:	mov    QWORD PTR [rsp+0x10],rax
   265da:	jmp    26483 <_ZNK12_GLOBAL__N_17AdapterILN15static_physical6LookupE0EE4findESt17basic_string_viewIcSt11char_traitsIcEE+0x723>
   265df:	mov    rax,rdx
   265e2:	jmp    26270 <_ZNK12_GLOBAL__N_17AdapterILN15static_physical6LookupE0EE4findESt17basic_string_viewIcSt11char_traitsIcEE+0x510>
   265e7:	mov    rcx,r12
   265ea:	mov    rdx,r14
   265ed:	lea    rsi,[rip+0x1e4a2f]        # 20b023 <_IO_stdin_used+0x23>
   265f4:	xor    eax,eax
   265f6:	lea    rdi,[rip+0x1e580b]        # 20be08 <_IO_stdin_used+0xe08>
   265fd:	call   4b80 <_ZSt24__throw_out_of_range_fmtPKcz@plt>
