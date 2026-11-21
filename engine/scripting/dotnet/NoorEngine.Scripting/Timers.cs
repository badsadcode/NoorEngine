using System.Runtime.CompilerServices;
using NoorEngine.Interop;

namespace NoorEngine;

/// <summary>A timer made by <see cref="NoorScript"/>'s After or Every; Cancel stops it.</summary>
public sealed class Timer
{
    internal double Due, Period;
    internal bool Real, Cancelled;
    internal Func<bool> Action = null!;
    internal long Order;
    public void Cancel() => Cancelled = true;
    public bool Active => !Cancelled;
}

/// <summary>A tween (Entity.Tween): Cancel stops it where it is; Active while it runs.</summary>
public sealed unsafe class Tween
{
    internal readonly long Id;
    internal Tween(long id) => Id = id;
    public void Cancel() => Engine.A.TweenOp(Id, 0);
    public bool Active => Engine.A.TweenOp(Id, 1) != 0;
}

/// <summary>What <c>await Wait(seconds)</c> or <c>await WaitUntil(condition)</c> waits on.</summary>
public readonly struct WaitAwaitable
{
    private readonly Scheduler _jobs;
    private readonly double _seconds;
    private readonly bool _real;
    private readonly Func<bool>? _until;
    public WaitAwaitable(Scheduler jobs, double seconds, bool real) => (_jobs, _seconds, _real, _until) = (jobs, seconds, real, null);
    public WaitAwaitable(Scheduler jobs, Func<bool> until) => (_jobs, _seconds, _real, _until) = (jobs, 0, false, until);
    public WaitAwaiter GetAwaiter() => new(_jobs, _seconds, _real, _until);
}

public readonly struct WaitAwaiter(Scheduler jobs, double seconds, bool real, Func<bool>? until) : INotifyCompletion
{
    public bool IsCompleted => until != null && until();
    public void OnCompleted(Action continuation)
    {
        if (until != null) jobs.Watch(until, continuation);
        else jobs.Add(seconds, 0, () => { continuation(); return true; }, real);
    }
    public void GetResult() { }
}

/// <summary>
/// A script's timers and waits (gameplay foundations, part 2): run on the main thread before any script's OnUpdate
/// that frame, in the order they were made, and never while paused unless the script runs while paused. The same
/// rules as Lua's noor.after / noor.every / noor.wait: a timer that throws is reported and stopped, the script goes on.
/// </summary>
public sealed class Scheduler
{
    private readonly List<Timer> _timers = new();
    private long _order;

    internal Timer Add(double seconds, double period, Func<bool> action, bool real)
    {
        var t = new Timer { Due = Now(real) + Math.Max(0, seconds), Period = period, Real = real, Action = action, Order = _order++ };
        _timers.Add(t);
        return t;
    }

    private static double Now(bool real) => real ? Noor.RealTime : Noor.Time;

    // Runs `action` once, on the first frame `condition` holds (tweens' onDone, WaitUntil).
    internal void Watch(Func<bool> condition, Action action)
    {
        Add(0, 1e-9, () =>
        {
            if (!condition()) return true;  // not yet: ask again next frame
            action();
            return false;
        }, false);
    }

    /// <summary>Fires what's due. An exception stops that timer (reported as an error); the others carry on.</summary>
    internal void Run()
    {
        if (_timers.Count == 0) return;
        foreach (var t in _timers.ToArray())  // a timer may add others
        {
            if (t.Cancelled || Now(t.Real) + 1e-9 < t.Due) continue;  // a hair of slack, as Lua: 30 x 1/60 is 0.49999...
            if (t.Period > 0) t.Due = Math.Max(t.Due + t.Period, Now(t.Real));
            else t.Cancelled = true;
            bool again;
            try
            {
                again = t.Action();
            }
            catch (Exception e)
            {
                t.Cancelled = true;
                Noor.Error($"{e.GetType().Name}: {e.Message} (a timer or wait); it is stopped");
                continue;
            }
            if (!again) t.Cancelled = true;
        }
        _timers.RemoveAll(x => x.Cancelled);
    }
}
