using System.Text.Json.Nodes;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.Primitives;
using Avalonia.Layout;
using Avalonia.Media;
using Avalonia.Media.Imaging;
using NoorEditor.Model;
using NoorEditor.Shell;

namespace NoorEditor.Panels;

/// <summary>
/// The Tilemap panel (2D foundations, part 5): make a tilemap from one of the project's tilesets, and paint it. Select a
/// tilemap, pick a tile from the palette (the tileset's sheet, cut into its frames) or the eraser, switch Paint on, and
/// left-drag in the viewport: every cell the pointer reaches gets that tile, and a whole drag is one undo step. Painting
/// past the map's edges grows it. Snap to tiles or to pixels sets the viewport's snapping for moving things on the map.
/// </summary>
public sealed class TilemapPanel : DockPanel
{
    private readonly EditorDocument _doc;
    private readonly ComboBox _tilesets = new() { MinWidth = 180 };
    private readonly Button _new = new() { Content = "New tilemap", Classes = { "tool" } };
    private readonly ToggleButton _paint = new() { Content = "Paint", Classes = { "tool" } };
    private readonly Button _snapTiles = new() { Content = "Snap to tiles", Classes = { "tool" } };
    private readonly Button _snapPixels = new() { Content = "Snap to pixels", Classes = { "tool" } };
    private readonly WrapPanel _palette = new();
    private readonly TextBlock _status = new() { FontSize = 11, Classes = { "muted" }, TextWrapping = TextWrapping.Wrap };
    private JsonArray _list = [];
    private string? _shownMap;
    private string? _shownRows;
    private double _cell = 1.0, _pixelsPerMetre = 16.0;

    /// <summary>The tiles in the palette as last built (tests read them).</summary>
    public int PaletteCount { get; private set; }

    public TilemapPanel(EditorDocument doc)
    {
        _doc = doc;
        ToolTip.SetTip(_new, "A new tilemap from the chosen tileset, at the origin (one undo step)");
        ToolTip.SetTip(_paint, "Left-drag in the viewport paints the picked tile on the selected tilemap (one undo step per drag)");
        ToolTip.SetTip(_snapTiles, "Snap moves to this map's cells");
        ToolTip.SetTip(_snapPixels, "Snap moves to whole pixels of this map's art");
        _new.Click += (_, _) => NewTilemap();
        _paint.Click += (_, _) => _doc.SetTilePainting(_paint.IsChecked == true);
        _snapTiles.Click += (_, _) => _doc.RequestSnapStep(_cell);
        _snapPixels.Click += (_, _) => _doc.RequestSnapStep(1.0 / _pixelsPerMetre);

        var content = new StackPanel
        {
            Spacing = 6, Margin = new Thickness(10, 8),
            Children =
            {
                new StackPanel { Orientation = Orientation.Horizontal, Spacing = 6, Children = { _tilesets, _new } },
                new StackPanel { Orientation = Orientation.Horizontal, Spacing = 6, Children = { _paint, _snapTiles, _snapPixels } },
                _status,
                new TextBlock { Text = "Tiles", Classes = { "section" } },
                _palette,
            },
        };
        Children.Add(new ScrollViewer { Content = content });
        _doc.AssetsChanged += RebuildList;
        _doc.SelectionChanged += Rebuild;
        _doc.SceneChanged += Rebuild;
        _doc.SculptChanged += SyncButtons;
        RebuildList();
        Rebuild();
    }

    private string? SelectedTilemap =>
        _doc.Selected is { FromPrefab: false } e && e.Components["tilemap"] != null ? e.Id : null;

    private void RebuildList()
    {
        _list = _doc.Tilemap(new JsonObject { ["op"] = "list" })?["tilesets"] as JsonArray ?? [];
        var before = _tilesets.SelectedIndex;
        _tilesets.Items.Clear();
        foreach (var t in _list.OfType<JsonObject>())
        {
            var name = t["name"]?.GetValue<string>();
            _tilesets.Items.Add(string.IsNullOrEmpty(name) ? t["path"]?.GetValue<string>() : name);
        }
        _tilesets.SelectedIndex = _list.Count == 0 ? -1 : Math.Clamp(before, 0, _list.Count - 1);
        _new.IsEnabled = _list.Count > 0 && !_doc.Playing;
    }

