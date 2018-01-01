using UnityEditor;
using System.Runtime.InteropServices;

public class MonoProfilerEditor : Editor
{
    [DllImport("MonoProfiler")]
    public static extern void Init(string szMonoMoudleFileName);
    [DllImport("MonoProfiler")]
    public static extern void Clear();
    [DllImport("MonoProfiler")]
    public static extern void Dump(string szDumpFileName, bool bDetails);


    [@MenuItem("MonoProfiler/Init")]
    public static void MonoProfilerInit()
    {
        Init("C:\\Program Files (x86)\\Unity\\Editor\\Data\\Mono\\EmbedRuntime\\mono.dll");
    }

    [@MenuItem("MonoProfiler/Clear")]
    public static void MonoProfilerClear()
    {
        Clear();
    }

    [@MenuItem("MonoProfiler/Dump")]
    public static void MonoProfilerDump()
    {
        Dump("dump.xml", false);
    }

    [@MenuItem("MonoProfiler/Dump Details")]
    public static void MonoProfilerDumpDetails()
    {
        Dump("dump_details.xml", true);
    }
}
