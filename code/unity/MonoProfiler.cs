using UnityEngine;
using UnityEditor;
using System;
using System.Text;
using System.Collections.Generic;
using System.Runtime.InteropServices;

public class MonoProfilerHelper
{
    [DllImport("MonoProfiler")]
    public static extern void Init(string szMonoMoudleFileName);
    [DllImport("MonoProfiler")]
    public static extern void Pause();
    [DllImport("MonoProfiler")]
    public static extern void Resume();
    [DllImport("MonoProfiler")]
    public static extern void Clear();
    [DllImport("MonoProfiler")]
    public static extern void Dump(string szDumpFileName);

    [DllImport("MonoProfiler")]
    public static extern void BeginMethodIterator();
    [DllImport("MonoProfiler")]
    public static extern bool NextMethodIterator();
    [DllImport("MonoProfiler")]
    public static extern void EndMethodIterator();

    [DllImport("MonoProfiler")]
    public static extern IntPtr GetMethodName();
    [DllImport("MonoProfiler")]
    public static extern IntPtr GetMethodCallStack();
    [DllImport("MonoProfiler")]
    public static extern uint GetMethodAllocSize();
    [DllImport("MonoProfiler")]
    public static extern uint GetMethodAllocSizeDelta();

    [DllImport("MonoProfiler")]
    public static extern void BeginObjectIterator();
    [DllImport("MonoProfiler")]
    public static extern bool NextObjectIterator();
    [DllImport("MonoProfiler")]
    public static extern void EndObjectIterator();

    [DllImport("MonoProfiler")]
    public static extern IntPtr GetObjectName();
    [DllImport("MonoProfiler")]
    public static extern uint GetObjectAllocSize();

    public static string GetMethodString()
    {
        return IntPtr2String(GetMethodName());
    }

    public static string GetMethodCallStackString()
    {
        return IntPtr2String(GetMethodCallStack());
    }

    public static string GetObjectString()
    {
        return IntPtr2String(GetObjectName());
    }

    private static  string IntPtr2String(IntPtr ptr)
    {
        int len = 0;
        while (Marshal.ReadByte(ptr, len) != 0) ++len;
        byte[] buffer = new byte[len];
        Marshal.Copy(ptr, buffer, 0, buffer.Length);
        return Encoding.UTF8.GetString(buffer);
    }
}

public class MonoProfiler : EditorWindow
{
    internal class ObjectAlloction
    {
        public string name = null;
        public uint allocated = 0;
    }

    internal class MethodAlloction
    {
        public string name = null;
        public string stack = null;
        public uint allocated = 0;
        public uint allocatedDelta = 0;
        public List<ObjectAlloction> objects = new List<ObjectAlloction>();
    }

    private Vector2 mSummaryScrollPosition;
    private Vector2 mDetailsScrollPosition;
    private Vector2 mCallStackScrollPosition;

    private float mDeltaTime = 0.0f;
    private bool mbProfiling = false;
    private string mCurrentMethodCallStack = null;
    private MethodAlloction mCurrentMethodAlloction = null;
    private List<MethodAlloction> mMethodAllocations = new List<MethodAlloction>();
    private List<String> mMethodFilter = new List<String>();


    [MenuItem("Window/Mono Profiler")]
    public static void ShowWindow()
    {
        EditorWindow.GetWindow(typeof(MonoProfiler));
    }

    public MonoProfiler()
    {
        mMethodFilter.Add("GUI");
        mMethodFilter.Add("HostView");
        mMethodFilter.Add("AppStatusBar");
        mMethodFilter.Add("MonoProfiler");
        mMethodFilter.Add("ProfilerWindow");
        mMethodFilter.Add("ContainerWindow");
        mMethodFilter.Add("SendMouseEvents");
        mMethodFilter.Add("EditorApplication");
    }

    private void OnEnable()
    {
        MonoProfilerHelper.Init("C:\\Program Files (x86)\\Unity\\Editor\\Data\\Mono\\EmbedRuntime\\mono.dll");
        MonoProfilerHelper.Pause();
        mMethodAllocations.Clear();
        mCurrentMethodCallStack = null;
        mCurrentMethodAlloction = null;
    }

    private void OnDisable()
    {
        MonoProfilerHelper.Pause();
        mMethodAllocations.Clear();
        mCurrentMethodCallStack = null;
        mCurrentMethodAlloction = null;
    }

