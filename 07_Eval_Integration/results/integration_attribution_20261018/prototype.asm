
/mnt/d/StringLearnedIndex_Workspace/07_Eval_Integration/results/static_interface_20261012c/build/bench:     file format elf64-x86-64


Disassembly of section .text:

0000000000011910 <_ZNK9Prototype4findESt17basic_string_viewIcSt11char_traitsIcEE>:
   11910:	endbr64
   11914:	push   rbp
   11915:	mov    rbp,rsp
   11918:	push   r15
   1191a:	push   r14
   1191c:	push   r13
   1191e:	push   r12
   11920:	push   rbx
   11921:	and    rsp,0xffffffffffffffe0
   11925:	add    rsp,0xffffffffffffff80
   11929:	mov    r12,QWORD PTR [rdi+0x8]
   1192d:	mov    r9,QWORD PTR [r12+0x38]
   11932:	mov    rcx,QWORD PTR [r12+0x30]
   11937:	cmp    r9,rcx
   1193a:	je     11aa0 <_ZNK9Prototype4findESt17basic_string_viewIcSt11char_traitsIcEE+0x190>
   11940:	mov    r10,QWORD PTR [r12+0x48]
   11945:	mov    r14,QWORD PTR [r12+0x78]
   1194a:	mov    r13,rdx
   1194d:	mov    rdx,rsi
   11950:	mov    rbx,rsi
   11953:	mov    rax,QWORD PTR [r10+0x8]
   11957:	cmp    r14,rsi
   1195a:	mov    rsi,QWORD PTR [r10]
   1195d:	cmovbe rdx,r14
   11961:	cmp    r14,rax
   11964:	cmovbe rax,r14
   11968:	cmp    rax,rdx
   1196b:	jb     11ac8 <_ZNK9Prototype4findESt17basic_string_viewIcSt11char_traitsIcEE+0x1b8>
   11971:	test   rdx,rdx
   11974:	jne    11d60 <_ZNK9Prototype4findESt17basic_string_viewIcSt11char_traitsIcEE+0x450>
   1197a:	test   rax,rax
   1197d:	jne    11af4 <_ZNK9Prototype4findESt17basic_string_viewIcSt11char_traitsIcEE+0x1e4>
   11983:	cmp    rbx,r14
   11986:	jb     12188 <_ZNK9Prototype4findESt17basic_string_viewIcSt11char_traitsIcEE+0x878>
   1198c:	mov    rax,rbx
   1198f:	sub    rax,r14
   11992:	je     11e18 <_ZNK9Prototype4findESt17basic_string_viewIcSt11char_traitsIcEE+0x508>
   11998:	movzx  edx,BYTE PTR [r13+r14*1+0x0]
   1199e:	mov    rsi,rdx
   119a1:	shl    rsi,0x8
   119a5:	cmp    rax,0x1
   119a9:	je     11e0b <_ZNK9Prototype4findESt17basic_string_viewIcSt11char_traitsIcEE+0x4fb>
   119af:	movzx  edx,BYTE PTR [r14+r13*1+0x1]
   119b5:	or     rdx,rsi
   119b8:	shl    rdx,0x8
   119bc:	cmp    rax,0x2
   119c0:	je     12180 <_ZNK9Prototype4findESt17basic_string_viewIcSt11char_traitsIcEE+0x870>
   119c6:	movzx  esi,BYTE PTR [r14+r13*1+0x2]
   119cc:	or     rsi,rdx
   119cf:	shl    rsi,0x8
   119d3:	cmp    rax,0x3
   119d7:	je     11e1f <_ZNK9Prototype4findESt17basic_string_viewIcSt11char_traitsIcEE+0x50f>
   119dd:	movzx  edx,BYTE PTR [r14+r13*1+0x3]
   119e3:	or     rdx,rsi
   119e6:	shl    rdx,0x8
   119ea:	cmp    rax,0x4
   119ee:	je     11e26 <_ZNK9Prototype4findESt17basic_string_viewIcSt11char_traitsIcEE+0x516>
   119f4:	movzx  esi,BYTE PTR [r14+r13*1+0x4]
   119fa:	or     rsi,rdx
   119fd:	shl    rsi,0x8
   11a01:	cmp    rax,0x5
   11a05:	je     11e2d <_ZNK9Prototype4findESt17basic_string_viewIcSt11char_traitsIcEE+0x51d>
   11a0b:	movzx  edx,BYTE PTR [r14+r13*1+0x5]
   11a11:	or     rdx,rsi
   11a14:	shl    rdx,0x8
   11a18:	cmp    rax,0x6
   11a1c:	je     11e34 <_ZNK9Prototype4findESt17basic_string_viewIcSt11char_traitsIcEE+0x524>
   11a22:	movzx  esi,BYTE PTR [r14+r13*1+0x6]
   11a28:	or     rdx,rsi
   11a2b:	mov    rdi,rdx
   11a2e:	shl    rdi,0x8
   11a32:	mov    QWORD PTR [rsp+0x38],rdi
   11a37:	cmp    rax,0x7
   11a3b:	je     11a4b <_ZNK9Prototype4findESt17basic_string_viewIcSt11char_traitsIcEE+0x13b>
   11a3d:	movzx  eax,BYTE PTR [r14+r13*1+0x7]
   11a43:	or     rdi,rax
   11a46:	mov    QWORD PTR [rsp+0x38],rdi
   11a4b:	mov    r11,QWORD PTR [r12+0x60]
   11a50:	mov    r9,QWORD PTR [r12+0x68]
   11a55:	sub    r9,r11
   11a58:	sar    r9,0x3
   11a5c:	je     11af4 <_ZNK9Prototype4findESt17basic_string_viewIcSt11char_traitsIcEE+0x1e4>
   11a62:	xor    r14d,r14d
   11a65:	test   rbx,rbx
   11a68:	jne    11dee <_ZNK9Prototype4findESt17basic_string_viewIcSt11char_traitsIcEE+0x4de>
   11a6e:	jmp    11dc2 <_ZNK9Prototype4findESt17basic_string_viewIcSt11char_traitsIcEE+0x4b2>
   11a73:	nop    DWORD PTR [rax+rax*1+0x0]
   11a78:	mov    rdx,r8
   11a7b:	mov    rdi,r13
   11a7e:	mov    QWORD PTR [rsp+0x30],rcx
   11a83:	mov    QWORD PTR [rsp+0x38],r8
   11a88:	call   47e0 <memcmp@plt>
   11a8d:	mov    r8,QWORD PTR [rsp+0x38]
   11a92:	mov    rcx,QWORD PTR [rsp+0x30]
   11a97:	test   eax,eax
   11a99:	je     11b0a <_ZNK9Prototype4findESt17basic_string_viewIcSt11char_traitsIcEE+0x1fa>
   11a9b:	nop    DWORD PTR [rax+rax*1+0x0]
   11aa0:	xor    eax,eax
   11aa2:	mov    QWORD PTR [rsp+0x70],r15
   11aa7:	mov    BYTE PTR [rsp+0x78],al
   11aab:	mov    rax,QWORD PTR [rsp+0x70]
   11ab0:	mov    rdx,QWORD PTR [rsp+0x78]
   11ab5:	lea    rsp,[rbp-0x28]
   11ab9:	pop    rbx
   11aba:	pop    r12
   11abc:	pop    r13
   11abe:	pop    r14
   11ac0:	pop    r15
   11ac2:	pop    rbp
   11ac3:	ret
   11ac4:	nop    DWORD PTR [rax+0x0]
   11ac8:	test   rax,rax
   11acb:	je     11af0 <_ZNK9Prototype4findESt17basic_string_viewIcSt11char_traitsIcEE+0x1e0>
   11acd:	mov    rdx,rax
   11ad0:	mov    rdi,r13
   11ad3:	mov    QWORD PTR [rsp+0x38],r9
   11ad8:	mov    QWORD PTR [rsp+0x30],rcx
   11add:	call   47e0 <memcmp@plt>
   11ae2:	mov    r9,QWORD PTR [rsp+0x38]
   11ae7:	test   eax,eax
   11ae9:	jne    11e48 <_ZNK9Prototype4findESt17basic_string_viewIcSt11char_traitsIcEE+0x538>
   11aef:	nop
   11af0:	lea    rcx,[r9-0x40]
   11af4:	mov    r8,QWORD PTR [rcx+0x10]
   11af8:	cmp    rbx,r8
   11afb:	jb     11aa0 <_ZNK9Prototype4findESt17basic_string_viewIcSt11char_traitsIcEE+0x190>
   11afd:	mov    rsi,QWORD PTR [rcx+0x8]
   11b01:	test   r8,r8
   11b04:	jne    11a78 <_ZNK9Prototype4findESt17basic_string_viewIcSt11char_traitsIcEE+0x168>
   11b0a:	lea    rax,[r13+r8*1+0x0]
   11b0f:	mov    r14,rbx
   11b12:	mov    QWORD PTR [rsp+0x30],rax
   11b17:	sub    r14,r8
   11b1a:	jne    11f18 <_ZNK9Prototype4findESt17basic_string_viewIcSt11char_traitsIcEE+0x608>
   11b20:	xor    eax,eax
   11b22:	shl    rax,0x9
   11b26:	mov    rsi,rax
   11b29:	mov    rax,rsi
   11b2c:	shl    rax,0x9
   11b30:	shl    rax,0x9
   11b34:	mov    rsi,rax
   11b37:	mov    rax,rsi
   11b3a:	shl    rax,0x9
   11b3e:	shl    rax,0x9
   11b42:	mov    r9,rax
   11b45:	mov    r11d,DWORD PTR [rcx+0x4]
   11b49:	mov    r8,QWORD PTR [rcx+0x30]
   11b4d:	lea    eax,[r11-0x1]
   11b51:	mov    DWORD PTR [rsp+0x28],eax
   11b55:	cmp    QWORD PTR [r8-0x28],r9
   11b59:	jb     120f0 <_ZNK9Prototype4findESt17basic_string_viewIcSt11char_traitsIcEE+0x7e0>
   11b5f:	movabs rax,0xaaaaaaaaaaaaaaab
   11b69:	mov    r10,QWORD PTR [rcx+0x28]
   11b6d:	xor    edi,edi
   11b6f:	sub    r8,r10
   11b72:	sar    r8,0x4
   11b76:	imul   r8,rax
   11b7a:	nop    WORD PTR [rax+rax*1+0x0]
   11b80:	cmp    rdi,r8
   11b83:	jae    11bad <_ZNK9Prototype4findESt17basic_string_viewIcSt11char_traitsIcEE+0x29d>
   11b85:	mov    rax,r8
   11b88:	sub    rax,rdi
   11b8b:	shr    rax,1
   11b8e:	add    rax,rdi
   11b91:	lea    rsi,[rax+rax*2]
   11b95:	shl    rsi,0x4
   11b99:	cmp    QWORD PTR [r10+rsi*1+0x8],r9
   11b9e:	jae    120c0 <_ZNK9Prototype4findESt17basic_string_viewIcSt11char_traitsIcEE+0x7b0>
   11ba4:	lea    rdi,[rax+0x1]
   11ba8:	cmp    rdi,r8
   11bab:	jb     11b85 <_ZNK9Prototype4findESt17basic_string_viewIcSt11char_traitsIcEE+0x275>
   11bad:	lea    rax,[rdi+rdi*2]
   11bb1:	vxorps xmm1,xmm1,xmm1
   11bb5:	shl    rax,0x4
   11bb9:	add    rax,r10
   11bbc:	mov    rsi,QWORD PTR [rax]
   11bbf:	cmp    r9,rsi
   11bc2:	jb     120f9 <_ZNK9Prototype4findESt17basic_string_viewIcSt11char_traitsIcEE+0x7e9>
   11bc8:	sub    r9,rsi
   11bcb:	js     1212b <_ZNK9Prototype4findESt17basic_string_viewIcSt11char_traitsIcEE+0x81b>
   11bd1:	vcvtsi2sd xmm0,xmm1,r9
   11bd6:	mov    esi,DWORD PTR [rax+0x18]
   11bd9:	mov    edi,DWORD PTR [rax+0x1c]
   11bdc:	vcvtsi2sd xmm2,xmm1,rsi
   11be1:	vfmadd132sd xmm0,xmm2,QWORD PTR [rax+0x10]
   11be7:	vcvtsi2sd xmm2,xmm1,rdi
   11bec:	vcomisd xmm0,xmm2
   11bf0:	jbe    11c0d <_ZNK9Prototype4findESt17basic_string_viewIcSt11char_traitsIcEE+0x2fd>
   11bf2:	mov    edi,DWORD PTR [rax+0x20]
   11bf5:	vcvtsi2sd xmm1,xmm1,rdi
   11bfa:	vcomisd xmm0,xmm1
   11bfe:	jae    11c0d <_ZNK9Prototype4findESt17basic_string_viewIcSt11char_traitsIcEE+0x2fd>
   11c00:	vroundsd xmm0,xmm0,xmm0,0x9
   11c06:	vcvttsd2si rdi,xmm0
   11c0b:	mov    edi,edi
   11c0d:	movsxd rsi,DWORD PTR [rax+0x24]
   11c11:	movsxd rax,DWORD PTR [rax+0x28]
   11c15:	mov    r8d,0x0
   11c1b:	add    rsi,rdi
   11c1e:	cmovs  rsi,r8
   11c22:	add    rax,rdi
   11c25:	mov    edi,DWORD PTR [rsp+0x28]
   11c29:	cmp    rax,rdi
   11c2c:	cmovg  rax,rdi
   11c30:	mov    DWORD PTR [rsp+0x20],eax
   11c34:	cmp    eax,esi
   11c36:	jb     121a3 <_ZNK9Prototype4findESt17basic_string_viewIcSt11char_traitsIcEE+0x893>
   11c3c:	mov    DWORD PTR [rsp+0x28],esi
   11c40:	cmp    DWORD PTR [rsp+0x20],r11d
   11c45:	jae    121a3 <_ZNK9Prototype4findESt17basic_string_viewIcSt11char_traitsIcEE+0x893>
   11c4b:	cmp    r14,0x7
   11c4f:	jbe    12110 <_ZNK9Prototype4findESt17basic_string_viewIcSt11char_traitsIcEE+0x800>
   11c55:	lea    r9,[r14-0x8]
   11c59:	mov    rbx,QWORD PTR [rsp+0x30]
   11c5e:	mov    rsi,r9
   11c61:	and    rsi,0xfffffffffffffff8
   11c65:	mov    rax,rbx
   11c68:	lea    r8,[rbx+rsi*1+0x8]
   11c6d:	mov    esi,0x9e3779b9
   11c72:	nop    WORD PTR [rax+rax*1+0x0]
   11c78:	mov    edi,esi
   11c7a:	add    rax,0x8
   11c7e:	crc32  rdi,QWORD PTR [rax-0x8]
   11c85:	mov    esi,edi
   11c87:	cmp    r8,rax
   11c8a:	jne    11c78 <_ZNK9Prototype4findESt17basic_string_viewIcSt11char_traitsIcEE+0x368>
   11c8c:	mov    rax,QWORD PTR [rsp+0x30]
   11c91:	and    r9,0xfffffffffffffff8
   11c95:	mov    rbx,r14
   11c98:	and    ebx,0x7
   11c9b:	lea    rax,[rax+r9*1+0x8]
   11ca0:	add    rbx,rax
   11ca3:	cmp    rbx,rax
   11ca6:	je     1215d <_ZNK9Prototype4findESt17basic_string_viewIcSt11char_traitsIcEE+0x84d>
   11cac:	nop    DWORD PTR [rax+0x0]
   11cb0:	crc32  esi,BYTE PTR [rax]
   11cb5:	add    rax,0x1
   11cb9:	cmp    rbx,rax
   11cbc:	jne    11cb0 <_ZNK9Prototype4findESt17basic_string_viewIcSt11char_traitsIcEE+0x3a0>
   11cbe:	mov    eax,DWORD PTR [rcx]
   11cc0:	vmovd  xmm5,esi
   11cc4:	vpbroadcastb xmm5,xmm5
   11cc9:	mov    DWORD PTR [rsp+0x18],eax
   11ccd:	mov    rax,QWORD PTR [r12+0x18]
   11cd2:	vmovdqa XMMWORD PTR [rsp],xmm5
   11cd7:	mov    QWORD PTR [rsp+0x10],rax
   11cdc:	test   r14,r14
   11cdf:	jne    12023 <_ZNK9Prototype4findESt17basic_string_viewIcSt11char_traitsIcEE+0x713>
   11ce5:	mov    eax,DWORD PTR [rsp+0x18]
   11ce9:	mov    ecx,DWORD PTR [rsp+0x28]
   11ced:	vmovdqa xmm3,XMMWORD PTR [rsp]
   11cf2:	add    eax,ecx
   11cf4:	mov    eax,eax
   11cf6:	lea    r9,[rax+rax*8]
   11cfa:	mov    rax,QWORD PTR [rsp+0x10]
   11cff:	shl    r9,0x5
   11d03:	lea    r13,[rax+r9*1]
   11d07:	vpcmpeqb xmm0,xmm3,XMMWORD PTR [r13+0x0]
   11d0d:	mov    edx,DWORD PTR [r13+0x10]
   11d11:	vpmovmskb eax,xmm0
   11d15:	bzhi   eax,eax,edx
   11d1a:	nop    WORD PTR [rax+rax*1+0x0]
   11d20:	test   eax,eax
   11d22:	je     120d0 <_ZNK9Prototype4findESt17basic_string_viewIcSt11char_traitsIcEE+0x7c0>
   11d28:	xor    ebx,ebx
   11d2a:	tzcnt  ebx,eax
   11d2e:	blsr   eax,eax
   11d33:	mov    rdx,rbx
   11d36:	shl    rdx,0x4
   11d3a:	mov    edx,DWORD PTR [r13+rdx*1+0x1c]
   11d3f:	test   edx,edx
   11d41:	jne    11d20 <_ZNK9Prototype4findESt17basic_string_viewIcSt11char_traitsIcEE+0x410>
   11d43:	shl    rbx,0x4
   11d47:	mov    eax,0x1
   11d4c:	mov    r15,QWORD PTR [r13+rbx*1+0x20]
   11d51:	jmp    11aa2 <_ZNK9Prototype4findESt17basic_string_viewIcSt11char_traitsIcEE+0x192>
   11d56:	cs nop WORD PTR [rax+rax*1+0x0]
   11d60:	mov    rdi,r13
   11d63:	mov    QWORD PTR [rsp+0x30],rcx
   11d68:	mov    QWORD PTR [rsp+0x38],r9
   11d6d:	mov    QWORD PTR [rsp+0x18],rax
   11d72:	mov    QWORD PTR [rsp+0x20],r10
   11d77:	mov    QWORD PTR [rsp+0x28],rdx
   11d7c:	call   47e0 <memcmp@plt>
   11d81:	mov    r9,QWORD PTR [rsp+0x38]
   11d86:	mov    rcx,QWORD PTR [rsp+0x30]
   11d8b:	test   eax,eax
   11d8d:	jne    11e4d <_ZNK9Prototype4findESt17basic_string_viewIcSt11char_traitsIcEE+0x53d>
   11d93:	mov    rdx,QWORD PTR [rsp+0x28]
   11d98:	mov    r8,QWORD PTR [rsp+0x18]
   11d9d:	mov    r10,QWORD PTR [rsp+0x20]
   11da2:	cmp    r8,rdx
   11da5:	je     11983 <_ZNK9Prototype4findESt17basic_string_viewIcSt11char_traitsIcEE+0x73>
   11dab:	jmp    11af4 <_ZNK9Prototype4findESt17basic_string_viewIcSt11char_traitsIcEE+0x1e4>
   11db0:	je     11e80 <_ZNK9Prototype4findESt17basic_string_viewIcSt11char_traitsIcEE+0x570>
   11db6:	mov    r9,rax
   11db9:	cmp    r14,r9
   11dbc:	jae    11e60 <_ZNK9Prototype4findESt17basic_string_viewIcSt11char_traitsIcEE+0x550>
   11dc2:	mov    rax,r9
   11dc5:	mov    rdi,QWORD PTR [rsp+0x38]
   11dca:	sub    rax,r14
   11dcd:	shr    rax,1
   11dd0:	add    rax,r14
   11dd3:	cmp    QWORD PTR [r11+rax*8],rdi
   11dd7:	jae    11db0 <_ZNK9Prototype4findESt17basic_string_viewIcSt11char_traitsIcEE+0x4a0>
   11dd9:	lea    r14,[rax+0x1]
   11ddd:	jmp    11db9 <_ZNK9Prototype4findESt17basic_string_viewIcSt11char_traitsIcEE+0x4a9>
   11ddf:	nop
   11de0:	je     11ea0 <_ZNK9Prototype4findESt17basic_string_viewIcSt11char_traitsIcEE+0x590>
   11de6:	mov    r9,r8
   11de9:	cmp    r14,r9
   11dec:	jae    11e60 <_ZNK9Prototype4findESt17basic_string_viewIcSt11char_traitsIcEE+0x550>
   11dee:	mov    r8,r9
   11df1:	mov    rax,QWORD PTR [rsp+0x38]
   11df6:	sub    r8,r14
   11df9:	shr    r8,1
   11dfc:	add    r8,r14
   11dff:	cmp    QWORD PTR [r11+r8*8],rax
   11e03:	jae    11de0 <_ZNK9Prototype4findESt17basic_string_viewIcSt11char_traitsIcEE+0x4d0>
   11e05:	lea    r14,[r8+0x1]
   11e09:	jmp    11de9 <_ZNK9Prototype4findESt17basic_string_viewIcSt11char_traitsIcEE+0x4d9>
   11e0b:	mov    rax,rdx
   11e0e:	shl    rax,0x10
   11e12:	nop    WORD PTR [rax+rax*1+0x0]
   11e18:	shl    rax,0x8
   11e1c:	mov    rsi,rax
   11e1f:	shl    rsi,0x8
   11e23:	mov    rdx,rsi
   11e26:	mov    rsi,rdx
   11e29:	shl    rsi,0x8
   11e2d:	shl    rsi,0x8
   11e31:	mov    rdx,rsi
   11e34:	shl    rdx,0x8
   11e38:	mov    QWORD PTR [rsp+0x38],rdx
   11e3d:	jmp    11a4b <_ZNK9Prototype4findESt17basic_string_viewIcSt11char_traitsIcEE+0x13b>
   11e42:	nop    WORD PTR [rax+rax*1+0x0]
   11e48:	mov    rcx,QWORD PTR [rsp+0x30]
   11e4d:	test   eax,eax
   11e4f:	jns    11af0 <_ZNK9Prototype4findESt17basic_string_viewIcSt11char_traitsIcEE+0x1e0>
   11e55:	jmp    11af4 <_ZNK9Prototype4findESt17basic_string_viewIcSt11char_traitsIcEE+0x1e4>
   11e5a:	nop    WORD PTR [rax+rax*1+0x0]
   11e60:	test   r14,r14
   11e63:	je     11af4 <_ZNK9Prototype4findESt17basic_string_viewIcSt11char_traitsIcEE+0x1e4>
   11e69:	sub    r14,0x1
   11e6d:	shl    r14,0x6
   11e71:	add    rcx,r14
   11e74:	jmp    11af4 <_ZNK9Prototype4findESt17basic_string_viewIcSt11char_traitsIcEE+0x1e4>
   11e79:	nop    DWORD PTR [rax+0x0]
   11e80:	mov    rdx,rax
   11e83:	shl    rdx,0x5
   11e87:	cmp    QWORD PTR [r10+rdx*1+0x8],0x0
   11e8d:	je     11dd9 <_ZNK9Prototype4findESt17basic_string_viewIcSt11char_traitsIcEE+0x4c9>
   11e93:	jmp    11db6 <_ZNK9Prototype4findESt17basic_string_viewIcSt11char_traitsIcEE+0x4a6>
   11e98:	nop    DWORD PTR [rax+rax*1+0x0]
   11ea0:	mov    rax,r8
   11ea3:	shl    rax,0x5
   11ea7:	add    rax,r10
   11eaa:	mov    rdx,QWORD PTR [rax+0x8]
   11eae:	mov    rdi,QWORD PTR [rax]
   11eb1:	cmp    rbx,rdx
   11eb4:	jb     11fc0 <_ZNK9Prototype4findESt17basic_string_viewIcSt11char_traitsIcEE+0x6b0>
   11eba:	test   rdx,rdx
   11ebd:	je     11e05 <_ZNK9Prototype4findESt17basic_string_viewIcSt11char_traitsIcEE+0x4f5>
   11ec3:	mov    rsi,r13
   11ec6:	mov    QWORD PTR [rsp+0x10],r9
   11ecb:	mov    QWORD PTR [rsp+0x18],rcx
   11ed0:	mov    QWORD PTR [rsp+0x20],r8
   11ed5:	mov    QWORD PTR [rsp+0x28],r11
   11eda:	mov    QWORD PTR [rsp+0x30],r10
   11edf:	call   47e0 <memcmp@plt>
   11ee4:	mov    r10,QWORD PTR [rsp+0x30]
   11ee9:	mov    r11,QWORD PTR [rsp+0x28]
   11eee:	test   eax,eax
   11ef0:	mov    r8,QWORD PTR [rsp+0x20]
   11ef5:	mov    rcx,QWORD PTR [rsp+0x18]
   11efa:	mov    r9,QWORD PTR [rsp+0x10]
   11eff:	je     11e05 <_ZNK9Prototype4findESt17basic_string_viewIcSt11char_traitsIcEE+0x4f5>
   11f05:	test   eax,eax
   11f07:	jg     11de6 <_ZNK9Prototype4findESt17basic_string_viewIcSt11char_traitsIcEE+0x4d6>
   11f0d:	jmp    11e05 <_ZNK9Prototype4findESt17basic_string_viewIcSt11char_traitsIcEE+0x4f5>
   11f12:	nop    WORD PTR [rax+rax*1+0x0]
   11f18:	movzx  eax,BYTE PTR [rax]
   11f1b:	add    rax,0x1
   11f1f:	shl    rax,0x9
   11f23:	cmp    r14,0x1
   11f27:	je     11b22 <_ZNK9Prototype4findESt17basic_string_viewIcSt11char_traitsIcEE+0x212>
   11f2d:	mov    rdi,QWORD PTR [rsp+0x30]
   11f32:	movzx  esi,BYTE PTR [rdi+0x1]
   11f36:	add    rsi,0x1
   11f3a:	or     rsi,rax
   11f3d:	shl    rsi,0x9
   11f41:	cmp    r14,0x2
   11f45:	je     11b29 <_ZNK9Prototype4findESt17basic_string_viewIcSt11char_traitsIcEE+0x219>
   11f4b:	movzx  eax,BYTE PTR [rdi+0x2]
   11f4f:	add    rax,0x1
   11f53:	or     rax,rsi
   11f56:	shl    rax,0x9
   11f5a:	cmp    r14,0x3
   11f5e:	je     11b30 <_ZNK9Prototype4findESt17basic_string_viewIcSt11char_traitsIcEE+0x220>
   11f64:	movzx  esi,BYTE PTR [rdi+0x3]
   11f68:	add    rsi,0x1
   11f6c:	or     rsi,rax
   11f6f:	shl    rsi,0x9
   11f73:	cmp    r14,0x4
   11f77:	je     11b37 <_ZNK9Prototype4findESt17basic_string_viewIcSt11char_traitsIcEE+0x227>
   11f7d:	movzx  eax,BYTE PTR [rdi+0x4]
   11f81:	add    rax,0x1
   11f85:	or     rax,rsi
   11f88:	shl    rax,0x9
   11f8c:	cmp    r14,0x5
   11f90:	je     11b3e <_ZNK9Prototype4findESt17basic_string_viewIcSt11char_traitsIcEE+0x22e>
   11f96:	movzx  r9d,BYTE PTR [rdi+0x5]
   11f9b:	add    r9,0x1
   11f9f:	or     r9,rax
   11fa2:	shl    r9,0x9
   11fa6:	cmp    r14,0x6
   11faa:	je     11b45 <_ZNK9Prototype4findESt17basic_string_viewIcSt11char_traitsIcEE+0x235>
   11fb0:	movzx  eax,BYTE PTR [rdi+0x6]
   11fb4:	add    rax,0x1
   11fb8:	or     r9,rax
   11fbb:	jmp    11b45 <_ZNK9Prototype4findESt17basic_string_viewIcSt11char_traitsIcEE+0x235>
   11fc0:	mov    rdx,rbx
   11fc3:	mov    rsi,r13
   11fc6:	mov    QWORD PTR [rsp+0x18],rcx
   11fcb:	mov    QWORD PTR [rsp+0x20],r8
   11fd0:	mov    QWORD PTR [rsp+0x28],r11
   11fd5:	mov    QWORD PTR [rsp+0x30],r10
   11fda:	mov    QWORD PTR [rsp+0x10],r9
   11fdf:	call   47e0 <memcmp@plt>
   11fe4:	mov    r10,QWORD PTR [rsp+0x30]
   11fe9:	mov    r11,QWORD PTR [rsp+0x28]
   11fee:	test   eax,eax
   11ff0:	mov    r8,QWORD PTR [rsp+0x20]
   11ff5:	mov    rcx,QWORD PTR [rsp+0x18]
   11ffa:	je     11de6 <_ZNK9Prototype4findESt17basic_string_viewIcSt11char_traitsIcEE+0x4d6>
   12000:	mov    r9,QWORD PTR [rsp+0x10]
   12005:	jmp    11f05 <_ZNK9Prototype4findESt17basic_string_viewIcSt11char_traitsIcEE+0x5f5>
   1200a:	nop    WORD PTR [rax+rax*1+0x0]
   12010:	add    DWORD PTR [rsp+0x28],0x1
   12015:	mov    eax,DWORD PTR [rsp+0x28]
   12019:	cmp    DWORD PTR [rsp+0x20],eax
   1201d:	jb     11aa0 <_ZNK9Prototype4findESt17basic_string_viewIcSt11char_traitsIcEE+0x190>
   12023:	mov    eax,DWORD PTR [rsp+0x18]
   12027:	mov    ecx,DWORD PTR [rsp+0x28]
   1202b:	vmovdqa xmm4,XMMWORD PTR [rsp]
   12030:	add    eax,ecx
   12032:	mov    eax,eax
   12034:	lea    r9,[rax+rax*8]
   12038:	mov    rax,QWORD PTR [rsp+0x10]
   1203d:	shl    r9,0x5
   12041:	lea    r13,[rax+r9*1]
   12045:	vpcmpeqb xmm0,xmm4,XMMWORD PTR [r13+0x0]
   1204b:	mov    eax,DWORD PTR [r13+0x10]
   1204f:	vpmovmskb r8d,xmm0
   12053:	bzhi   r8d,r8d,eax
   12058:	nop    DWORD PTR [rax+rax*1+0x0]
   12060:	test   r8d,r8d
   12063:	je     12010 <_ZNK9Prototype4findESt17basic_string_viewIcSt11char_traitsIcEE+0x700>
   12065:	xor    ebx,ebx
   12067:	tzcnt  ebx,r8d
   1206c:	blsr   r8d,r8d
   12071:	mov    rax,rbx
   12074:	shl    rax,0x4
   12078:	add    rax,r13
   1207b:	mov    esi,DWORD PTR [rax+0x1c]
   1207e:	cmp    rsi,r14
   12081:	jne    12060 <_ZNK9Prototype4findESt17basic_string_viewIcSt11char_traitsIcEE+0x750>
   12083:	mov    rsi,QWORD PTR [r12]
   12087:	lea    rdi,[rip+0x1f92eb]        # 20b379 <_IO_stdin_used+0x379>
   1208e:	cmp    QWORD PTR [r12+0x8],rsi
   12093:	je     1209b <_ZNK9Prototype4findESt17basic_string_viewIcSt11char_traitsIcEE+0x78b>
   12095:	mov    edi,DWORD PTR [rax+0x18]
   12098:	add    rdi,rsi
   1209b:	mov    rsi,QWORD PTR [rsp+0x30]
   120a0:	mov    rdx,r14
   120a3:	mov    DWORD PTR [rsp+0x38],r8d
   120a8:	call   47e0 <memcmp@plt>
   120ad:	mov    r8d,DWORD PTR [rsp+0x38]
   120b2:	test   eax,eax
   120b4:	jne    12060 <_ZNK9Prototype4findESt17basic_string_viewIcSt11char_traitsIcEE+0x750>
   120b6:	jmp    11d43 <_ZNK9Prototype4findESt17basic_string_viewIcSt11char_traitsIcEE+0x433>
   120bb:	nop    DWORD PTR [rax+rax*1+0x0]
   120c0:	mov    r8,rax
   120c3:	jmp    11b80 <_ZNK9Prototype4findESt17basic_string_viewIcSt11char_traitsIcEE+0x270>
   120c8:	nop    DWORD PTR [rax+rax*1+0x0]
   120d0:	add    DWORD PTR [rsp+0x28],0x1
   120d5:	mov    eax,DWORD PTR [rsp+0x28]
   120d9:	cmp    DWORD PTR [rsp+0x20],eax
   120dd:	jae    11ce5 <_ZNK9Prototype4findESt17basic_string_viewIcSt11char_traitsIcEE+0x3d5>
   120e3:	jmp    11aa0 <_ZNK9Prototype4findESt17basic_string_viewIcSt11char_traitsIcEE+0x190>
   120e8:	nop    DWORD PTR [rax+rax*1+0x0]
   120f0:	mov    DWORD PTR [rsp+0x20],eax
   120f4:	jmp    11c40 <_ZNK9Prototype4findESt17basic_string_viewIcSt11char_traitsIcEE+0x330>
   120f9:	sub    rsi,r9
   120fc:	js     12146 <_ZNK9Prototype4findESt17basic_string_viewIcSt11char_traitsIcEE+0x836>
   120fe:	vcvtsi2sd xmm0,xmm1,rsi
   12103:	vxorpd xmm0,xmm0,XMMWORD PTR [rip+0x2aec85]        # 2c0d90 <_ZZNSt19_Sp_make_shared_tag5_S_tiEvE5__tag+0x18>
   1210b:	jmp    11bd6 <_ZNK9Prototype4findESt17basic_string_viewIcSt11char_traitsIcEE+0x2c6>
   12110:	mov    rax,QWORD PTR [rsp+0x30]
   12115:	add    rbx,r13
   12118:	mov    esi,0x9e3779b9
   1211d:	cmp    rbx,rax
   12120:	jne    11cb0 <_ZNK9Prototype4findESt17basic_string_viewIcSt11char_traitsIcEE+0x3a0>
   12126:	jmp    11cbe <_ZNK9Prototype4findESt17basic_string_viewIcSt11char_traitsIcEE+0x3ae>
   1212b:	mov    rsi,r9
   1212e:	and    r9d,0x1
   12132:	shr    rsi,1
   12135:	or     rsi,r9
   12138:	vcvtsi2sd xmm0,xmm1,rsi
   1213d:	vaddsd xmm0,xmm0,xmm0
   12141:	jmp    11bd6 <_ZNK9Prototype4findESt17basic_string_viewIcSt11char_traitsIcEE+0x2c6>
   12146:	mov    rdi,rsi
   12149:	and    esi,0x1
   1214c:	shr    rdi,1
   1214f:	or     rdi,rsi
   12152:	vcvtsi2sd xmm0,xmm1,rdi
   12157:	vaddsd xmm0,xmm0,xmm0
   1215b:	jmp    12103 <_ZNK9Prototype4findESt17basic_string_viewIcSt11char_traitsIcEE+0x7f3>
   1215d:	mov    eax,DWORD PTR [rcx]
   1215f:	vmovd  xmm6,edi
   12163:	vpbroadcastb xmm6,xmm6
   12168:	mov    DWORD PTR [rsp+0x18],eax
   1216c:	mov    rax,QWORD PTR [r12+0x18]
   12171:	vmovdqa XMMWORD PTR [rsp],xmm6
   12176:	mov    QWORD PTR [rsp+0x10],rax
   1217b:	jmp    12023 <_ZNK9Prototype4findESt17basic_string_viewIcSt11char_traitsIcEE+0x713>
   12180:	mov    rax,rdx
   12183:	jmp    11e18 <_ZNK9Prototype4findESt17basic_string_viewIcSt11char_traitsIcEE+0x508>
   12188:	mov    rcx,rbx
   1218b:	mov    rdx,r14
   1218e:	lea    rsi,[rip+0x1f8e8e]        # 20b023 <_IO_stdin_used+0x23>
   12195:	xor    eax,eax
   12197:	lea    rdi,[rip+0x1f9c6a]        # 20be08 <_IO_stdin_used+0xe08>
   1219e:	call   4b80 <_ZSt24__throw_out_of_range_fmtPKcz@plt>
   121a3:	mov    edi,0x10
   121a8:	call   4800 <__cxa_allocate_exception@plt>
   121ad:	lea    rsi,[rip+0x1f8e89]        # 20b03d <_IO_stdin_used+0x3d>
   121b4:	mov    rdi,rax
   121b7:	mov    rbx,rax
   121ba:	call   4b50 <_ZNSt11logic_errorC1EPKc@plt>
   121bf:	mov    rdx,QWORD PTR [rip+0x2bbdfa]        # 2cdfc0 <_ZNSt11logic_errorD1Ev@GLIBCXX_3.4>
   121c6:	lea    rsi,[rip+0x2bb653]        # 2cd820 <_ZTISt11logic_error@GLIBCXX_3.4>
   121cd:	mov    rdi,rbx
   121d0:	call   4c20 <__cxa_throw@plt>
   121d5:	endbr64
   121d9:	mov    r12,rax
   121dc:	mov    rdi,rbx
   121df:	vzeroupper
   121e2:	call   4910 <__cxa_free_exception@plt>
   121e7:	mov    rdi,r12
   121ea:	call   4c60 <_Unwind_Resume@plt>
