using System;
using System.Diagnostics;
using System.IO;
using System.Threading.Tasks;
using Microsoft.VisualStudio.Shell;
using Microsoft.VisualStudio.Shell.Interop;
using Task = System.Threading.Tasks.Task;

namespace GrassGreen.VisualStudio.Execution
{
    public static class GrassExecutionService
    {
        private static readonly Guid OutputPaneGuid = new Guid("4A7E0041-0678-4D05-8D8C-7C4550186BC2");

        public static string GetActiveGrassFilePath()
        {
            ThreadHelper.ThrowIfNotOnUIThread();
            try
            {
                var dte = Package.GetGlobalService(typeof(EnvDTE.DTE)) as EnvDTE.DTE;
                if (dte == null || dte.ActiveDocument == null) return null;

                string fullName = dte.ActiveDocument.FullName;
                if (!string.IsNullOrEmpty(fullName) && fullName.EndsWith(".grass", StringComparison.OrdinalIgnoreCase))
                {
                    return fullName;
                }
            }
            catch
            {
                // Игнорируем исключения при обращении к DTE в специфических контекстах
            }
            return null;
        }

        public static bool SaveActiveDocument()
        {
            ThreadHelper.ThrowIfNotOnUIThread();
            try
            {
                var dte = Package.GetGlobalService(typeof(EnvDTE.DTE)) as EnvDTE.DTE;
                if (dte?.ActiveDocument != null)
                {
                    if (!dte.ActiveDocument.Saved)
                    {
                        dte.ActiveDocument.Save();
                    }
                    return true;
                }
            }
            catch (Exception ex)
            {
                Log("Предупреждение при сохранении документа: " + ex.Message);
            }
            return false;
        }

        public static void Log(string message)
        {
            ThreadHelper.JoinableTaskFactory.Run(async () =>
            {
                await ThreadHelper.JoinableTaskFactory.SwitchToMainThreadAsync();
                try
                {
                    var outWindow = await AsyncServiceProvider.GlobalProvider.GetServiceAsync(typeof(SVsOutputWindow)) as IVsOutputWindow;
                    if (outWindow == null) return;

                    Guid paneGuid = OutputPaneGuid;
                    outWindow.GetPane(ref paneGuid, out IVsOutputWindowPane pane);
                    if (pane == null)
                    {
                        outWindow.CreatePane(ref paneGuid, "GrassGreen", 1, 1);
                        outWindow.GetPane(ref paneGuid, out pane);
                    }

                    if (pane != null)
                    {
                        pane.Activate();
                        pane.OutputStringThreadSafe($"[{DateTime.Now:HH:mm:ss}] {message}\n");
                    }
                }
                catch
                {
                    // Игнорируем ошибки вывода в панель
                }
            });
        }

        public static string FindGrassCompiler(string sourceFilePath)
        {
            // 1. Поиск bin\grassc.exe или bin\grg.exe в родительских каталогах исходного файла
            if (!string.IsNullOrEmpty(sourceFilePath))
            {
                string currentDir = Path.GetDirectoryName(sourceFilePath);
                while (!string.IsNullOrEmpty(currentDir))
                {
                    string candidateGrassc = Path.Combine(currentDir, "bin", "grassc.exe");
                    if (File.Exists(candidateGrassc)) return candidateGrassc;

                    string candidateGrg = Path.Combine(currentDir, "bin", "grg.exe");
                    if (File.Exists(candidateGrg)) return candidateGrg;

                    string rootCandidate = Path.Combine(currentDir, "grassc.exe");
                    if (File.Exists(rootCandidate)) return rootCandidate;

                    var parentDir = Directory.GetParent(currentDir);
                    currentDir = parentDir?.FullName;
                }
            }

            // 2. Стандартный путь установки C:\GrassGreen\bin
            string defaultPath = @"C:\GrassGreen\bin\grassc.exe";
            if (File.Exists(defaultPath)) return defaultPath;

            string defaultGrg = @"C:\GrassGreen\bin\grg.exe";
            if (File.Exists(defaultGrg)) return defaultGrg;

            // 3. Поиск в системной переменной PATH
            string pathEnv = Environment.GetEnvironmentVariable("PATH");
            if (!string.IsNullOrEmpty(pathEnv))
            {
                var paths = pathEnv.Split(';');
                foreach (var p in paths)
                {
                    string clean = p.Trim();
                    if (string.IsNullOrEmpty(clean)) continue;

                    string checkGrassc = Path.Combine(clean, "grassc.exe");
                    if (File.Exists(checkGrassc)) return checkGrassc;

                    string checkGrg = Path.Combine(clean, "grg.exe");
                    if (File.Exists(checkGrg)) return checkGrg;
                }
            }

            // 4. Fallback на имя исполняемого файла в надежде на системный PATH
            return "grassc.exe";
        }

