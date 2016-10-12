#ifndef _MONO_PROFILER_H_
#define _MONO_PROFILER_H_

#include <stdio.h>
#include <stdlib.h>
#include <windows.h>

#define EXPORT_API __declspec(dllexport)

extern "C"
{
	EXPORT_API void Init(const char *szMonoModuleName);
	EXPORT_API void Pause(void);
	EXPORT_API void Resume(void);
	EXPORT_API void Clear(void);
	EXPORT_API void Dump(const char *szDumpFileName);

	EXPORT_API void BeginMethodIterator(void);
	EXPORT_API bool NextMethodIterator(void);
	EXPORT_API void EndMethodIterator(void);

	EXPORT_API const char* GetMethodName(void);
	EXPORT_API const char* GetMethodCallStack(void);
	EXPORT_API unsigned int GetMethodAllocSize(void);
	EXPORT_API unsigned int GetMethodAllocSizeDelta(void);

	EXPORT_API void BeginObjectIterator(void);
	EXPORT_API bool NextObjectIterator(void);
	EXPORT_API void EndObjectIterator(void);

	EXPORT_API const char* GetObjectName(void);
	EXPORT_API unsigned int GetObjectAllocSize(void);
}

#endif
