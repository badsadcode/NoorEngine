using Avalonia;
using Avalonia.Controls;
using Avalonia.Media;
using Avalonia.Styling;
using Avalonia.Themes.Fluent;

namespace NoorEditor.Shell;

/// <summary>NoorEditor dark theme tokens (direction from Noor's concept mockups).</summary>
public static class NoorTheme
{
    public static readonly Color Background = Color.Parse("#121019");
    public static readonly Color Panel = Color.Parse("#191623");
    public static readonly Color PanelRaised = Color.Parse("#211D2E");
    public static readonly Color Border = Color.Parse("#2C2640");
    public static readonly Color Accent = Color.Parse("#7C4DFF");
    public static readonly Color AccentDeep = Color.Parse("#6B3FD9");
    public static readonly Color Cyan = Color.Parse("#22D3EE");
    public static readonly Color Text = Color.Parse("#E7E3F2");
    public static readonly Color TextMuted = Color.Parse("#9A93B0");
    public static readonly Color Good = Color.Parse("#4ADE80");
    public static readonly Color Warn = Color.Parse("#FBBF24");
    public static readonly Color Bad = Color.Parse("#F87171");
    public static readonly Color AxisX = Color.Parse("#F06A6A");
    public static readonly Color AxisY = Color.Parse("#7BD88F");
    public static readonly Color AxisZ = Color.Parse("#6AA8F0");

    public static IBrush Brush(Color c) => new SolidColorBrush(c);

    public static FluentTheme CreateFluent()
    {
        var fluent = new FluentTheme();
        fluent.Palettes[ThemeVariant.Dark] = new ColorPaletteResources
        {
            Accent = Accent,
            RegionColor = Panel,
            AltHigh = Background,
            AltMediumLow = Panel,
            ChromeLow = Background,
            ChromeMedium = Panel,
            ChromeMediumLow = PanelRaised,
            ChromeHigh = Border,
            BaseHigh = Text,
            BaseMedium = TextMuted,
            ListLow = PanelRaised,
            ListMedium = Border,
        };
        return fluent;
    }

    /// <summary>Overrides Dock's theme resources so panels and tabs match the palette.</summary>
    public static void ApplyResources(IResourceDictionary resources)
    {
        resources["DockThemeBackgroundBrush"] = Brush(Background);
        resources["DockThemeBorderLowBrush"] = Brush(Border);
        resources["DockThemeBorderMidBrush"] = Brush(Border);
        resources["DockThemeAccentBrush"] = Brush(Accent);
        resources["DockThemeForegroundBrush"] = Brush(Text);
        resources["DockApplicationAccentBrushLow"] = Brush(AccentDeep);
        resources["DockApplicationAccentBrushMed"] = Brush(Accent);
        resources["DockApplicationAccentBrushHigh"] = Brush(Accent);
        resources["DockApplicationAccentBrushIndicator"] = Brush(Cyan);
        resources["DockApplicationAccentForegroundBrush"] = Brush(Text);
        resources["DockThemeControlBackgroundBrush"] = Brush(Panel);
    }

    public static Styles CreateStyles() => new()
    {
        new Style(x => x.OfType<TextBlock>().Class("muted"))
        {
            Setters = { new Setter(TextBlock.ForegroundProperty, Brush(TextMuted)) },
        },
        new Style(x => x.OfType<TextBlock>().Class("section"))
        {
            Setters =
            {
                new Setter(TextBlock.FontWeightProperty, FontWeight.SemiBold),
                new Setter(TextBlock.FontSizeProperty, 12.0),
                new Setter(TextBlock.ForegroundProperty, Brush(Text)),
            },
        },
        new Style(x => x.OfType<TextBlock>().Class("title"))
        {
            Setters =
            {
                new Setter(TextBlock.FontWeightProperty, FontWeight.SemiBold),
                new Setter(TextBlock.FontSizeProperty, 22.0),
                new Setter(TextBlock.ForegroundProperty, Brush(Text)),
            },
        },
        new Style(x => x.OfType<Button>().Class("tool"))
        {
            Setters =
            {
                new Setter(Button.BackgroundProperty, Brushes.Transparent),
                new Setter(Button.PaddingProperty, new Thickness(8, 4)),
                new Setter(Button.FontSizeProperty, 12.0),
            },
        },
        new Style(x => x.OfType<Button>().Class("primary"))
        {
            Setters =
            {
                new Setter(Button.BackgroundProperty, Brush(AccentDeep)),
                new Setter(Button.ForegroundProperty, Brushes.White),
                new Setter(Button.PaddingProperty, new Thickness(14, 6)),
                new Setter(Button.FontSizeProperty, 12.0),
            },
        },
        new Style(x => x.OfType<TextBox>().Class("field"))
        {
            Setters =
            {
                new Setter(TextBox.FontSizeProperty, 12.0),
                new Setter(TextBox.MinHeightProperty, 26.0),
                new Setter(TextBox.PaddingProperty, new Thickness(6, 3)),
                new Setter(TextBox.BackgroundProperty, Brush(Background)),
            },
        },
        new Style(x => x.OfType<TreeViewItem>())
        {
            Setters = { new Setter(TreeViewItem.MinHeightProperty, 24.0), new Setter(TreeViewItem.FontSizeProperty, 12.0) },
        },
    };
}