        public static async Task<bool> BuildActiveFileAsync()
        {
            await ThreadHelper.JoinableTaskFactory.SwitchToMainThreadAsync();

            string filePath = GetActiveGrassFilePath();
            if (string.IsNullOrEmpty(filePath))
            {
                Log("Ошибка: активный файл не является документом GrassGreen (.grass).");
                return false;
            }

            SaveActiveDocument();

            string dir = Path.GetDirectoryName(filePath);
            string fileName = Path.GetFileName(filePath);
            string compiler = FindGrassCompiler(filePath);
            string expectedExe = Path.ChangeExtension(filePath, ".exe");

            Log($"--- Сборка GrassGreen начата: {fileName} ---");
            Log($"Путь к компилятору: {compiler}");
            Log($"Аргументы: -O3 -emit-exe \"{filePath}\"");

            var psi = new ProcessStartInfo
            {
                FileName = compiler,
                Arguments = $"-O3 -emit-exe \"{filePath}\"",
                WorkingDirectory = dir,
                UseShellExecute = false,
                RedirectStandardOutput = true,
                RedirectStandardError = true,
                CreateNoWindow = true
            };

            return await Task.Run(() =>
            {
                try
                {
                    using (var proc = new Process { StartInfo = psi })
                    {
                        proc.OutputDataReceived += (s, e) =>
                        {
                            if (!string.IsNullOrEmpty(e.Data))
                            {
                                Log(e.Data);
                            }
                        };
                        proc.ErrorDataReceived += (s, e) =>
                        {
                            if (!string.IsNullOrEmpty(e.Data))
                            {
                                Log(e.Data);
                            }
                        };

                        proc.Start();
                        proc.BeginOutputReadLine();
                        proc.BeginErrorReadLine();
                        proc.WaitForExit();

                        if (proc.ExitCode == 0)
                        {
                            Log($"Сборка успешно завершена: {expectedExe}");
                            return true;
                        }
                        else
                        {
                            Log($"Ошибка компиляции. Код завершения: {proc.ExitCode}");
                            return false;
                        }
                    }
                }
                catch (Exception ex)
                {
                    Log($"Исключение при вызове компилятора: {ex.Message}");
                    return false;
                }
            });
        }

        public static async Task RunActiveFileAsync()
        {
            await ThreadHelper.JoinableTaskFactory.SwitchToMainThreadAsync();

            string filePath = GetActiveGrassFilePath();
            if (string.IsNullOrEmpty(filePath))
            {
                Log("Ошибка: активный файл не является документом GrassGreen (.grass).");
                return;
            }

            SaveActiveDocument();

            string dir = Path.GetDirectoryName(filePath);
            string fileName = Path.GetFileName(filePath);
            string compiler = FindGrassCompiler(filePath);

            Log($"--- Запуск GrassGreen: {fileName} ---");
            Log($"Исполняемый файл: {compiler} \"{filePath}\"");

            // Запуск в отдельном окне командной строки для полноценного консольного I/O
            string cmdArgs = $"/c title GrassGreen: {fileName} && \"{compiler}\" \"{filePath}\" & pause";

            var psi = new ProcessStartInfo
            {
                FileName = "cmd.exe",
                Arguments = cmdArgs,
                WorkingDirectory = dir,
                UseShellExecute = true,
                CreateNoWindow = false
            };

            try
            {
                Process.Start(psi);
                Log($"Процесс запущен в отдельном терминальном окне: {fileName}");
            }
            catch (Exception ex)
            {
                Log($"Ошибка при запуске процесса: {ex.Message}");
            }
        }
    }
}
