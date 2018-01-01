#include "_MonoProfiler.h"


#define LOG DebugOut


typedef struct AllocationSample {
	AllocationSample(const char *_name)
		: name{ 0 }
		, dwCount(0)
		, dwMemorySize(0)
	{
		strcpy(name, _name);
	}

	char name[260];

	DWORD dwCount;
	DWORD dwMemorySize;
} AllocationSample;

typedef struct MethodSample {
	MethodSample(const char *_name)
		: pParent(NULL)
		, name{ 0 }
		, dwTime(0)
		, dwCount(0)
		, dwMemorySize(0)
		, dwTick(0)
	{
		strcpy(name, _name);
	}

	MethodSample *pParent;

	char name[260];

	DWORD dwTick;
	DWORD dwTime;
	DWORD dwCount;
	DWORD dwMemorySize;

	std::map<DWORD, AllocationSample*> alloctions;
} MethodSample;

typedef std::map<DWORD, std::stack<std::string>> MethodStackMap; // [ThreadID, Method Stack]
typedef std::map<DWORD, std::map<DWORD, MethodSample*>> MethodSampleMap; // [ThreadID, Method Stack Hash, Method Sample]


typedef void(*MonoProfileMethodFunc)(MonoProfiler *prof, MonoMethod *method);
typedef void(*MonoProfileGCFunc)(MonoProfiler *prof, MonoGCEvent event, int generation);
typedef void(*MonoProfileGCResizeFunc)(MonoProfiler *prof, gint64 new_size);
typedef void(*MonoProfileAllocFunc)(MonoProfiler *prof, MonoObject *obj, MonoClass *klass);


typedef void(*MonoProfilerInstallEnterLeaveFunc)(MonoProfileMethodFunc enter, MonoProfileMethodFunc leave);
typedef void(*MonoProfilerSetEventsFunc)(MonoProfileFlags events);
typedef void(*MonoProfilerInstallGCFunc)(MonoProfileGCFunc callback, MonoProfileGCResizeFunc heap_resize_callback);
typedef void(*MonoProfilerInstallAllocation)(MonoProfileAllocFunc callback);
typedef guint(*MonoObjectGetSize)(MonoObject* o);


static PRTL_CRITICAL_SECTION mutex = NULL;

static bool bPause = true;
static MethodStackMap methodStacks;
static MethodSampleMap methodSamples;

static MonoProfilerInstallEnterLeaveFunc mono_profiler_install_enter_leave = NULL;
static MonoProfilerSetEventsFunc mono_profiler_set_events = NULL;
static MonoProfilerInstallGCFunc mono_profiler_install_gc = NULL;
static MonoProfilerInstallAllocation mono_profiler_install_allocation = NULL;
static MonoObjectGetSize mono_object_get_size = NULL;


static unsigned int tick(void)
{
	LARGE_INTEGER freq;
	LARGE_INTEGER count;

	QueryPerformanceFrequency(&freq);
	QueryPerformanceCounter(&count);

	return (unsigned int)(((double)count.QuadPart / freq.QuadPart) * 1000000);
}

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

static void DebugOut(const char *szFormat, ...)
{
	va_list vaList;
	va_start(vaList, szFormat);
	{
		char szBuffer[1024];
		vsprintf(szBuffer, szFormat, vaList);
		OutputDebugString(szBuffer);
	}
	va_end(vaList);
}

static DWORD GetMethodStackHash(DWORD dwThreadID)
{
	if (methodStacks.find(dwThreadID) == methodStacks.end()) {
		return 0;
	}

	std::string name = "";
	std::stack<std::string> methods = methodStacks[dwThreadID];

	while (methods.empty() == false) {
		name += methods.top();
		methods.pop();
	}

	return HashValue(name.c_str());
}

static void gc_event(MonoProfiler *prof, MonoGCEvent event, int generation)
{
	LOG("gc_event\n");
}

static void gc_resize(MonoProfiler *prof, gint64 new_size)
{
	LOG("gc_resize %d\n", new_size);
}

static void sample_method_enter(MonoProfiler *prof, MonoMethod *method)
{
	if (bPause) {
		return;
	}

	EnterCriticalSection(mutex);
	{
		char name[260];
		sprintf(name, "%s::%s::%s", method->klass->name_space, method->klass->name, method->name);

		DWORD dwThreadID = GetCurrentThreadId();
		DWORD dwParentMethod = GetMethodStackHash(dwThreadID); methodStacks[dwThreadID].push(name);
		DWORD dwCurrentMethod = GetMethodStackHash(dwThreadID);

		if (methodSamples[dwThreadID][dwCurrentMethod] == NULL) {
			methodSamples[dwThreadID][dwCurrentMethod] = new MethodSample(name);
			methodSamples[dwThreadID][dwCurrentMethod]->pParent = methodSamples[dwThreadID].find(dwParentMethod) != methodSamples[dwThreadID].end() ? methodSamples[dwThreadID][dwParentMethod] : NULL;
		}

		methodSamples[dwThreadID][dwCurrentMethod]->dwTick = tick();
		methodSamples[dwThreadID][dwCurrentMethod]->dwCount++;
	}
	LeaveCriticalSection(mutex);
}

