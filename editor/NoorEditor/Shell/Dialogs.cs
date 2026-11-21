using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Layout;
using Avalonia.Media;

namespace NoorEditor.Shell;

/// <summary>Small modal dialogs in the editor's style (Avalonia has no built-in message box).</summary>
public static class Dialogs
{
    /// <summary>Shows a message with buttons; returns the clicked button's text, or null if closed.</summary>
    public static async Task<string?> Ask(Window owner, string title, string message, params string[] buttons)
    {
        var window = Shell(title);
        string? result = null;
        var row = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 8, HorizontalAlignment = HorizontalAlignment.Right };
        for (var i = 0; i < buttons.Length; i++)
        {
            var text = buttons[i];
            var b = new Button { Content = text, Classes = { i == 0 ? "primary" : "tool" }, IsDefault = i == 0 };
            if (text == "Cancel") b.IsCancel = true;
            b.Click += (_, _) => { result = text; window.Close(); };
            row.Children.Add(b);
        }
        window.Content = Body(new TextBlock { Text = message, TextWrapping = TextWrapping.Wrap, FontSize = 13 }, row);
        await window.ShowDialog(owner);
        return result;
    }

    /// <summary>Asks for one line of text; returns null on cancel.</summary>
    public static async Task<string?> Prompt(Window owner, string title, string label, string initial)
    {
        var window = Shell(title);
        string? result = null;
        var box = new TextBox { Text = initial, Classes = { "field" } };
        var ok = new Button { Content = "OK", Classes = { "primary" }, IsDefault = true };
        var cancel = new Button { Content = "Cancel", Classes = { "tool" }, IsCancel = true };
        ok.Click += (_, _) => { result = box.Text?.Trim(); window.Close(); };
        cancel.Click += (_, _) => window.Close();
        window.Content = Body(
            new StackPanel { Spacing = 6, Children = { new TextBlock { Text = label, FontSize = 12, Classes = { "muted" } }, box } },
            new StackPanel { Orientation = Orientation.Horizontal, Spacing = 8, HorizontalAlignment = HorizontalAlignment.Right, Children = { ok, cancel } });
        window.Opened += (_, _) => { box.Focus(); box.SelectAll(); };
        await window.ShowDialog(owner);
        return string.IsNullOrWhiteSpace(result) ? null : result;
    }

    /// <summary>A project template the New Project dialog offers (2D part 7); Folder null: an empty project.</summary>
    public sealed record TemplateChoice(string Name, string Description, string? Folder);

    /// <summary>New Project: a name and a template. Returns null on cancel.</summary>
    public static async Task<(string Name, TemplateChoice Template)?> NewProject(Window owner, IReadOnlyList<TemplateChoice> templates)
    {
        var window = Shell("New Project");
        window.Width = 520;
        (string, TemplateChoice)? result = null;
        var box = new TextBox { Text = "MyGame", Classes = { "field" } };
        var list = new ListBox { MaxHeight = 300, SelectedIndex = 0 };
        foreach (var t in templates)
        {
            list.Items.Add(new ListBoxItem
            {
                Tag = t,
                Padding = new Thickness(10, 6),
                Content = new StackPanel
                {
                    Spacing = 2,
                    Children =
                    {
                        new TextBlock { Text = t.Name, FontSize = 13, FontWeight = FontWeight.SemiBold },
                        new TextBlock { Text = t.Description, FontSize = 11, Classes = { "muted" }, TextWrapping = TextWrapping.Wrap },
                    },
                },
            });
        }
        list.SelectedIndex = 0;
        var ok = new Button { Content = "Create", Classes = { "primary" }, IsDefault = true };
        var cancel = new Button { Content = "Cancel", Classes = { "tool" }, IsCancel = true };
        ok.Click += (_, _) =>
        {
            var name = box.Text?.Trim();
            if (string.IsNullOrWhiteSpace(name) || list.SelectedItem is not ListBoxItem { Tag: TemplateChoice t }) return;
            result = (name, t);
            window.Close();
        };
        cancel.Click += (_, _) => window.Close();
        window.Content = Body(
            new StackPanel
            {
                Spacing = 6,
                Children =
                {
                    new TextBlock { Text = "Project name", FontSize = 12, Classes = { "muted" } }, box,
                    new TextBlock { Text = "Start from", FontSize = 12, Classes = { "muted" }, Margin = new Thickness(0, 10, 0, 0) }, list,
                },
            },
            new StackPanel { Orientation = Orientation.Horizontal, Spacing = 8, HorizontalAlignment = HorizontalAlignment.Right, Children = { ok, cancel } });
        window.Opened += (_, _) => { box.Focus(); box.SelectAll(); };
        await window.ShowDialog(owner);
        return result;
    }

    private static Window Shell(string title) => new()
    {
        Title = title,
        Width = 440,
        SizeToContent = SizeToContent.Height,
        CanResize = false,
        ShowInTaskbar = false,
        WindowStartupLocation = WindowStartupLocation.CenterOwner,
        Background = NoorTheme.Brush(NoorTheme.Panel),
    };

    private static Control Body(Control content, Control buttons) => new StackPanel
    {
        Margin = new Thickness(20, 18),
        Spacing = 18,
        Children = { content, buttons },
    };
}
