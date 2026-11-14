; Copyright Svh03ra (C) 2026, All rights reserved
; Source File: realmode.asm, protected-mode <-> real-mode BIOS call bridge.
; This repository is licensed under the GNU General Public License.
;
; The video BIOS is the only thing that can change the display mode on a
; machine without a programmable video device (real hardware, an emulated
; BIOS, a VM whose card has no Bochs/VMware registers), and its services live behind
; INT 10h - that is, in real mode.  This file is that bridge.
;
; The bridge also carries the debug boot-log flush: the kernel has no USB
; stack, so writing the serial trace back onto a dd'd ISO stick goes
; through INT 13h (BIOS disk services) on this same path, with the vector
; chosen per call through the RM_R_INT word (0x10 when zero).
;
; The bridge needs a 16-bit code descriptor, which the flat table the kernel
; runs on does not have, so it installs a table of its own (realmode_install_gdt)
; with one entry added.  That table has to describe the SAME world the kernel
; already lives in - its selectors are not ours to renumber:
;
;   0x10  flat 32-bit code   the kernel's CS, and the selector every interrupt
;                            gate in the IDT names
;   0x18  flat 32-bit data   the kernel's DS/ES/SS
;   0x20  16-bit code        the bridge's own selector for the hop into the stub
;
; 0x00 and 0x08 stay null, exactly as they are in the bootloader's table.  The
; bootloader's table ends at 0x18, so 0x20 is a free index: adding the 16-bit
; descriptor there displaces nothing.  (Renumbering these would be silently
; fatal rather than obviously wrong: a data descriptor where the gates expect
; code only faults when the first interrupt is delivered, which is well after
; the BIOS call has returned and reported success.)
;
; The round trip (realmode_call):
;
;   1. save ESP, copy the 16-bit stub down to RM_STUB, cli;
;   2. far-jump to the 16-bit code selector - still protected mode, but 16-bit
;      decoding at the stub;
;   3. the stub clears CR0.PE and re-enters itself with a REAL-MODE far jump, so
;      that every segment register ends up holding the value real mode gives
;      it: a plain zero, with the block addressed as segment:offset just below
;      the 64 KB boundary;
;   4. the stub loads the caller's register file, executes INT 10h, stores the
;      result registers back, sets CR0.PE, and far-jumps back to the flat
;      32-bit code selector;
;   5. realmode_return reloads the flat data segments, restores ESP and returns,
;      handing back the descriptor table pointers the kernel was using.
;
; Step 3 is the part that has to be exactly right, and it costs a little
; explanation.  Two separate things go wrong if the stub just clears CR0.PE:
;
;   * The segment CACHES keep the values the protected-mode switch loaded, so
;     CS would still hold the 16-bit code selector and DS/SS still protected
;     selectors.
;     Code that saves, restores or far-jumps that state (a BIOS handler, or an
;     SMM mirror behind a VBE call) then computes an address from the selector
;     rather than the cached base.
;   * An interrupt taken from that state misbehaves outright: the first real
;     INT 10h attempt died inside the CPU's delivery path and reset the machine
;     with the handler never entered.
;
; Both are fixed by re-entering the stub with a real-mode far jump and then
; loading every data segment with a real-mode zero.  That is deliberately the
; same shape a bootloader's own real-mode trampoline uses, because that shape is
; the one firmware and its SMM are written against: every segment register holds
; an ordinary real-mode value, and the stub lives at the bottom of memory where
; such a value can address it.
;
; Everything the stub touches lives at fixed low-memory addresses in the block
; defined below; the C side reads those addresses out of the exported rm_layout
; table rather than repeating them, so the two halves cannot drift apart.

[BITS 32]

; ---------------------------------------------------------------------------
; Low-memory block.  It sits below 64 KB (so a real-mode segment value of zero
; can address it) and clear of the areas the firmware itself keeps live: the
; BIOS data area at 0x400, the boot sector at 0x7C00, the firmware's own
; real-mode stack in the 0x6000s, and the EBDA at 0x9FC00.
;
; 0x8000 is chosen over the 0x3000s deliberately.  It is the top of the
; conventional-memory freelist, right below the EBDA, which is where a caller
; with no allocator has always put a real-mode trampoline; the 0x3000s are
; where the video BIOS keeps its own scratch, and a mode set that wrote there
; was followed by the VBE calls answering with the function number in AX as if
; they had never been handled at all.
; ---------------------------------------------------------------------------
RM_LOW_BASE  equ 0x8000

