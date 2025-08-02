#include <windows.h>
#include <stdio.h>
#include <node_api.h>
#include <delayimp.h>
#include <array>
#include "node.h"
#include "string.h"

#if 1
extern "C" const IMAGE_DOS_HEADER __ImageBase;
template<class T> const T* fromRva(RVA rva) { return (const T*)((const char*)&__ImageBase + rva); }

extern "C" FARPROC WINAPI __delayLoadHelper2(const ImgDelayDescr *pidd, FARPROC *dest) {
	HMODULE hmod	= *fromRva<HMODULE>(pidd->rvaHmod);
	FARPROC func	= NULL;

	auto dllname	= fromRva<char>(pidd->rvaDLLName);

	if (_stricmp(dllname, "node.exe") == 0) {
		auto index = (const IMAGE_THUNK_DATA*)dest - fromRva<IMAGE_THUNK_DATA>(pidd->rvaIAT);
		auto entry = fromRva<IMAGE_THUNK_DATA>(pidd->rvaINT) + index;

		union {
			LPCSTR  name;
			DWORD   ordinal;
		} dlp;

		if (!IMAGE_SNAP_BY_ORDINAL(entry->u1.Ordinal))
			dlp.name	= fromRva<IMAGE_IMPORT_BY_NAME>(entry->u1.AddressOfData)->Name;
		else
			dlp.ordinal	= IMAGE_ORDINAL(entry->u1.Ordinal);

		hmod	= GetModuleHandle(NULL);
		func	= GetProcAddress(hmod, dlp.name);
		*dest	= func;
	}
	return func;
}
#else
static FARPROC WINAPI load_exe_hook(unsigned int event, DelayLoadInfo* info) {
	if (event == dliNotePreLoadLibrary && _stricmp(info->szDll, "node.exe") == 0)
		return (FARPROC)GetModuleHandle(NULL);
	return NULL;
}

decltype(__pfnDliNotifyHook2) __pfnDliNotifyHook2 = load_exe_hook;
#endif


auto HelloWorld() {
	return "Hello, World!";
}

//-----------------------------------------------------------------------------
// main
//-----------------------------------------------------------------------------

napi_value Init(napi_env env, napi_value exports) {
	Node::global_env	= env;

	Node::object(exports).defineProperties({
		{"HelloWorld", Node::function::make<HelloWorld>()},
	});

	//Node::object(Node::global["console"]).call("log", "hello", 32);
	//Node::number a(1), b(2);
	//auto c = a < b;

	return exports;
}

//NAPI_MODULE(NODE_GYP_MODULE_NAME, Init)

extern "C" {
	NAPI_MODULE_EXPORT int32_t NODE_API_MODULE_GET_API_VERSION(void) { return NAPI_VERSION; }
	NAPI_MODULE_EXPORT napi_value NAPI_MODULE_INITIALIZER(napi_env env, napi_value exports) { return Init(env, exports); }
}
