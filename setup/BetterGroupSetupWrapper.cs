using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Reflection;
using System.Windows.Forms;

internal static class Program
{
    private sealed class ResourceFile
    {
        public string Resource;
        public string FileName;

        public ResourceFile(string resource, string fileName)
        {
            Resource = resource;
            FileName = fileName;
        }
    }

    [STAThread]
    private static void Main()
    {
        string temp = Path.Combine(
            Path.GetTempPath(),
            "BetterGroup_Setup_" + Guid.NewGuid().ToString("N"));

        Directory.CreateDirectory(temp);

        var files = new List<ResourceFile>
        {
            new ResourceFile("BetterGroup.installer.ps1", "installer.ps1"),
            new ResourceFile("BetterGroup.dll", "BetterGroup.dll"),
            new ResourceFile("BetterGroup.roles", "BetterGroup_roles.cfg"),
            new ResourceFile("BetterGroup.config", "BetterGroup.cfg"),
            new ResourceFile("BetterGroup.version", "version.txt"),
            new ResourceFile("BetterGroup.updateini", "BetterGroup_update.ini"),
            new ResourceFile("BetterGroup.updateexe", "BetterGroup_Update.exe"),
            new ResourceFile("BetterGroup.uninstallexe", "BetterGroup_Uninstall.exe")
        };

        try
        {
            Assembly asm = Assembly.GetExecutingAssembly();

            foreach (ResourceFile rf in files)
            {
                using (Stream input = asm.GetManifestResourceStream(rf.Resource))
                {
                    if (input == null)
                        throw new Exception("Embedded installer file missing: " + rf.FileName);

                    using (FileStream output = File.Create(Path.Combine(temp, rf.FileName)))
                        input.CopyTo(output);
                }
            }

            string script = Path.Combine(temp, "installer.ps1");

            var psi = new ProcessStartInfo
            {
                FileName = "powershell.exe",
                Arguments = "-NoProfile -ExecutionPolicy Bypass -File \"" + script + "\"",
                UseShellExecute = false,
                CreateNoWindow = false,
                WorkingDirectory = temp
            };

            using (Process p = Process.Start(psi))
            {
                if (p != null) p.WaitForExit();
            }
        }
        catch (Exception ex)
        {
            MessageBox.Show(ex.Message, "BetterGroup Setup",
                MessageBoxButtons.OK, MessageBoxIcon.Error);
        }
        finally
        {
            try { Directory.Delete(temp, true); } catch { }
        }
    }
}
