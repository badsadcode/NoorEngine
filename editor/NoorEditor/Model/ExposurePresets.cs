namespace NoorEditor.Model;

/// <summary>
/// Camera exposure (EV100) for common situations, tuned to NoorEngine's photometric defaults
/// (a 100,000 lux sun, an 8,000 nit sky; docs/rendering/PBR.md). Lower EV = brighter picture.
/// </summary>
public static class ExposurePresets
{
    public static readonly (string Name, double Ev100)[] All =
    [
        ("Bright sun", 14),
        ("HDRI daylight", 13),
        ("Overcast", 12),
        ("Sunset, shade", 10),
        ("Indoor lamps", 8),
        ("Street at night", 4),
    ];

    /// <summary>"Overcast (EV 12)" for menus.</summary>
    public static string Label((string Name, double Ev100) preset) => $"{preset.Name} (EV {preset.Ev100:0})";
}