RM_REGS      equ RM_LOW_BASE + 0x000     ; INT 10h register file
RM_R_EAX     equ 0x00
RM_R_EBX     equ 0x04
RM_R_ECX     equ 0x08
RM_R_EDX     equ 0x0C
RM_R_ESI     equ 0x10
RM_R_EDI     equ 0x14
RM_R_EBP     equ 0x18
RM_R_EFLAGS  equ 0x1C
RM_R_DS      equ 0x20
RM_R_ES      equ 0x24
RM_R_INT     equ 0x26                    ; WORD: interrupt vector the stub
                                         ; executes. Zero (the state the VBE
                                         ; callers leave it in) keeps the
                                         ; historical INT 10h, so the video
                                         ; path needs no change; the boot-log
                                         ; flush fills in 0x13 for BIOS disk
                                         ; services.

RM_FAULT     equ RM_LOW_BASE + 0x028     ; the vector a BIOS call faulted on,
                                         ; 0xFF while nothing has
RM_STACK_SEG equ RM_LOW_BASE + 0x030     ; WORD: SS for the real-mode stub.  The
                                         ; C side picks a stack that is clear of
                                         ; live boot data (realmode_configure_stack)
                                         ; because a firmware USB disk stack can
                                         ; use several KB - far more than the
                                         ; 2.5 KB the old fixed 0x9F00 top left.
RM_STACK_PTR equ RM_LOW_BASE + 0x032     ; WORD: SP for the real-mode stub

RM_SAVED     equ RM_LOW_BASE + 0x040     ; callers' state to come back to
RM_S_ESP     equ 0x00
RM_S_GDT     equ 0x08                    ; GDTR as it was before the call
RM_S_IDT     equ 0x10                    ; IDTR as it was before the call
RM_S_PIC1    equ 0x18                    ; the 8259 masks, so the interrupts the
RM_S_PIC2    equ 0x19                    ; firmware holds off go back as they were

; The interrupt table real mode uses.  Base zero, limit 0x3FF: the IVT.
RM_IDTR      equ RM_LOW_BASE + 0x060
RM_IDT_LIMIT equ 0x03FF

RM_BUF       equ RM_LOW_BASE + 0x100     ; 768 bytes for the VBE info blocks
RM_STUB      equ RM_LOW_BASE + 0x400     ; the 16-bit stub is copied here
RM_STACK_TOP equ 0x9F00                  ; fallback stack top when nothing has
                                         ; configured RM_STACK_SEG/RM_STACK_PTR
                                         ; yet: the historical small stack, still
                                         ; below the EBDA and above the stub
RM_IVT_SAVE  equ RM_LOW_BASE + 0x800     ; 64-byte copy of the firmware's IVT
                                         ; (0x0000..0x003F), taken before the
                                         ; stub borrows the exception vectors
RM_IVT_FLAG  equ RM_IVT_SAVE + 0x040     ; BYTE: 1 once that copy is complete, so
                                         ; a fault during the copy itself cannot
                                         ; make the handler restore half an IVT

RM_SEL_CODE32 equ 0x10
RM_SEL_DATA32 equ 0x18
RM_SEL_CODE16 equ 0x20

section .text

global realmode_install_gdt
global realmode_call
global rm_stub16_start
global rm_stub16_end

; void realmode_install_gdt(void)
;
; Loads the kernel's own GDT (see the table at the bottom) and re-points every
; segment register at it.  Called once, before anything uses the bridge.
realmode_install_gdt:
    push eax

    ; A sane stack exists even if no caller configured one: the low block is
    ; ordinary memory, so its words can hold anything after the loader.  The
    ; configured size is kept when it is a legal one (1 KB..32 KB); anything
    ; else falls back to the historical top.
    mov ax, [RM_STACK_PTR]
    test ax, ax
    jz .stack_default
    cmp ax, 0x8000
    jbe .stack_done
.stack_default:
    mov word [RM_STACK_SEG], 0
    mov word [RM_STACK_PTR], RM_STACK_TOP
.stack_done:

    lgdt [rm_gdt_pointer]
    mov ax, RM_SEL_DATA32
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    jmp RM_SEL_CODE32:rm_gdt_reloaded
rm_gdt_reloaded:
    pop eax
    ret

