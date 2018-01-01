#ifndef _MONO_PROFILER_H_
#define _MONO_PROFILER_H_

#include <stdio.h>
#include <stdlib.h>
#include <windows.h>

#define EXPORT_API __declspec(dllexport)

extern "C"
{
	EXPORT_API void Init(const char *szMonoModuleName);
	EXPORT_API void Clear(void);
	EXPORT_API void Dump(const char *szDumpFileName, bool bDetails);
}

#endif
