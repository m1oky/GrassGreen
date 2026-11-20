using System;
using System.ComponentModel.Composition;
using Microsoft.VisualStudio;
using Microsoft.VisualStudio.Editor;
using Microsoft.VisualStudio.OLE.Interop;
using Microsoft.VisualStudio.Shell;
using Microsoft.VisualStudio.Text.Editor;
using Microsoft.VisualStudio.TextManager.Interop;
using Microsoft.VisualStudio.Utilities;
using GrassGreen.VisualStudio.Constants;
using GrassGreen.VisualStudio.Execution;
using OleConstants = Microsoft.VisualStudio.OLE.Interop.Constants;

namespace GrassGreen.VisualStudio.Commands
{
    public class GrassEditorCommandFilter : IOleCommandTarget
    {
        private readonly IOleCommandTarget _nextTarget;

        public GrassEditorCommandFilter(IVsTextView textViewAdapter)
        {
            ThreadHelper.ThrowIfNotOnUIThread();
            textViewAdapter.AddCommandFilter(this, out _nextTarget);
        }

        public int QueryStatus(ref Guid pguidCmdGroup, uint cCmds, OLECMD[] prgCmds, IntPtr pCmdText)
        {
            ThreadHelper.ThrowIfNotOnUIThread();

            if (pguidCmdGroup == VSConstants.GUID_VSStandardCommandSet97)
            {
                switch ((VSConstants.VSStd97CmdID)prgCmds[0].cmdID)
                {
                    case VSConstants.VSStd97CmdID.Start:
                    case VSConstants.VSStd97CmdID.StartNoDebug:
                    case VSConstants.VSStd97CmdID.BuildSel:
                    case VSConstants.VSStd97CmdID.BuildSln:
                        prgCmds[0].cmdf = (uint)(OLECMDF.OLECMDF_SUPPORTED | OLECMDF.OLECMDF_ENABLED);
                        return VSConstants.S_OK;
                }
            }

            return _nextTarget != null ? _nextTarget.QueryStatus(ref pguidCmdGroup, cCmds, prgCmds, pCmdText) : (int)OleConstants.OLECMDERR_E_NOTSUPPORTED;
        }

        public int Exec(ref Guid pguidCmdGroup, uint nCmdID, uint nCmdexecopt, IntPtr pvaIn, IntPtr pvaOut)
        {
            ThreadHelper.ThrowIfNotOnUIThread();

            if (pguidCmdGroup == VSConstants.GUID_VSStandardCommandSet97)
            {
                switch ((VSConstants.VSStd97CmdID)nCmdID)
                {
                    case VSConstants.VSStd97CmdID.Start:
                    case VSConstants.VSStd97CmdID.StartNoDebug:
                        ThreadHelper.JoinableTaskFactory.RunAsync(async () =>
                        {
                            await GrassExecutionService.RunActiveFileAsync();
                        });
                        return VSConstants.S_OK;

                    case VSConstants.VSStd97CmdID.BuildSel:
                    case VSConstants.VSStd97CmdID.BuildSln:
                        ThreadHelper.JoinableTaskFactory.RunAsync(async () =>
                        {
                            await GrassExecutionService.BuildActiveFileAsync();
                        });
                        return VSConstants.S_OK;
                }
            }

            return _nextTarget != null ? _nextTarget.Exec(ref pguidCmdGroup, nCmdID, nCmdexecopt, pvaIn, pvaOut) : (int)OleConstants.OLECMDERR_E_NOTSUPPORTED;
        }
    }

    [Export(typeof(IVsTextViewCreationListener))]
    [ContentType(GrassConstants.ContentType)]
    [TextViewRole(PredefinedTextViewRoles.Editable)]
    public class GrassTextViewCreationListener : IVsTextViewCreationListener
    {
        [Import]
        internal IVsEditorAdaptersFactoryService EditorAdaptersFactoryService { get; set; }

        public void VsTextViewCreated(IVsTextView textViewAdapter)
        {
            ThreadHelper.ThrowIfNotOnUIThread();
            if (textViewAdapter == null) return;

            new GrassEditorCommandFilter(textViewAdapter);
        }
    }
}