; int realmode_call(void)
;
; Runs INT 10h with the register file at RM_REGS (filled in by the caller) and
; writes the BIOS's result registers back into it.  Returns 0.
realmode_call:
    push ebp
    mov ebp, esp
    pushad
    pushfd

    mov dword [RM_SAVED + RM_S_ESP], esp

    ; The kernel's own descriptor table pointers.  The stub has to point the
    ; interrupt table at the IVT to take an INT, and nothing on the way home
    ; puts the kernel's back: an interrupt taken after the call would then be
    ; delivered through the IVT's real-mode gates and fault the machine.  So
    ; both are saved here and restored on the far side.
    sgdt [RM_SAVED + RM_S_GDT]
    sidt [RM_SAVED + RM_S_IDT]

    ; Nothing may interrupt the hop out of protected mode.  Interrupts are off
    ; from here to the far side of the stub on every path, including the one
    ; that comes back after a fault.
    cli

    ; Mask both 8259s for the duration of the call.  The kernel remaps them, so
    ; an IRQ arrives on a vector the kernel's IDT serves - but the stub has the
    ; interrupt table pointed at the IVT, where that vector belongs to whatever
    ; the firmware keeps there.  The masks go back exactly as they were, which
    ; leaves anything that arrived meanwhile pending and serviced on the way
    ; out.
    in al, 0x21
    mov [RM_SAVED + RM_S_PIC1], al
    in al, 0xA1
    mov [RM_SAVED + RM_S_PIC2], al
    mov al, 0xFF
    out 0x21, al
    out 0xA1, al

    ; The real-mode stack was chosen by realmode_configure_stack() (a size
    ; and position that clear the multiboot data and the EBDA).  With nothing
    ; configured yet, keep the historical 0x9F00 top rather than running on
    ; whatever SS:SP the caller happened to have.
    cmp word [RM_STACK_PTR], 0
    jne .stack_ready
    mov word [RM_STACK_SEG], 0
    mov word [RM_STACK_PTR], RM_STACK_TOP
.stack_ready:

    ; the interrupt table the stub will use: the IVT, at the bottom of memory
    mov word [RM_IDTR], RM_IDT_LIMIT
    mov dword [RM_IDTR + 2], 0

    ; ---- put the 16-bit stub where real mode can reach it -------------
    cld
    mov esi, rm_stub16_start
    mov edi, RM_STUB
    mov ecx, rm_stub16_end - rm_stub16_start
    rep movsb

    ; ---- caller's interrupt vector into the copied `int imm8` ---------
    ; The operand byte of the stub's INT instruction is patched per call
    ; (the original blob in .text keeps INT 10h, so nothing here is
    ; permanent).  Zero means INT 10h: the VBE callers clear the whole
    ; register file, including this word, so they behave exactly as
    ; before the field existed.  INT 13h (0x13) is outside the borrowed
    ; exception range 0x00..0x0F, so it reaches the firmware's own disk
    ; services untouched, the same way INT 10h reaches the video BIOS.
    mov al, [RM_REGS + RM_R_INT]
    test al, al
    jnz .vector_chosen
    mov al, 0x10
.vector_chosen:
    mov [RM_STUB + (rm16_int_op - rm_stub16_start) + 1], al

    ; ---- switch to the 16-bit view -----------------------------------
    ; The 16-bit code descriptor is flat and 16-bit, so this lands on the
    ; stub's own address with 16-bit decoding; nothing else has to be set up
    ; here, because the stub loads every data segment itself once it is in
    ; real mode.
    jmp RM_SEL_CODE16:RM_STUB

; ---------------------------------------------------------------------------
; Back in protected mode, on the far side of the stub's return jump.
; ---------------------------------------------------------------------------
realmode_return:
    mov ax, RM_SEL_DATA32
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, [RM_SAVED + RM_S_ESP]

    ; Back exactly as we found it: the segments are loaded above, so the tables
    ; can go back to whatever the kernel had before the call - and the 8259
    ; masks with them, or the desktop would never see another interrupt.
    lgdt [RM_SAVED + RM_S_GDT]
    lidt [RM_SAVED + RM_S_IDT]
    mov al, [RM_SAVED + RM_S_PIC1]
    out 0x21, al
    mov al, [RM_SAVED + RM_S_PIC2]
    out 0xA1, al

    popfd
    popad
    pop ebp
    xor eax, eax
    ret

