using System;
using System.ComponentModel.Design;
using System.Runtime.InteropServices;
using System.Threading;
using Microsoft.VisualStudio;
using Microsoft.VisualStudio.Shell;
using GrassGreen.VisualStudio.Constants;
using GrassGreen.VisualStudio.Execution;
using Task = System.Threading.Tasks.Task;

namespace GrassGreen.VisualStudio
{
    [PackageRegistration(UseManagedResourcesOnly = true, AllowsBackgroundLoading = true)]
    [Guid(GrassConstants.PackageGuidString)]
    [ProvideAutoLoad(VSConstants.UICONTEXT.SolutionExists_string, PackageAutoLoadFlags.BackgroundLoad)]
    [ProvideAutoLoad(VSConstants.UICONTEXT.NoSolution_string, PackageAutoLoadFlags.BackgroundLoad)]
    public sealed class GrassGreenPackage : AsyncPackage
    {
        protected override async Task InitializeAsync(CancellationToken cancellationToken, IProgress<ServiceProgressData> progress)
        {
            await base.InitializeAsync(cancellationToken, progress);
            await JoinableTaskFactory.SwitchToMainThreadAsync(cancellationToken);

            var commandService = await GetServiceAsync(typeof(IMenuCommandService)) as OleMenuCommandService;
            if (commandService != null)
            {
                RegisterInterceptCommand(commandService, VSConstants.GUID_VSStandardCommandSet97, (int)VSConstants.VSStd97CmdID.Start, OnExecuteRun);
                RegisterInterceptCommand(commandService, VSConstants.GUID_VSStandardCommandSet97, (int)VSConstants.VSStd97CmdID.StartNoDebug, OnExecuteRun);
                RegisterInterceptCommand(commandService, VSConstants.GUID_VSStandardCommandSet97, (int)VSConstants.VSStd97CmdID.BuildSel, OnExecuteBuild);
                RegisterInterceptCommand(commandService, VSConstants.GUID_VSStandardCommandSet97, (int)VSConstants.VSStd97CmdID.BuildSln, OnExecuteBuild);
            }
        }

        private void RegisterInterceptCommand(OleMenuCommandService service, Guid menuGroup, int cmdId, EventHandler executeHandler)
        {
            var commandId = new CommandID(menuGroup, cmdId);
            var menuCommand = new OleMenuCommand(executeHandler, commandId);
            menuCommand.BeforeQueryStatus += OnBeforeQueryStatus;
            service.AddCommand(menuCommand);
        }

        private void OnBeforeQueryStatus(object sender, EventArgs e)
        {
            ThreadHelper.ThrowIfNotOnUIThread();
            var cmd = (OleMenuCommand)sender;
            string activeFile = GrassExecutionService.GetActiveGrassFilePath();
            bool isGrass = !string.IsNullOrEmpty(activeFile);

            cmd.Supported = isGrass;
            cmd.Enabled = isGrass;
            cmd.Visible = isGrass;
        }

        private void OnExecuteRun(object sender, EventArgs e)
        {
            ThreadHelper.ThrowIfNotOnUIThread();
            JoinableTaskFactory.RunAsync(async () =>
            {
                await GrassExecutionService.RunActiveFileAsync();
            });
        }

        private void OnExecuteBuild(object sender, EventArgs e)
        {
            ThreadHelper.ThrowIfNotOnUIThread();
            JoinableTaskFactory.RunAsync(async () =>
            {
                await GrassExecutionService.BuildActiveFileAsync();
            });
        }
    }
}
