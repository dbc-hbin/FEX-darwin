%ifdef CONFIG
{
  "RegData": {
    "RAX": "0x2",
    "RBX": "0x17D992",
    "RCX": "0x1",
    "RDX": "0x0"
  },
  "MemoryRegions": {
    "0x10000000": "4096"
  },
  "Mode": "32BIT"
}
%endif

; Bit operations add a linear byte offset to an already computed address.
; An absent offset base must not become an Invalid IR value in arithmetic.
mov dword [0x10000000], 2
mov dword [0x10000004], 1
xor ebx, ebx

bt dword [0x10000000], 1
adc ebx, ebx
bts dword [0x10000000], 3
adc ebx, ebx
btr dword [0x10000000], 1
adc ebx, ebx
btc dword [0x10000000], 3
adc ebx, ebx
mov edx, [0x10000000]

mov esi, 32
bt dword [0x10000000], esi
adc ebx, ebx
mov dword [0x10000000], 2
mov esi, -31
bt dword [0x10000004], esi
adc ebx, ebx
bt word [0x10000000], 1
adc ebx, ebx

; Dynamic byte offsets must reach both scalar TSO loads and stores.
mov dword [0x10000008], 0
mov esi, 64
bts dword [0x10000000], esi
adc ebx, ebx
bt dword [0x10000000], esi
adc ebx, ebx
btc dword [0x10000000], esi
adc ebx, ebx
btr dword [0x10000000], esi
adc ebx, ebx
mov esi, -64
bts dword [0x10000010], esi
adc ebx, ebx
bt dword [0x10000010], esi
adc ebx, ebx
btr dword [0x10000010], esi
adc ebx, ebx
btc dword [0x10000010], esi
adc ebx, ebx

; Locked updates use the same signed, linear offset.
mov dword [0x10000008], 0
mov esi, 64
lock bts dword [0x10000000], esi
adc ebx, ebx
lock btc dword [0x10000000], esi
adc ebx, ebx
lock btr dword [0x10000000], esi
adc ebx, ebx
mov esi, -64
lock bts dword [0x10000010], esi
adc ebx, ebx
lock btr dword [0x10000010], esi
adc ebx, ebx
lock btc dword [0x10000010], esi
adc ebx, ebx

mov eax, [0x10000000]
mov ecx, [0x10000004]
hlt