; ---------------------------------------------------------------------------
; The way home after a fault inside the firmware call.  Reached from the
; stub's own handler, so the machine is still on the bridge's GDT and nothing
; between the fault and here has been able to disturb the kernel's state.
; Returns 1: the caller must not read the register file as an answer.
; ---------------------------------------------------------------------------
realmode_faulted:
    mov ax, RM_SEL_DATA32
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, [RM_SAVED + RM_S_ESP]

    lgdt [RM_SAVED + RM_S_GDT]
    lidt [RM_SAVED + RM_S_IDT]
    mov al, [RM_SAVED + RM_S_PIC1]
    out 0x21, al
    mov al, [RM_SAVED + RM_S_PIC2]
    out 0xA1, al

    popfd
    popad
    pop ebp
    mov eax, 1
    ret

; ---------------------------------------------------------------------------
; The 16-bit half.  It is copied to RM_STUB before every call, so its operands
; are the fixed addresses above and its position in the blob does not matter.
; ---------------------------------------------------------------------------
[BITS 16]
section .text.rm16

rm_stub16_start:
    ; Leave protected mode.  The caches still hold the descriptors the switch
    ; loaded, so this runs with 16-bit decoding at the right place.
    mov eax, cr0
    and eax, 0xFFFFFFFE
    mov cr0, eax

    ; Re-enter at the same code with a real-mode far jump.  CS becomes an
    ; ordinary real-mode zero, which is what tells the CPU - and anything else
    ; that inspects the saved state - that this really is real mode.  The
    ; offset is absolute because CS is zero from here on.
    jmp word 0x0000:(RM_STUB + (rm16_real - rm_stub16_start))

