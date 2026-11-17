using System.Text.Json.Nodes;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Layout;
using Avalonia.Media;
using Avalonia.Media.Imaging;
using Dock.Model.Mvvm.Controls;
using NoorEditor.Interop;
using NoorEditor.Model;
using NoorEditor.Shell;

namespace NoorEditor.Panels;

/// <summary>
/// A texture's import settings (ADR-029, Milestone 5): kind, normal convention, compression,
/// mipmaps, maximum size, and what they cost on the GPU. A tab next to the viewport; changes are
/// written to the texture's .noormeta at once and the viewport reloads the texture.
/// </summary>
public sealed class TextureDocument : Document
{
    public EditorDocument Doc { get; }
    public string RelativePath { get; }
    public string FullPath => Path.Combine(Doc.ProjectRoot, RelativePath);

    /// <summary>Makes the view (MainWindow sets it; Dock calls it whenever the tab is shown).</summary>
    public Func<TextureDocument, Control>? CreateView { get; set; }

    public TextureDocument(EditorDocument doc, string relativePath)
    {
        Doc = doc;
        RelativePath = relativePath;
        Id = "Texture:" + relativePath;
        Title = Path.GetFileName(relativePath);
        CanFloat = true;
        CanClose = true;
    }

    /// <summary>The engine's view of the texture (noor_doc_texture), or null (the error is logged).</summary>
    public JsonObject? Info()
    {
        var json = Native.Texture(Doc.Handle, RelativePath);
        if (json == null)
        {
            EngineLog.Warn("Textures", $"{RelativePath}: {Native.LastError()}");
            return null;
        }
        return JsonNode.Parse(json) as JsonObject;
    }

    /// <summary>Writes new settings into the .noormeta. Returns false (logged) if that failed.</summary>
    public bool Apply(JsonObject settings)
    {
        if (Native.noor_doc_set_texture_settings(Doc.Handle, RelativePath, settings.ToJsonString()) == 1) return true;
        EngineLog.Error("Textures", $"{RelativePath}: {Native.LastError()}");
        return false;
    }
}

public sealed class TexturePanel : Grid
{
    public TextureDocument Texture { get; }

    private readonly ComboBox _kind = new() { HorizontalAlignment = HorizontalAlignment.Stretch, Classes = { "field" } };
    private readonly ComboBox _convention = new() { ItemsSource = new[] { "OpenGL (+Y up, glTF)", "DirectX (green flipped)" }, HorizontalAlignment = HorizontalAlignment.Stretch, Classes = { "field" } };
    private readonly ComboBox _compression = new() { ItemsSource = new[] { "Compressed (BC7 / BC5)", "None (RGBA8, 4× memory)" }, HorizontalAlignment = HorizontalAlignment.Stretch, Classes = { "field" } };
    private readonly ComboBox _maxSize = new() { ItemsSource = MaxSizes.Select(s => $"{s}").ToArray(), HorizontalAlignment = HorizontalAlignment.Stretch, Classes = { "field" } };
    private readonly CheckBox _mips = new() { Content = "Mipmaps (smaller copies for far away)" };
    private readonly TextBlock _info = new() { FontSize = 12, TextWrapping = TextWrapping.Wrap, Margin = new Thickness(0, 10, 0, 0) };
    private readonly TextBlock _saving = new() { FontSize = 11, Classes = { "muted" }, TextWrapping = TextWrapping.Wrap };
    private Control? _conventionRow;
    private bool _filling;

    private static readonly int[] MaxSizes = [256, 512, 1024, 2048, 4096, 8192];
    private static readonly (string Value, string Label)[] Kinds =
    [
        ("color", "Colour (base colour, emissive)"), ("data", "Data (roughness, metallic, AO, ORM, masks)"),
        ("normal", "Normal map"), ("hdr", "HDR light values (.hdr)"),
    ];

    /// <summary>The last info from the engine (tests read it).</summary>
    public JsonObject? Last { get; private set; }

