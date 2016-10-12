#include "_MonoProfiler.h"


#define ENABLE_LOG 0
#define OUTPUT_FILENAME "x:\\MonoProfiler.txt"

#if ENABLE_LOG == 1
#   define LOG(...) DebugOut(__VA_ARGS__)
#else
#   define LOG(...) 
#endif


struct MethodAllocation {
	MonoMethod* method;
	unsigned int allocated;
	unsigned int allocatedLast;

	std::string stack;
	std::map<MonoClass*, guint> objects;
};

typedef std::map<DWORD, std::stack<MonoMethod*>> MonoMethodStackMap;
typedef std::map<DWORD, MethodAllocation* > MonoMethodAllocationMap;
typedef std::map<MonoClass*, guint> MonoObjectAllocationMap;

typedef void(*MonoProfileFunc)(MonoProfiler *prof);
typedef void(*MonoProfileMethodFunc)(MonoProfiler *prof, MonoMethod *method);
typedef void(*MonoProfileGCFunc)(MonoProfiler *prof, MonoGCEvent event, int generation);
typedef void(*MonoProfileGCResizeFunc)(MonoProfiler *prof, gint64 new_size);
typedef void(*MonoProfileAllocFunc)(MonoProfiler *prof, MonoObject *obj, MonoClass *klass);


typedef void(*MonoProfilerInstallFunc)(MonoProfiler *prof, MonoProfileFunc callback);
typedef void(*MonoProfilerInstallEnterLeaveFunc)(MonoProfileMethodFunc enter, MonoProfileMethodFunc fleave);
typedef void(*MonoProfilerSetEventsFunc)(MonoProfileFlags events);
typedef void(*MonoProfilerInstallGCFunc)(MonoProfileGCFunc callback, MonoProfileGCResizeFunc heap_resize_callback);
typedef void(*MonoProfilerInstallAllocation)(MonoProfileAllocFunc callback);
typedef guint(*MonoObjectGetSize)(MonoObject* o);


static PRTL_CRITICAL_SECTION mutex = NULL;

static bool bPause = true;
static bool bMethodIterator = false;
static bool bObjectIterator = false;
static MonoProfiler monoProfiler;
static MonoMethodStackMap monoMethodStacks;
static MonoMethodAllocationMap monoMethodAllocations;
static MonoMethodAllocationMap::const_iterator itMethod;
static MonoObjectAllocationMap::const_iterator itObject;

static MonoProfilerInstallFunc mono_profiler_install = NULL;
static MonoProfilerInstallEnterLeaveFunc mono_profiler_install_enter_leave = NULL;
static MonoProfilerSetEventsFunc mono_profiler_set_events = NULL;
static MonoProfilerInstallGCFunc mono_profiler_install_gc = NULL;
static MonoProfilerInstallAllocation mono_profiler_install_allocation = NULL;
static MonoObjectGetSize mono_object_get_size = NULL;


//
// ����Hash
//
static DWORD HashValue(const char *szString)
{
	const char *c = szString;
	DWORD dwHashValue = 0x00000000;

	while (*c) {
		dwHashValue = (dwHashValue << 5) - dwHashValue + (*c == '/' ? '\\' : *c);
		c++;
	}

	return dwHashValue;
}

//
// �������
//
static void DebugOut(const char *szFormat, ...)
{
	va_list vaList;
	va_start(vaList, szFormat);
	{
		char szBuffer[1024];
		vsprintf(szBuffer, szFormat, vaList);

		if (FILE *pFile = fopen(OUTPUT_FILENAME, "a+")) {
			fprintf(pFile, szBuffer);
			fclose(pFile);
		}

		printf(szBuffer);
	}
	va_end(vaList);
}

//
// ���Թرջص�
//
static void profiler_shutdown(MonoProfiler *prof)
{
	LOG("profiler_shutdown\n");
}

//
// GC�¼��ص�
//
static void gc_event(MonoProfiler *prof, MonoGCEvent event, int generation)
{
	LOG("gc_event\n");
}