rm16_real:
    ; The same for EVERY data segment: real-mode zeros, so the BIOS can save,
    ; restore and far-jump our state without ever seeing a protected selector.
    ;
    ; FS and GS are especially easy to miss because the original VBE callers
    ; do not use them.  Leaving either at the protected-mode data selector
    ; (0x18) leaves its protected descriptor cache live after CR0.PE is
    ; cleared.  SeaBIOS happens not to inspect those registers, but VMware's
    ; CSM and several physical CSM implementations do while handling INT 13h;
    ; their disk calls can then fail even though DS/ES/SS look like real mode.
    ; A firmware-visible real-mode call must therefore normalise all six data
    ; segment registers, not just the three this stub addresses directly.
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    ;
    ; ESP needs the same treatment even though only SP is named below.  A
    ; 16-bit load into SP leaves the upper half of ESP holding whatever the
    ; protected-mode caller left there, and the CPU does not clear it when
    ; CR0.PE goes to zero.  A BIOS written for 32-bit flat mode (SeaBIOS is)
    ; then pushes its register frame at the full 32-bit SS:ESP - far above the
    ; stack we reserved - while its own `movzx esp,sp` view of the frame is
    ; down at the real-mode SP.  It reads a frame nobody wrote, dispatches on
    ; the garbage function number and, in the worst case, sets a legacy video
    ; mode under the running desktop.  Zero the high half before SS is loaded,
    ; so the `mov ss` / `mov sp` pair below stays adjacent and interrupt-atomic.
    xor esp, esp
    mov ax, [RM_STACK_SEG]
    mov ss, ax
    mov sp, [RM_STACK_PTR]

    ; Save the firmware's own interrupt table before borrowing any of it.
    ;
    ; The stub installs its exception trampolines over vectors 0x00..0x0F and
    ; used to leave them there after the call.  That is not cosmetic: a
    ; USB-legacy keyboard is injected into real mode by calling the INT 09h
    ; handler found in this table, and a firmware INT 13h handler can chain
    ; through INT 08h for timing.  Leaving our trampolines behind replaced
    ; both with code that switches back to protected mode and returns into a
    ; stale stack - the machine then ignores every key (or hangs) even though
    ; the kernel itself is fine.  The copy is restored on both the normal and
    ; the fault path, so the firmware's real-mode world is exactly as found.
    mov byte [RM_IVT_FLAG], 0
    cld
    xor si, si
    mov di, RM_IVT_SAVE
    mov cx, 16
    rep movsd
    mov byte [RM_IVT_FLAG], 1

    ; Point the interrupt table at the IVT.  Clearing CR0.PE does not reload
    ; it, so without this the first INT still reads its vector out of whatever
    ; descriptor table was live in protected mode and lands in unrelated
    ; memory: the handler is never reached and the machine resets inside the
    ; delivery.  A bootloader's real-mode trampoline loads a real-mode IDT for
    ; exactly this reason.
    lidt [RM_IDTR]

    ; Borrow the CPU's exception vectors 0x00..0x0F for the duration of the
    ; call.
    ;
    ; A fault anywhere inside the firmware call - a bad descriptor it built, a
    ; divide it did, an invalid opcode in a card's ROM - is delivered through
    ; the IVT in real mode.  Without this it lands on whatever the firmware
    ; keeps at that vector, and a fault inside THAT is a triple fault: the
    ; machine resets, POSTs and beeps, with nothing on screen and nothing in
    ; the log.  Pointed at our own handler, the fault instead ends the call and
    ; is reported as a failure.
    ;
    ; The range stops at 0x0F on purpose.  In real mode vectors 0x10..0x1F are
    ; not exceptions at all: they are the BIOS's own services, and 0x10 among
    ; them is INT 10h - the very call this stub is about to make.  Taking that
    ; vector over pointed the call at our own handler, which looked exactly
    ; like a firmware fault and ended every call before the BIOS had run.
    ; Everything a card can fault on (#DE..#GP,#PF and the rest) is in
    ; 0x00..0x0F, and 0x08..0x0F being the IRQ vectors costs nothing here: the
    ; 8259s are masked and interrupts are off for the whole call.
    ;
    ; The entries are not saved: the IVT is only ever used while this stub is
    ; running (the kernel's own table goes back with lidt on the way out), and
    ; the next call writes these again.
    mov byte [RM_FAULT], 0xFF
    xor di, di
    mov si, RM_STUB + (rm16_vector_table - rm_stub16_start)
    mov cx, 16
rm16_set_vectors:
    mov ax, [si]
    add ax, RM_STUB
    mov [di], ax
    mov word [di + 2], 0
    add si, 2
    add di, 4
    loop rm16_set_vectors

    ; ES points at the buffer the BIOS may want to write.  The caller supplies
    ; it as a linear address, which for a real-mode segment of zero is already
    ; the offset.
    mov ax, [RM_REGS + RM_R_ES]
    mov es, ax

    mov eax, [RM_REGS + RM_R_EAX]
    mov ebx, [RM_REGS + RM_R_EBX]
    mov ecx, [RM_REGS + RM_R_ECX]
    mov edx, [RM_REGS + RM_R_EDX]
    mov esi, [RM_REGS + RM_R_ESI]
    mov edi, [RM_REGS + RM_R_EDI]
    mov ebp, [RM_REGS + RM_R_EBP]

rm16_int_op:
    int 0x10    ; the operand byte is patched per call by realmode_call
                ; (see the vector note there); the original stays 0x10 so
                ; the blob is intact for the next copy

    ; Result registers back into the file.  The low 16 bits of the flags are
    ; enough: VBE reports failure only through carry.  DS is reloaded first,
    ; because there is no promise that the call left it alone, and AX travels
    ; on the stack so that the reload cannot destroy the BIOS's answer.
    push ax
    pushf
    pop bp
    cli
    xor ax, ax
    mov ds, ax
    pop ax

    mov word [RM_REGS + RM_R_EFLAGS], bp
    mov dword [RM_REGS + RM_R_EAX], eax
    mov dword [RM_REGS + RM_R_EBX], ebx
    mov dword [RM_REGS + RM_R_ECX], ecx
    mov dword [RM_REGS + RM_R_EDX], edx
    mov dword [RM_REGS + RM_R_ESI], esi
    mov dword [RM_REGS + RM_R_EDI], edi
    mov dword [RM_REGS + RM_R_EBP], ebp
    mov ax, es
    mov word [RM_REGS + RM_R_ES], ax

    ; Hand the firmware its interrupt table back before leaving.  See the
    ; save above: the trampolines must not outlive the call.
    ;
    ; ES must be zeroed first.  A BIOS is free to return with any ES it
    ; likes, and this copy goes through ES:DI - with a firmware-chosen ES the
    ; restore would write the saved table somewhere else entirely and leave
    ; the trampolines in place, which is exactly the failure this restore
    ; exists to prevent.  (The save at the top is immune: ES is still zero
    ; there, because the caller's ES is loaded only just before the INT.)
    xor ax, ax
    mov es, ax
    cld
    mov si, RM_IVT_SAVE
    xor di, di
    mov cx, 16
    rep movsd
    mov byte [RM_IVT_FLAG], 0

    ; ---- straight back into protected mode ---------------------------
    ; CS still holds the real-mode zero loaded above; the far jump is what
    ; replaces it with a selector from the GDT, so it must be the very next
    ; control transfer after CR0.PE is set.
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    jmp dword RM_SEL_CODE32:realmode_return

; ---------------------------------------------------------------------------
; The exception trampolines the borrowed vectors point at, and the table the
; installer above walks to find them.
;
; The offsets come from the assembler, one entry per trampoline, rather than
; from arithmetic on a hand-counted stride.  A stride is a trap here: NASM
; encodes `jmp rm16_fault` as a short jump or a near one depending on distance,
; so a trampoline is four bytes or five - and a stride that does not match
; points the vectors at the middle of one.  That is not hypothetical: with the
; count off, IVT entry 0x10 landed on the trampoline for 0x14, so every call
; reported a fault on a vector nothing had raised.
; ---------------------------------------------------------------------------
rm16_vector_table:
%assign rm16_vector 0
%rep 16
    dw rm16_vec_%+rm16_vector - rm_stub16_start
%assign rm16_vector rm16_vector + 1
%endrep

%assign rm16_vector 0
%rep 16
rm16_vec_%+rm16_vector:
    mov al, rm16_vector
    jmp rm16_fault
%assign rm16_vector rm16_vector + 1
%endrep

; Each one tags itself with its own vector number and goes to the common
; handler; no vector is special-cased, so it does not matter which of them the
; CPU pushes an error code for - the handler ignores whatever the fault left on
; the stack.
rm16_fault:
    ; Nothing the fault left behind can be trusted, SS:SP included, so this
    ; establishes its own stack before it touches anything at all.  ESP's high
    ; half is as stale here as it is in rm16_real, so it is cleared first.
    mov bl, al
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    xor esp, esp
    mov ax, [RM_STACK_SEG]
    mov ss, ax
    mov sp, [RM_STACK_PTR]
    mov [RM_FAULT], bl

    ; Give the IVT back too, unless the fault happened while it was being
    ; copied (RM_IVT_FLAG is only set once the whole 64 bytes are there).
    cmp byte [RM_IVT_FLAG], 0
    je .ivt_done
    cld
    mov si, RM_IVT_SAVE
    xor di, di
    mov cx, 16
    rep movsd
    mov byte [RM_IVT_FLAG], 0
.ivt_done:

    ; ... and leaves real mode exactly the way the normal path does, only
    ; reporting the failure rather than carrying on with the call.
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    jmp dword RM_SEL_CODE32:realmode_faulted

rm_stub16_end:

; The IVT copy lives directly above the copied stub.  If the stub ever grows
; into it the save would overwrite running code; fail the build instead.
%if (rm_stub16_end - rm_stub16_start) > (RM_IVT_SAVE - RM_STUB)
%error "realmode stub no longer fits below the IVT save area"
%endif

; ---------------------------------------------------------------------------
; The bridge's GDT and the layout table the C side reads.
; ---------------------------------------------------------------------------
section .rodata
align 8
rm_gdt:
    ; 0x00 null
    dq 0x0000000000000000
    ; 0x08 null, as in the bootloader's table
    dq 0x0000000000000000
    ; 0x10 flat 32-bit code: base 0, 4 GB limit, D=1
    dq 0x00CF9A000000FFFF
    ; 0x18 flat 32-bit data: base 0, 4 GB limit, D=1
    dq 0x00CF92000000FFFF
    ; 0x20 16-bit code: base 0, 64 KB limit, D=0, so a far jump to the stub's
    ; own address gets 16-bit decoding without a second GDT.
    dq 0x00009A000000FFFF
rm_gdt_end:
rm_gdt_pointer:
    dw rm_gdt_end - rm_gdt - 1
    dd rm_gdt

align 4
global rm_layout
rm_layout:
    dd RM_LOW_BASE
    dd RM_REGS
    dd RM_SAVED
    dd RM_BUF
    dd RM_STUB
    dd RM_STACK_TOP
    dd RM_R_EAX
    dd RM_R_EBX
    dd RM_R_ECX
    dd RM_R_EDX
    dd RM_R_ESI
    dd RM_R_EDI
    dd RM_R_EBP
    dd RM_R_EFLAGS
    dd RM_R_DS
    dd RM_R_ES
    dd RM_FAULT
    dd RM_R_INT
    dd RM_STACK_SEG
    dd RM_STACK_PTR
    dd RM_IVT_FLAG

section .note.GNU-stack noalloc noexec nowrite progbits
