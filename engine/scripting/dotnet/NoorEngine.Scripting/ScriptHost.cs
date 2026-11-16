using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Runtime.Loader;
using System.Text.Json.Nodes;
using NoorEngine.Interop;

namespace NoorEngine;

/// <summary>
/// The managed script host (noor_rt_host). A host application (NoorEditor; noor_player via <see cref="Boot"/>):
///   1. <see cref="Initialize"/> with the engine's noor_rt_api table,
///   2. <see cref="LoadScripts"/> with the compiled project scripts (a collectible load context,
///      so <see cref="UnloadScripts"/> frees them and a rebuilt assembly can be loaded),
///   3. gives the engine <see cref="HostTable"/>.
/// The engine then creates script instances and runs hooks through the table. Exceptions thrown by
/// scripts never cross into native code: the call returns 0 and <c>last_error</c> explains.
/// </summary>
public static unsafe class ScriptHost
{
    private sealed class ScriptContext() : AssemblyLoadContext("NoorScripts", isCollectible: true)
    {
        // NoorEngine.Scripting resolves to the host's own copy, wherever that was loaded (the editor's
        // default context, or the isolated context hostfxr gives noor_player), so NoorScript is one
        // type. The framework falls through to the default context.
        protected override Assembly? Load(AssemblyName name) =>
            name.Name == typeof(NoorScript).Assembly.GetName().Name ? typeof(NoorScript).Assembly : null;
    }

    private static ScriptContext? _context;
    private static readonly Dictionary<string, Type> Types = new(StringComparer.Ordinal);
    private static readonly Dictionary<long, NoorScript> Instances = new();
    private static long _next = 1;
    private static nint _error;
    private static RtHost* _host;

    /// <summary>Script classes in the loaded assembly, by name.</summary>
    public static IReadOnlyCollection<string> ScriptClasses => Types.Keys;

    public static void Initialize(nint api)
    {
        var table = (RtApi*)api;
        if (table == null || table->Version != Engine.ExpectedVersion || table->Size != (uint)sizeof(RtApi))
            throw new InvalidOperationException($"noor_rt_api mismatch: engine v{(table == null ? 0 : table->Version)}, " +
                                                $"NoorEngine.Scripting expects v{Engine.ExpectedVersion} ({sizeof(RtApi)} bytes)");
        Engine.Api = table;
    }

    /// <summary>
    /// The native entry point for hosts without managed code of their own (noor_player starts .NET
    /// with hostfxr and calls this): <see cref="Initialize"/>, load the compiled scripts from a file
    /// (and its .pdb, for line numbers), and return <see cref="HostTable"/>. On failure returns 0 and
    /// writes the reason (UTF-8, NUL-terminated) into <paramref name="error"/>.
    /// </summary>
    [UnmanagedCallersOnly]
    public static nint Boot(nint api, byte* scriptsPath, byte* error, int errorCapacity)
    {
        try
        {
            Initialize(api);
            var path = Engine.Str(scriptsPath) ?? throw new ArgumentException("no scripts path");
            var pdb = Path.ChangeExtension(path, ".pdb");
            LoadScripts(File.ReadAllBytes(path), File.Exists(pdb) ? File.ReadAllBytes(pdb) : null);
            return HostTable;
        }
        catch (Exception e)
        {
            if (error != null && errorCapacity > 0)
            {
                var bytes = System.Text.Encoding.UTF8.GetBytes(Describe(e));
                var n = Math.Min(bytes.Length, errorCapacity - 1);
                bytes.AsSpan(0, n).CopyTo(new Span<byte>(error, errorCapacity));
                error[n] = 0;
            }
            return 0;
        }
    }

    /// <summary>The noor_rt_host table for the engine (allocated once, lives for the process).</summary>
    public static nint HostTable
    {
        get
        {
            if (_host == null)
            {
                _host = (RtHost*)NativeMemory.AllocZeroed((nuint)sizeof(RtHost));
                _host->Create = &Create;
                _host->Call = &Call;
                _host->Event = &Event;
                _host->Release = &Release;
                _host->LastError = &LastError;
            }
            return (nint)_host;
        }
    }

