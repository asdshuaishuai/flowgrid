// 主题选择：跟随系统 / 浅 / 深。切换 = 整体替换合并字典中的颜色层，
// 组件全部走 DynamicResource，无需重启。偏好持久化到 %APPDATA%\FlowGrid\ui.json。
using System.IO;
using System.Text.Json;
using System.Windows;
using Microsoft.Win32;

namespace FlowGrid.UI;

public enum ThemePreference
{
    Auto,
    Light,
    Dark,
}

public static class ThemeManager
{
    private const string LightSource = "Themes/ThemeLight.xaml";
    private const string DarkSource = "Themes/ThemeDark.xaml";
    private static readonly string SettingsPath = Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData),
        "FlowGrid", "ui.json");

    public static ThemePreference Preference { get; private set; } = ThemePreference.Auto;

    public static void Initialize()
    {
        Preference = Load();
        Apply(Resolve(Preference));
    }

    public static void SetTheme(ThemePreference preference)
    {
        Preference = preference;
        Apply(Resolve(preference));
        Save(preference);
    }

    private static Uri Resolve(ThemePreference preference)
    {
        var dark = preference == ThemePreference.Dark ||
                   (preference == ThemePreference.Auto && SystemPrefersDark());
        return new Uri(dark ? DarkSource : LightSource, UriKind.Relative);
    }

    private static bool SystemPrefersDark()
    {
        // HKCU Themes\Personalize\AppsUseLightTheme：0 = 深色。缺键视作浅色。
        using var key = Registry.CurrentUser.OpenSubKey(
            @"Software\Microsoft\Windows\CurrentVersion\Themes\Personalize");
        return key?.GetValue("AppsUseLightTheme") is 0;
    }

    private static void Apply(Uri themeSource)
    {
        var dicts = Application.Current.Resources.MergedDictionaries;
        // 颜色层始终是最后一个字典（App.xaml 的合并顺序约定），原位替换。
        var colors = new ResourceDictionary { Source = themeSource };
        if (dicts.Count > 0 && IsThemeDictionary(dicts[^1]))
        {
            dicts[^1] = colors;
        }
        else
        {
            dicts.Add(colors);
        }
    }

    private static bool IsThemeDictionary(ResourceDictionary d) =>
        d.Source?.OriginalString is { } s &&
        (s.EndsWith("ThemeLight.xaml", StringComparison.Ordinal) ||
         s.EndsWith("ThemeDark.xaml", StringComparison.Ordinal));

    private static ThemePreference Load()
    {
        try
        {
            if (!File.Exists(SettingsPath))
            {
                return ThemePreference.Auto;
            }
            return JsonSerializer.Deserialize<UiSettings>(File.ReadAllText(SettingsPath))?.Theme
                   ?? ThemePreference.Auto;
        }
        catch (IOException)
        {
            return ThemePreference.Auto;
        }
        catch (JsonException)
        {
            return ThemePreference.Auto;
        }
    }

    private static void Save(ThemePreference preference)
    {
        try
        {
            Directory.CreateDirectory(Path.GetDirectoryName(SettingsPath)!);
            File.WriteAllText(SettingsPath,
                JsonSerializer.Serialize(new UiSettings(preference)));
        }
        catch (IOException)
        {
            // 写不进偏好不致命：本次会话仍生效，下次启动回到跟随系统。
        }
    }

    private sealed record UiSettings(ThemePreference Theme);
}