    private void NewTilemap()
    {
        if (_tilesets.SelectedIndex < 0 || _tilesets.SelectedIndex >= _list.Count) return;
        var tileset = _list[_tilesets.SelectedIndex]!["id"]!.GetValue<string>();
        var id = _doc.Create("Tilemap", new JsonObject { ["tilemap"] = new JsonObject { ["tileset"] = tileset, ["rows"] = new JsonArray(".") } },
                             new Vec3(0, 0, 0));
        if (id != null) _doc.Select(id);
    }

    private void SyncButtons()
    {
        _paint.IsChecked = _doc.TilePainting;
        foreach (var b in _palette.Children.OfType<ToggleButton>())
            b.IsChecked = b.Tag is int t && t == _doc.TileBrush;
    }

    /// <summary>The palette for the selected tilemap (rebuilt only when another map is picked or its tileset changes).</summary>
    private void Rebuild()
    {
        var map = SelectedTilemap;
        _paint.IsEnabled = map != null && !_doc.Playing;
        _snapTiles.IsEnabled = _snapPixels.IsEnabled = map != null;
        if (map == null)
        {
            _status.Text = _list.Count == 0
                ? "No tilesets in this project yet: a .noortileset points at a sprite sheet and says which tiles are solid."
                : "Select a tilemap to paint it, or make a new one.";
            if (_shownMap != null) { _palette.Children.Clear(); PaletteCount = 0; }
            _shownMap = null;
            if (_doc.TilePainting) _doc.SetTilePainting(false);
            return;
        }
        var component = _doc.Selected!.Components["tilemap"]!;
        var tileset = component["tileset"]?.GetValue<string>();
        var info = _doc.Tilemap(new JsonObject { ["op"] = "info", ["tilemap"] = map });
        _status.Text = info == null ? "This tilemap can't be read." :
            info["error"] is { } err ? err.GetValue<string>() :
            $"{info["width"]} × {info["height"]} cells, {info["layout"]}. Left-drag paints; past the edges the map grows.";
        _cell = component["cell"] is JsonArray c && c.Count > 0 ? c[0]!.GetValue<double>() : 1.0;
        _pixelsPerMetre = info?["pixels_per_metre"]?.GetValue<double>() ?? 16.0;
        if (map == _shownMap && tileset == _shownRows) return;  // same map and tileset: the palette stands
        _shownMap = map;
        _shownRows = tileset;
        _palette.Children.Clear();
        PaletteCount = 0;
        if (info?["frames"] is not JsonArray frames || info["texture_path"]?.GetValue<string>() is not { } texture) return;
        Bitmap? sheet = null;
        try { sheet = new Bitmap(Path.Combine(_doc.ProjectRoot, texture)); } catch { /* no picture: numbered buttons */ }
        var solid = (info["solid"] as JsonArray)?.Select(n => n!.GetValue<int>()).ToHashSet() ?? [];
        var oneWay = (info["one_way"] as JsonArray)?.Select(n => n!.GetValue<int>()).ToHashSet() ?? [];
        _palette.Children.Add(TileButton(-1, null, "Eraser"));
        for (var i = 0; i < frames.Count; i++)
        {
            Control? picture = null;
            if (sheet != null && frames[i] is JsonArray r && r.Count == 4)
            {
                var rect = new PixelRect(r[0]!.GetValue<int>(), r[1]!.GetValue<int>(), r[2]!.GetValue<int>(), r[3]!.GetValue<int>());
                var image = new Image { Source = new CroppedBitmap(sheet, rect), Width = 40, Height = 40, Stretch = Stretch.Uniform };
                RenderOptions.SetBitmapInterpolationMode(image, BitmapInterpolationMode.None);  // pixel art stays crisp
                picture = image;
            }
            var tip = $"Tile {i}" + (solid.Contains(i) ? " (solid)" : oneWay.Contains(i) ? " (one-way)" : "");
            _palette.Children.Add(TileButton(i, picture, tip));
            PaletteCount++;
        }
        SyncButtons();
    }

    private ToggleButton TileButton(int tile, Control? picture, string tip)
    {
        var b = new ToggleButton
        {
            Tag = tile, Margin = new Thickness(2), Padding = new Thickness(2), MinWidth = 46, MinHeight = 46,
            Content = picture ?? new TextBlock { Text = tile < 0 ? "Erase" : tile.ToString(), FontSize = 11, HorizontalAlignment = HorizontalAlignment.Center },
        };
        ToolTip.SetTip(b, tip);
        b.Click += (_, _) =>
        {
            _doc.SetTileBrush(tile);
            if (!_doc.TilePainting) _doc.SetTilePainting(true);
            SyncButtons();
        };
        return b;
    }
}