//
// GC�����ص�
//
static void gc_resize(MonoProfiler *prof, gint64 new_size)
{
	LOG("gc_resize %d\n", new_size);
}

//
// ������ʼ���ûص�
//
static void sample_method_enter(MonoProfiler *prof, MonoMethod *method)
{
	EnterCriticalSection(mutex);
	{
		DWORD threadid = GetCurrentThreadId();
		monoMethodStacks[threadid].push(method);
		LOG("method_enter %s.%s\n", method->klass->name, method->name);
	}
	LeaveCriticalSection(mutex);
}

//
// �����������ûص�
//
static void sample_method_leave(MonoProfiler *prof, MonoMethod *method)
{
	EnterCriticalSection(mutex);
	{
		DWORD threadid = GetCurrentThreadId();
		if (monoMethodStacks[threadid].empty() == false) {
			monoMethodStacks[threadid].pop();
		}
		LOG("method_leave %s.%s\n", method->klass->name, method->name);
	}
	LeaveCriticalSection(mutex);
}

//
// �ڴ����ص�
//
static void simple_allocation(MonoProfiler *prof, MonoObject *obj, MonoClass *klass)
{
	EnterCriticalSection(mutex);
	{
		if (bPause == false) {
			DWORD threadid = GetCurrentThreadId();
			guint size = mono_object_get_size(obj);

			if (monoMethodStacks[threadid].empty() == false) {
				std::string stack = "";
				std::stack<MonoMethod*> call = monoMethodStacks[threadid];
				do {
					MonoMethod *method = call.top(); call.pop();

					static char szBuffer[1024];
					sprintf(szBuffer, "%s\n%s.%s", stack.c_str(), method->klass->name, method->name);
					stack = szBuffer;
				} while (call.empty() == false);

				DWORD dwHash = HashValue(stack.c_str());
				MethodAllocation *pMethodAllocation = monoMethodAllocations[dwHash];

				if (pMethodAllocation == NULL) {
					pMethodAllocation = new MethodAllocation;
					pMethodAllocation->stack = stack;
					pMethodAllocation->method = monoMethodStacks[threadid].top();
					pMethodAllocation->allocated = 0;
					pMethodAllocation->allocatedLast = 0;
					monoMethodAllocations[dwHash] = pMethodAllocation;
				}

				pMethodAllocation->allocated += size;
				pMethodAllocation->objects[klass] += size;
			}

			LOG("allocation %s: %d\n", klass->name, size);
		}
	}
	LeaveCriticalSection(mutex);
}

//
// ��ʼ��������
//
EXPORT_API void Init(const char *szMonoModuleName)
{
	//
	// 1. ��ʼ��������
	//
	if (mutex == NULL) {
		mutex = (PRTL_CRITICAL_SECTION)malloc(sizeof(RTL_CRITICAL_SECTION));
		memset(mutex, 0, sizeof(RTL_CRITICAL_SECTION));
		InitializeCriticalSection(mutex);
	}

	EnterCriticalSection(mutex);
	{
		bPause = true;
		bMethodIterator = false;
		bObjectIterator = false;

		if (HMODULE hMonoLibrary = LoadLibrary(szMonoModuleName)) {
			mono_profiler_install = (MonoProfilerInstallFunc)GetProcAddress(hMonoLibrary, "mono_profiler_install");
			mono_profiler_install_enter_leave = (MonoProfilerInstallEnterLeaveFunc)GetProcAddress(hMonoLibrary, "mono_profiler_install_enter_leave");
			mono_profiler_set_events = (MonoProfilerSetEventsFunc)GetProcAddress(hMonoLibrary, "mono_profiler_set_events");
			mono_profiler_install_gc = (MonoProfilerInstallGCFunc)GetProcAddress(hMonoLibrary, "mono_profiler_install_gc");
			mono_profiler_install_allocation = (MonoProfilerInstallAllocation)GetProcAddress(hMonoLibrary, "mono_profiler_install_allocation");
			mono_object_get_size = (MonoObjectGetSize)GetProcAddress(hMonoLibrary, "mono_object_get_size");

			mono_profiler_install(&monoProfiler, profiler_shutdown);
			mono_profiler_install_gc(gc_event, gc_resize);
			mono_profiler_install_enter_leave(sample_method_enter, sample_method_leave);
			mono_profiler_install_allocation(simple_allocation);
			mono_profiler_set_events((MonoProfileFlags)(MONO_PROFILE_ALLOCATIONS | MONO_PROFILE_GC | MONO_PROFILE_ENTER_LEAVE));
		}
		else {
			LOG("Init mono profiler fail!!!\n");
		}
	}
	LeaveCriticalSection(mutex);

	//
	// 2. ��ռ�¼����Ϣ
	//
	Clear();
	remove(OUTPUT_FILENAME);
}

