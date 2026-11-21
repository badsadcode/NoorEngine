using NoorEngine.Interop;

namespace NoorEngine;

/// <summary>A script's listener (NoorScript.On): Cancel stops it hearing anything more.</summary>
public sealed class Listener
{
    private readonly long _id;
    private Action? _forget;

    internal Listener(long id, Action forget)
    {
        _id = id;
        _forget = forget;
    }

    public unsafe void Cancel()
    {
        if (_forget == null) return;
        Engine.A.Unlisten(_id);
        _forget();
        _forget = null;
    }
}
