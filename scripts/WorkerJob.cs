// A private Windows job contains only the process created here and its descendants.
// Start suspended so children cannot escape between CreateProcess and assignment.
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;
using System.Text;

public sealed class MusicWorkerJob : IDisposable {
    [StructLayout(LayoutKind.Sequential)] struct Security {
        public int Length; public IntPtr Descriptor; public int Inherit;
    }
    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)] struct Startup {
        public int Size; public string Reserved, Desktop, Title;
        public int X, Y, Width, Height, XChars, YChars, Fill, Flags;
        public short Show, ReservedSize; public IntPtr ReservedBytes, Input, Output, Error;
    }
    [StructLayout(LayoutKind.Sequential)] struct ProcessInfo {
        public IntPtr Process, Thread; public int ProcessId, ThreadId;
    }
    [StructLayout(LayoutKind.Sequential)] struct BasicLimits {
        public long ProcessTime, JobTime; public uint Flags;
        public UIntPtr MinWorkingSet, MaxWorkingSet; public uint ActiveProcesses;
        public UIntPtr Affinity; public uint Priority, Scheduling;
    }
    [StructLayout(LayoutKind.Sequential)] struct IoCounters {
        public ulong ReadOps, WriteOps, OtherOps, ReadBytes, WriteBytes, OtherBytes;
    }
    [StructLayout(LayoutKind.Sequential)] struct ExtendedLimits {
        public BasicLimits Basic; public IoCounters Io;
        public UIntPtr ProcessMemory, JobMemory, PeakProcessMemory, PeakJobMemory;
    }
    [DllImport("kernel32.dll", SetLastError = true)] static extern IntPtr CreateJobObject(IntPtr attributes, string name);
    [DllImport("kernel32.dll", SetLastError = true)] static extern bool SetInformationJobObject(IntPtr job, int kind, ref ExtendedLimits info, uint size);
    [DllImport("kernel32.dll", SetLastError = true)] static extern bool AssignProcessToJobObject(IntPtr job, IntPtr process);
    [DllImport("kernel32.dll", SetLastError = true)] static extern bool IsProcessInJob(IntPtr process, IntPtr job, out bool result);
    [DllImport("kernel32.dll", SetLastError = true)] static extern IntPtr OpenProcess(uint access, bool inherit, int id);
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)] static extern bool CreateProcess(string app, StringBuilder command, IntPtr pa, IntPtr ta, bool inherit, uint flags, IntPtr env, string cwd, ref Startup startup, out ProcessInfo info);
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)] static extern IntPtr CreateFile(string path, uint access, uint share, ref Security security, uint disposition, uint flags, IntPtr template);
    [DllImport("kernel32.dll", SetLastError = true)] static extern uint ResumeThread(IntPtr thread);
    [DllImport("kernel32.dll")] static extern bool TerminateProcess(IntPtr process, uint code);
    [DllImport("kernel32.dll")] static extern uint WaitForSingleObject(IntPtr handle, uint milliseconds);
    [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr handle);
    IntPtr job, process;
    public int ProcessId { get; private set; }
    public bool HasExited { get { return process == IntPtr.Zero || WaitForSingleObject(process, 0) == 0; } }

    static void Check(bool success) { if (!success) throw new Win32Exception(Marshal.GetLastWin32Error()); }
    public MusicWorkerJob(string executable, string arguments, string cwd, string logPath) {
        IntPtr log = new IntPtr(-1), input = new IntPtr(-1);
        ProcessInfo info = new ProcessInfo();
        try {
            job = CreateJobObject(IntPtr.Zero, null);
            Check(job != IntPtr.Zero);
            ExtendedLimits limits = new ExtendedLimits();
            limits.Basic.Flags = 0x2000; // JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE
            Check(SetInformationJobObject(job, 9, ref limits, (uint)Marshal.SizeOf(typeof(ExtendedLimits))));
            Security security = new Security();
            security.Length = Marshal.SizeOf(typeof(Security)); security.Inherit = 1;
            log = CreateFile(logPath, 0x40000000, 3, ref security, 2, 0x80, IntPtr.Zero);
            Check(log != new IntPtr(-1));
            input = CreateFile("NUL", 0x80000000, 3, ref security, 3, 0x80, IntPtr.Zero);
            Check(input != new IntPtr(-1));
            Startup startup = new Startup();
            startup.Size = Marshal.SizeOf(typeof(Startup)); startup.Flags = 0x100;
            startup.Input = input; startup.Output = log; startup.Error = log;
            Check(CreateProcess(executable, new StringBuilder("\"" + executable + "\" " + arguments),
                IntPtr.Zero, IntPtr.Zero, true, 0x08000004, IntPtr.Zero, cwd, ref startup, out info));
            process = info.Process; ProcessId = info.ProcessId;
            Check(AssignProcessToJobObject(job, process));
            Check(ResumeThread(info.Thread) != 0xffffffff);
        } catch {
            if (process != IntPtr.Zero) TerminateProcess(process, 5);
            Dispose(); throw;
        } finally {
            if (info.Thread != IntPtr.Zero) CloseHandle(info.Thread);
            if (input != new IntPtr(-1)) CloseHandle(input);
            if (log != new IntPtr(-1)) CloseHandle(log);
        }
    }
    public bool Contains(int id) {
        IntPtr candidate = OpenProcess(0x1000, false, id);
        if (candidate == IntPtr.Zero) return false;
        try { bool owned; return IsProcessInJob(candidate, job, out owned) && owned; }
        finally { CloseHandle(candidate); }
    }
    public void Dispose() {
        if (job != IntPtr.Zero) { CloseHandle(job); job = IntPtr.Zero; }
        if (process != IntPtr.Zero) { WaitForSingleObject(process, 5000); CloseHandle(process); process = IntPtr.Zero; }
        GC.SuppressFinalize(this);
    }
    ~MusicWorkerJob() { Dispose(); }
}
