using System;
using System.Diagnostics;
using System.IO;
using System.Reflection;
using System.Windows.Forms;

internal static class Program
{
    [STAThread]
    private static void Main(string[] args)
    {
        bool silent = false;

        foreach (string arg in args)
        {
            if (string.Equals(arg, "--silent", StringComparison.OrdinalIgnoreCase) ||
                string.Equals(arg, "/silent", StringComparison.OrdinalIgnoreCase))
            {
                silent = true;
            }
        }

        RunEmbeddedPowerShell("BetterGroup.updater.ps1", "updater.ps1", silent);
    }

    private static void RunEmbeddedPowerShell(string resource, string fileName, bool silent)
    {
        string temp = Path.Combine(
            Path.GetTempPath(),
            "BetterGroup_" + Guid.NewGuid().ToString("N"));

        Directory.CreateDirectory(temp);
        string script = Path.Combine(temp, fileName);

        try
        {
            using (Stream input = Assembly.GetExecutingAssembly().GetManifestResourceStream(resource))
            {
                if (input == null) throw new Exception("Embedded updater script missing.");
                using (FileStream output = File.Create(script))
                    input.CopyTo(output);
            }

            var psi = new ProcessStartInfo
            {
                FileName = "powershell.exe",
                Arguments = "-NoProfile -ExecutionPolicy Bypass -File \"" + script + "\"" + (silent ? " -Silent" : ""),
                UseShellExecute = false,
                CreateNoWindow = true,
                WorkingDirectory = temp
            };

            using (Process p = Process.Start(psi))
            {
                if (p != null) p.WaitForExit();
            }
        }
        catch (Exception ex)
        {
            MessageBox.Show(ex.Message, "BetterGroup Update",
                MessageBoxButtons.OK, MessageBoxIcon.Error);
        }
        finally
        {
            try { Directory.Delete(temp, true); } catch { }
        }
    }
}
