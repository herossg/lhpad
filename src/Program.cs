using System.Runtime.InteropServices;
using System.Text;

namespace LHPad;

internal static class Program
{
    [STAThread]
    static int Main(string[] args)
    {
        Encoding.RegisterProvider(CodePagesEncodingProvider.Instance);
        Application.SetHighDpiMode(HighDpiMode.PerMonitorV2);
        Application.EnableVisualStyles();
        Application.SetCompatibleTextRenderingDefault(false);
        NativeLibrary.Load(Path.Combine(AppContext.BaseDirectory, "Scintilla.dll"));
        if (args.Contains("--self-test"))
            return SelfTests.Run();
        if (args.Contains("--preview-test"))
            return SelfTests.Run(true);
        if (args.Contains("--macro-test"))
            return SelfTests.Run(true, true);
        if (args.Contains("--monitor-test"))
            return SelfTests.Run(monitorOnly: true);
        if (args.Contains("--ui-test"))
            return SelfTests.Run(uiOnly: true);
        if (args.Contains("--search-test"))
            return SelfTests.Run(searchOnly: true);
        if (args.Contains("--window-test"))
            return SelfTests.Run(windowOnly: true);
        if (args.Contains("--fold-test"))
            return SelfTests.Run(foldOnly: true);
        if (args.Contains("--indent-test"))
            return SelfTests.Run(indentOnly: true);
        if (args.Contains("--dialog-test"))
            return SelfTests.Run(dialogOnly: true);
        var isolated = args.Contains("--demo");
        var profileArg = Array.IndexOf(args, "--profile");
        var profile = profileArg >= 0 && profileArg + 1 < args.Length ? Path.GetFullPath(args[profileArg + 1]) :
            Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "LitePad");
        Directory.CreateDirectory(profile);
        // Exclusive profile lock prevents a second process from overwriting recovery data.
        FileStream profileLock;
        try
        {
            profileLock = new(Path.Combine(profile, ".lock"), FileMode.OpenOrCreate, FileAccess.ReadWrite, FileShare.None);
        }
        catch (IOException) { Dialogs.Message("LHPad가 이미 실행 중입니다. 열려 있는 창을 사용해 주세요.", "LHPad"); return 1; }
        using (profileLock)
        {
            Application.ThreadException += (_, e) => Dialogs.Message(e.Exception.Message, "LHPad 오류", MessageBoxButtons.OK, MessageBoxIcon.Error);
            var files = args.Where((x, i) => !x.StartsWith("--") && (profileArg < 0 || i != profileArg + 1)).ToArray();
            Application.Run(new MainForm(profile, files, isolated));
        }
        return 0;
    }
}
