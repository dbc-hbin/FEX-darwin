%ifdef CONFIG
{
  "RegData": {
    "RAX": "5",
    "RBX": "3",
    "RCX": "2",
    "RDX": "1",
    "RSI": "0xE0"
  }
}
%endif

; FXSAVE stores logical ST(i), while the context stores physical registers.
; Three pushes give TOP=5. Exact integers also exercise F80-to-F64 restore.
fninit
fld1
fild dword [rel two]
fild dword [rel three]
fxsave [rel saved]
fninit
fxrstor [rel saved]
fnstsw ax
shr eax, 11
and eax, 7
fxsave [rel restored]
movzx esi, byte [rel restored + 4]
fistp dword [rel result]
mov ebx, dword [rel result]
fistp dword [rel result]
mov ecx, dword [rel result]
fistp dword [rel result]
mov edx, dword [rel result]
hlt

align 16
saved: times 512 db 0
restored: times 512 db 0
result: dd 0
two: dd 2
three: dd 3