//
// ��ͣ
//
EXPORT_API void Pause(void)
{
	EnterCriticalSection(mutex);
	{
		bPause = true;
	}
	LeaveCriticalSection(mutex);
}

//
// ����
//
EXPORT_API void Resume(void)
{
	EnterCriticalSection(mutex);
	{
		bPause = false;
	}
	LeaveCriticalSection(mutex);
}

//
// ��ռ�¼����Ϣ
//
EXPORT_API void Clear(void)
{
	EnterCriticalSection(mutex);
	{
		for (MonoMethodStackMap::iterator it = monoMethodStacks.begin(); it != monoMethodStacks.end(); ++it) {
			while (it->second.empty() == false) it->second.pop();
		}

		for (MonoMethodAllocationMap::iterator it = monoMethodAllocations.begin(); it != monoMethodAllocations.end(); ++it) {
			delete it->second;
		}

		monoMethodStacks.clear();
		monoMethodAllocations.clear();
	}
	LeaveCriticalSection(mutex);
}

//
// Dump
//
EXPORT_API void Dump(const char *szDumpFileName)
{
	EnterCriticalSection(mutex);
	{
		if (FILE *pFile = fopen(szDumpFileName, "wb")) {
			for (MonoMethodAllocationMap::const_iterator it = monoMethodAllocations.begin(); it != monoMethodAllocations.end(); ++it) {
				fprintf(pFile, "%s.%s\t\t%d\n", it->second->method->klass->name, it->second->method->name, it->second->allocated);
			}

			fclose(pFile);
		}
	}
	LeaveCriticalSection(mutex);
}

//
// ��ʼ����������
//
EXPORT_API void BeginMethodIterator(void)
{
	if (bMethodIterator == false) {
		Pause();

		EnterCriticalSection(mutex);
		{
			itMethod = monoMethodAllocations.begin();
			bMethodIterator = true;
		}
		LeaveCriticalSection(mutex);
	}
}

//
// ��������
//
EXPORT_API bool NextMethodIterator(void)
{
	bool bNext = false;

	if (bMethodIterator) {
		Pause();

		EnterCriticalSection(mutex);
		{
			if (itMethod != monoMethodAllocations.end()) {
				bNext = ++itMethod != monoMethodAllocations.end();
			}
		}
		LeaveCriticalSection(mutex);
	}

	return bNext;
}

//
// ��ֹ����������
//
EXPORT_API void EndMethodIterator(void)
{
	if (bMethodIterator) {
		Pause();

		EnterCriticalSection(mutex);
		{
			itMethod = monoMethodAllocations.end();
			bMethodIterator = false;
		}
		LeaveCriticalSection(mutex);
	}
}

//
// ��÷�����
//
EXPORT_API const char* GetMethodName(void)
{
	static char szMethodName[1024];
	memset(szMethodName, 0, sizeof(szMethodName));

	if (bMethodIterator) {
		EnterCriticalSection(mutex);
		{
			if (itMethod != monoMethodAllocations.end()) {
				sprintf(szMethodName, "%s.%s", itMethod->second->method->klass->name, itMethod->second->method->name);
			}
		}
		LeaveCriticalSection(mutex);
	}

	return szMethodName;
}

