#include "safehook.h"
#include "reflector.h"

EXTERN_C void hookDispatch();

void safehook::add_hook(uint64_t hook_addr, uint64_t proxy_func)
{
	uint32_t count = hooklist_.size();
	safehook::HookInfo hi;
	hi.id = count + 1;
	hi.hook_addr = hook_addr;
	hi.proxy_addr = proxy_func;
	hooklist_.push_back(hi);
}

safehook::HookInfo safehook::get_hook_info(uint64_t proxy_func)
{
	for (auto info : hooklist_)
	{
		if (info.proxy_addr == proxy_func)
		{
			return info;
		}
	}
	return HookInfo();
}

uint32_t safehook::get_patch_len(uint64_t addr)
{
	uint32_t LenCount = 0, Len = 0;
	while (LenCount <= 5)        //至少需要5字节
	{
		Len = GetCodeLength((PUCHAR)addr, 64);
		addr = addr + Len;
		LenCount = LenCount + Len;
	}
	return LenCount;
}

uint64_t safehook::alloc_mem_nearby(uint64_t addr, uint32_t Size)
{

	ULONG64 A = addr / 65536;
	ULONG64 AllocPtr = A * 65536;
	BOOL Direc = FALSE;
	ULONG64 Increase = 0;
	ULONG64 AllocBase = 0;
	do
	{
		AllocBase = (ULONG64)VirtualAlloc((PVOID64)AllocPtr, Size, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
		if (AllocBase == 0)
		{
			if (Direc == FALSE)
			{
				if (addr + 2147483642 >= AllocPtr)
				{
					Increase = Increase + 65536;
				}
				else
				{
					Increase = 0;
					Direc = TRUE;
				}
			}
			else
			{
				if (addr - 2147483642 <= AllocPtr)
				{
					Increase = Increase - 65536;
				}
				else
				{
					return 0;
				}
			}

			AllocPtr = AllocPtr + Increase;
		}


	} while (AllocBase == 0);

	return AllocBase;
}

uint32_t safehook::hook()
{
	if (hooklist_.empty())
	{
		return 0;
	}

	// 获取分发函数的字节长度
	if (dispatch_func_length_ <= 0)
	{
		InitializeReflector();
		dispatch_func_length_ = 0;
		uint8_t* start = reinterpret_cast<uint8_t*>(hookDispatch);
		while (*start++ != 0xC3)
		{
			dispatch_func_length_++;
		}

		if (dispatch_func_length_ <= 0)
		{
			return 0;
		}

	}

	uint32_t hook_ok = 0;
	for (auto& hi : hooklist_)
	{
		// 申请分发函数的内存地址
		uint64_t lpMem = alloc_mem_nearby(hi.hook_addr, 0x1000);

		hi.disp_addr = lpMem;

		memset((PVOID)lpMem, 0, 0x1000);
		if (lpMem <= NULL)
		{
			continue;
		}
		memcpy((PVOID)lpMem, hookDispatch, dispatch_func_length_);
		for (int i = 0; i < dispatch_func_length_; i++)
		{
			// 填充目标执行函数
			if (*(uint64_t*)(lpMem + i) == 0x1212121212121212)
			{
				*(uint64_t*)(lpMem + i) = hi.proxy_addr;
			}
		}

		// 获取需要patch的字节
		uint32_t need_patch_size = get_patch_len(hi.hook_addr);
		hi.patch_len = min(need_patch_size, 20);
		memmove_s(hi.patch_opcode, hi.patch_len, (PVOID)(hi.hook_addr), hi.patch_len);
		// 生成shellcode
		ReflectCode((PUCHAR)hi.hook_addr, need_patch_size, (PUCHAR)(lpMem + dispatch_func_length_), need_patch_size);

		// hook
		DWORD old{};
		char opcode[] = { 0xE8,0x00,0x00,0x00,0x00 };
		if (VirtualProtect((PVOID)hi.hook_addr, 0x100, PAGE_EXECUTE_READWRITE, &old))
		{
			*(uint32_t*)&opcode[1] = lpMem - hi.hook_addr - 5;
			memcpy((PVOID)hi.hook_addr, opcode, sizeof(opcode));
			VirtualProtect((PVOID)hi.hook_addr, 0x100, old, NULL);
		}

		hook_ok++;
	}
	return hook_ok;
}

bool safehook::unhook(uint64_t proxy_func)
{
	BOOL status = TRUE;
	int i = 0;
	for (; i < hooklist_.size(); i++)
	{
		if (hooklist_[i].proxy_addr == proxy_func)
		{
			// 恢复原代码
			DWORD old{};
			if (VirtualProtect((PVOID)hooklist_[i].hook_addr, 0x100, PAGE_EXECUTE_READWRITE, &old))
			{
				memcpy((PVOID)hooklist_[i].hook_addr, hooklist_[i].patch_opcode, hooklist_[i].patch_len);
				VirtualProtect((PVOID)hooklist_[i].hook_addr, 0x100, old, NULL);
				//status = VirtualFree((PVOID)hooklist_[i].disp_addr, 0x1000, MEM_COMMIT | MEM_RESERVE);
				hooklist_.erase(hooklist_.begin() + i);
				break;
			}

		}
	}

	return status;
}