static void sample_method_leave(MonoProfiler *prof, MonoMethod *method)
{
	if (bPause) {
		return;
	}

	EnterCriticalSection(mutex);
	{
		char name[260];
		sprintf(name, "%s::%s::%s", method->klass->name_space, method->klass->name, method->name);

		DWORD dwThreadID = GetCurrentThreadId();
		DWORD dwCurrentMethod = GetMethodStackHash(dwThreadID);

		if (methodSamples.find(dwThreadID) != methodSamples.end()) {
			if (methodStacks[dwThreadID].empty() == false && methodStacks[dwThreadID].top() == name) {
				methodStacks[dwThreadID].pop();
			}

			if (methodSamples[dwThreadID].find(dwCurrentMethod) != methodSamples[dwThreadID].end()) {
				methodSamples[dwThreadID][dwCurrentMethod]->dwTime += tick() - methodSamples[dwThreadID][dwCurrentMethod]->dwTick;
			}
		}
	}
	LeaveCriticalSection(mutex);
}

static void sample_allocation(MonoProfiler *prof, MonoObject *obj, MonoClass *klass)
{
	if (bPause) {
		return;
	}

	EnterCriticalSection(mutex);
	{
		char name[260];
		sprintf(name, "%s::%s", klass->name_space, klass->name);

		DWORD dwThreadID = GetCurrentThreadId();
		DWORD dwCurrentMethod = GetMethodStackHash(dwThreadID);

		DWORD dwObjectName = HashValue(name);
		DWORD dwObjectSize = mono_object_get_size(obj);

		if (methodSamples.find(dwThreadID) != methodSamples.end() && 
			methodSamples[dwThreadID].find(dwCurrentMethod) != methodSamples[dwThreadID].end()) {
			if (methodSamples[dwThreadID][dwCurrentMethod]->alloctions[dwObjectName] == NULL) {
				methodSamples[dwThreadID][dwCurrentMethod]->alloctions[dwObjectName] = new AllocationSample(name);
				methodSamples[dwThreadID][dwCurrentMethod]->alloctions[dwObjectName]->dwMemorySize = dwObjectSize;
			}

			methodSamples[dwThreadID][dwCurrentMethod]->dwMemorySize += dwObjectSize;
			methodSamples[dwThreadID][dwCurrentMethod]->alloctions[dwObjectName]->dwCount++;
		}
	}
	LeaveCriticalSection(mutex);
}

EXPORT_API void Init(const char *szMonoModuleName)
{
	if (mutex == NULL) {
		mutex = (PRTL_CRITICAL_SECTION)malloc(sizeof(RTL_CRITICAL_SECTION));
		memset(mutex, 0, sizeof(RTL_CRITICAL_SECTION));
		InitializeCriticalSection(mutex);
	}

	EnterCriticalSection(mutex);
	{
		if (HMODULE hMonoLibrary = LoadLibrary(szMonoModuleName)) {
			mono_profiler_install_enter_leave = (MonoProfilerInstallEnterLeaveFunc)GetProcAddress(hMonoLibrary, "mono_profiler_install_enter_leave");
			mono_profiler_set_events = (MonoProfilerSetEventsFunc)GetProcAddress(hMonoLibrary, "mono_profiler_set_events");
			mono_profiler_install_gc = (MonoProfilerInstallGCFunc)GetProcAddress(hMonoLibrary, "mono_profiler_install_gc");
			mono_profiler_install_allocation = (MonoProfilerInstallAllocation)GetProcAddress(hMonoLibrary, "mono_profiler_install_allocation");
			mono_object_get_size = (MonoObjectGetSize)GetProcAddress(hMonoLibrary, "mono_object_get_size");

			mono_profiler_install_gc(gc_event, gc_resize);
			mono_profiler_install_enter_leave(sample_method_enter, sample_method_leave);
			mono_profiler_install_allocation(sample_allocation);
			mono_profiler_set_events((MonoProfileFlags)(MONO_PROFILE_ALLOCATIONS | MONO_PROFILE_GC | MONO_PROFILE_ENTER_LEAVE));
		}
		else {
			LOG("Init mono profiler fail!!!\n");
		}
	}
	LeaveCriticalSection(mutex);

	Clear();

	bPause = false;
}