    /// <summary>Loads compiled scripts (replacing any loaded before). Returns the script class names.</summary>
    public static IReadOnlyCollection<string> LoadScripts(byte[] assembly, byte[]? symbols)
    {
        UnloadScripts();
        _context = new ScriptContext();
        using var image = new MemoryStream(assembly);  // from memory: the file stays free for the next build
        using var pdb = symbols == null ? null : new MemoryStream(symbols);
        var asm = _context.LoadFromStream(image, pdb);
        foreach (var t in asm.GetTypes())
            if (t.IsSubclassOf(typeof(NoorScript)) && !t.IsAbstract) Types[t.Name] = t;
        return Types.Keys;
    }

    /// <summary>Drops every script instance and unloads the scripts' load context.</summary>
    [MethodImpl(MethodImplOptions.NoInlining)]
    public static void UnloadScripts()
    {
        Instances.Clear();
        Types.Clear();
        _context?.Unload();
        _context = null;
    }

    private static void SetError(string message)
    {
        if (_error != 0) Marshal.FreeCoTaskMem(_error);
        _error = Marshal.StringToCoTaskMemUTF8(message);
    }

    private static string Describe(Exception e)
    {
        e = e is TargetInvocationException { InnerException: { } inner } ? inner : e;
        // The first frame inside the script, with file:line when symbols are loaded.
        var trace = new System.Diagnostics.StackTrace(e, fNeedFileInfo: true);
        var where = trace.GetFrames().FirstOrDefault(f => f.GetFileName() != null);
        var at = where == null ? "" : $" at {Path.GetFileName(where.GetFileName())}:{where.GetFileLineNumber()}";
        return $"{e.GetType().Name}: {e.Message}{at}";
    }

    [UnmanagedCallersOnly]
    private static long Create(RtEntity entity, byte* className, byte* paramsJson)
    {
        try
        {
            var name = Engine.Str(className) ?? "";
            if (!Types.TryGetValue(name, out var type))
            {
                SetError(Types.Count == 0
                    ? $"no compiled C# scripts are loaded (class {name})"
                    : $"no script class named {name} (a class deriving from NoorScript, named like its file)");
                return 0;
            }
            var script = (NoorScript)Activator.CreateInstance(type)!;
            script.Entity = new Entity(entity);
            script.Params = JsonNode.Parse(Engine.Str(paramsJson) ?? "{}") as JsonObject ?? new JsonObject();
            var handle = _next++;
            Instances[handle] = script;
            return handle;
        }
        catch (Exception e)
        {
            SetError(Describe(e));
            return 0;
        }
    }

    [UnmanagedCallersOnly]
    private static int Call(long handle, int hook, float dt)
    {
        if (!Instances.TryGetValue(handle, out var s)) return 1;
        try
        {
            switch (hook)
            {
                case 0: s.OnStart(); break;
                case 1: s.OnUpdate(dt); break;
                case 2: s.OnFixedUpdate(dt); break;
                case 3: s.OnLateUpdate(dt); break;
                case 4: s.OnDestroy(); break;
            }
            return 1;
        }
        catch (Exception e)
        {
            SetError(Describe(e));
            return 0;
        }
    }

    [UnmanagedCallersOnly]
    private static int Event(long handle, int kind, RtEntity other)
    {
        if (!Instances.TryGetValue(handle, out var s)) return 1;
        try
        {
            var o = new Entity(other);
            switch (kind)
            {
                case 0: s.OnCollisionEnter(o); break;
                case 1: s.OnCollisionExit(o); break;
                case 2: s.OnTriggerEnter(o); break;
                case 3: s.OnTriggerExit(o); break;
            }
            return 1;
        }
        catch (Exception e)
        {
            SetError(Describe(e));
            return 0;
        }
    }

    [UnmanagedCallersOnly]
    private static void Release(long handle) => Instances.Remove(handle);

    [UnmanagedCallersOnly]
    private static byte* LastError() => (byte*)_error;
}
