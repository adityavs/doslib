bits 16			; 16-bit protected mode

section text class=CODE

extern INITTASK
extern INITAPP

..start:	call far INITTASK
		; ^ input: AX=0 (or something else in Windows 2.x???)
		;          BX=stack size
		;          CX=heap size
		;          DX=unused
		;          SI=previous instance handle
		;          DI=instance handle
		;          BP=0
		;          DS=app data segment
		;          ES=selector of program segment prefix
		;          SS=app data segment
		;          SP=top of stack
		;
		;   output: AX=selector of program segment prefix or 0 on error
		;           BX=offset of command line
		;           CX=stack limit
		;           DX=nCmdShow (to be passed to WinMain)
		;           SI=previous instance handle
		;           DI=instance handle
		;           BP=top of stack
		;           DS=app data segment
		;           ES=selector of command line
		;           SS=app data segment (SS==DS)
		;           SP=edited top of stack
		or	ax,ax
		jz	_exit	; if AX==0

		push	di		; InitApp(hInstance)
		call far INITAPP
		or	ax,ax
		jz	_exit

_exit:		mov	ax,4C00h
		int	21h

segment data class=DATA

inittask_dgroup:	; DGROUP first 16 bytes used by Windows
		dw	0,0,5,0, 0,0,0,0
		; ^ According to WINE sources this is initialized and used during execution:
		;    WORD	0
		;    DWORD      Old SS:SP
		;    WORD       pointer to local heap info if any
		;    WORD       pointer to local atom table if any
		;    WORD       top of stack
		;    WORD       lowest stack addr so far
		;    WORD       bottom of the stsack
		;    = 8 WORDs
		;
		;   Now someone explain why it is initialized in the EXE image to "Old SS:SP" = 0005:0000....

segment bss
		; no uninit data

segment stack class=STACK

; for whatever reason, this is MANDATORY, or else the NE header will cause
; windows to call our code with a data segment consisting of the first bytes
; of this EXE! and INITTASK won't work. there MUST be a DGROUP! Probably
; a strange Watcom Linker bug too, who knows?
group dgroup data bss

