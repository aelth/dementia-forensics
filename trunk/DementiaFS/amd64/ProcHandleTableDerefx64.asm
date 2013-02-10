.code
ObDeReferenceProcessHandleTablex64 proc
	xor rax, rax
	xor rbx, rbx
	add rcx, rdx
	prefetchw [rcx]
    mov rax, [rcx]
    and rax, 0FFFFFFFFFFFFFFFEh
    lea rdx, [rax-2]
    lock cmpxchg [rcx], rdx
    jz noRundownProtect
    mov rbx, 1
noRundownProtect:
	mov rax, rbx
	ret
ObDeReferenceProcessHandleTablex64 endp
END