//
// ��÷�������ջ
//
EXPORT_API const char* GetMethodCallStack(void)
{
	static char szMethodCallStack[1024];
	memset(szMethodCallStack, 0, sizeof(szMethodCallStack));

	if (bMethodIterator) {
		EnterCriticalSection(mutex);
		{
			if (itMethod != monoMethodAllocations.end()) {
				sprintf(szMethodCallStack, "%s", itMethod->second->stack.c_str());
			}
		}
		LeaveCriticalSection(mutex);
	}

	return szMethodCallStack;
}

//
// ��÷���������ڴ���
//
EXPORT_API unsigned int GetMethodAllocSize(void)
{
	unsigned int size = 0;

	if (bMethodIterator) {
		EnterCriticalSection(mutex);
		{
			if (itMethod != monoMethodAllocations.end()) {
				size = itMethod->second->allocated;
			}
		}
		LeaveCriticalSection(mutex);
	}

	return size;
}

//
// ��÷�������仯���ڴ���
//
EXPORT_API unsigned int GetMethodAllocSizeDelta(void)
{
	unsigned int size = 0;

	if (bMethodIterator) {
		EnterCriticalSection(mutex);
		{
			if (itMethod != monoMethodAllocations.end()) {
				size = itMethod->second->allocated - itMethod->second->allocatedLast;
				itMethod->second->allocatedLast = itMethod->second->allocated;
			}
		}
		LeaveCriticalSection(mutex);
	}

	return size;
}

//
// ��ʼ���������
//
EXPORT_API void BeginObjectIterator(void)
{
	if (bMethodIterator) {
		if (bObjectIterator == false) {
			Pause();

			EnterCriticalSection(mutex);
			{
				if (itMethod != monoMethodAllocations.end()) {
					itObject = itMethod->second->objects.begin();
					bObjectIterator = true;
				}
			}
			LeaveCriticalSection(mutex);
		}
	}
}

//
// �������
//
EXPORT_API bool NextObjectIterator(void)
{
	bool bNext = false;

	if (bMethodIterator) {
		if (bObjectIterator) {
			Pause();

			EnterCriticalSection(mutex);
			{
				if (itMethod != monoMethodAllocations.end()) {
					if (itObject != itMethod->second->objects.end()) {
						bNext = ++itObject != itMethod->second->objects.end();
					}
				}
			}
			LeaveCriticalSection(mutex);
		}
	}

	return bNext;
}

//
// ��ֹ���������
//
EXPORT_API void EndObjectIterator(void)
{
	if (bMethodIterator) {
		if (bObjectIterator) {
			Pause();

			EnterCriticalSection(mutex);
			{
				if (itMethod != monoMethodAllocations.end()) {
					itObject = itMethod->second->objects.end();
					bObjectIterator = false;
				}
			}
			LeaveCriticalSection(mutex);
		}
	}
}

//
// ��ö�����
//
EXPORT_API const char* GetObjectName(void)
{
	static char szObjectName[1024];
	memset(szObjectName, 0, sizeof(szObjectName));

	if (bMethodIterator) {
		if (bObjectIterator) {
			EnterCriticalSection(mutex);
			{
				if (itObject != itMethod->second->objects.end()) {
					sprintf(szObjectName, "%s", itObject->first->name);
				}
			}
			LeaveCriticalSection(mutex);
		}
	}

	return szObjectName;
}

//
// ��ö��������ڴ���
//
EXPORT_API unsigned int GetObjectAllocSize(void)
{
	unsigned int size = 0;

	if (bMethodIterator) {
		if (bObjectIterator) {
			EnterCriticalSection(mutex);
			{
				if (itObject != itMethod->second->objects.end()) {
					size = itObject->second;
				}
			}
			LeaveCriticalSection(mutex);
		}
	}

	return size;
}