    private void Update()
    {
        if (mbProfiling == false)
        {
            return;
        }

        mDeltaTime += Time.deltaTime;

        if (mDeltaTime > 1.0f)
        {
            //
            // 1. 清除当前数据
            //
            mMethodAllocations.Clear();
            mCurrentMethodCallStack = null;
            mCurrentMethodAlloction = null;

            //
            // 2. 重新获得数据
            //
            MonoProfilerHelper.Pause();
            MonoProfilerHelper.BeginMethodIterator();
            {
                do
                {
                    if (Filter(MonoProfilerHelper.GetMethodCallStackString()))
                    {
                        MethodAlloction method = new MethodAlloction();
                        {
                            method.name = MonoProfilerHelper.GetMethodString();
                            method.stack = MonoProfilerHelper.GetMethodCallStackString();
                            method.allocated = MonoProfilerHelper.GetMethodAllocSize();
                            method.allocatedDelta = MonoProfilerHelper.GetMethodAllocSizeDelta();

                            MonoProfilerHelper.BeginObjectIterator();
                            {
                                do
                                {
                                    ObjectAlloction obj = new ObjectAlloction();
                                    {
                                        obj.name = MonoProfilerHelper.GetObjectString();
                                        obj.allocated = MonoProfilerHelper.GetObjectAllocSize();
                                    }
                                    method.objects.Add(obj);
                                } while (MonoProfilerHelper.NextObjectIterator());
                            }
                            MonoProfilerHelper.EndObjectIterator();
                        }
                        mMethodAllocations.Add(method);
                    }
                } while (MonoProfilerHelper.NextMethodIterator());
            }
            MonoProfilerHelper.EndMethodIterator();
            MonoProfilerHelper.Resume();

            //
            // 3. 排序
            //
            if (mMethodAllocations.Count > 0)
            {
                mMethodAllocations.Sort(CompareMethod);
                mCurrentMethodCallStack = mMethodAllocations[0].stack;
                mCurrentMethodAlloction = mMethodAllocations[0];
            }

            //
            // 4. 重绘
            //
            Repaint();

            //
            // 5. 重新计数
            //
            mDeltaTime = 0.0f;
        }
    }

    private void OnGUI()
    {
        DrawToolBar();
        DrawSummaryPanel();
        DrawDetailsPanel();
        DrawCallStack();
    }

    private void DrawToolBar()
    {
        //
        // 1. 自适应窗口
        //
        int pos = 0;
        int space = 5;
        int width = (int)(Screen.width * 0.1f);

        width = Mathf.Max(width, 75);
        width = Mathf.Min(width, 100);
        pos = (Screen.width - width * 3 - space * 2) / 2;

        //
        // 2. 分析按钮
        //
        bool bProfiling = GUI.Toggle(new Rect(pos, 5, width, 20), mbProfiling, "Profiler", "button");
        if (mbProfiling != bProfiling)
        {
            if (bProfiling)
            {
                mDeltaTime = 0.0f;
                mbProfiling = true;
                MonoProfilerHelper.Resume();
            }
            else
            {
                mDeltaTime = 0.0f;
                mbProfiling = false;
                MonoProfilerHelper.Pause();
            }
        }
        
        //
        // 3. 清除数据
        //
        if (GUI.Button(new Rect(pos + width + space, 5, width, 20), "Clear"))
        {
            mMethodAllocations.Clear();
            mCurrentMethodCallStack = null;
            mCurrentMethodAlloction = null;
            MonoProfilerHelper.Clear();
        }

        //
        // 4. 输出到文件
        //
        if (GUI.Button(new Rect(pos + 2 * (width + space), 5, width, 20), "Dump"))
        {
            string path = EditorUtility.SaveFilePanel("Dump mono memory", "", "mono", "txt");
            if (path != null)
            {
                MonoProfilerHelper.Dump(path);
            }
        }
    }

    private void DrawSummaryPanel()
    {
        //
        // 1. 绘制背景
        //
        Rect rcSrceen = new Rect(0, 30, Screen.width / 2.0f - 1, (Screen.height - 30) * 0.75f);
        EditorGUI.DrawRect(rcSrceen, new Color(0.125f, 0.125f, 0.125f));

        //
        // 2. 安全检查
        //
        if (mMethodAllocations.Count == 0)
        {
            return;
        }

        //
        // 3. 统计需要显示的条目
        //
        int count = 0;
        uint size = 0;
        uint totalSize = 0;

        for (int index = 0; index < mMethodAllocations.Count; index++)
        {
            totalSize += mMethodAllocations[index].allocated;
        }

        for (count = 0; count < mMethodAllocations.Count; count++)
        {
            size += mMethodAllocations[count].allocated;
            if (1.0f * size / totalSize > 0.95f) break;
        }

        GUI.Label(new Rect(0, 5, Screen.width / 2.0f, 20), "TotalSize: " + (totalSize / 1024.0f / 1024.0f).ToString() + "MB");

        //
        // 4. 绘制方法内存条目
        //
        mSummaryScrollPosition = GUI.BeginScrollView(rcSrceen, mSummaryScrollPosition, new Rect(0, 0, Screen.width / 2.0f - 1, count * 22));
        {
            for (int index = 0; index < Mathf.Max(count, 1); index++)
            {
                DrawMethodMemory(index, mMethodAllocations[index].name, mMethodAllocations[index].allocated, mMethodAllocations[index].allocatedDelta, totalSize);
            }
        }
        GUI.EndScrollView();
    }