EXPORT_API void Clear(void)
{
	bPause = true;

	EnterCriticalSection(mutex);
	{
		for (const auto &itThreadMethodSamples : methodSamples) {
			for (const auto &itMethodSample : itThreadMethodSamples.second) {
				if (itMethodSample.second) {
					for (const auto &itAllocationSample : itMethodSample.second->alloctions) {
						if (itAllocationSample.second) {
							delete itAllocationSample.second;
						}
					}
					delete itMethodSample.second;
				}
			}
		}

		methodStacks.clear();
		methodSamples.clear();
	}
	LeaveCriticalSection(mutex);
}

EXPORT_API void Dump(const char *szDumpFileName, bool bDetails)
{
	EnterCriticalSection(mutex);
	{
		std::map<DWORD, std::vector<MethodSample*>> methodSampleByTime;
		std::map<DWORD, std::vector<MethodSample*>> methodSampleByMemory;

		for (const auto &itThreadMethodSamples : methodSamples) {
			for (const auto &itMethodSample : itThreadMethodSamples.second) {
				if (itMethodSample.second) {
					if (itMethodSample.second->dwTime > 0) {
						methodSampleByTime[itMethodSample.second->dwTime].push_back(itMethodSample.second);
					}
					if (itMethodSample.second->dwMemorySize > 0) {
						methodSampleByMemory[itMethodSample.second->dwMemorySize].push_back(itMethodSample.second);
					}
				}
			}
		}

		TiXmlDocument doc;
		TiXmlElement *pReportNode = new TiXmlElement("Report");
		{
			TiXmlElement *pTimeNode = new TiXmlElement("Time");
			{
				for (std::map<DWORD, std::vector<MethodSample*>>::const_reverse_iterator itMethodSamples = methodSampleByTime.rbegin(); itMethodSamples != methodSampleByTime.rend(); itMethodSamples++) {
					for (const auto &itMethodSample : itMethodSamples->second) {
						TiXmlElement *pMethodNode = new TiXmlElement("Method");
						{
							pMethodNode->SetAttributeString("name", itMethodSample->name);
							pMethodNode->SetAttributeFloat("total_time", itMethodSample->dwTime / 1000000.0f);
							pMethodNode->SetAttributeFloat("time", itMethodSample->dwTime / 1000000.0f / itMethodSample->dwCount);
							pMethodNode->SetAttributeInt("calls", itMethodSample->dwCount);

							if (bDetails) {
								if (MethodSample *pParent = itMethodSample->pParent) {
									do {
										TiXmlElement *pStackNode = new TiXmlElement("CallStack");
										{
											pStackNode->SetAttributeString("name", pParent->name);
											pStackNode->SetAttributeFloat("total_time", pParent->dwTime / 1000000.0f);
											pStackNode->SetAttributeFloat("time", pParent->dwTime / 1000000.0f / pParent->dwCount);
										}
										pMethodNode->LinkEndChild(pStackNode);
									} while (pParent = pParent->pParent);
								}
							}
						}
						pTimeNode->LinkEndChild(pMethodNode);
					}
				}
			}
			pReportNode->LinkEndChild(pTimeNode);

			TiXmlElement *pMemoryNode = new TiXmlElement("Memory");
			{
				for (std::map<DWORD, std::vector<MethodSample*>>::const_reverse_iterator itMethodSamples = methodSampleByMemory.rbegin(); itMethodSamples != methodSampleByMemory.rend(); itMethodSamples++) {
					for (const auto &itMethodSample : itMethodSamples->second) {
						TiXmlElement *pMethodNode = new TiXmlElement("Method");
						{
							pMethodNode->SetAttributeString("name", itMethodSample->name);
							pMethodNode->SetAttributeInt("total_size", itMethodSample->dwMemorySize);
							pMethodNode->SetAttributeInt("calls", itMethodSample->dwCount);

							if (bDetails) {
								for (const auto &itAllocationSample : itMethodSample->alloctions) {
									TiXmlElement *pObjectNode = new TiXmlElement("Object");
									{
										pObjectNode->SetAttributeString("name", itAllocationSample.second->name);
										pObjectNode->SetAttributeInt("size", itAllocationSample.second->dwMemorySize);
										pObjectNode->SetAttributeInt("count", itAllocationSample.second->dwCount);
									}
									pMethodNode->LinkEndChild(pObjectNode);
								}
							}
						}
						pMemoryNode->LinkEndChild(pMethodNode);
					}
				}
			}
			pReportNode->LinkEndChild(pMemoryNode);
		}
		doc.LinkEndChild(pReportNode);
		doc.SaveFile(szDumpFileName);
	}
	LeaveCriticalSection(mutex);
}