    public TexturePanel(TextureDocument texture)
    {
        Texture = texture;
        ColumnDefinitions = new ColumnDefinitions("*,4,360");

        // Preview: the source picture (PNG, JPEG and BMP; others are listed without one).
        var preview = new DockPanel { Background = NoorTheme.Brush(NoorTheme.Background) };
        var caption = new TextBlock { Text = texture.RelativePath, FontSize = 12, Margin = new Thickness(10, 6), Classes = { "muted" } };
        DockPanel.SetDock(caption, Avalonia.Controls.Dock.Top);
        preview.Children.Add(caption);
        preview.Children.Add(PreviewImage(texture.FullPath));
        Children.Add(preview);

        var splitter = new GridSplitter { Background = NoorTheme.Brush(NoorTheme.Border), ResizeDirection = GridResizeDirection.Columns };
        SetColumn(splitter, 1);
        Children.Add(splitter);

        _kind.ItemsSource = Kinds.Select(k => k.Label).ToArray();
        ToolTip.SetTip(_kind, "How the pixels are read: colour is sRGB; data and normal maps are used as stored");
        ToolTip.SetTip(_compression, "Compressed: a quarter of the GPU memory, nearly the same picture. Compressing takes a moment the first time, then it's cached.");
        ToolTip.SetTip(_maxSize, "Bigger pictures are halved until they fit");
        var fields = new StackPanel { Spacing = 6, Margin = new Thickness(12, 10) };
        fields.Children.Add(new TextBlock { Text = "Import settings", Classes = { "section" } });
        fields.Children.Add(Row("Kind", _kind));
        _conventionRow = Row("Normal convention", _convention);
        fields.Children.Add(_conventionRow);
        fields.Children.Add(Row("Compression", _compression));
        fields.Children.Add(Row("Max size", _maxSize));
        fields.Children.Add(_mips);
        fields.Children.Add(_info);
        fields.Children.Add(_saving);
        var scroll = new ScrollViewer { Content = fields, Background = NoorTheme.Brush(NoorTheme.Panel) };
        SetColumn(scroll, 2);
        Children.Add(scroll);

        foreach (var c in new[] { _kind, _convention, _compression, _maxSize }) c.SelectionChanged += (_, _) => Changed();
        _mips.IsCheckedChanged += (_, _) => Changed();
        Fill();
    }

    private static Control PreviewImage(string file)
    {
        var ext = Path.GetExtension(file).ToLowerInvariant();
        if (ext is ".png" or ".jpg" or ".jpeg" or ".bmp")
        {
            try
            {
                return new Image { Source = new Bitmap(file), Stretch = Stretch.Uniform, Margin = new Thickness(12) };
            }
            catch (Exception e) when (e is IOException or ArgumentException or NotSupportedException or InvalidOperationException)
            {
                EngineLog.Warn("Textures", $"No preview for {Path.GetFileName(file)}: {e.Message}");
            }
        }
        return new TextBlock { Text = $"No preview for {ext} files here (the engine reads them)", Classes = { "muted" }, Margin = new Thickness(12),
                               HorizontalAlignment = HorizontalAlignment.Center, VerticalAlignment = VerticalAlignment.Center };
    }

    private static Control Row(string label, Control editor)
    {
        var grid = new Grid { ColumnDefinitions = new ColumnDefinitions("128,*") };
        var text = new TextBlock { Text = label, FontSize = 12, VerticalAlignment = VerticalAlignment.Center };
        SetColumn(editor, 1);
        grid.Children.Add(text);
        grid.Children.Add(editor);
        return grid;
    }

    /// <summary>Shows the engine's current settings and numbers.</summary>
    public void Fill()
    {
        Last = Texture.Info();
        if (Last?["settings"] is not JsonObject s)
        {
            _info.Text = "The engine can't read this texture (see the Console).";
            return;
        }
        _filling = true;
        var kind = s["kind"]?.GetValue<string>() ?? "color";
        _kind.SelectedIndex = Math.Max(0, Array.FindIndex(Kinds, k => k.Value == kind));
        _convention.SelectedIndex = s["normal_convention"]?.GetValue<string>() == "directx" ? 1 : 0;
        _compression.SelectedIndex = s["compression"]?.GetValue<string>() == "none" ? 1 : 0;
        var max = s["max_size"]?.GetValue<int>() ?? 4096;
        _maxSize.SelectedIndex = Math.Max(0, Array.FindLastIndex(MaxSizes, m => m <= max));
        _mips.IsChecked = s["mips"]?.GetValue<bool>() ?? true;
        if (_conventionRow != null) _conventionRow.IsVisible = kind == "normal";
        _compression.IsEnabled = kind != "hdr";
        _filling = false;

        long N(string key) => Last[key]?.GetValue<long>() ?? 0;
        var gpu = N("gpu_bytes");
        var plain = N("rgba8_bytes");
        var size = N("source_width") == N("width") ? $"{N("width")} × {N("height")}"
                 : $"{N("source_width")} × {N("source_height")}, reduced to {N("width")} × {N("height")}";
        _info.Text = $"{size}, {N("mips")} mips\n{Last["format"]?.GetValue<string>()}: {RenderPanel.Bytes(gpu)} on the GPU" +
                     (gpu < plain ? $" (uncompressed: {RenderPanel.Bytes(plain)})" : "");
    }

    private void Changed()
    {
        if (_filling) return;
        var settings = new JsonObject
        {
            ["kind"] = Kinds[Math.Max(0, _kind.SelectedIndex)].Value,
            ["normal_convention"] = _convention.SelectedIndex == 1 ? "directx" : "opengl",
            ["compression"] = _compression.SelectedIndex == 1 ? "none" : "auto",
            ["max_size"] = MaxSizes[Math.Max(0, _maxSize.SelectedIndex)],
            ["mips"] = _mips.IsChecked == true,
        };
        if (Texture.Apply(settings)) _saving.Text = "Saved to the .noormeta. The viewport reloads the texture (compressing a big one takes a moment).";
        Fill();
    }

    /// <summary>For tests: changes one setting as if picked in the panel.</summary>
    public void SetCompression(bool compressed) => _compression.SelectedIndex = compressed ? 0 : 1;
}