    private void DrawDetailsPanel()
    {
        //
        // 1. 绘制背景
        //
        Rect rcSrceen = new Rect(Screen.width / 2.0f, 30, Screen.width / 2.0f - 1, (Screen.height - 30) * 0.75f);
        EditorGUI.DrawRect(rcSrceen, new Color(0.125f, 0.125f, 0.125f));

        //
        // 2. 安全检查
        //
        if (mCurrentMethodAlloction == null)
        {
            return;
        }

        //
        // 3. 排序
        //
        mCurrentMethodAlloction.objects.Sort(CompareObject);

        //
        // 4. 统计需要显示的条目
        //
        uint totalSize = 0;
        for (int index = 0; index < mCurrentMethodAlloction.objects.Count; index++)
        {
            totalSize += mCurrentMethodAlloction.objects[index].allocated;
        }

        //
        // 5. 绘制对象内存条目
        //
        mDetailsScrollPosition = GUI.BeginScrollView(rcSrceen, mDetailsScrollPosition, new Rect(Screen.width / 2.0f, 0, Screen.width / 2.0f - 1, mCurrentMethodAlloction.objects.Count * 22));
        {
            for (int index = 0; index < mCurrentMethodAlloction.objects.Count; index++)
            {
                DrawObjectMemory(index, mCurrentMethodAlloction.objects[index].name, mCurrentMethodAlloction.objects[index].allocated, totalSize);
            }
        }
        GUI.EndScrollView();
    }

    private void DrawCallStack()
    {
        //
        // 1. 绘制背景
        //
        Rect rcSrceen = new Rect(0, 30 + (Screen.height - 30) * 0.75f + 1, Screen.width, (Screen.height - 30) * 0.25f - 1);
        EditorGUI.DrawRect(rcSrceen, new Color(0.125f, 0.125f, 0.125f));

        //
        // 2. 安全检查
        //
        if (mCurrentMethodCallStack == null)
        {
            return;
        }

        //
        // 3. 绘制对象内存条目
        //
        Rect rcView = new Rect(0, 30 + (Screen.height - 30) * 0.75f + 1, Screen.width, (Screen.height - 30) * 0.5f);
        mCallStackScrollPosition = GUI.BeginScrollView(rcSrceen, mCallStackScrollPosition, rcView);
        {
            GUI.TextField(rcView, "Stack:" + mCurrentMethodCallStack);
        }
        GUI.EndScrollView();
    }

    private void DrawMethodMemory(int index, string name, uint size, uint delta, uint totalSize)
    {
        Color lastColor = GUI.color;
        {
            Rect rect = new Rect(0, index * 22, 0.5f * Screen.width * size / totalSize, 20);

            Color color;
            color.r = 1.0f * size / totalSize;
            color.g = 1.0f - color.r;
            color.b = 0;
            color.a = 1;

            GUIStyle style = new GUIStyle("label");
            style.clipping = TextClipping.Overflow;

            GUI.color = color;
            if (GUI.Button(rect, ""))
            {
                mbProfiling = false;
                mCurrentMethodCallStack = mMethodAllocations[index].stack;
                mCurrentMethodAlloction = mMethodAllocations[index];
                MonoProfilerHelper.Pause();
            }
            GUI.Label(rect, name + ": " + (size / 1024.0f / 1024.0f).ToString() + "MB" + " Delta: " + (delta / 1024.0f/1024.0f) + "MB", style);
        }
        GUI.color = lastColor;
    }

    private void DrawObjectMemory(int index, string name, uint size, uint totalSize)
    {
        Color lastColor = GUI.color;
        {
            Rect rect = new Rect(0.5f * Screen.width, index * 22, 0.5f * Screen.width * size / totalSize, 20);

            Color color;
            color.r = 1.0f * size / totalSize;
            color.g = 1.0f - color.r;
            color.b = 0;
            color.a = 1;

            GUIStyle style = new GUIStyle("label");
            style.clipping = TextClipping.Overflow;

            GUI.color = color;
            GUI.Button(rect, "");
            GUI.Label(rect, name + ": " + (size / 1024.0f / 1024.0f).ToString() + "MB", style);
        }
        GUI.color = lastColor;
    }

    private bool Filter(string name)
    {
        for (int index = 0; index < mMethodFilter.Count; index++)
        {
            if (name.Contains(mMethodFilter[index]))
            {
                return false;
            }
        }

        return true;
    }

    private int CompareMethod(MethodAlloction a, MethodAlloction b)
    {
        if (a == null && b != null) return 1;
        if (a != null && b == null) return -1;
        if (a.allocated > b.allocated) return -1;
        if (a.allocated < b.allocated) return 1;
        return 0;
    }

    private int CompareObject(ObjectAlloction a, ObjectAlloction b)
    {
        if (a == null && b != null) return 1;
        if (a != null && b == null) return -1;
        if (a.allocated > b.allocated) return -1;
        if (a.allocated < b.allocated) return 1;
        return 0;
    }
}
